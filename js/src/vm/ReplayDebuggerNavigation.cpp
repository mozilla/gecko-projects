/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ReplayDebugger.h"

#include "mozilla/Sprintf.h"

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

typedef AllocPolicy<TrackedMemoryKind> TrackedAllocPolicy;
typedef AllocPolicy<DebuggerAllocatedMemoryKind> UntrackedAllocPolicy;

typedef JS::replay::ExecutionPosition ExecutionPosition;
typedef JS::replay::ExecutionPoint<TrackedAllocPolicy> ExecutionPoint;
typedef JS::replay::ExecutionPoint<UntrackedAllocPolicy> UntrackedExecutionPoint;
typedef Vector<ExecutionPosition, 4, UntrackedAllocPolicy> UntrackedExecutionPositionVector;

template <typename T>
static inline void
CopyVector(T& dst, const T& src)
{
    dst.clear();
    TRY(dst.append(src.begin(), src.length()));
}

static void
CheckpointToString(const CheckpointId& checkpoint, Sprinter& sp)
{
    sp.printf("%d:%d", (int) checkpoint.mNormal, (int) checkpoint.mTemporary);
}

static void
ExecutionPositionToString(const ExecutionPosition& pos, Sprinter& sp)
{
    sp.printf("{ Kind: %s, Script: %d, Offset: %d, Frame: %d }",
              pos.kindString(), (int) pos.script, (int) pos.offset, (int) pos.frameIndex);
}

template <typename ExecutionPoint>
static void
ExecutionPointToString(const ExecutionPoint& point, Sprinter& sp)
{
    sp.printf("Checkpoint ");
    CheckpointToString(point.checkpoint, sp);
    sp.printf(" Positions %d:", (int) point.positions.length());
    for (const ExecutionPosition& pos : point.positions) {
        sp.printf(" ");
        ExecutionPositionToString(pos, sp);
    }
}

