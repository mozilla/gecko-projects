/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ReplayDebugger.h"

#include "vm/Debugger-inl.h"

using namespace js;
using namespace mozilla::recordreplay;
using mozilla::Maybe;
using mozilla::Some;
using mozilla::Nothing;
using mozilla::TimeStamp;

// This file has definitions associated with the replay debugger for managing
// breakpoints and all other state that persists across rewinds, and for
// handling all interactions with the actual record/replay infrastructure,
// including keeping track of where we are during execution and where we are
// trying to navigate to.
//
// The precise execution position of the replaying process is managed by the
// replaying process itself. The middleman will send the replaying process
// ResumeForward and ResumeBackward messages, but it is up to the replaying
// process to keep track of the rewinding and resuming necessary to find the
// next or previous point where a breakpoint or snapshot is hit.

static JSContext* gHookContext;
static PersistentRootedObject* gHookDebugger;

JSRuntime* ReplayDebugger::gMainRuntime;
PersistentRootedObject* ReplayDebugger::gHookGlobal;

// These are set whenever we stop on exit from a frame, and indicate the
// execution status of that frame.
static bool gPopFrameThrowing;
static PersistentRootedValue* gPopFrameResult;

#define TRY(op) do { if (!(op)) MOZ_CRASH(); } while (false)

/* static */ void
ReplayDebugger::NoteNewGlobalObject(JSContext* cx, GlobalObject* global)
{
    MOZ_RELEASE_ASSERT(IsRecordingOrReplaying());

    if (!gHookContext) {
        gHookContext = cx;
        gMainRuntime = cx->runtime();

        gPopFrameResult = js_new<PersistentRootedValue>(cx);
    }

    // The replay debugger is created in the first global with trusted principals.
    if (!gHookGlobal &&
        cx->runtime()->trustedPrincipals() &&
        cx->runtime()->trustedPrincipals() == global->compartment()->principals())
    {
        TRY(gHookGlobal = js_new<PersistentRootedObject>(cx));
        {
            AutoPassThroughThreadEvents pt;
            *gHookGlobal = global;
        }
    }
}

typedef ReplayDebugger::ExecutionPosition ExecutionPosition;

// Magic constant for the kind to use for untracked debugger memory.
// See UntrackedMemoryKind in ProcessRecordReplay.h
static const size_t DebuggerAllocatedMemoryKind = 1;

typedef AllocPolicy<DebuggerAllocatedMemoryKind> UntrackedAllocPolicy;

typedef Vector<ExecutionPosition, 4, UntrackedAllocPolicy> UntrackedExecutionPositionVector;

template <typename T>
static inline void
CopyVector(T& dst, const T& src)
{
    dst.clear();
    TRY(dst.append(src.begin(), src.length()));
}

// Identify a unique point in the JS execution of a process.
struct ExecutionPoint
{
    // Most recent snapshot prior to the execution point.
    size_t snapshot;

    // When starting at |snapshot|, the positions to reach, in sequence, before
    // arriving at the execution point.
    UntrackedExecutionPositionVector positions;

    ExecutionPoint()
      : snapshot((size_t)-1)
    {}

    ExecutionPoint& operator=(const ExecutionPoint& o)
    {
        snapshot = o.snapshot;
        CopyVector(positions, o.positions);
        return *this;
    }

    ExecutionPoint(const ExecutionPoint& o) { *this = o; }

    typedef size_t Prefix;
};

// Information about a debugger request sent by the middleman.
struct RequestInfo {
    // JSON contents for the request and (possibly) response.
    Vector<char16_t, 0, UntrackedAllocPolicy> requestBuffer;
    Vector<char16_t, 0, UntrackedAllocPolicy> responseBuffer;

    // Whether processing this request triggered an unhandled divergence.
    bool unhandledDivergence;

    RequestInfo() : unhandledDivergence(false) {}

    RequestInfo(const RequestInfo& o)
      : unhandledDivergence(o.unhandledDivergence)
    {
        CopyVector(requestBuffer, o.requestBuffer);
        CopyVector(responseBuffer, o.responseBuffer);
    }
};
typedef Vector<RequestInfo, 4, UntrackedAllocPolicy> UntrackedRequestVector;

// Abstract class for where we are at in the navigation state machine.
// Each subclass has a single instance contained in NavigationState (see below)
// and it and all its data are allocated using untracked memory that is not
// affected by restoring earlier snapshots.
class NavigationPhase
{
    MOZ_NORETURN void unsupported(const char* operation) {
        char buf[1024];
        toString(buf, sizeof(buf));

        AutoEnsurePassThroughThreadEvents pt;
        fprintf(stderr, "Operation %s not supported: %s\n", operation, buf);
        MOZ_CRASH();
    }

  public:
    virtual void toString(char* buf, size_t len) = 0;

    // The process has just reached or rewound to a snapshot.
    virtual void afterSnapshot(size_t snapshot, bool final) {
        unsupported("afterSnapshot");
    }

    // Called when some position with an installed handler has been reached.
    virtual void positionHit(const std::function<bool(const ExecutionPosition&)>& match) {
        unsupported("positionHit");
    }

    // Called after receiving a resume command from the middleman.
    virtual void resume(bool forward, bool hitOtherBreakpoints) {
        unsupported("resume");
    }

    // Process an incoming debugger request from the middleman.
    virtual void handleDebuggerRequest(JS::replay::CharBuffer* requestBuffer) {
        unsupported("handleDebuggerRequest");
    }

