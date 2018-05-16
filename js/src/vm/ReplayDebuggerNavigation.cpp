/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/Sprintf.h"
#include "vm/ReplayDebugger.h"

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
// The precise execution position of the child process is managed by the
// child process itself. The middleman will send the child process
// Resume messages to travel forward and backward, but it is up to the child
// process to keep track of the rewinding and resuming necessary to find the
// next or previous point where a breakpoint or checkpoint is hit.

static JSContext* gHookContext;
static PersistentRootedObject* gHookDebugger;

JSRuntime* ReplayDebugger::gMainRuntime;
JS::replay::ProgressCounter ReplayDebugger::gProgressCounter;
PersistentRootedObject* ReplayDebugger::gHookGlobal;

// These are set whenever we stop on exit from a frame, and indicate the
// execution status of that frame.
static bool gPopFrameThrowing;
static PersistentRootedValue* gPopFrameResult;

#define TRY(op) do { if (!(op)) MOZ_CRASH(#op); } while (false)

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

/* static */ bool
ReplayDebugger::trackProgressSlow(JSScript* script)
{
    // Only code that executes in the main runtime may be debugged, so only its
    // progress is tracked.
    if (script->runtimeFromAnyThread() != gMainRuntime)
        return false;
    // Whether self hosted scripts execute may depend on compilation mode and
    // performed optimizations.
    if (script->selfHosted())
        return false;
    return true;
}

/* static */ const char*
ReplayDebugger::progressString(const char* why, JSScript* script, jsbytecode* pc)
{
#if 0
    Sprinter sp(nullptr);
    TRY(sp.init());
    sp.printf("Progress: %s:%d:%d %s\n", script->filename(), (int) script->lineno(),
              (int) (pc ? pc - script->code() : 0), why);
    return strdup(sp.string());
#endif
    return nullptr;
}

typedef AllocPolicy<TrackedMemoryKind> TrackedAllocPolicy;
typedef AllocPolicy<DebuggerAllocatedMemoryKind> UntrackedAllocPolicy;

typedef JS::replay::ExecutionPosition ExecutionPosition;
typedef JS::replay::ExecutionPoint ExecutionPoint;

template <typename T>
static inline void
CopyVector(T& dst, const T& src)
{
    dst.clear();
    TRY(dst.append(src.begin(), src.length()));
}

static void
ExecutionPositionToString(const ExecutionPosition& pos, Sprinter& sp)
{
    sp.printf("{ Kind: %s, Script: %d, Offset: %d, Frame: %d }",
              pos.kindString(), (int) pos.script, (int) pos.offset, (int) pos.frameIndex);
}

static void
ExecutionPointToString(const ExecutionPoint& point, Sprinter& sp)
{
    sp.printf("{ Checkpoint %d", (int) point.checkpoint);
    if (point.hasPosition()) {
        sp.printf(" Progress %llu Position ", point.progress);
        ExecutionPositionToString(point.position, sp);
    }
    sp.printf(" }");
}

static CheckpointId
NextTemporaryCheckpoint(const CheckpointId& checkpoint)
{
    return CheckpointId(checkpoint.mNormal, checkpoint.mTemporary + 1);
}

static CheckpointId
NextNormalCheckpoint(const CheckpointId& checkpoint)
{
    return CheckpointId(checkpoint.mNormal + 1);
}

// Abstract class for where we are at in the navigation state machine.
// Each subclass has a single instance contained in NavigationState (see below)
// and it and all its data are allocated using untracked memory that is not
// affected by restoring earlier checkpoints.
class NavigationPhase
{
    // All virtual members should only be accessed through NavigationState.
    friend class NavigationState;

    MOZ_NORETURN void unsupported(const char* operation) {
        Sprinter sp(nullptr);
        (void) sp.init();
        toString(sp);

        Print("Operation %s not supported: %s\n", operation, sp.string());
        MOZ_CRASH("Unsupported navigation operation");
    }

    virtual void toString(Sprinter& sp) = 0;

    // The process has just reached or rewound to a checkpoint.
    virtual void afterCheckpoint(const CheckpointId& checkpoint) {
        unsupported("afterCheckpoint");
    }

    // Called when some position with an installed handler has been reached.
    virtual void positionHit(const ExecutionPoint& point) {
        unsupported("positionHit");
    }

    // Called after receiving a resume command from the middleman.
    virtual void resume(bool forward) {
        unsupported("resume");
    }

    // Called after the middleman tells us to rewind to a specific checkpoint.
    virtual void restoreCheckpoint(size_t checkpoint) {
        unsupported("restoreCheckpoint");
    }

    // Process an incoming debugger request from the middleman.
    virtual void handleDebuggerRequest(JS::replay::CharBuffer* requestBuffer) {
        unsupported("handleDebuggerRequest");
    }

    // A debugger request wants to know the result of a just-popped frame.
    virtual void getPoppedFrameResult(bool* throwing, MutableHandleValue result) {
        unsupported("getPoppedFrameResult");
    }

    // Called when a debugger request wants to try an operation that may
    // trigger an unhandled divergence from the recording.
    virtual bool maybeDivergeFromRecording() {
        unsupported("maybeDivergeFromRecording");
    }

    // Get the current execution point when recording.
    virtual ExecutionPoint getRecordingEndpoint() {
        unsupported("getRecordingEndpoint");
    }

    // Called when execution reaches the endpoint of the recording.
    virtual void hitRecordingEndpoint(const ExecutionPoint& point) {
        unsupported("hitRecordingEndpoint");
    }
};

