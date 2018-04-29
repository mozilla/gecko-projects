/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ProcessRewind.h"

#include "nsString.h"
#include "mozilla/dom/ScriptSettings.h"
#include "mozilla/StaticMutex.h"
#include "InfallibleVector.h"
#include "MemorySnapshot.h"
#include "Monitor.h"
#include "ProcessRecordReplay.h"
#include "Thread.h"
#include "prcvar.h"

#include <setjmp.h>

#ifdef XP_MACOSX
#include <sys/time.h>
#endif

namespace mozilla {
namespace recordreplay {

// Information about the current rewinding state. The contents of this structure
// are in untracked memory.
struct RewindInfo {
  // The most recent checkpoint which was encountered.
  CheckpointId mLastCheckpoint;

  // Whether this is the active child process.
  bool mIsActiveChild;

  // Checkpoints which have been saved. This includes only entries from
  // mShouldSaveCheckpoints, plus all temporary checkpoints.
  InfallibleVector<CheckpointId, 1024, AllocPolicy<UntrackedMemoryKind::Generic>> mSavedCheckpoints;

  // Unsorted list of checkpoints which the middleman has instructed us to
  // save. All those equal to or prior to mLastCheckpoint will have been saved.
  InfallibleVector<size_t, 1024, AllocPolicy<UntrackedMemoryKind::Generic>> mShouldSaveCheckpoints;
};

static RewindInfo* gRewindInfo;

// Lock for managing pending main thread callbacks.
static Monitor* gMainThreadCallbackMonitor;

// Callbacks to execute on the main thread, in FIFO order. Protected by
// gMainThreadCallbackMonitor.
static StaticInfallibleVector<std::function<void()>> gMainThreadCallbacks;

void
InitializeRewindState()
{
  MOZ_RELEASE_ASSERT(gRewindInfo == nullptr);
  void* memory = AllocateMemory(sizeof(RewindInfo), UntrackedMemoryKind::Generic);
  gRewindInfo = new(memory) RewindInfo();

  gMainThreadCallbackMonitor = new Monitor();
}

static bool
CheckpointPrecedes(const CheckpointId& aFirst, const CheckpointId& aSecond)
{
  return aFirst.mNormal < aSecond.mNormal || aFirst.mTemporary < aSecond.mTemporary;
}

extern "C" {

MOZ_EXPORT void
RecordReplayInterface_RestoreCheckpointAndResume(const CheckpointId& aCheckpoint)
{
  MOZ_RELEASE_ASSERT(IsReplaying());
  MOZ_RELEASE_ASSERT(Thread::CurrentIsMainThread());
  MOZ_RELEASE_ASSERT(!AreThreadEventsPassedThrough());
  MOZ_RELEASE_ASSERT(aCheckpoint == gRewindInfo->mLastCheckpoint ||
                     CheckpointPrecedes(aCheckpoint, gRewindInfo->mLastCheckpoint));

  // Make sure we don't lose pending main thread callbacks due to rewinding.
  MOZ_RELEASE_ASSERT(gMainThreadCallbacks.empty());

  Thread::WaitForIdleThreads();

  double start = CurrentTime();

  // Rewind heap memory to the target checkpoint, which must have been saved.
  CheckpointId newCheckpoint = gRewindInfo->mSavedCheckpoints.back();
  RestoreMemoryToLastSavedCheckpoint();
  while (CheckpointPrecedes(aCheckpoint, newCheckpoint)) {
    gRewindInfo->mSavedCheckpoints.popBack();
    RestoreMemoryToLastSavedDiffCheckpoint();
    newCheckpoint = gRewindInfo->mSavedCheckpoints.back();
  }
  MOZ_RELEASE_ASSERT(newCheckpoint == aCheckpoint);

  FixupFreeRegionsAfterRewind();

  double end = CurrentTime();
  PrintSpew("Restore #%d:%d -> #%d:%d %.2fs\n",
            (int) gRewindInfo->mLastCheckpoint.mNormal,
            (int) gRewindInfo->mLastCheckpoint.mTemporary,
            (int) newCheckpoint.mNormal,
            (int) newCheckpoint.mTemporary,
            (end - start) / 1000000.0);

  // Finally, let threads restore themselves to their stacks at the checkpoint
  // we are rewinding to.
  Thread::RestoreAllThreads(newCheckpoint);
  Unreachable();
}

static BeforeCheckpointHook gBeforeCheckpointHook;
static AfterCheckpointHook gAfterCheckpointHook;

MOZ_EXPORT void
RecordReplayInterface_SetCheckpointHooks(BeforeCheckpointHook aBeforeCheckpoint,
                                         AfterCheckpointHook aAfterCheckpoint)
{
  gBeforeCheckpointHook = aBeforeCheckpoint;
  gAfterCheckpointHook = aAfterCheckpoint;
}

} // extern "C"

void
SetSaveCheckpoint(size_t aCheckpoint, bool aSave)
{
  MOZ_RELEASE_ASSERT(aCheckpoint > gRewindInfo->mLastCheckpoint.mNormal);
  VectorAddOrRemoveEntry(gRewindInfo->mShouldSaveCheckpoints, aCheckpoint, aSave);
}

extern "C" {

// Mark a checkpoint, which we might or might not save.
MOZ_EXPORT bool
RecordReplayInterface_NewCheckpoint(bool aTemporary)
{
  MOZ_RELEASE_ASSERT(Thread::CurrentIsMainThread());
  MOZ_RELEASE_ASSERT(!AreThreadEventsPassedThrough());
  MOZ_RELEASE_ASSERT(IsReplaying() || !aTemporary);

  gBeforeCheckpointHook();

  // Get the ID of the new checkpoint.
  CheckpointId checkpoint = gRewindInfo->mLastCheckpoint;
  if (aTemporary) {
    checkpoint.mTemporary++;
  } else {
    checkpoint.mNormal++;
    checkpoint.mTemporary = 0;
  }

  // Save all checkpoints the middleman tells us to, and temporary checkpoints
  // (which the middleman never knows about).
  bool save = aTemporary
           || VectorContains(gRewindInfo->mShouldSaveCheckpoints, checkpoint.mNormal);
  bool reachedCheckpoint = true;

  if (save) {
    Thread::WaitForIdleThreads();

    PrintSpew("Starting checkpoint...\n");

    double start = CurrentTime();

    // Record either the first or a subsequent diff memory snapshot.
    if (gRewindInfo->mSavedCheckpoints.empty()) {
      TakeFirstMemorySnapshot();
    } else {
      TakeDiffMemorySnapshot();
    }
    gRewindInfo->mSavedCheckpoints.emplaceBack(checkpoint);

    double end = CurrentTime();

    // Save all thread stacks for the checkpoint. If we rewind here from a
    // later point of execution then this will return false.
    if (Thread::SaveAllThreads(checkpoint)) {
      PrintSpew("Saved checkpoint #%d:%d %.2fs\n",
                (int) checkpoint.mNormal, (int) checkpoint.mTemporary,
                (end - start) / 1000000.0);
    } else {
      PrintSpew("Restored checkpoint #%d:%d\n",
                (int) checkpoint.mNormal, (int) checkpoint.mTemporary);

      reachedCheckpoint = false;

      // After restoring, make sure all threads have updated their stacks
      // before letting any of them resume execution. Threads might have
      // pointers into each others' stacks.
      Thread::WaitForIdleThreadsToRestoreTheirStacks();
    }

    Thread::ResumeIdleThreads();
  }

  gRewindInfo->mLastCheckpoint = checkpoint;

  AutoDisallowThreadEvents disallow;

  dom::AutoJSAPI jsapi;
  jsapi.Init();
  gAfterCheckpointHook(checkpoint);

  return reachedCheckpoint;
}

static bool gRecordingDiverged;
static bool gUnhandledDivergeAllowed;

MOZ_EXPORT void
RecordReplayInterface_DivergeFromRecording()
{
  MOZ_RELEASE_ASSERT(Thread::CurrentIsMainThread());
  MOZ_RELEASE_ASSERT(IsReplaying());
  gRecordingDiverged = true;
  gUnhandledDivergeAllowed = true;
}

MOZ_EXPORT bool
RecordReplayInterface_InternalHasDivergedFromRecording()
{
  return gRecordingDiverged && Thread::CurrentIsMainThread();
}

MOZ_EXPORT void
RecordReplayInterface_DisallowUnhandledDivergeFromRecording()
{
  MOZ_RELEASE_ASSERT(Thread::CurrentIsMainThread());
  gUnhandledDivergeAllowed = false;
}

MFBT_API void
RecordReplayInterface_SaveTemporaryCheckpoint()
{
  NewCheckpoint(/* aTemporary = */ true);
}

} // extern "C"

void
EnsureNotDivergedFromRecording()
{
  MOZ_RELEASE_ASSERT(!AreThreadEventsPassedThrough());
  if (HasDivergedFromRecording()) {
    MOZ_RELEASE_ASSERT(gUnhandledDivergeAllowed);
    PrintSpew("Unhandled recording divergence, restoring checkpoint...\n");
    RestoreCheckpointAndResume(gRewindInfo->mSavedCheckpoints.back());
    Unreachable();
  }
}

bool
HasSavedCheckpoint()
{
  return gRewindInfo && !gRewindInfo->mSavedCheckpoints.empty();
}

CheckpointId
GetLastSavedCheckpoint()
{
  MOZ_RELEASE_ASSERT(!gRewindInfo->mSavedCheckpoints.empty());
  return gRewindInfo->mSavedCheckpoints.back();
}

static bool gMainThreadShouldPause = false;

bool
MainThreadShouldPause()
{
  return gMainThreadShouldPause;
}

// Whether there is a PauseMainThreadAndServiceCallbacks frame on the stack.
static bool gMainThreadIsPaused = false;

void
PauseMainThreadAndServiceCallbacks()
{
  MOZ_RELEASE_ASSERT(Thread::CurrentIsMainThread());
  MOZ_RELEASE_ASSERT(!AreThreadEventsPassedThrough());
  MOZ_RELEASE_ASSERT(!gRecordingDiverged);

  if (gMainThreadIsPaused)
    return;
  gMainThreadIsPaused = true;

  MonitorAutoLock lock(*gMainThreadCallbackMonitor);

  // Loop and invoke callbacks until one of them unpauses this thread.
  while (gMainThreadShouldPause) {
    if (!gMainThreadCallbacks.empty()) {
      std::function<void()> callback = gMainThreadCallbacks[0];
      gMainThreadCallbacks.erase(&gMainThreadCallbacks[0]);
      {
        MonitorAutoUnlock unlock(*gMainThreadCallbackMonitor);
        AutoDisallowThreadEvents disallow;
        callback();
      }
      continue;
    }
    gMainThreadCallbackMonitor->Wait();
  }

  // As for RestoreCheckpointAndResume, we shouldn't resume the main thread while
  // it still has callbacks to execute.
  MOZ_RELEASE_ASSERT(gMainThreadCallbacks.empty());

  // If we diverge from the recording the only way we can get back to resuming
  // normal execution is to rewind to a checkpoint prior to the divergence.
  MOZ_RELEASE_ASSERT(!gRecordingDiverged);

  gMainThreadIsPaused = false;
}

void
PauseMainThreadAndInvokeCallback(const std::function<void()>& aCallback)
{
  {
    MonitorAutoLock lock(*gMainThreadCallbackMonitor);
    gMainThreadShouldPause = true;
    gMainThreadCallbacks.append(aCallback);
    gMainThreadCallbackMonitor->Notify();
  }

  if (Thread::CurrentIsMainThread()) {
    PauseMainThreadAndServiceCallbacks();
  }
}

extern "C" {

MOZ_EXPORT void
RecordReplayInterface_ResumeExecution()
{
  MonitorAutoLock lock(*gMainThreadCallbackMonitor);
  gMainThreadShouldPause = false;
  gMainThreadCallbackMonitor->Notify();
}

} // extern "C"

void
SetIsActiveChild(bool aActive)
{
  gRewindInfo->mIsActiveChild = aActive;
}

bool
IsActiveChild()
{
  return gRewindInfo->mIsActiveChild;
}

} // namespace recordreplay
} // namespace mozilla