    // A debugger request wants to know the result of a just-popped frame.
    virtual bool getPoppedFrameResult(bool* throwing, MutableHandleValue result) {
        unsupported("getPoppedFrameResult");
    }

    // Called when a debugger request wants to make a change to an installed breakpoint.
    virtual void addBreakpointOperation(size_t id, const ExecutionPosition& pos) {
        unsupported("addBreakpointOperation");
    }

    // Called when a debugger request wants to try an operation that may
    // trigger an unhandled divergence from the recording.
    virtual bool maybeDivergeFromRecording() {
        unsupported("maybeDivergeFromRecording");
    }
};

typedef Vector<size_t, 0, SystemAllocPolicy> BreakpointVector;
typedef Vector<size_t, 0, UntrackedAllocPolicy> UntrackedBreakpointVector;

// Information about a pause to carry around when recovering from a recording
// divergence at that pause.
struct PauseInfo
{
    // Where the pause is at.
    ExecutionPoint mPoint;

    // Breakpoint the pause is at. Note that this is not uniquely identified by
    // |mPoint|, as there may be multiple breakpoints at the same position.
    size_t mBreakpoint;

    // All debugger requests we saw for the breakpoint.
    UntrackedRequestVector mRequests;

    // Other breakpoints at the current position which haven't been paused at.
    UntrackedBreakpointVector mRemainingBreakpoints;

    PauseInfo& operator=(const PauseInfo& o)
    {
        mPoint = o.mPoint;
        mBreakpoint = o.mBreakpoint;
        CopyVector(mRequests, o.mRequests);
        CopyVector(mRemainingBreakpoints, o.mRemainingBreakpoints);
        return *this;
    }
};

// Phase when the replaying process is paused at a breakpoint.
class BreakpointPausedPhase : public NavigationPhase
{
    // Information about the pause.
    PauseInfo mInfo;

    // Whether we had to restore a snapshot to deal with an unhandled recording
    // divergence, and haven't finished returning to the state when the
    // divergence occurred.
    bool mRecoveringFromDivergence;

    // Breakpoint operations to perform before resuming. These are delayed until
    // we resume so that changes to breakpoints don't interfere with activity when
    // recovering from an unhandled divergence.
    Vector<std::pair<size_t, ExecutionPosition>, 0, UntrackedAllocPolicy> mPendingBreakpointOperations;

    // Index of the request currently being processed. Normally this is the
    // last entry in |mRequests|, though may be earlier if we are recovering
    // from an unhandled divergence.
    size_t mRequestIndex;

  public:
    void enter(const PauseInfo& info, bool recoveringFromDivergence = false);

    void toString(char* buf, size_t len) override {
        snprintf(buf, len, "BreakpointPaused Breakpoint %zu OtherBreakpointsCount %zu",
                 mInfo.mBreakpoint, mInfo.mRemainingBreakpoints.length());
    }

    void afterSnapshot(size_t snapshot, bool final) override;
    void resume(bool forward, bool hitOtherBreakpoints) override;
    bool getPoppedFrameResult(bool* throwing, MutableHandleValue result) override;
    void handleDebuggerRequest(JS::replay::CharBuffer* requestBuffer) override;
    void addBreakpointOperation(size_t id, const ExecutionPosition& pos) override;
    bool maybeDivergeFromRecording() override;

    void respondAfterRecoveringFromDivergence();
};

// Phase when the replaying process is paused at a snapshot.
class SnapshotPausedPhase : public NavigationPhase
{
    size_t mSnapshot;
    bool mFinal;

  public:
    void enter(size_t snapshot, bool final, bool rewind);

    void toString(char* buf, size_t len) override {
        snprintf(buf, len, "SnapshotPaused");
    }

    void afterSnapshot(size_t snapshot, bool final) override;
    void resume(bool forward, bool hitOtherBreakpoints) override;
    void handleDebuggerRequest(JS::replay::CharBuffer* requestBuffer) override;
    void addBreakpointOperation(size_t id, const ExecutionPosition& pos) override;
};

// Phase when execution is proceeding forwards in search of breakpoint hits.
class ForwardPhase : public NavigationPhase
{
    // Some execution point in the recent past. There are no snapshots or
    // breakpoint hits between this point and the current point of execution.
    ExecutionPoint mPoint;

  public:
    void enter(const ExecutionPoint& point);

    void toString(char* buf, size_t len) override {
        snprintf(buf, len, "Forward");
    }

    void afterSnapshot(size_t snapshot, bool final) override;
    void positionHit(const std::function<bool(const ExecutionPosition&)>& match) override;
};

// Information about how to reach a point and what to do afterwards.
struct ReachPointInfo
{
    enum Kind {
        Resume,
        HitBreakpoint,
        RecoverFromDivergence
    };
    Kind mKind;

    // The point we are running to.
    ExecutionPoint mPoint;

    // If we are recovering from a recording divergence, the information to
    // instantiate the pause state with when we reach the target point.
    PauseInfo mPauseInfo;
};

// Phase when the replaying process is running forward from a snapshot to a
// particular execution point.
class ReachPointPhase : public NavigationPhase
{
    // Information about the search.
    ReachPointInfo mInfo;

    // How much of the point we have reached so far.
    ExecutionPoint::Prefix mReached;

  public:
    void enter(const ReachPointInfo& info, bool rewind);

    void toString(char* buf, size_t len) override {
        snprintf(buf, len, "ReachPoint");
    }

