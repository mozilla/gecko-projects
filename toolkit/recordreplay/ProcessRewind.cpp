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

// Information about the current snapshot state. The contents of this structure
// are in untracked memory.
struct RewindInfo {
  // Whether the first snapshot has been encountered.
  bool mTakenSnapshot;

  // The most recent snapshot which was encountered.
  size_t mLastSnapshot;

  // The last snapshot in the execution, zero if it has not been encountered.
  size_t mFinalSnapshot;

  // The snapshot which will become the next recorded snapshot, unless
  // RestoreSnapshotAndResume is called first.
  size_t mActiveRecordedSnapshot;

  // Snapshots which have actually been recorded.
  InfallibleVector<size_t, 1024, AllocPolicy<UntrackedMemoryKind::Generic>> mRecordedSnapshots;

  // Any snapshot which we are trying to rewind back to but did not record when
  // we encountered it earlier, zero if not set.
  size_t mRestoreTargetSnapshot;
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

static bool gAllowRecordingSnapshots;

void
SetRecordSnapshots(bool aAllowed)
{
  gAllowRecordingSnapshots = aAllowed;
}

extern "C" {

MOZ_EXPORT void
RecordReplayInterface_RestoreSnapshotAndResume(size_t aSnapshot)
{
  MOZ_RELEASE_ASSERT(Thread::CurrentIsMainThread());
  MOZ_RELEASE_ASSERT(!AreThreadEventsPassedThrough());
  MOZ_RELEASE_ASSERT(aSnapshot <= gRewindInfo->mLastSnapshot);

  // Make sure we don't lose pending main thread callbacks due to rewinding.
  MOZ_RELEASE_ASSERT(gMainThreadCallbacks.empty());

  Thread::WaitForIdleThreads();

  if (IsRecording()) {
    PrepareForFirstRecordingRewind();
  }

  // This is a structural assert. If we are recording and snapshots are only
  // enabled while replaying, then we will bust on this assert and the
  // middleman will start up a replaying process to recover this state and
  // then rewind.
  MOZ_RELEASE_ASSERT(gAllowRecordingSnapshots);

  double start = CurrentTime();

  // Rewind heap memory to the last recorded point which is at or before the
  // target snapshot.
  size_t newSnapshot = gRewindInfo->mActiveRecordedSnapshot;
  RestoreMemoryToActiveSnapshot();
  while (newSnapshot > aSnapshot) {
    newSnapshot = gRewindInfo->mRecordedSnapshots.back();
    RestoreMemoryToLastRecordedDiffSnapshot();
    gRewindInfo->mRecordedSnapshots.popBack();
  }

  FixupAfterRewind();

  // If we are going back further than was asked for, we will need to visit
  // interim snapshots on our way to the target one.
  if (newSnapshot != aSnapshot) {
    MOZ_RELEASE_ASSERT(!gRewindInfo->mRestoreTargetSnapshot);
    gRewindInfo->mRestoreTargetSnapshot = aSnapshot;
  }

  double end = CurrentTime();
  PrintSpew("Restore #%d -> #%d %.2fs\n",
            (int) gRewindInfo->mLastSnapshot, (int) newSnapshot,
            (end - start) / 1000000.0);

  // Finally, let threads restore themselves to their stacks at the snapshot
  // we are rewinding to.
  Thread::RestoreAllThreads(newSnapshot);
  Unreachable();
}

static BeforeSnapshotHook gBeforeSnapshotHook;
static AfterSnapshotHook gAfterSnapshotHook;

MOZ_EXPORT void
RecordReplayInterface_SetSnapshotHooks(BeforeSnapshotHook aBeforeSnapshot,
                                       AfterSnapshotHook aAfterSnapshot)
{
  gBeforeSnapshotHook = aBeforeSnapshot;
  gAfterSnapshotHook = aAfterSnapshot;
}

} // extern "C"

// Whether we have recorded a temporary snapshot.
static bool gHasTemporarySnapshot;

bool
HasTemporarySnapshot()
{
  return gHasTemporarySnapshot;
}

static double gLastRecordedSnapshot;

static double SecondsBetweenSnapshots = 3.0;

static bool
ShouldRecordSnapshot(size_t aSnapshot)
{
  if (!gAllowRecordingSnapshots) {
    return false;
  }

  // The first snapshot and temporary snapshots are always recorded.
  if (!aSnapshot || gHasTemporarySnapshot) {
    return true;
  }

  // All interim snapshots are recorded (in case we do more rewinding immediately).
  if (gRewindInfo->mRestoreTargetSnapshot) {
    return true;
  }

  double elapsed = CurrentTime() - gLastRecordedSnapshot;
  if (elapsed / 1000000.0 >= SecondsBetweenSnapshots) {
    return true;
  }

  return false;
}

// Take a snapshot, which we might or might not record.
void
TakeSnapshot(bool aFinal, bool aTemporary)
{
  MOZ_RELEASE_ASSERT(Thread::CurrentIsMainThread());
  MOZ_RELEASE_ASSERT(!AreThreadEventsPassedThrough());
  MOZ_RELEASE_ASSERT(!aFinal || !aTemporary);
  MOZ_RELEASE_ASSERT(IsReplaying() || !aTemporary);

  gBeforeSnapshotHook();

  // Get the ID of the new snapshot.
  size_t snapshot = gRewindInfo->mTakenSnapshot ? gRewindInfo->mLastSnapshot + 1 : 0;
  if (aFinal) {
    MOZ_RELEASE_ASSERT(!gRewindInfo->mFinalSnapshot || gRewindInfo->mFinalSnapshot == snapshot);
    gRewindInfo->mFinalSnapshot = snapshot;
  } else if (aTemporary) {
    gHasTemporarySnapshot = true;
  }

  if (!gAllowRecordingSnapshots) {
    // Setup the dirty memory handler even if we aren't taking snapshots, for
    // reporting crashes to the middleman.
    AutoPassThroughThreadEvents pt;
    SetupDirtyMemoryHandler();
  }

  bool justTookSnapshot = true;

  if (ShouldRecordSnapshot(snapshot)) {
    gLastRecordedSnapshot = CurrentTime();

    Thread::WaitForIdleThreads();

    PrintSpew("Starting snapshot...\n");

    double start = CurrentTime();

    // Record either the first or a subsequent diff snapshot.
    if (!gRewindInfo->mTakenSnapshot) {
      TakeFirstMemorySnapshot();
      gRewindInfo->mTakenSnapshot = true;
    } else {
      gRewindInfo->mRecordedSnapshots.emplaceBack(gRewindInfo->mActiveRecordedSnapshot);
      TakeDiffMemorySnapshot();
    }

    double end = CurrentTime();

    // Save all thread stacks for the snapshot. If we rewind here from a later
    // point of execution then this will return false.
    if (Thread::SaveAllThreads(snapshot)) {
      PrintSpew("Took snapshot #%d%s %.2fs\n", (int) snapshot,
                aTemporary ? " (temporary)" : "", (end - start) / 1000000.0);
    } else {
      PrintSpew("Restored snapshot #%d%s\n", (int) snapshot,
                aTemporary ? " (temporary)" : "");

      justTookSnapshot = false;

      // After restoring, make sure all threads have updated their stacks
      // before letting any of them resume execution. Threads might have
      // pointers into each others' stacks.
      Thread::WaitForIdleThreadsToRestoreTheirStacks();
    }

    if (gRewindInfo->mRestoreTargetSnapshot && gRewindInfo->mRestoreTargetSnapshot == snapshot) {
      gRewindInfo->mRestoreTargetSnapshot = 0;
    }

    gRewindInfo->mActiveRecordedSnapshot = gRewindInfo->mLastSnapshot = snapshot;
    Thread::ResumeIdleThreads();
  } else {
    MOZ_RELEASE_ASSERT(!gRewindInfo->mRestoreTargetSnapshot);
    MOZ_RELEASE_ASSERT(!aTemporary);
    gRewindInfo->mTakenSnapshot = true;
    gRewindInfo->mLastSnapshot = snapshot;
  }

  if (aFinal) {
    for (size_t i = 0; i < 50; i++) {
      PrintSpew("!!!!! REPLAY FINISHED\n");
    }
  }

  bool final = snapshot && snapshot == gRewindInfo->mFinalSnapshot;
  bool interim = !!gRewindInfo->mRestoreTargetSnapshot;
  MOZ_RELEASE_ASSERT(!interim || snapshot < gRewindInfo->mRestoreTargetSnapshot);

  AutoDisallowThreadEvents disallow;

  dom::AutoJSAPI jsapi;
  jsapi.Init();
  gAfterSnapshotHook(snapshot, final, interim);

  // Taking snapshots after a temporary one is allowed, but we shouldn't be
  // executing past the point of the next normal snapshot point. We might get
  // to such normal snapshots after having taken temporary snapshots, but the
  // AfterSnapshot hook should rewind in such cases.
  MOZ_RELEASE_ASSERT(!gHasTemporarySnapshot || aTemporary);
}

static bool gRecordingDiverged;
static bool gUnhandledDivergeAllowed;

extern "C" {

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
RecordReplayInterface_TakeTemporarySnapshot()
{
  TakeSnapshot(/* aFinal = */ false, /* aTemporary = */ true);
}

} // extern "C"

void
EnsureNotDivergedFromRecording()
{
  MOZ_RELEASE_ASSERT(!AreThreadEventsPassedThrough());
  if (HasDivergedFromRecording()) {
    MOZ_RELEASE_ASSERT(gUnhandledDivergeAllowed);
    PrintSpew("Unhandled recording divergence, restoring snapshot...\n");
    RestoreSnapshotAndResume(gRewindInfo->mActiveRecordedSnapshot);
    Unreachable();
  }
}

bool
HasTakenSnapshot()
{
  return gRewindInfo && gRewindInfo->mTakenSnapshot && gAllowRecordingSnapshots;
}

bool
LastSnapshotIsInterim()
{
  return gRewindInfo && gRewindInfo->mRestoreTargetSnapshot;
}

size_t
GetLastRecordedDiffSnapshot()
{
  MOZ_RELEASE_ASSERT(gRewindInfo->mRecordedSnapshots.length());
  return gRewindInfo->mRecordedSnapshots.back();
}

size_t
GetActiveRecordedSnapshot()
{
  return gRewindInfo->mActiveRecordedSnapshot;
}

static bool gMainThreadShouldPause = false;

bool
MainThreadShouldPause()
{
  return gMainThreadShouldPause;
}

// Whether there is a MaybePauseMainThread frame on the stack.
static bool gMainThreadIsPaused = false;

void
MaybePauseMainThread()
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

  // As for RestoreSnapshotAndResume, we shouldn't resume the main thread while
  // it still has callbacks to execute.
  MOZ_RELEASE_ASSERT(gMainThreadCallbacks.empty());

  // If we diverge from the recording the only way we can get back to resuming
  // normal execution is to rewind to a snapshot prior to the divergence.
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
    MaybePauseMainThread();
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

} // namespace recordreplay
} // namespace mozilla