// Information about a debugger request sent by the middleman.
struct RequestInfo {
    // JSON contents for the request and response.
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

typedef Vector<uint32_t, 0, SystemAllocPolicy> BreakpointVector;

// Phase when the replaying process is paused at a breakpoint.
class BreakpointPausedPhase : public NavigationPhase
{
    // Where the pause is at.
    ExecutionPoint mPoint;

    // All debugger requests we have seen while paused here.
    UntrackedRequestVector mRequests;

    // Whether we had to restore a checkpoint to deal with an unhandled
    // recording divergence, and haven't finished rehandling old requests.
    bool mRecoveringFromDivergence;

    // Index of the request currently being processed. Normally this is the
    // last entry in |mRequests|, though may be earlier if we are recovering
    // from an unhandled divergence.
    size_t mRequestIndex;

    // Set when we were told to resume forward and need to clean up our state.
    bool mResumeForward;

  public:
    void enter(const ExecutionPoint& point, const BreakpointVector& breakpoints);

    void toString(Sprinter& sp) override {
        sp.printf("BreakpointPaused RecoveringFromDivergence %d", mRecoveringFromDivergence);
    }

    void afterCheckpoint(const CheckpointId& checkpoint) override;
    void positionHit(const ExecutionPoint& point) override;
    void resume(bool forward) override;
    void restoreCheckpoint(size_t checkpoint) override;
    void getPoppedFrameResult(bool* throwing, MutableHandleValue result) override;
    void handleDebuggerRequest(JS::replay::CharBuffer* requestBuffer) override;
    bool maybeDivergeFromRecording() override;
    ExecutionPoint getRecordingEndpoint() override;

    void respondAfterRecoveringFromDivergence();
};

// Phase when the replaying process is paused at a normal checkpoint.
class CheckpointPausedPhase : public NavigationPhase
{
    size_t mCheckpoint;
    bool mAtRecordingEndpoint;

  public:
    void enter(size_t checkpoint, bool rewind, bool atRecordingEndpoint);

    void toString(Sprinter& sp) override {
        sp.printf("CheckpointPaused");
    }

    void afterCheckpoint(const CheckpointId& checkpoint) override;
    void positionHit(const ExecutionPoint& point) override;
    void resume(bool forward) override;
    void restoreCheckpoint(size_t checkpoint) override;
    void handleDebuggerRequest(JS::replay::CharBuffer* requestBuffer) override;
    ExecutionPoint getRecordingEndpoint() override;
};

// Phase when execution is proceeding forwards in search of breakpoint hits.
class ForwardPhase : public NavigationPhase
{
    // Some execution point in the recent past. There are no checkpoints or
    // breakpoint hits between this point and the current point of execution.
    ExecutionPoint mPoint;

  public:
    void enter(const ExecutionPoint& point);

    void toString(Sprinter& sp) override {
        sp.printf("Forward");
    }

    void afterCheckpoint(const CheckpointId& checkpoint) override;
    void positionHit(const ExecutionPoint& point) override;
    void hitRecordingEndpoint(const ExecutionPoint& point) override;
};

// Phase when the replaying process is running forward from a checkpoint to a
// breakpoint at a particular execution point.
class ReachBreakpointPhase : public NavigationPhase
{
  private:
    // Where to start running from.
    CheckpointId mStart;

    // The point we are running to.
    ExecutionPoint mPoint;

    // Point at which to decide whether to save a temporary checkpoint.
    Maybe<ExecutionPoint> mTemporaryCheckpoint;

    // Whether we have saved a temporary checkpoint at the specified point.
    bool mSavedTemporaryCheckpoint;

    // The time at which we started running forward from the initial
    // checkpoint.
    TimeStamp mStartTime;

  public:
    // Note: this always rewinds.
    void enter(const CheckpointId& start,
               const ExecutionPoint& point,
               const Maybe<ExecutionPoint>& temporaryCheckpoint);

    void toString(Sprinter& sp) override {
        sp.printf("ReachBreakpoint: ");
        ExecutionPointToString(mPoint, sp);
        if (mTemporaryCheckpoint.isSome()) {
            sp.printf(" TemporaryCheckpoint: ");
            ExecutionPointToString(mTemporaryCheckpoint.ref(), sp);
        }
    }

    void afterCheckpoint(const CheckpointId& checkpoint) override;
    void positionHit(const ExecutionPoint& point) override;
};

// Phase when the replaying process is searching forward from a checkpoint to
// find the last point a breakpoint is hit before reaching an execution point.
class FindLastHitPhase : public NavigationPhase
{
    // Where we started searching from.
    CheckpointId mStart;

    // Endpoint of the search, nothing if the endpoint is the next checkpoint.
    Maybe<ExecutionPoint> mEnd;

    // Counter that increases as we run forward, for ordering hits.
    size_t mCounter;

    // All positions we are interested in hits for, including all breakpoint
    // positions (and possibly other positions).
    struct TrackedPosition {
        ExecutionPosition position;

        // The last time this was hit so far, or invalid.
        ExecutionPoint lastHit;

        // The value of the counter when the last hit occurred.
        size_t lastHitCount;

        explicit TrackedPosition(const ExecutionPosition& position)
          : position(position), lastHitCount(0)
        {}
    };
    Vector<TrackedPosition, 4, UntrackedAllocPolicy> mTrackedPositions;

    TrackedPosition findTrackedPosition(const ExecutionPosition& pos);
    void onRegionEnd();

  public:
    // Note: this always rewinds.
    void enter(const CheckpointId& start, const Maybe<ExecutionPoint>& end);