    void afterSnapshot(size_t snapshot, bool final) override;
    void positionHit(const std::function<bool(const ExecutionPosition&)>& match) override;
};

// Phase when the replaying process is searching forward from a snapshot to
// find the last point a breakpoint is hit before reaching an execution point.
class FindLastHitPhase : public NavigationPhase
{
    // Endpoint of the search. The positions in this may be empty, in which
    // case the endpoint is the following snapshot.
    ExecutionPoint mPoint;

    // How much of the endpoint we have reached so far.
    ExecutionPoint::Prefix mReached;

    // All positions we are interested in hits for, including all breakpoint
    // positions (and possibly other positions).
    UntrackedExecutionPositionVector mTrackedPositions;

    // Tracked positions that have been reached since the snapshot, in the
    // order they were reached.
    UntrackedExecutionPositionVector mTrackedHits;

    void addTrackedPosition(const ExecutionPosition& position);
    void onRegionEnd();

  public:
    void enter(const ExecutionPoint& point);

    void toString(char* buf, size_t len) override {
        snprintf(buf, len, "FindLastHit");
    }

    void afterSnapshot(size_t snapshot, bool final) override;
    void resume(bool forward, bool hitOtherBreakpoints) override;
    void positionHit(const std::function<bool(const ExecutionPosition&)>& match) override;
};

// Structure which manages state about the breakpoints in existence and about
// how the process is being navigated through. This is allocated in untracked
// memory and its contents will not change when restoring an earlier snapshot.
struct NavigationState
{
    // All the currently installed breakpoints, indexed by their ID.
    UntrackedExecutionPositionVector breakpoints;

    ExecutionPosition& getBreakpoint(size_t id) {
        while (id >= breakpoints.length())
            TRY(breakpoints.emplaceBack());
        return breakpoints[id];
    }

    // The current phase of the process.
    NavigationPhase* mPhase;

    void setPhase(NavigationPhase* phase) {
        mPhase = phase;

        /*
        char buf[1024];
        mPhase->toString(buf, sizeof(buf));

        AutoEnsurePassThroughThreadEvents pt;
        fprintf(stderr, "SetNavigationPhase %s\n", buf);
        */
    }

    BreakpointPausedPhase mBreakpointPausedPhase;
    SnapshotPausedPhase mSnapshotPausedPhase;
    ForwardPhase mForwardPhase;
    ReachPointPhase mReachPointPhase;
    FindLastHitPhase mFindLastHitPhase;

    // Note: NavigationState is initially zeroed.
    NavigationState()
      : mPhase(&mForwardPhase)
    {}
};

static NavigationState* gNavigation;

// Make sure the positionHit() method will be called whenever |position| is
// reached. This is valid until the next rewind or snapshot is reached.
static void EnsurePositionHandler(const ExecutionPosition& position);

static void
GetAllBreakpointHits(const std::function<bool(const ExecutionPosition&)>& match,
                     BreakpointVector& hitBreakpoints)
{
    for (size_t id = 0; id < gNavigation->breakpoints.length(); id++) {
        const ExecutionPosition& breakpoint = gNavigation->breakpoints[id];
        if (breakpoint.isValid() && match(breakpoint))
            TRY(hitBreakpoints.append(id));
    }
}

///////////////////////////////////////////////////////////////////////////////
// BreakpointPaused Phase
///////////////////////////////////////////////////////////////////////////////

void
BreakpointPausedPhase::enter(const PauseInfo& info, bool recoveringFromDivergence /* = false */)
{
    mInfo = info;

    mRecoveringFromDivergence = recoveringFromDivergence;
    mPendingBreakpointOperations.clear();
    mRequestIndex = 0;

    gNavigation->setPhase(this);

    JS::replay::hooks.hitBreakpointReplay(mInfo.mBreakpoint, mRecoveringFromDivergence);
}

void
BreakpointPausedPhase::afterSnapshot(size_t snapshot, bool final)
{
    // We just restored a snapshot because an unhandled recording
    // divergence was encountered while responding to a debugger request.
    MOZ_RELEASE_ASSERT(mInfo.mPoint.snapshot == snapshot);
    MOZ_RELEASE_ASSERT(!mRecoveringFromDivergence);

    // Return to the point where we were just paused at, remembering that we
    // will need to finish recovering from the divergence once we get there.
    ReachPointInfo info;
    info.mKind = ReachPointInfo::RecoverFromDivergence;
    info.mPoint = mInfo.mPoint;
    info.mPauseInfo = mInfo;
    gNavigation->mReachPointPhase.enter(info, /* rewind = */ false);
}