static bool
CheckpointPrecedes(const CheckpointId& first, const CheckpointId& second)
{
    return first.mNormal < second.mNormal || first.mTemporary < second.mTemporary;
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
    virtual void positionHit(const std::function<bool(const ExecutionPosition&)>& match) {
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

    // Save the current execution point when recording.
    virtual void getRecordingEndpoint(ExecutionPoint* endpoint) {
        unsupported("getRecordingEndpoint");
    }

    // Called when execution reaches the endpoint of the recording.
    virtual void hitRecordingEndpoint() {
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
    UntrackedExecutionPoint mPoint;

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
    void enter(const ExecutionPoint& point, const BreakpointVector& breakpoints,
               const std::function<bool(const ExecutionPosition&)>& match);
    void enterAtEndpoint(const ExecutionPoint& point);

    void toString(Sprinter& sp) override {
        sp.printf("BreakpointPaused RecoveringFromDivergence %d", mRecoveringFromDivergence);
    }

    void afterCheckpoint(const CheckpointId& checkpoint) override;
    void positionHit(const std::function<bool(const ExecutionPosition&)>& match) override;
    void resume(bool forward) override;
    void restoreCheckpoint(size_t checkpoint) override;
    void getPoppedFrameResult(bool* throwing, MutableHandleValue result) override;
    void handleDebuggerRequest(JS::replay::CharBuffer* requestBuffer) override;
    bool maybeDivergeFromRecording() override;
    void getRecordingEndpoint(ExecutionPoint* endpoint) override;

    void respondAfterRecoveringFromDivergence();
};

// Phase when the replaying process is paused at a normal checkpoint.
class CheckpointPausedPhase : public NavigationPhase
{
    CheckpointId mCheckpoint;

  public:
    void enter(size_t checkpoint, bool rewind);

    void toString(Sprinter& sp) override {
        sp.printf("CheckpointPaused");
    }

    void afterCheckpoint(const CheckpointId& checkpoint) override;
    void positionHit(const std::function<bool(const ExecutionPosition&)>& match) override;
    void resume(bool forward) override;
    void restoreCheckpoint(size_t checkpoint) override;
    void handleDebuggerRequest(JS::replay::CharBuffer* requestBuffer) override;
    void getRecordingEndpoint(ExecutionPoint* endpoint) override;
};

// Phase when execution is proceeding forwards in search of breakpoint hits.
class ForwardPhase : public NavigationPhase
{
    // Some execution point in the recent past. There are no checkpoints or
    // breakpoint hits between this point and the current point of execution.
    UntrackedExecutionPoint mPoint;

  public:
    void enter(const ExecutionPoint& point);

    void toString(Sprinter& sp) override {
        sp.printf("Forward");
    }

    void afterCheckpoint(const CheckpointId& checkpoint) override;
    void positionHit(const std::function<bool(const ExecutionPosition&)>& match) override;
    void hitRecordingEndpoint() override;
};

// Phase when the replaying process is running forward from a checkpoint to a
// breakpoint at a particular execution point.
class ReachBreakpointPhase : public NavigationPhase
{
  private:
    // The point we are running to.
    UntrackedExecutionPoint mPoint;

    // How much of the point we have reached so far.
    ExecutionPoint::Prefix mReached;

    // Prefix after which to decide whether to save a temporary checkpoint.
    Maybe<ExecutionPoint::Prefix> mTemporaryCheckpointPrefix;

    // Whether we have saved a temporary checkpoint at the specified prefix.
    bool mSavedTemporaryCheckpoint;

    // The time at which we started running forward from the initial
    // checkpoint.
    TimeStamp mStartTime;

  public:
    // Note: this always rewinds.
    void enter(const ExecutionPoint& point,
               const Maybe<ExecutionPoint::Prefix>& temporaryCheckpointPrefix);

    void toString(Sprinter& sp) override {
        sp.printf("ReachBreakpoint: ");
        ExecutionPointToString(mPoint, sp);
        if (mTemporaryCheckpointPrefix.isSome())
            sp.printf(" TemporaryCheckpointPrefix %d", (int) mTemporaryCheckpointPrefix.ref());
    }

    void afterCheckpoint(const CheckpointId& checkpoint) override;
    void positionHit(const std::function<bool(const ExecutionPosition&)>& match) override;
};

// Phase when the replaying process is searching forward from a checkpoint to
// find the last point a breakpoint is hit before reaching an execution point.
class FindLastHitPhase : public NavigationPhase
{
    // Endpoint of the search. The positions in this may be empty, in which
    // case the endpoint is the following checkpoint.
    UntrackedExecutionPoint mPoint;

    // How much of the endpoint we have reached so far.
    ExecutionPoint::Prefix mReached;

    // All positions we are interested in hits for, including all breakpoint
    // positions (and possibly other positions).
    UntrackedExecutionPositionVector mTrackedPositions;

    // Tracked positions that have been reached since the checkpoint, in the
    // order they were reached.
    UntrackedExecutionPositionVector mTrackedHits;

    void addTrackedPosition(const ExecutionPosition& position, bool allowSubsumeExisting);
    size_t countTrackedHitsInRange(const ExecutionPosition& pos, size_t start, size_t end);
    Maybe<size_t> lastMatchingTrackedHit(const std::function<bool(const ExecutionPosition&)>& match,
                                         size_t start, size_t end);
    void onRegionEnd();

  public:
    // Note: this always rewinds.
    void enter(const ExecutionPoint& point);

    void toString(Sprinter& sp) override {
        sp.printf("FindLastHit");
    }

    void afterCheckpoint(const CheckpointId& checkpoint) override;
    void positionHit(const std::function<bool(const ExecutionPosition&)>& match) override;
    void hitRecordingEndpoint() override;
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
    // When replaying, any recording endpoint which we cannot run past.
    UntrackedExecutionPoint mRecordingEndpoint;

    // How much of mRecordingEndpoint we have consumed, or nothing if we have
    // not reached the last checkpoint in the recording.
    Maybe<ExecutionPoint::Prefix> mRecordingEndpointConsumed;

    // All temporary checkpoints we have saved. All temporary checkpoints
    // are between two adjacent normal checkpoints.
    struct TemporaryCheckpoint {
        // The location of the checkpoint, expressed in relation to the
        // previous temporary or normal checkpoint.
        UntrackedExecutionPoint mPoint;

        // How much of mRecordingEndpoint was consumed when this checkpoint was
        // taken.
        Maybe<ExecutionPoint::Prefix> mRecordingEndpointConsumed;

        CheckpointId GetCheckpointId() {
            return NextTemporaryCheckpoint(mPoint.checkpoint);
        }
    };
    Vector<TemporaryCheckpoint, 0, UntrackedAllocPolicy> mTemporaryCheckpoints;

  public:
    // All the currently installed breakpoints, indexed by their ID.
    UntrackedExecutionPositionVector mBreakpoints;

    ExecutionPosition& getBreakpoint(size_t id) {
        while (id >= mBreakpoints.length())
            TRY(mBreakpoints.emplaceBack());
        return mBreakpoints[id];
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
            mRecordingEndpoint.checkpoint = CheckpointId(FirstCheckpointId);
        }
    }

    void afterCheckpoint(const CheckpointId& checkpoint) {
        // Forget any temporary checkpoints we just rewound past, or made
        // obsolete by reaching the next normal checkpoint.
        while (!mTemporaryCheckpoints.empty() &&
               mTemporaryCheckpoints.back().GetCheckpointId() != checkpoint)
        {
            mTemporaryCheckpoints.popBack();
        }

        mPhase->afterCheckpoint(checkpoint);

        // We will be running forward from this checkpoint. Keep track of how
        // much of the recording endpoint has been consumed as we run forward.
        if (!mTemporaryCheckpoints.empty()) {
            MOZ_RELEASE_ASSERT(checkpoint == mTemporaryCheckpoints.back().GetCheckpointId());
            mRecordingEndpointConsumed = mTemporaryCheckpoints.back().mRecordingEndpointConsumed;
        } else if (checkpoint == mRecordingEndpoint.checkpoint) {
            mRecordingEndpointConsumed.emplace(0);
            checkForRecordingEndpoint();
        } else {
            MOZ_RELEASE_ASSERT(IsRecording() ||
                               CheckpointPrecedes(checkpoint, mRecordingEndpoint.checkpoint));
            mRecordingEndpointConsumed.reset();
        }

        ensureRecordingEndpointHandlers();
    }

    void ensureRecordingEndpointHandlers() {
        if (mRecordingEndpointConsumed.isSome()) {
            for (const ExecutionPosition& pos : mRecordingEndpoint.positions)
                EnsurePositionHandler(pos);
        }
    }

    void positionHit(const std::function<bool(const ExecutionPosition&)>& match,
                     bool updateEndpointConsumed) {
        mPhase->positionHit(match);

        // The updateEndpointConsumed flag avoids double-counting when the
        // above call reenters this method.
        if (updateEndpointConsumed &&
            mRecordingEndpointConsumed.isSome() &&
            match(mRecordingEndpoint.positions[mRecordingEndpointConsumed.ref()]))
        {
            ++mRecordingEndpointConsumed.ref();
            checkForRecordingEndpoint();
        }
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

    void getRecordingEndpoint(ExecutionPoint* endpoint) {
        mPhase->getRecordingEndpoint(endpoint);
    }

    void setRecordingEndpoint(const ExecutionPoint& endpoint) {
        // Update the recording endpoint, ignoring endpoints that come prior to
        // the latest endpoint we know about.
        if (CheckpointPrecedes(mRecordingEndpoint.checkpoint, endpoint.checkpoint)) {
            mRecordingEndpoint.copyFrom(endpoint);
            mRecordingEndpointConsumed.reset();
            for (TemporaryCheckpoint& temporaryCheckpoint : mTemporaryCheckpoints)
                temporaryCheckpoint.mRecordingEndpointConsumed.reset();
        } else if (endpoint.checkpoint == mRecordingEndpoint.checkpoint) {
            // Make sure the two endpoints share a common prefix, which should
            // be the case because the recording process only runs forward.
            // This ensures any prefixes we compute for the old endpoint are
            // still valid for the new endpoint.
            size_t oldPositions = mRecordingEndpoint.positions.length();
            size_t newPositions = endpoint.positions.length();
            for (size_t i = 0; i < std::min(oldPositions, newPositions); i++)
                MOZ_RELEASE_ASSERT(endpoint.positions[i] == mRecordingEndpoint.positions[i]);
            if (newPositions > oldPositions) {
                TRY(mRecordingEndpoint.positions.append(&endpoint.positions[oldPositions],
                                                        newPositions - oldPositions));
                ensureRecordingEndpointHandlers();
            }
        }
    }

    void checkForRecordingEndpoint() {
        while (mRecordingEndpointConsumed.isSome() &&
               mRecordingEndpointConsumed.ref() == mRecordingEndpoint.positions.length())
        {
            // The recording ended after the checkpoint, but maybe there is
            // another, later endpoint now. This may call back into
            // setRecordingEndpoint and notify us there is more recording data
            // available.
            if (!JS::replay::hooks.hitCurrentRecordingEndpointReplay())
                mPhase->hitRecordingEndpoint();
        }
    }

    size_t numTemporaryCheckpoints() {
        return mTemporaryCheckpoints.length();
    }

    bool saveTemporaryCheckpoint(const ExecutionPoint& point) {
        TRY(mTemporaryCheckpoints.emplaceBack());
        TRY(mTemporaryCheckpoints.back().mPoint.copyFrom(point));
        mTemporaryCheckpoints.back().mRecordingEndpointConsumed = mRecordingEndpointConsumed;
        return NewCheckpoint(/* aTemporary = */ true);
    }

    void lastTemporaryCheckpoint(ExecutionPoint* point) {
        MOZ_RELEASE_ASSERT(!mTemporaryCheckpoints.empty());
        point->copyFrom(mTemporaryCheckpoints.back().mPoint);
    }
};

static NavigationState* gNavigation;

static void
GetAllBreakpointHits(const std::function<bool(const ExecutionPosition&)>& match,
                     BreakpointVector& hitBreakpoints)
{
    for (size_t id = 0; id < gNavigation->mBreakpoints.length(); id++) {
        const ExecutionPosition& breakpoint = gNavigation->mBreakpoints[id];
        if (breakpoint.isValid() && match(breakpoint))
            TRY(hitBreakpoints.append(id));
    }
}

///////////////////////////////////////////////////////////////////////////////
// BreakpointPaused Phase
///////////////////////////////////////////////////////////////////////////////

void
BreakpointPausedPhase::enter(const ExecutionPoint& point, const BreakpointVector& breakpoints,
                             const std::function<bool(const ExecutionPosition&)>& match)
{
    ExecutionPosition breakpointPosition;
    if (!breakpoints.empty()) {
        MOZ_RELEASE_ASSERT(!point.positions.empty());
        breakpointPosition = point.positions.back();
        MOZ_RELEASE_ASSERT(match(breakpointPosition));
    } else {
        // We are at the endpoint of the recording.
    }

    mPoint.clear();
    mRequests.clear();
    mRecoveringFromDivergence = false;
    mRequestIndex = 0;
    mResumeForward = false;

    gNavigation->setPhase(this);

    if (IsRecording()) {
        TRY(mPoint.copyFrom(point));
    } else {
        // Immediately take a temporary checkpoint and upate the point to be
        // in relation to this checkpoint. If we rewind due to a recording
        // divergence we will end up here.
        mPoint.checkpoint = NextTemporaryCheckpoint(point.checkpoint);
        if (!gNavigation->saveTemporaryCheckpoint(point)) {
            // We just restored the checkpoint, and could be in any phase,
            // including this one.
            if (gNavigation->mPhase == this) {
                MOZ_RELEASE_ASSERT(!mRecoveringFromDivergence);
                // If we are transitioning to the forward phase, avoid hitting
                // breakpoints at this point but update the new phase's point
                // to reflect that.
                if (mResumeForward) {
                    MOZ_RELEASE_ASSERT(mResumeForward);
                    ExecutionPoint newPoint;
                    newPoint.checkpoint = mPoint.checkpoint;
                    if (!breakpoints.empty())
                        TRY(newPoint.positions.append(breakpointPosition));
                    gNavigation->mForwardPhase.enter(newPoint);
                    return;
                }
                // Otherwise we restored after hitting an unhandled recording
                // divergence.
                mRecoveringFromDivergence = true;
                JS::replay::hooks.pauseAndRespondAfterRecoveringFromDivergence();
                MOZ_CRASH("Unreachable");
            }
            gNavigation->positionHit(match, /* updateEndpointConsumed = */ false);
            return;
        }
    }

    if (!breakpoints.empty())
        JS::replay::hooks.hitBreakpointReplay(breakpoints.begin(), breakpoints.length());
    else
        JS::replay::hooks.hitLastRecordingEndpointReplay();

    // When replaying we will rewind before resuming to erase side effects.
    MOZ_RELEASE_ASSERT(IsRecording());
}

void
BreakpointPausedPhase::enterAtEndpoint(const ExecutionPoint& point)
{
    BreakpointVector breakpoints;
    enter(point, breakpoints, [=](const ExecutionPosition& position) { return false; });
}

void
BreakpointPausedPhase::afterCheckpoint(const CheckpointId& checkpoint)
{
    // We just saved or restored the temporary checkpoint before reaching the
    // breakpoint.
    MOZ_RELEASE_ASSERT(IsReplaying());
    MOZ_RELEASE_ASSERT(checkpoint == mPoint.checkpoint);
}

void
BreakpointPausedPhase::positionHit(const std::function<bool(const ExecutionPosition&)>& match)
{
    // Ignore positions hit while paused (we're probably doing an eval).
}

void
BreakpointPausedPhase::resume(bool forward)
{
    MOZ_RELEASE_ASSERT(!mRecoveringFromDivergence);

    if (forward) {
        // If we are paused at a breakpoint and are replaying, we may have
        // diverged from the recording. We have to clear any unwanted changes
        // induced by evals and so forth by restoring the temporary checkpoint
        // we saved before pausing here.
        if (IsReplaying()) {
            mResumeForward = true;
            RestoreCheckpointAndResume(mPoint.checkpoint);
            MOZ_CRASH("Unreachable");
        }

        ReplayDebugger::ClearDebuggerPausedObjects();

        // Run forward from the current execution point.
        ExecutionPoint point;
        TRY(point.copyFrom(mPoint));
        gNavigation->mForwardPhase.enter(point);
        return;
    }

    // Search backwards in the execution space.
    ExecutionPoint newPoint;
    gNavigation->lastTemporaryCheckpoint(&newPoint);
    gNavigation->mFindLastHitPhase.enter(newPoint);
    MOZ_CRASH("Unreachable");
}

void
BreakpointPausedPhase::restoreCheckpoint(size_t checkpoint)
{
    gNavigation->mCheckpointPausedPhase.enter(checkpoint, /* rewind = */ true);
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
    if (IsRecording()) {
        // Recording divergence is not supported if we are still recording.
        // We don't rewind processes that are recording, and can't simply allow
        // execution to proceed from here as if we were not diverged, since any
        // events or other activity that show up afterwards won't occur when we
        // are replaying later.
        return false;
    }
    if (mRequests[mRequestIndex].unhandledDivergence)
        return false;
    DivergeFromRecording();
    return true;
}

void
BreakpointPausedPhase::getRecordingEndpoint(ExecutionPoint* endpoint)
{
    MOZ_RELEASE_ASSERT(IsRecording());
    MOZ_RELEASE_ASSERT(!gNavigation->numTemporaryCheckpoints());
    endpoint->checkpoint = mPoint.checkpoint;
    TRY(endpoint->positions.append(mPoint.positions.begin(), mPoint.positions.length()));
}

///////////////////////////////////////////////////////////////////////////////
// CheckpointPausedPhase
///////////////////////////////////////////////////////////////////////////////

void
CheckpointPausedPhase::enter(size_t checkpoint, bool rewind)
{
    mCheckpoint = CheckpointId(checkpoint);

    gNavigation->setPhase(this);

    if (rewind) {
        RestoreCheckpointAndResume(mCheckpoint);
        MOZ_CRASH("Unreachable");
    }

    afterCheckpoint(mCheckpoint);
}

void
CheckpointPausedPhase::afterCheckpoint(const CheckpointId& checkpoint)
{
    MOZ_RELEASE_ASSERT(checkpoint == mCheckpoint);
    JS::replay::hooks.hitCheckpointReplay(mCheckpoint.mNormal);
}

void
CheckpointPausedPhase::positionHit(const std::function<bool(const ExecutionPosition&)>& match)
{
    // Ignore positions hit while paused (we're probably doing an eval).
}

void
CheckpointPausedPhase::resume(bool forward)
{
    // We can't rewind past the beginning of the replay.
    MOZ_RELEASE_ASSERT(forward || mCheckpoint.mNormal != FirstCheckpointId);

    if (forward) {
        // Run forward from the current execution point.
        ReplayDebugger::ClearDebuggerPausedObjects();
        ExecutionPoint search;
        search.checkpoint = mCheckpoint;
        gNavigation->mForwardPhase.enter(search);
    } else {
        ExecutionPoint search;
        search.checkpoint = CheckpointId(mCheckpoint.mNormal - 1);
        gNavigation->mFindLastHitPhase.enter(search);
        MOZ_CRASH("Unreachable");
    }
}

void
CheckpointPausedPhase::restoreCheckpoint(size_t checkpoint)
{
    enter(checkpoint, /* rewind = */ true);
}

void
CheckpointPausedPhase::handleDebuggerRequest(JS::replay::CharBuffer* requestBuffer)
{
    JS::replay::CharBuffer responseBuffer;
    ReplayDebugger::ProcessRequest(requestBuffer->begin(), requestBuffer->length(), &responseBuffer);

    js_delete(requestBuffer);

    JS::replay::hooks.debugResponseReplay(responseBuffer);
}

void
CheckpointPausedPhase::getRecordingEndpoint(ExecutionPoint* endpoint)
{
    endpoint->checkpoint = mCheckpoint;
}

///////////////////////////////////////////////////////////////////////////////
// ForwardPhase
///////////////////////////////////////////////////////////////////////////////

void
ForwardPhase::enter(const ExecutionPoint& point)
{
    TRY(mPoint.copyFrom(point));

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
                       checkpoint.mNormal == mPoint.checkpoint.mNormal + 1);
    gNavigation->mCheckpointPausedPhase.enter(checkpoint.mNormal, /* rewind = */ false);
}

void
ForwardPhase::positionHit(const std::function<bool(const ExecutionPosition&)>& match)
{
    BreakpointVector hitBreakpoints;
    GetAllBreakpointHits(match, hitBreakpoints);

    if (!hitBreakpoints.empty()) {
        ExecutionPoint point;
        TRY(point.copyFrom(mPoint));
        TRY(point.positions.append(gNavigation->getBreakpoint(hitBreakpoints[0])));
        gNavigation->mBreakpointPausedPhase.enter(point, hitBreakpoints, match);
    }
}

void
ForwardPhase::hitRecordingEndpoint()
{
    ExecutionPoint point;
    TRY(point.copyFrom(mPoint));
    gNavigation->mBreakpointPausedPhase.enterAtEndpoint(point);
}

///////////////////////////////////////////////////////////////////////////////
// ReachBreakpointPhase
///////////////////////////////////////////////////////////////////////////////

void
ReachBreakpointPhase::enter(const ExecutionPoint& point,
                            const Maybe<ExecutionPoint::Prefix>& temporaryCheckpointPrefix)
{
    MOZ_RELEASE_ASSERT(!point.positions.empty());

    TRY(mPoint.copyFrom(point));
    mReached = 0;
    mTemporaryCheckpointPrefix = temporaryCheckpointPrefix;
    mSavedTemporaryCheckpoint = false;

    gNavigation->setPhase(this);

    RestoreCheckpointAndResume(mPoint.checkpoint);
    MOZ_CRASH("Unreachable");
}

void
ReachBreakpointPhase::afterCheckpoint(const CheckpointId& checkpoint)
{
    MOZ_RELEASE_ASSERT(checkpoint == mPoint.checkpoint);

    for (const ExecutionPosition& pos : mPoint.positions)
        EnsurePositionHandler(pos);

    if (mTemporaryCheckpointPrefix.isSome()) {
        // Remember the time we started running forwards from the initial checkpoint.
        mStartTime = ReallyNow();
    }
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
ReachBreakpointPhase::positionHit(const std::function<bool(const ExecutionPosition&)>& match)
{
    if (!match(mPoint.positions[mReached]))
        return;

    ++mReached;

    if (mTemporaryCheckpointPrefix.isSome() && mTemporaryCheckpointPrefix.ref() == mReached) {
        // We've reached the point at which we have the option of saving a
        // temporary checkpoint.
        double elapsedMs = (ReallyNow() - mStartTime).ToMilliseconds();
        if (elapsedMs >= TemporaryCheckpointThresholdMs ||
            gNavigation->mAlwaysSaveTemporaryCheckpoints)
        {
            MOZ_RELEASE_ASSERT(!mSavedTemporaryCheckpoint);
            mSavedTemporaryCheckpoint = true;

            ExecutionPoint temporaryCheckpoint;
            temporaryCheckpoint.checkpoint = mPoint.checkpoint;
            TRY(temporaryCheckpoint.positions.append(mPoint.positions.begin(), mReached));

            // Update our state to be in relation to the temporary checkpoint.
            ExecutionPoint newPoint;
            newPoint.checkpoint = NextTemporaryCheckpoint(mPoint.checkpoint);
            if (mReached < mPoint.positions.length()) {
                TRY(newPoint.positions.append(&mPoint.positions[mReached],
                                              mPoint.positions.length() - mReached));
            }
            TRY(mPoint.copyFrom(newPoint));
            mReached = 0;
            mTemporaryCheckpointPrefix = Nothing();

            if (!gNavigation->saveTemporaryCheckpoint(temporaryCheckpoint)) {
                // We just restored the checkpoint, and could be in any phase.
                gNavigation->positionHit(match, /* updateEndpointConsumed = */ false);
                return;
            }
        }
    }

    if (mReached < mPoint.positions.length())
        return;

    BreakpointVector hitBreakpoints;
    GetAllBreakpointHits(match, hitBreakpoints);
    MOZ_RELEASE_ASSERT(!hitBreakpoints.empty());

    ExecutionPoint point;
    TRY(point.copyFrom(mPoint));
    gNavigation->mBreakpointPausedPhase.enter(point, hitBreakpoints, match);
}

///////////////////////////////////////////////////////////////////////////////
// FindLastHitPhase
///////////////////////////////////////////////////////////////////////////////

void
FindLastHitPhase::addTrackedPosition(const ExecutionPosition& position,
                                     bool allowSubsumeExisting)
{
    // Maintain an invariant that no tracked positions subsume one other.
    // Whenever we hit a position, there can be at most one tracked position
    // which matches it.
    for (ExecutionPosition& existing : mTrackedPositions) {
        if (existing.subsumes(position))
            return;
        if (position.subsumes(existing)) {
            if (allowSubsumeExisting)
                existing = position;
            return;
        }
    }
    TRY(mTrackedPositions.append(position));
}

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
FindLastHitPhase::enter(const ExecutionPoint& point)
{
    TRY(mPoint.copyFrom(point));

    mTrackedPositions.clear();
    mReached = 0;

    gNavigation->setPhase(this);

    // All breakpoints are tracked positions.
    for (const ExecutionPosition& breakpoint : gNavigation->mBreakpoints) {
        if (breakpoint.isValid())
            addTrackedPosition(breakpoint, /* allowSubsumeExisting = */ true);
    }

    // All entry points to scripts containing breakpoints are tracked
    // positions, unless there is a breakpoint which the entry point subsumes.
    // We don't want hits on the entry point to mask hits on real breakpoints.
    for (const ExecutionPosition& breakpoint : gNavigation->mBreakpoints) {
        Maybe<ExecutionPosition> entry = GetEntryPosition(breakpoint);
        if (entry.isSome())
            addTrackedPosition(entry.ref(), /* allowSubsumeExisting = */ false);
    }

    RestoreCheckpointAndResume(mPoint.checkpoint);
    MOZ_CRASH("Unreachable");
}

void
FindLastHitPhase::afterCheckpoint(const CheckpointId& checkpoint)
{
    if (checkpoint == NextNormalCheckpoint(mPoint.checkpoint)) {
        // We reached the next checkpoint, and are done searching.
        MOZ_RELEASE_ASSERT(mPoint.positions.empty());
        onRegionEnd();
        MOZ_CRASH("Unreachable");
    }

    // We are at the start of the search.
    MOZ_RELEASE_ASSERT(checkpoint == mPoint.checkpoint);
    mTrackedHits.clear();

    for (const ExecutionPosition& pos : mTrackedPositions)
        EnsurePositionHandler(pos);

    for (const ExecutionPosition& pos : mPoint.positions)
        EnsurePositionHandler(pos);
}

void
FindLastHitPhase::positionHit(const std::function<bool(const ExecutionPosition&)>& match)
{
    if (!mPoint.positions.empty()) {
        if (match(mPoint.positions[mReached])) {
            if (++mReached == mPoint.positions.length()) {
                onRegionEnd();
                MOZ_CRASH("Unreachable");
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

void
FindLastHitPhase::hitRecordingEndpoint()
{
    onRegionEnd();
    MOZ_CRASH("Unreachable");
}

size_t
FindLastHitPhase::countTrackedHitsInRange(const ExecutionPosition& pos, size_t start, size_t end)
{
    size_t count = 0;
    for (size_t i = start; i <= end; i++) {
        if (pos == mTrackedHits[i])
            count++;
    }
    return count;
}

Maybe<size_t>
FindLastHitPhase::lastMatchingTrackedHit(const std::function<bool(const ExecutionPosition&)>& match,
                                         size_t start, size_t end)
{
    for (int i = end; i >= (int) start; i--) {
        if (match(mTrackedHits[i]))
            return Some((size_t) i);
    }
    return Nothing();
}

static bool
PositionMatchesBreakpoint(const ExecutionPosition& pos)
{
    for (const ExecutionPosition& breakpoint : gNavigation->mBreakpoints) {
        if (breakpoint == pos)
            return true;
    }
    return false;
}

void
FindLastHitPhase::onRegionEnd()
{
    // Find the index of the last hit which coincides with a breakpoint.
    Maybe<size_t> lastBreakpointHit =
        lastMatchingTrackedHit(PositionMatchesBreakpoint, 0, mTrackedHits.length() - 1);

    if (lastBreakpointHit.isNothing()) {
        // No breakpoints were encountered up until the execution point.
        if (gNavigation->numTemporaryCheckpoints()) {
            // The last checkpoint is a temporary one. Continue searching
            // backwards without notifying the middleman.
            ExecutionPoint point;
            gNavigation->lastTemporaryCheckpoint(&point);
            gNavigation->mFindLastHitPhase.enter(point);
            MOZ_CRASH("Unreachable");
        } else {
            // Rewind to the last checkpoint and pause.
            MOZ_RELEASE_ASSERT(!mPoint.checkpoint.mTemporary);
            gNavigation->mCheckpointPausedPhase.enter(mPoint.checkpoint.mNormal,
                                                      /* rewind = */ true);
            MOZ_CRASH("Unreachable");
        }
    }

    const ExecutionPosition& breakpoint = mTrackedHits[lastBreakpointHit.ref()];

    // When running backwards, we don't want to place temporary checkpoints at
    // the breakpoint where we are going to stop at. If the user continues
    // rewinding then we will just have to discard the checkpoint and waste the
    // work we did in saving it.
    //
    // Instead, try to place a temporary checkpoint at the last time the
    // breakpoint's script was entered. This optimizes for the case of stepping
    // around within a frame.
    Maybe<ExecutionPosition> baseEntry = GetEntryPosition(breakpoint);
    if (baseEntry.isSome() && baseEntry.ref().offset != breakpoint.offset) {
        Maybe<size_t> lastEntryHit =
            lastMatchingTrackedHit([=](const ExecutionPosition& pos) {
                return baseEntry.ref().subsumes(pos);
            }, 0, lastBreakpointHit.ref() - 1);
        if (lastEntryHit.isSome()) {
            // The hit we found might not be identical to |baseEntry|
            // if there is an OnStep breakpoint at the script's entry point.
            ExecutionPosition entry = mTrackedHits[lastEntryHit.ref()];
            MOZ_RELEASE_ASSERT(baseEntry.ref().subsumes(entry));

            size_t entryHits = countTrackedHitsInRange(entry, 0, lastBreakpointHit.ref() - 1);
            MOZ_RELEASE_ASSERT(entryHits);

            size_t breakpointHitsAfterEntry =
                countTrackedHitsInRange(breakpoint,
                                        lastEntryHit.ref() + 1, lastBreakpointHit.ref());
            MOZ_RELEASE_ASSERT(breakpointHitsAfterEntry);

            ExecutionPoint newPoint;
            newPoint.checkpoint = mPoint.checkpoint;
            TRY(newPoint.positions.appendN(entry, entryHits));
            TRY(newPoint.positions.appendN(breakpoint, breakpointHitsAfterEntry));

            gNavigation->mReachBreakpointPhase.enter(newPoint, Some(entryHits));
            MOZ_CRASH("Unreachable");
        }
    }

    // There was no suitable place for a temporary checkpoint, so rewind to the
    // last checkpoint and play forward to the last breakpoint hit we found.
    size_t breakpointHits = countTrackedHitsInRange(breakpoint, 0, lastBreakpointHit.ref());
    MOZ_RELEASE_ASSERT(breakpointHits);

    ExecutionPoint newPoint;
    newPoint.checkpoint = mPoint.checkpoint;
    TRY(newPoint.positions.appendN(breakpoint, breakpointHits));

    gNavigation->mReachBreakpointPhase.enter(newPoint, Nothing());
    MOZ_CRASH("Unreachable");
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

    gNavigation->positionHit(
        [=](const ExecutionPosition& position) {
            return position.script == scriptId
                && position.offset == offset
                && (position.kind == ExecutionPosition::Break ||
                    position.frameIndex == frameIndex);
        }, /* updateEndpointConsumed = */ true);

    args.rval().setUndefined();
    return true;
}

static bool
EnterFrameHandler(JSContext* cx, unsigned argc, Value* vp)
{
    CallArgs args = CallArgsFromVp(argc, vp);

    gNavigation->positionHit(
        [=](const ExecutionPosition& position) {
            return position.kind == ExecutionPosition::EnterFrame;
        }, /* updateEndpointConsumed = */ true);

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

    gNavigation->positionHit(
        [=](const ExecutionPosition& position) {
            return position.kind == ExecutionPosition::OnPop
                && (position.script == ExecutionPosition::EMPTY_SCRIPT ||
                    position.script == scriptId);
        }, /* updateEndpointConsumed = */ true);

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
        gNavigation->positionHit(
            [=](const ExecutionPosition& position) {
                return position.kind == ExecutionPosition::NewScript;
            }, /* updateEndpointConsumed = */ true);
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

static void
GetRecordingEndpointHook(JS::replay::ExecutionPoint<JS::replay::Hooks::TrackedAllocPolicy>* endpoint)
{
    MOZ_RELEASE_ASSERT(IsRecording());
    ExecutionPoint newEndpoint;
    gNavigation->getRecordingEndpoint(&newEndpoint);
    endpoint->copyFrom(newEndpoint);
}

static void
SetRecordingEndpointHook(const JS::replay::ExecutionPoint<JS::replay::Hooks::TrackedAllocPolicy>& endpoint)
{
    MOZ_RELEASE_ASSERT(IsReplaying());
    ExecutionPoint newEndpoint;
    newEndpoint.copyFrom(endpoint);
    gNavigation->setRecordingEndpoint(newEndpoint);
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