    void toString(Sprinter& sp) override {
        sp.printf("FindLastHit");
    }

    void afterCheckpoint(const CheckpointId& checkpoint) override;
    void positionHit(const ExecutionPoint& point) override;
    void hitRecordingEndpoint(const ExecutionPoint& point) override;
};

// Make sure the positionHit() method will be called whenever |position| is
// reached. This is valid until the next checkpoint is reached or rewound to.
static void EnsurePositionHandler(const ExecutionPosition& position);

// Structure which manages state about the breakpoints in existence and about
// how the process is being navigated through. This is allocated in untracked
// memory and its contents will not change when restoring an earlier
// checkpoint.
class NavigationState
{
    // When replaying, the last known recording endpoint. There may be other,
    // later endpoints we haven't been informed about.
    ExecutionPoint mRecordingEndpoint;
    size_t mRecordingEndpointIndex;

    // The last checkpoint we ran forward or rewound to.
    CheckpointId mLastCheckpoint;

    // The locations of all temporary checkpoints we have saved. Temporary
    // checkpoints are taken immediately prior to reaching these points.
    Vector<ExecutionPoint, 0, UntrackedAllocPolicy> mTemporaryCheckpoints;

  public:
    // All the currently installed breakpoints, indexed by their ID.
    Vector<ExecutionPosition, 4, UntrackedAllocPolicy> mBreakpoints;

    ExecutionPosition& getBreakpoint(size_t id) {
        while (id >= mBreakpoints.length())
            TRY(mBreakpoints.emplaceBack());
        return mBreakpoints[id];
    }

    CheckpointId lastCheckpoint() {
        return mLastCheckpoint;
    }

    // The current phase of the process.
    NavigationPhase* mPhase;

    void setPhase(NavigationPhase* phase) {
        mPhase = phase;

        if (SpewEnabled()) {
            Sprinter sp(nullptr);
            (void) sp.init();
            mPhase->toString(sp);

            PrintSpew("SetNavigationPhase %s\n", sp.string());
        }
    }

    BreakpointPausedPhase mBreakpointPausedPhase;
    CheckpointPausedPhase mCheckpointPausedPhase;
    ForwardPhase mForwardPhase;
    ReachBreakpointPhase mReachBreakpointPhase;
    FindLastHitPhase mFindLastHitPhase;

    // For testing, specify that temporary checkpoints should be taken regardless
    // of how much time has elapsed.
    bool mAlwaysSaveTemporaryCheckpoints;

    // Note: NavigationState is initially zeroed.
    NavigationState()
      : mPhase(&mForwardPhase)
    {
        if (IsReplaying()) {
            // The recording must include everything up to the first
            // checkpoint. After that point we will ask the record/replay
            // system to notify us about any further endpoints.
            mRecordingEndpoint = ExecutionPoint(FirstCheckpointId);
        }
    }

    void afterCheckpoint(const CheckpointId& checkpoint) {
        mLastCheckpoint = checkpoint;

        // Forget any temporary checkpoints we just rewound past, or made
        // obsolete by reaching the next normal checkpoint.
        while (mTemporaryCheckpoints.length() > checkpoint.mTemporary)
            mTemporaryCheckpoints.popBack();

        mPhase->afterCheckpoint(checkpoint);

        // Make sure we don't run past the end of the recording.
        if (!checkpoint.mTemporary) {
            ExecutionPoint point(checkpoint.mNormal);
            checkForRecordingEndpoint(point);
        }

        MOZ_RELEASE_ASSERT(IsRecording() ||
                           checkpoint.mNormal <= mRecordingEndpoint.checkpoint);
        if (checkpoint.mNormal == mRecordingEndpoint.checkpoint) {
            MOZ_RELEASE_ASSERT(mRecordingEndpoint.hasPosition());
            EnsurePositionHandler(mRecordingEndpoint.position);
        }
    }

    void positionHit(const ExecutionPoint& point) {
        mPhase->positionHit(point);
        checkForRecordingEndpoint(point);
    }

    void resume(bool forward) {
        mPhase->resume(forward);
    }

    void restoreCheckpoint(size_t checkpoint) {
        mPhase->restoreCheckpoint(checkpoint);
    }

    void handleDebuggerRequest(JS::replay::CharBuffer* requestBuffer) {
        mPhase->handleDebuggerRequest(requestBuffer);
    }

    void getPoppedFrameResult(bool* throwing, MutableHandleValue result) {
        mPhase->getPoppedFrameResult(throwing, result);
    }

    bool maybeDivergeFromRecording() {
        return mPhase->maybeDivergeFromRecording();
    }

    ExecutionPoint getRecordingEndpoint() {
        return mPhase->getRecordingEndpoint();
    }

    void setRecordingEndpoint(size_t index, const ExecutionPoint& endpoint) {
        // Ignore endpoints older than the last one we know about.
        if (index <= mRecordingEndpointIndex)
            return;
        MOZ_RELEASE_ASSERT(mRecordingEndpoint.checkpoint <= endpoint.checkpoint);
        mRecordingEndpointIndex = index;
        mRecordingEndpoint = endpoint;
        if (endpoint.hasPosition())
            EnsurePositionHandler(endpoint.position);
    }

    void checkForRecordingEndpoint(const ExecutionPoint& point) {
        while (point == mRecordingEndpoint) {
            // The recording ended after the checkpoint, but maybe there is
            // another, later endpoint now. This may call back into
            // setRecordingEndpoint and notify us there is more recording data
            // available.
            if (!JS::replay::hooks.hitCurrentRecordingEndpointReplay())
                mPhase->hitRecordingEndpoint(mRecordingEndpoint);
        }
    }