void
BreakpointPausedPhase::resume(bool forward, bool hitOtherBreakpoints)
{
    MOZ_RELEASE_ASSERT(!mRecoveringFromDivergence);

    ReplayDebugger::ClearDebuggerPausedObjects();

    if (hitOtherBreakpoints) {
        // hitOtherBreakpoints should be set only if we didn't do anything
        // meaningful at this breakpoint. There isn't anything in place to
        // enforce this, though.
        MOZ_RELEASE_ASSERT(mPendingBreakpointOperations.empty());

        if (!mInfo.mRemainingBreakpoints.empty()) {
            // Enter a nested pause at the next breakpoint in the list.
            PauseInfo newInfo;
            newInfo.mPoint = mInfo.mPoint;
            newInfo.mBreakpoint = mInfo.mRemainingBreakpoints[0];
            for (size_t i = 1; i < mInfo.mRemainingBreakpoints.length(); i++)
                TRY(newInfo.mRemainingBreakpoints.append(mInfo.mRemainingBreakpoints[i]));
            gNavigation->mBreakpointPausedPhase.enter(newInfo);
            return;
        }
    }

    // Apply changes to installed breakpoints.
    for (auto entry : mPendingBreakpointOperations) {
        ExecutionPosition& breakpoint = gNavigation->getBreakpoint(entry.first);
        breakpoint = entry.second;
    }
    mPendingBreakpointOperations.clear();

    if (forward) {
        // If we are paused at a breakpoint and are replaying, we may have
        // diverged from the recording. We have to clear any unwanted changes
        // induced by evals and so forth by rewinding to the last snapshot
        // encountered, then running  forward to the current execution point
        // and resuming normal forward execution from there.
        if (IsReplaying()) {
            ReachPointInfo info;
            info.mKind = ReachPointInfo::Resume;
            info.mPoint = mInfo.mPoint;
            gNavigation->mReachPointPhase.enter(info, /* rewind = */ true);
            MOZ_CRASH(); // Unreachable.
        }

        // Run forward from the current execution point.
        gNavigation->mForwardPhase.enter(mInfo.mPoint);
        return;
    }

    // Search backwards in the execution space.
    gNavigation->mFindLastHitPhase.enter(mInfo.mPoint);
    MOZ_CRASH(); // Unreachable.
}

bool
BreakpointPausedPhase::getPoppedFrameResult(bool* throwing, MutableHandleValue result)
{
    // Ignore the pop frame result unless we're paused at an OnPop breakpoint.
    if (gNavigation->getBreakpoint(mInfo.mBreakpoint).kind != ExecutionPosition::OnPop)
        return false;

    *throwing = gPopFrameThrowing;
    result.set(*gPopFrameResult);
    return true;
}

void
BreakpointPausedPhase::handleDebuggerRequest(JS::replay::CharBuffer* requestBuffer)
{
    MOZ_RELEASE_ASSERT(!mRecoveringFromDivergence);

    TRY(mInfo.mRequests.emplaceBack());
    RequestInfo& info = mInfo.mRequests.back();
    mRequestIndex = mInfo.mRequests.length() - 1;

    TRY(info.requestBuffer.append(requestBuffer->begin(), requestBuffer->length()));

    Maybe<JS::replay::CharBuffer> responseBuffer;
    ReplayDebugger::ProcessRequest(requestBuffer->begin(), requestBuffer->length(), &responseBuffer);

    js_delete(requestBuffer);

    if (responseBuffer.isSome()) {
        TRY(info.responseBuffer.append(responseBuffer.ref().begin(), responseBuffer.ref().length()));
        JS::replay::hooks.debugResponseReplay(responseBuffer.ref());
    }
}

void
BreakpointPausedPhase::respondAfterRecoveringFromDivergence()
{
    MOZ_RELEASE_ASSERT(mRecoveringFromDivergence);
    MOZ_RELEASE_ASSERT(mInfo.mRequests.length());

    // Remember that the last request has triggered an unhandled divergence.
    MOZ_RELEASE_ASSERT(!mInfo.mRequests.back().unhandledDivergence);
    mInfo.mRequests.back().unhandledDivergence = true;

    // Redo all existing requests.
    for (size_t i = 0; i < mInfo.mRequests.length(); i++) {
        RequestInfo& info = mInfo.mRequests[i];
        mRequestIndex = i;

        Maybe<JS::replay::CharBuffer> responseBuffer;
        ReplayDebugger::ProcessRequest(info.requestBuffer.begin(), info.requestBuffer.length(), &responseBuffer);

        if (i < mInfo.mRequests.length() - 1) {
            // This is an old request, and we don't need to send another
            // response to it. Make sure the response we just generated matched
            // the earlier one we sent, though.
            if (responseBuffer.isSome()) {
                MOZ_RELEASE_ASSERT(responseBuffer.ref().length() == info.responseBuffer.length());
                MOZ_RELEASE_ASSERT(memcmp(responseBuffer.ref().begin(), info.responseBuffer.begin(),
                                          responseBuffer.ref().length() * sizeof(char16_t)) == 0);
            } else {
                MOZ_RELEASE_ASSERT(info.responseBuffer.empty());
            }
        } else {
            // This is the current request we need to respond to.
            MOZ_RELEASE_ASSERT(responseBuffer.isSome());
            MOZ_RELEASE_ASSERT(info.responseBuffer.empty());
            TRY(info.responseBuffer.append(responseBuffer.ref().begin(), responseBuffer.ref().length()));
            JS::replay::hooks.debugResponseReplay(responseBuffer.ref());
        }
    }

    // We've finished recovering, and can now process new incoming requests.
    mRecoveringFromDivergence = false;
}

void
BreakpointPausedPhase::addBreakpointOperation(size_t id, const ExecutionPosition& position)
{
    TRY(mPendingBreakpointOperations.emplaceBack(id, position));
}

bool
BreakpointPausedPhase::maybeDivergeFromRecording()
{
    if (IsRecording()) {
        // Recording divergence is not supported if we are still recording.
        // We don't rewind processes that are still recording, and can't simply
        // allow execution to proceed from here as if we were not diverged, since
        // any events or other activity that show up afterwards won't occur when we
        // are replaying later.
        return false;
    }
    if (mInfo.mRequests[mRequestIndex].unhandledDivergence)
        return false;
    DivergeFromRecording();
    return true;
}

///////////////////////////////////////////////////////////////////////////////
// SnapshotPausedPhase
///////////////////////////////////////////////////////////////////////////////

void
SnapshotPausedPhase::enter(size_t snapshot, bool final, bool rewind)
{
    mSnapshot = snapshot;
    mFinal = final;

    gNavigation->setPhase(this);

    if (rewind) {
        RestoreSnapshotAndResume(mSnapshot);
        MOZ_CRASH(); // Unreachable.
    }

    afterSnapshot(mSnapshot, mFinal);
}

void
SnapshotPausedPhase::afterSnapshot(size_t snapshot, bool final)
{
    MOZ_RELEASE_ASSERT(snapshot == mSnapshot);
    MOZ_RELEASE_ASSERT(final == mFinal);
    JS::replay::hooks.hitSnapshotReplay(mSnapshot, mFinal, /* interim = */ false);
}

void
SnapshotPausedPhase::resume(bool forward, bool hitOtherBreakpoints)
{
    ReplayDebugger::ClearDebuggerPausedObjects();

    // Stay paused if we are running off either end of the replay.
    if (forward ? mFinal : !mSnapshot) {
        JS::replay::hooks.hitSnapshotReplay(mSnapshot, mFinal, /* interim = */ false);
        return;
    }

    if (forward) {
        // Run forward from the current execution point.
        ExecutionPoint search;
        search.snapshot = mSnapshot;
        gNavigation->mForwardPhase.enter(search);
    } else {
        ExecutionPoint search;
        search.snapshot = mSnapshot - 1;
        gNavigation->mFindLastHitPhase.enter(search);
        MOZ_CRASH(); // Unreachable.
    }
}

void
SnapshotPausedPhase::handleDebuggerRequest(JS::replay::CharBuffer* requestBuffer)
{
    Maybe<JS::replay::CharBuffer> responseBuffer;
    ReplayDebugger::ProcessRequest(requestBuffer->begin(), requestBuffer->length(), &responseBuffer);

    js_delete(requestBuffer);

    if (responseBuffer.isSome())
        JS::replay::hooks.debugResponseReplay(responseBuffer.ref());
}

void
SnapshotPausedPhase::addBreakpointOperation(size_t id, const ExecutionPosition& pos)
{
    ExecutionPosition& breakpoint = gNavigation->getBreakpoint(id);
    breakpoint = pos;
}

///////////////////////////////////////////////////////////////////////////////
// ForwardPhase
///////////////////////////////////////////////////////////////////////////////

void
ForwardPhase::enter(const ExecutionPoint& point)
{
    mPoint = point;

    gNavigation->setPhase(this);

    // Install handlers for all breakpoints.
    for (const ExecutionPosition& breakpoint : gNavigation->breakpoints) {
        if (breakpoint.isValid())
            EnsurePositionHandler(breakpoint);
    }

    ResumeExecution();
}

void
ForwardPhase::afterSnapshot(size_t snapshot, bool final)
{
    MOZ_RELEASE_ASSERT(snapshot == mPoint.snapshot + 1);
    gNavigation->mSnapshotPausedPhase.enter(snapshot, final, /* rewind = */ false);
}

void
ForwardPhase::positionHit(const std::function<bool(const ExecutionPosition&)>& match)
{
    BreakpointVector hitBreakpoints;
    GetAllBreakpointHits(match, hitBreakpoints);

    if (!hitBreakpoints.empty()) {
        size_t breakpointId = hitBreakpoints[0];

        PauseInfo info;
        info.mPoint = mPoint;
        TRY(info.mPoint.positions.append(gNavigation->getBreakpoint(breakpointId)));
        info.mBreakpoint = breakpointId;
        for (size_t i = 1; i < hitBreakpoints.length(); i++)
            TRY(info.mRemainingBreakpoints.append(hitBreakpoints[i]));
        gNavigation->mBreakpointPausedPhase.enter(info);
    }
}

///////////////////////////////////////////////////////////////////////////////
// ReachPointPhase
///////////////////////////////////////////////////////////////////////////////

void
ReachPointPhase::enter(const ReachPointInfo& info, bool rewind)
{
    MOZ_RELEASE_ASSERT(!info.mPoint.positions.empty());

    mInfo = info;
    mReached = 0;

    gNavigation->setPhase(this);

    if (rewind) {
        RestoreSnapshotAndResume(mInfo.mPoint.snapshot);
        MOZ_CRASH(); // Unreachable.
    } else {
        afterSnapshot(mInfo.mPoint.snapshot, false);
    }
}

void
ReachPointPhase::afterSnapshot(size_t snapshot, bool final)
{
    MOZ_RELEASE_ASSERT(snapshot == mInfo.mPoint.snapshot);
    EnsurePositionHandler(mInfo.mPoint.positions[0]);
}