    size_t numTemporaryCheckpoints() {
        return mTemporaryCheckpoints.length();
    }

    bool saveTemporaryCheckpoint(const ExecutionPoint& point) {
        MOZ_RELEASE_ASSERT(point.checkpoint == mLastCheckpoint.mNormal);
        TRY(mTemporaryCheckpoints.append(point));
        return NewCheckpoint(/* aTemporary = */ true);
    }

    ExecutionPoint lastTemporaryCheckpointLocation() {
        MOZ_RELEASE_ASSERT(!mTemporaryCheckpoints.empty());
        return mTemporaryCheckpoints.back();
    }

    CheckpointId lastTemporaryCheckpointId() {
        MOZ_RELEASE_ASSERT(!mTemporaryCheckpoints.empty());
        size_t normal = mTemporaryCheckpoints.back().checkpoint;
        size_t temporary = mTemporaryCheckpoints.length();
        return CheckpointId(normal, temporary);
    }
};

static NavigationState* gNavigation;

static void
GetAllBreakpointHits(const ExecutionPoint& point, BreakpointVector& hitBreakpoints)
{
    MOZ_RELEASE_ASSERT(point.hasPosition());
    for (size_t id = 0; id < gNavigation->mBreakpoints.length(); id++) {
        const ExecutionPosition& breakpoint = gNavigation->mBreakpoints[id];
        if (breakpoint.isValid() && breakpoint.subsumes(point.position))
            TRY(hitBreakpoints.append(id));
    }
}

///////////////////////////////////////////////////////////////////////////////
// BreakpointPaused Phase
///////////////////////////////////////////////////////////////////////////////

static bool
ThisProcessCanRewind()
{
    return JS::replay::hooks.canRewindReplay();
}

void
BreakpointPausedPhase::enter(const ExecutionPoint& point, const BreakpointVector& breakpoints)
{
    MOZ_RELEASE_ASSERT(point.hasPosition());

    mPoint = point;
    mRequests.clear();
    mRecoveringFromDivergence = false;
    mRequestIndex = 0;
    mResumeForward = false;

    gNavigation->setPhase(this);

    if (ThisProcessCanRewind()) {
        // Immediately save a temporary checkpoint and upate the point to be
        // in relation to this checkpoint. If we rewind due to a recording
        // divergence we will end up here.
        if (!gNavigation->saveTemporaryCheckpoint(point)) {
            // We just restored the checkpoint, and could be in any phase,
            // including this one.
            if (gNavigation->mPhase == this) {
                MOZ_RELEASE_ASSERT(!mRecoveringFromDivergence);
                // If we are transitioning to the forward phase, avoid hitting
                // breakpoints at this point.
                if (mResumeForward) {
                    gNavigation->mForwardPhase.enter(point);
                    return;
                }
                // Otherwise we restored after hitting an unhandled recording
                // divergence.
                mRecoveringFromDivergence = true;
                JS::replay::hooks.pauseAndRespondAfterRecoveringFromDivergence();
                MOZ_CRASH("Unreachable");
            }
            gNavigation->positionHit(point);
            return;
        }
    }

    bool endpoint = breakpoints.empty();
    JS::replay::hooks.hitBreakpointReplay(endpoint, breakpoints.begin(), breakpoints.length());

    // When rewinding is allowed we will rewind before resuming to erase side effects.
    MOZ_RELEASE_ASSERT(!ThisProcessCanRewind());
}

void
BreakpointPausedPhase::afterCheckpoint(const CheckpointId& checkpoint)
{
    // We just saved or restored the temporary checkpoint before reaching the
    // breakpoint.
    MOZ_RELEASE_ASSERT(ThisProcessCanRewind());
    MOZ_RELEASE_ASSERT(checkpoint == gNavigation->lastTemporaryCheckpointId());
}

void
BreakpointPausedPhase::positionHit(const ExecutionPoint& point)
{
    // Ignore positions hit while paused (we're probably doing an eval).
}

void
BreakpointPausedPhase::resume(bool forward)
{
    MOZ_RELEASE_ASSERT(!mRecoveringFromDivergence);

    if (forward) {
        // If we are paused at a breakpoint and can rewind, we may have
        // diverged from the recording. We have to clear any unwanted changes
        // induced by evals and so forth by restoring the temporary checkpoint
        // we saved before pausing here.
        if (ThisProcessCanRewind()) {
            mResumeForward = true;
            RestoreCheckpointAndResume(gNavigation->lastTemporaryCheckpointId());
            MOZ_CRASH("Unreachable");
        }

        ReplayDebugger::ClearDebuggerPausedObjects();

        // Run forward from the current execution point.
        gNavigation->mForwardPhase.enter(mPoint);
        return;
    }

    // Search backwards in the execution space.
    CheckpointId start = gNavigation->lastTemporaryCheckpointId();
    start.mTemporary--;
    gNavigation->mFindLastHitPhase.enter(start, Some(mPoint));
    MOZ_CRASH("Unreachable");
}

void
BreakpointPausedPhase::restoreCheckpoint(size_t checkpoint)
{
    gNavigation->mCheckpointPausedPhase.enter(checkpoint, /* rewind = */ true,
                                              /* atRecordingEndpoint = */ false);
}

void
BreakpointPausedPhase::getPoppedFrameResult(bool* throwing, MutableHandleValue result)
{
    *throwing = gPopFrameThrowing;
    result.set(*gPopFrameResult);
}

void
BreakpointPausedPhase::handleDebuggerRequest(JS::replay::CharBuffer* requestBuffer)
{
    MOZ_RELEASE_ASSERT(!mRecoveringFromDivergence);

    TRY(mRequests.emplaceBack());
    RequestInfo& info = mRequests.back();
    mRequestIndex = mRequests.length() - 1;

    TRY(info.requestBuffer.append(requestBuffer->begin(), requestBuffer->length()));

    JS::replay::CharBuffer responseBuffer;
    ReplayDebugger::ProcessRequest(requestBuffer->begin(), requestBuffer->length(), &responseBuffer);

    js_delete(requestBuffer);

    TRY(info.responseBuffer.append(responseBuffer.begin(), responseBuffer.length()));
    JS::replay::hooks.debugResponseReplay(responseBuffer);
}

void
BreakpointPausedPhase::respondAfterRecoveringFromDivergence()
{
    MOZ_RELEASE_ASSERT(mRecoveringFromDivergence);
    MOZ_RELEASE_ASSERT(mRequests.length());

    // Remember that the last request has triggered an unhandled divergence.
    MOZ_RELEASE_ASSERT(!mRequests.back().unhandledDivergence);
    mRequests.back().unhandledDivergence = true;

    // Redo all existing requests.
    for (size_t i = 0; i < mRequests.length(); i++) {
        RequestInfo& info = mRequests[i];
        mRequestIndex = i;

        JS::replay::CharBuffer responseBuffer;
        ReplayDebugger::ProcessRequest(info.requestBuffer.begin(), info.requestBuffer.length(), &responseBuffer);

        if (i < mRequests.length() - 1) {
            // This is an old request, and we don't need to send another
            // response to it. Make sure the response we just generated matched
            // the earlier one we sent, though.
            MOZ_RELEASE_ASSERT(responseBuffer.length() == info.responseBuffer.length());
            MOZ_RELEASE_ASSERT(memcmp(responseBuffer.begin(), info.responseBuffer.begin(),
                                      responseBuffer.length() * sizeof(char16_t)) == 0);
        } else {
            // This is the current request we need to respond to.
            MOZ_RELEASE_ASSERT(info.responseBuffer.empty());
            TRY(info.responseBuffer.append(responseBuffer.begin(), responseBuffer.length()));
            JS::replay::hooks.debugResponseReplay(responseBuffer);
        }
    }

    // We've finished recovering, and can now process new incoming requests.
    mRecoveringFromDivergence = false;
}

bool
BreakpointPausedPhase::maybeDivergeFromRecording()
{
    if (!ThisProcessCanRewind()) {
        // Recording divergence is not supported if we can't rewind. We can't
        // simply allow execution to proceed from here as if we were not
        // diverged, since any events or other activity that show up afterwards
        // will not be reflected in the recording.
        return false;
    }
    if (mRequests[mRequestIndex].unhandledDivergence)
        return false;
    DivergeFromRecording();
    return true;
}

ExecutionPoint
BreakpointPausedPhase::getRecordingEndpoint()
{
    MOZ_RELEASE_ASSERT(IsRecording());
    return mPoint;
}

///////////////////////////////////////////////////////////////////////////////
// CheckpointPausedPhase
///////////////////////////////////////////////////////////////////////////////

void
CheckpointPausedPhase::enter(size_t checkpoint, bool rewind, bool atRecordingEndpoint)
{
    mCheckpoint = checkpoint;
    mAtRecordingEndpoint = atRecordingEndpoint;

    gNavigation->setPhase(this);

    if (rewind) {
        RestoreCheckpointAndResume(CheckpointId(mCheckpoint));
        MOZ_CRASH("Unreachable");
    }

    afterCheckpoint(CheckpointId(mCheckpoint));
}

void
CheckpointPausedPhase::afterCheckpoint(const CheckpointId& checkpoint)
{
    MOZ_RELEASE_ASSERT(checkpoint == CheckpointId(mCheckpoint));
    JS::replay::hooks.hitCheckpointReplay(mCheckpoint, mAtRecordingEndpoint);
}

void
CheckpointPausedPhase::positionHit(const ExecutionPoint& point)
{
    // Ignore positions hit while paused (we're probably doing an eval).
}

void
CheckpointPausedPhase::resume(bool forward)
{
    // We can't rewind past the beginning of the replay.
    MOZ_RELEASE_ASSERT(forward || mCheckpoint != FirstCheckpointId);

    if (forward) {
        // Run forward from the current execution point.
        ReplayDebugger::ClearDebuggerPausedObjects();
        ExecutionPoint search(mCheckpoint);
        gNavigation->mForwardPhase.enter(search);
    } else {
        CheckpointId start(mCheckpoint - 1);
        gNavigation->mFindLastHitPhase.enter(start, Nothing());
        MOZ_CRASH("Unreachable");
    }
}

void
CheckpointPausedPhase::restoreCheckpoint(size_t checkpoint)
{
    enter(checkpoint, /* rewind = */ true, /* atRecordingEndpoint = */ false);
}

void
CheckpointPausedPhase::handleDebuggerRequest(JS::replay::CharBuffer* requestBuffer)
{
    JS::replay::CharBuffer responseBuffer;
    ReplayDebugger::ProcessRequest(requestBuffer->begin(), requestBuffer->length(), &responseBuffer);

    js_delete(requestBuffer);

    JS::replay::hooks.debugResponseReplay(responseBuffer);
}

ExecutionPoint
CheckpointPausedPhase::getRecordingEndpoint()
{
    return ExecutionPoint(mCheckpoint);
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
    for (const ExecutionPosition& breakpoint : gNavigation->mBreakpoints) {
        if (breakpoint.isValid())
            EnsurePositionHandler(breakpoint);
    }

    ResumeExecution();
}

void
ForwardPhase::afterCheckpoint(const CheckpointId& checkpoint)
{
    MOZ_RELEASE_ASSERT(!checkpoint.mTemporary &&
                       checkpoint.mNormal == mPoint.checkpoint + 1);
    gNavigation->mCheckpointPausedPhase.enter(checkpoint.mNormal, /* rewind = */ false,
                                              /* atRecordingEndpoint = */ false);
}

void
ForwardPhase::positionHit(const ExecutionPoint& point)
{
    BreakpointVector hitBreakpoints;
    GetAllBreakpointHits(point, hitBreakpoints);

    if (!hitBreakpoints.empty())
        gNavigation->mBreakpointPausedPhase.enter(point, hitBreakpoints);
}

void
ForwardPhase::hitRecordingEndpoint(const ExecutionPoint& point)
{
    if (point.hasPosition()) {
        BreakpointVector emptyBreakpoints;
        gNavigation->mBreakpointPausedPhase.enter(point, emptyBreakpoints);
    } else {
        gNavigation->mCheckpointPausedPhase.enter(point.checkpoint, /* rewind = */ false,
                                                  /* atRecordingEndpoint = */ true);
    }
}

///////////////////////////////////////////////////////////////////////////////
// ReachBreakpointPhase
///////////////////////////////////////////////////////////////////////////////

void
ReachBreakpointPhase::enter(const CheckpointId& start,
                            const ExecutionPoint& point,
                            const Maybe<ExecutionPoint>& temporaryCheckpoint)
{
    MOZ_RELEASE_ASSERT(point.hasPosition());
    MOZ_RELEASE_ASSERT(temporaryCheckpoint.isNothing() ||
                       (temporaryCheckpoint.ref().hasPosition() &&
                        temporaryCheckpoint.ref() != point));
    mStart = start;
    mPoint = point;
    mTemporaryCheckpoint = temporaryCheckpoint;
    mSavedTemporaryCheckpoint = false;

    gNavigation->setPhase(this);

    RestoreCheckpointAndResume(start);
    MOZ_CRASH("Unreachable");
}

void
ReachBreakpointPhase::afterCheckpoint(const CheckpointId& checkpoint)
{
    if (checkpoint == mStart && mTemporaryCheckpoint.isSome()) {
        EnsurePositionHandler(mTemporaryCheckpoint.ref().position);

        // Remember the time we started running forwards from the initial checkpoint.
        mStartTime = ReallyNow();
    } else {
        MOZ_RELEASE_ASSERT((checkpoint == mStart && mTemporaryCheckpoint.isNothing()) ||
                           (checkpoint == NextTemporaryCheckpoint(mStart) &&
                            mSavedTemporaryCheckpoint));
    }

    EnsurePositionHandler(mPoint.position);
}

// The number of milliseconds to elapse during a ReachBreakpoint search before
// we will save a temporary checkpoint.
static const double TemporaryCheckpointThresholdMs = 10;

static void
AlwaysSaveTemporaryCheckpointsHook()
{
    gNavigation->mAlwaysSaveTemporaryCheckpoints = true;
}

void
ReachBreakpointPhase::positionHit(const ExecutionPoint& point)
{
    if (mTemporaryCheckpoint.isSome() && mTemporaryCheckpoint.ref() == point) {
        // We've reached the point at which we have the option of saving a
        // temporary checkpoint.
        double elapsedMs = (ReallyNow() - mStartTime).ToMilliseconds();
        if (elapsedMs >= TemporaryCheckpointThresholdMs ||
            gNavigation->mAlwaysSaveTemporaryCheckpoints)
        {
            MOZ_RELEASE_ASSERT(!mSavedTemporaryCheckpoint);
            mSavedTemporaryCheckpoint = true;

            if (!gNavigation->saveTemporaryCheckpoint(point)) {
                // We just restored the checkpoint, and could be in any phase.
                gNavigation->positionHit(point);
                return;
            }
        }
    }

    if (mPoint == point) {
        BreakpointVector hitBreakpoints;
        GetAllBreakpointHits(point, hitBreakpoints);
        MOZ_RELEASE_ASSERT(!hitBreakpoints.empty());

        gNavigation->mBreakpointPausedPhase.enter(point, hitBreakpoints);
    }
}

///////////////////////////////////////////////////////////////////////////////
// FindLastHitPhase
///////////////////////////////////////////////////////////////////////////////

static Maybe<ExecutionPosition>
GetEntryPosition(const ExecutionPosition& position)
{
    if (position.kind == ExecutionPosition::Break ||
        position.kind == ExecutionPosition::OnStep)
    {
        if (JSScript* script = ReplayDebugger::IdScript(position.script)) {
            ExecutionPosition entry(ExecutionPosition::Break, position.script,
                                    script->mainOffset());
            return Some(entry);
        }
    }
    return Nothing();
}

void
FindLastHitPhase::enter(const CheckpointId& start, const Maybe<ExecutionPoint>& end)
{
    MOZ_RELEASE_ASSERT(end.isNothing() || end.ref().hasPosition());

    mStart = start;
    mEnd = end;
    mCounter = 0;
    mTrackedPositions.clear();

    gNavigation->setPhase(this);

    // All breakpoints are tracked positions.
    for (const ExecutionPosition& breakpoint : gNavigation->mBreakpoints) {
        if (breakpoint.isValid())
            TRY(mTrackedPositions.emplaceBack(breakpoint));
    }

    // Entry points to scripts containing breakpoints are tracked positions.
    for (const ExecutionPosition& breakpoint : gNavigation->mBreakpoints) {
        Maybe<ExecutionPosition> entry = GetEntryPosition(breakpoint);
        if (entry.isSome())
            TRY(mTrackedPositions.emplaceBack(entry.ref()));
    }

    RestoreCheckpointAndResume(mStart);
    MOZ_CRASH("Unreachable");
}

void
FindLastHitPhase::afterCheckpoint(const CheckpointId& checkpoint)
{
    if (checkpoint == NextNormalCheckpoint(mStart)) {
        // We reached the next checkpoint, and are done searching.
        MOZ_RELEASE_ASSERT(!mEnd.isSome());
        onRegionEnd();
        MOZ_CRASH("Unreachable");
    }

    // We are at the start of the search.
    MOZ_RELEASE_ASSERT(checkpoint == mStart);

    for (const TrackedPosition& tracked : mTrackedPositions)
        EnsurePositionHandler(tracked.position);

    if (mEnd.isSome())
        EnsurePositionHandler(mEnd.ref().position);
}

void
FindLastHitPhase::positionHit(const ExecutionPoint& point)
{
    if (mEnd.isSome() && mEnd.ref() == point) {
        onRegionEnd();
        MOZ_CRASH("Unreachable");
    }

    ++mCounter;

    for (TrackedPosition& tracked : mTrackedPositions) {
        if (tracked.position.subsumes(point.position)) {
            tracked.lastHit = point;
            tracked.lastHitCount = mCounter;
            break;
        }
    }
}

void
FindLastHitPhase::hitRecordingEndpoint(const ExecutionPoint& point)
{
    onRegionEnd();
    MOZ_CRASH("Unreachable");
}

FindLastHitPhase::TrackedPosition
FindLastHitPhase::findTrackedPosition(const ExecutionPosition& pos)
{
    for (const TrackedPosition& tracked : mTrackedPositions) {
        if (tracked.position == pos)
            return tracked;
    }
    MOZ_CRASH("Could not find tracked position");
}

void
FindLastHitPhase::onRegionEnd()
{
    // Find the point of the last hit which coincides with a breakpoint.
    Maybe<TrackedPosition> lastBreakpoint;
    for (const ExecutionPosition& breakpoint : gNavigation->mBreakpoints) {
        if (!breakpoint.isValid())
            continue;
        TrackedPosition tracked = findTrackedPosition(breakpoint);
        if (tracked.lastHit.hasPosition() &&
            (lastBreakpoint.isNothing() ||
             lastBreakpoint.ref().lastHitCount < tracked.lastHitCount))
        {
            lastBreakpoint = Some(tracked);
        }
    }

    if (lastBreakpoint.isNothing()) {
        // No breakpoints were encountered in the search space.
        if (mStart.mTemporary) {
            // We started searching forwards from a temporary checkpoint.
            // Continue searching backwards without notifying the middleman.
            CheckpointId start = mStart;
            start.mTemporary--;
            ExecutionPoint end = gNavigation->lastTemporaryCheckpointLocation();
            gNavigation->mFindLastHitPhase.enter(start, Some(end));
            MOZ_CRASH("Unreachable");
        } else {
            // Rewind to the last normal checkpoint and pause.
            gNavigation->mCheckpointPausedPhase.enter(mStart.mNormal, /* rewind = */ true,
                                                      /* atRecordingEndpoint = */ false);
            MOZ_CRASH("Unreachable");
        }
    }

    // When running backwards, we don't want to place temporary checkpoints at
    // the breakpoint where we are going to stop at. If the user continues
    // rewinding then we will just have to discard the checkpoint and waste the
    // work we did in saving it.
    //
    // Instead, try to place a temporary checkpoint at the last time the
    // breakpoint's script was entered. This optimizes for the case of stepping
    // around within a frame.
    Maybe<ExecutionPosition> baseEntry = GetEntryPosition(lastBreakpoint.ref().position);
    if (baseEntry.isSome()) {
        TrackedPosition tracked = findTrackedPosition(baseEntry.ref());
        if (tracked.lastHit.hasPosition() &&
            tracked.lastHitCount < lastBreakpoint.ref().lastHitCount)
        {
            gNavigation->mReachBreakpointPhase.enter(mStart, lastBreakpoint.ref().lastHit,
                                                     Some(tracked.lastHit));
            MOZ_CRASH("Unreachable");
        }
    }

    // There was no suitable place for a temporary checkpoint, so rewind to the
    // last checkpoint and play forward to the last breakpoint hit we found.
    gNavigation->mReachBreakpointPhase.enter(mStart, lastBreakpoint.ref().lastHit, Nothing());
    MOZ_CRASH("Unreachable");
}

///////////////////////////////////////////////////////////////////////////////
// Debugger Handlers
///////////////////////////////////////////////////////////////////////////////

// Replay phases can install handlers on ExecutionPositions that call back
// into the phase's positionHit method when the position is reached.

static ExecutionPoint
NewExecutionPoint(const ExecutionPosition& pos)
{
    return ExecutionPoint(gNavigation->lastCheckpoint().mNormal,
                          ReplayDebugger::gProgressCounter, pos);
}

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