void
ReachPointPhase::positionHit(const std::function<bool(const ExecutionPosition&)>& match)
{
    if (!match(mInfo.mPoint.positions[mReached]))
        return;

    if (++mReached < mInfo.mPoint.positions.length()) {
        EnsurePositionHandler(mInfo.mPoint.positions[mReached]);
        return;
    }

    switch (mInfo.mKind) {
      case ReachPointInfo::Resume:
        gNavigation->mForwardPhase.enter(mInfo.mPoint);
        return;
      case ReachPointInfo::HitBreakpoint: {
        BreakpointVector hitBreakpoints;
        GetAllBreakpointHits(match, hitBreakpoints);
        MOZ_RELEASE_ASSERT(!hitBreakpoints.empty());
        
        PauseInfo info;
        info.mPoint = mInfo.mPoint;
        info.mBreakpoint = hitBreakpoints[0];
        for (size_t i = 1; i < hitBreakpoints.length(); i++)
            TRY(info.mRemainingBreakpoints.append(hitBreakpoints[i]));
        gNavigation->mBreakpointPausedPhase.enter(info);
        return;
      }
      case ReachPointInfo::RecoverFromDivergence:
        MOZ_RELEASE_ASSERT(match(gNavigation->getBreakpoint(mInfo.mPauseInfo.mBreakpoint)));
        gNavigation->mBreakpointPausedPhase.enter(mInfo.mPauseInfo,
                                                       /* recoveringFromDivergence = */ true);
        return;
    }
}

///////////////////////////////////////////////////////////////////////////////
// FindLastHitPhase
///////////////////////////////////////////////////////////////////////////////

void
FindLastHitPhase::addTrackedPosition(const ExecutionPosition& position)
{
    for (const ExecutionPosition& existing : mTrackedPositions) {
        if (existing == position)
            return;
    }
    TRY(mTrackedPositions.append(position));
}

void
FindLastHitPhase::enter(const ExecutionPoint& point)
{
    mPoint = point;

    mTrackedPositions.clear();
    mTrackedHits.clear();
    mReached = 0;

    gNavigation->setPhase(this);

    // All breakpoints are tracked positions.
    for (const ExecutionPosition& breakpoint : gNavigation->breakpoints) {
        if (breakpoint.isValid())
            addTrackedPosition(breakpoint);
    }

    RestoreSnapshotAndResume(mPoint.snapshot);
    MOZ_CRASH(); // Unreachable.
}

void
FindLastHitPhase::afterSnapshot(size_t snapshot, bool final)
{
    if (snapshot == mPoint.snapshot + 1) {
        // We reached the next snapshot, and are done searching.
        MOZ_RELEASE_ASSERT(mPoint.positions.empty());
        onRegionEnd();
        MOZ_CRASH(); // Unreachable.
    }

    // We are at the start of the search.
    MOZ_RELEASE_ASSERT(snapshot == mPoint.snapshot);
    MOZ_RELEASE_ASSERT(mTrackedHits.empty());

    for (const ExecutionPosition& position : mTrackedPositions)
        EnsurePositionHandler(position);

    if (!mPoint.positions.empty())
        EnsurePositionHandler(mPoint.positions[0]);
}

void
FindLastHitPhase::resume(bool forward, bool hitOtherBreakpoints)
{
    // The LastHitPhase will pause at interim snapshots.
    MOZ_RELEASE_ASSERT(forward);
    ResumeExecution();
}

void
FindLastHitPhase::positionHit(const std::function<bool(const ExecutionPosition&)>& match)
{
    if (!mPoint.positions.empty()) {
        if (match(mPoint.positions[mReached])) {
            if (++mReached == mPoint.positions.length()) {
                onRegionEnd();
                MOZ_CRASH(); // Unreachable.
            } else {
                EnsurePositionHandler(mPoint.positions[mReached]);
            }
        }
    }

    for (const ExecutionPosition& position : mTrackedPositions) {
        if (match(position)) {
            TRY(mTrackedHits.append(position));
            break;
        }
    }
}

static bool
PositionMatchesBreakpoint(const ExecutionPosition& pos)
{
    for (const ExecutionPosition& breakpoint : gNavigation->breakpoints) {
        if (breakpoint == pos)
            return true;
    }
    return false;
}

void
FindLastHitPhase::onRegionEnd()
{
    // Find the index of the last hit which coincides with a breakpoint.
    int lastHit;
    for (lastHit = mTrackedHits.length() - 1; lastHit >= 0; lastHit--) {
        if (PositionMatchesBreakpoint(mTrackedHits[lastHit]))
            break;
    }

    if (lastHit < 0) {
        // No breakpoints were encountered up until the execution point.
        // Rewind to the last snapshot and pause.
        gNavigation->mSnapshotPausedPhase.enter(mPoint.snapshot, false, /* rewind = */ true);
        MOZ_CRASH(); // Unreachable.
    }

    // Construct an execution point for the last breakpoint hit to return to
    // after rewinding.
    ExecutionPoint newPoint;
    newPoint.snapshot = mPoint.snapshot;
    for (size_t i = 0; i <= (size_t) lastHit; i++) {
        const ExecutionPosition& pos = mTrackedHits[i];
        if (pos == mTrackedHits[lastHit])
            TRY(newPoint.positions.append(pos));
    }
    MOZ_RELEASE_ASSERT(!newPoint.positions.empty());

    ReachPointInfo info;
    info.mKind = ReachPointInfo::HitBreakpoint;
    info.mPoint = newPoint;
    gNavigation->mReachPointPhase.enter(info, /* rewind = */ true);
    MOZ_CRASH(); // Unreachable.
}

///////////////////////////////////////////////////////////////////////////////
// Debugger Handlers
///////////////////////////////////////////////////////////////////////////////

// Replay phases can install handlers on ExecutionPositions that call back
// into the phase's positionHit method when the position is reached.

// Handler installed for hits on a script/pc.
static bool
ScriptPcHandler(JSContext* cx, unsigned argc, Value* vp)
{
    CallArgs args = CallArgsFromVp(argc, vp);

    jsbytecode* pc;
    JSScript* script = cx->currentScript(&pc, JSContext::ALLOW_CROSS_COMPARTMENT);
    MOZ_RELEASE_ASSERT(script && pc);

    size_t scriptId = ReplayDebugger::ScriptId(script);
    MOZ_RELEASE_ASSERT(scriptId);

    size_t offset = pc - script->code();
    size_t frameIndex = ReplayDebugger::CountScriptFrames(cx) - 1;

    gNavigation->mPhase->positionHit(
        [=](const ExecutionPosition& position) {
            return position.script == scriptId
                && position.offset == offset
                && (position.kind == ExecutionPosition::Break ||
                    position.frameIndex == frameIndex);
        });

    args.rval().setUndefined();
    return true;
}

static bool
EnterFrameHandler(JSContext* cx, unsigned argc, Value* vp)
{
    CallArgs args = CallArgsFromVp(argc, vp);

    gNavigation->mPhase->positionHit(
        [=](const ExecutionPosition& position) {
            return position.kind == ExecutionPosition::EnterFrame;
        });

    args.rval().setUndefined();
    return true;
}

/* static */ bool
ReplayDebugger::onLeaveFrame(JSContext* cx, AbstractFramePtr frame, jsbytecode* pc, bool ok)
{
    MOZ_RELEASE_ASSERT(IsRecordingOrReplaying());

    JSScript* script = frame.script();
    if (!script)
        return ok;
    size_t scriptId = ScriptId(script);
    if (!scriptId)
        return ok;

    // Update the frame return state in case we hit a breakpoint here.
    gPopFrameThrowing = !ok;
    *gPopFrameResult = frame.returnValue();

    gNavigation->mPhase->positionHit(
        [=](const ExecutionPosition& position) {
            return position.kind == ExecutionPosition::OnPop
                && (position.script == ExecutionPosition::EMPTY_SCRIPT ||
                    position.script == scriptId);
        });

    *gPopFrameResult = UndefinedValue();

    return ok;
}

// Structure for encapsulating the installation and management of installed
// handlers on the singleton replaying process debugger.
class DebuggerHandlerManager
{
    // Which handlers are currently installed. We cannot have duplicate handlers,
    // even if there are multiple breakpoints for the same position, as each
    // handler triggers all breakpoints for the position.
    Vector<std::pair<size_t, size_t>, 0, SystemAllocPolicy> mInstalledScriptPcHandlers;
    bool mInstalledEnterFrameHandler;

    // Handlers we tried to install but couldn't due to a script not existing.
    Vector<ExecutionPosition, 0, SystemAllocPolicy> mPendingHandlers;

    // Try to install a handler, returning true on success and false if a
    // required script does not exist yet.
    bool tryInstallHandler(JSContext* cx, const ExecutionPosition& position) {
        MOZ_RELEASE_ASSERT(position.isValid());
        JSAutoCompartment ac(cx, *ReplayDebugger::gHookGlobal);
        RootedValue unused(cx);

        RootedScript script(cx);
        if (position.script != ExecutionPosition::EMPTY_SCRIPT) {
            script = ReplayDebugger::IdScript(position.script);
            if (!script)
                return false;
            RootedValue scriptGlobal(cx, ObjectValue(script->global()));
            TRY(JS_WrapValue(cx, &scriptGlobal));
            TRY(JS_CallFunctionName(cx, *gHookDebugger, "addDebuggee", HandleValueArray(scriptGlobal), &unused));
        }

        Debugger* debugger = Debugger::fromJSObject(*gHookDebugger);
        switch (position.kind) {
          case ExecutionPosition::Break:
          case ExecutionPosition::OnStep: {
            for (auto pair : mInstalledScriptPcHandlers) {
                if (pair.first == position.script && pair.second == position.offset)
                    return true;
            }

            Rooted<TaggedProto> nullProto(cx, TaggedProto(nullptr));
            RootedObject handler(cx, JS_NewObject(cx, nullptr));
            TRY(handler);

            RootedObject fun(cx, NewNativeFunction(cx, ScriptPcHandler, 1, nullptr));
            TRY(fun);

            RootedValue funValue(cx, ObjectValue(*fun));
            TRY(JS_DefineProperty(cx, handler, "hit", funValue, 0));

            RootedObject debugScript(cx, debugger->wrapScript(cx, script));
            TRY(debugScript);
            JS::AutoValueArray<2> args(cx);
            args[0].setInt32(position.offset);
            args[1].setObject(*handler);
            TRY(JS_CallFunctionName(cx, debugScript, "setBreakpoint", args, &unused));

            TRY(mInstalledScriptPcHandlers.emplaceBack(position.script, position.offset));
            break;
          }
          case ExecutionPosition::OnPop:
            if (script)
                TRY(debugger->ensureExecutionObservabilityOfScript(cx, script));
            else
                TRY(debugger->updateObservesAllExecutionOnDebuggees(cx, Debugger::Observing));
            break;
          case ExecutionPosition::EnterFrame: {
            if (mInstalledEnterFrameHandler)
                return true;
            RootedObject handler(cx, NewNativeFunction(cx, EnterFrameHandler, 1, nullptr));
            TRY(handler);
            RootedValue handlerValue(cx, ObjectValue(*handler));
            TRY(JS_SetProperty(cx, *gHookDebugger, "onEnterFrame", handlerValue));
            mInstalledEnterFrameHandler = true;
            break;
          }
        default:
          MOZ_CRASH();
        }
        return true;
    }