    ExecutionPosition pos(ExecutionPosition::OnStep, scriptId, offset, frameIndex);
    gNavigation->positionHit(NewExecutionPoint(pos));

    args.rval().setUndefined();
    return true;
}

static bool
EnterFrameHandler(JSContext* cx, unsigned argc, Value* vp)
{
    CallArgs args = CallArgsFromVp(argc, vp);

    ExecutionPosition pos(ExecutionPosition::EnterFrame);
    gNavigation->positionHit(NewExecutionPoint(pos));

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

    size_t frameIndex = ReplayDebugger::CountScriptFrames(cx) - 1;

    // Update the frame return state in case we hit a breakpoint here.
    gPopFrameThrowing = !ok;
    *gPopFrameResult = frame.returnValue();

    ExecutionPosition pos(ExecutionPosition::OnPop, scriptId,
                          ExecutionPosition::EMPTY_OFFSET, frameIndex);
    gNavigation->positionHit(NewExecutionPoint(pos));

    gPopFrameThrowing = false;
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
                observeAllExecution(cx);
            break;
          case ExecutionPosition::EnterFrame: {
            if (mInstalledEnterFrameHandler)
                return true;
            observeAllExecution(cx);
            RootedObject handler(cx, NewNativeFunction(cx, EnterFrameHandler, 1, nullptr));
            TRY(handler);
            RootedValue handlerValue(cx, ObjectValue(*handler));
            TRY(JS_SetProperty(cx, *gHookDebugger, "onEnterFrame", handlerValue));
            mInstalledEnterFrameHandler = true;
            break;
          }
          case ExecutionPosition::NewScript:
            break;
          default:
            MOZ_CRASH("Bad execution position kind");
        }
        return true;
    }

    void observeAllExecution(JSContext* cx) {
        RootedValue unused(cx);
        TRY(JS_CallFunctionName(cx, *gHookDebugger, "addAllGlobalsAsDebuggees",
                                JS::HandleValueArray::empty(), &unused));
        Debugger* debugger = Debugger::fromJSObject(*gHookDebugger);
        TRY(debugger->updateObservesAllExecutionOnDebuggees(cx, Debugger::Observing));
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
ReplayDebugger::HandleBreakpointsForNewScript(JSScript* script, size_t scriptId, bool toplevel)
{
    gHandlerManager->onNewScript(scriptId);

    // NewScript breakpoints are only hit for top level scripts (as for the
    // normal debugger).
    if (toplevel) {
        ExecutionPosition pos(ExecutionPosition::NewScript);
        gNavigation->positionHit(NewExecutionPoint(pos));
    }
}

///////////////////////////////////////////////////////////////////////////////
// Hooks
///////////////////////////////////////////////////////////////////////////////

static void
BeforeCheckpointHook()
{
    // Reset the debugger to a consistent state before each checkpoint. Ensure
    // that the hook context and global exist and have a debugger object, and
    // that no debuggees have debugger information attached.

    MOZ_RELEASE_ASSERT(gHookContext && ReplayDebugger::gHookGlobal);

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
AfterCheckpointHook(const CheckpointId& checkpoint)
{
    MOZ_RELEASE_ASSERT(IsRecordingOrReplaying());
    gNavigation->afterCheckpoint(checkpoint);
}

static void
DebugRequestHook(JS::replay::CharBuffer* requestBuffer)
{
    gNavigation->handleDebuggerRequest(requestBuffer);
}

/* static */ void
ReplayDebugger::GetPoppedFrameResult(bool* throwing, MutableHandleValue result)
{
    gNavigation->getPoppedFrameResult(throwing, result);
}

/* static */ bool
ReplayDebugger::MaybeDivergeFromRecording()
{
    return gNavigation->maybeDivergeFromRecording();
}

static void
SetBreakpointHook(size_t id, const ExecutionPosition& position)
{
    gNavigation->getBreakpoint(id) = position;
}

static void
ResumeHook(bool forward)
{
    gNavigation->resume(forward);
}

static void
RestoreCheckpointHook(size_t id)
{
    gNavigation->restoreCheckpoint(id);
}

static void
RespondAfterRecoveringFromDivergenceHook()
{
    MOZ_RELEASE_ASSERT(gNavigation->mPhase == &gNavigation->mBreakpointPausedPhase);
    gNavigation->mBreakpointPausedPhase.respondAfterRecoveringFromDivergence();
}

static ExecutionPoint
GetRecordingEndpointHook()
{
    MOZ_RELEASE_ASSERT(IsRecording());
    return gNavigation->getRecordingEndpoint();
}

static void
SetRecordingEndpointHook(size_t index, const ExecutionPoint& endpoint)
{
    MOZ_RELEASE_ASSERT(IsReplaying());
    gNavigation->setRecordingEndpoint(index, endpoint);
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
        JS::replay::hooks.restoreCheckpointReplay = RestoreCheckpointHook;
        JS::replay::hooks.respondAfterRecoveringFromDivergence = RespondAfterRecoveringFromDivergenceHook;
        JS::replay::hooks.setBreakpointReplay = SetBreakpointHook;
        JS::replay::hooks.alwaysSaveTemporaryCheckpoints = AlwaysSaveTemporaryCheckpointsHook;
        JS::replay::hooks.getRecordingEndpoint = GetRecordingEndpointHook;
        JS::replay::hooks.setRecordingEndpoint = SetRecordingEndpointHook;

        SetCheckpointHooks(::BeforeCheckpointHook, ::AfterCheckpointHook);
    }
}

#undef TRY