  public:
    void resetHandlers(JSContext* cx) {
        AutoDisallowThreadEvents disallow;
        RootedValue unused(cx);
        TRY(JS_CallFunctionName(cx, *gHookDebugger, "clearAllBreakpoints",
                                JS::HandleValueArray::empty(), &unused));
        TRY(JS_CallFunctionName(cx, *gHookDebugger, "removeAllDebuggees",
                                JS::HandleValueArray::empty(), &unused));

        mInstalledScriptPcHandlers.clear();
        mInstalledEnterFrameHandler = false;
        mPendingHandlers.clear();
    }

    void ensureHandler(const ExecutionPosition& position) {
        if (!tryInstallHandler(gHookContext, position))
            TRY(mPendingHandlers.append(position));
    }

    void onNewScript(size_t scriptId) {
        for (const ExecutionPosition& position : mPendingHandlers) {
            if (position.script == scriptId) {
                bool success = tryInstallHandler(gHookContext, position);
                MOZ_RELEASE_ASSERT(success);
            }
        }
    }
};

static DebuggerHandlerManager* gHandlerManager;

static void
EnsurePositionHandler(const ExecutionPosition& position)
{
    gHandlerManager->ensureHandler(position);
}

/* static */ void
ReplayDebugger::MaybeSetupBreakpointsForScript(size_t scriptId)
{
    gHandlerManager->onNewScript(scriptId);
}

///////////////////////////////////////////////////////////////////////////////
// Hooks
///////////////////////////////////////////////////////////////////////////////

static void
BeforeSnapshotHook()
{
    // Reset the debugger to a consistent state before each snapshot. Ensure
    // that the hook context and global exist and have a debugger object, and
    // that no debuggees have debugger information attached.

    if (!gHookContext || !ReplayDebugger::gHookGlobal)
        MOZ_CRASH();

    JSContext* cx = gHookContext;
    RootedObject hookGlobal(cx, *ReplayDebugger::gHookGlobal);

    JSAutoRequest ar(cx);
    JSAutoCompartment ac(cx, hookGlobal);

    if (!gHookDebugger) {
        TRY(JS_DefineDebuggerObject(cx, hookGlobal));

        RootedValue debuggerFunctionValue(cx);
        TRY(JS_GetProperty(cx, hookGlobal, "Debugger", &debuggerFunctionValue));

        RootedObject debuggerFunction(cx, &debuggerFunctionValue.toObject());
        RootedObject debuggerObject(cx);
        TRY(JS::Construct(cx, debuggerFunctionValue, debuggerFunction,
                          JS::HandleValueArray::empty(), &debuggerObject));

        gHookDebugger = js_new<PersistentRootedObject>(gHookContext);
        *gHookDebugger = debuggerObject;
        return;
    }

    gHandlerManager->resetHandlers(cx);
}

static void
AfterSnapshotHook(size_t snapshot, bool final, bool interim)
{
    MOZ_RELEASE_ASSERT(IsRecordingOrReplaying());

    // Interim snapshots come before the one we were trying to restore to.
    // Just notify the middleman so it can do the processing it needs.
    if (interim) {
        JS::replay::hooks.hitSnapshotReplay(snapshot, final, true);
        return;
    }

    gNavigation->mPhase->afterSnapshot(snapshot, final);
}

static void
BeforeLastDitchRestoreHook()
{
    MOZ_CRASH();
}

static void
DebugRequestHook(JS::replay::CharBuffer* requestBuffer)
{
    gNavigation->mPhase->handleDebuggerRequest(requestBuffer);
}

/* static */ bool
ReplayDebugger::GetPoppedFrameResult(bool* throwing, MutableHandleValue result)
{
    return gNavigation->mPhase->getPoppedFrameResult(throwing, result);
}

/* static */ bool
ReplayDebugger::MaybeDivergeFromRecording()
{
    return gNavigation->mPhase->maybeDivergeFromRecording();
}

/* static */ void
ReplayDebugger::AddBreakpointOperation(size_t id, const ExecutionPosition& position)
{
    return gNavigation->mPhase->addBreakpointOperation(id, position);
}

static void
ResumeHook(bool forward, bool hitOtherBreakpoints)
{
    gNavigation->mPhase->resume(forward, hitOtherBreakpoints);
}

static void
RespondAfterRecoveringFromDivergenceHook()
{
    MOZ_RELEASE_ASSERT(gNavigation->mPhase == &gNavigation->mBreakpointPausedPhase);
    gNavigation->mBreakpointPausedPhase.respondAfterRecoveringFromDivergence();
}

/* static */ void
ReplayDebugger::Initialize()
{
    if (IsMiddleman()) {
        JS::replay::hooks.hitBreakpointMiddleman = ReplayDebugger::hitBreakpointMiddleman;
    } else if (IsRecordingOrReplaying()) {
        InitializeContentSet();
        void* navigationMem =
            AllocateMemory(sizeof(NavigationState), DebuggerAllocatedMemoryKind);
        gNavigation = new (navigationMem) NavigationState();
        gHandlerManager = js_new<DebuggerHandlerManager>();

        JS::replay::hooks.debugRequestReplay = DebugRequestHook;
        JS::replay::hooks.resumeReplay = ResumeHook;
        JS::replay::hooks.respondAfterRecoveringFromDivergence = RespondAfterRecoveringFromDivergenceHook;

        SetSnapshotHooks(::BeforeSnapshotHook, ::AfterSnapshotHook, ::BeforeLastDitchRestoreHook);
    }
}
