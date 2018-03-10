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

  // Set from another thread if the main thread should restore the last
  // snapshot.
  bool mNeedLastDitchRestore;

  // The number of times a last ditch snapshot restore has been performed.
  size_t mNumLastDitchRestores;

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

// Callback to execute on the main thread, assigned by either the main thread or
// the replay message loop thread. Protected by the global lock.
static std::function<void()> gMainThreadCallback;

// Condvar for notifying when a callback has been executed.
static Monitor* gMainThreadCallbackMonitor;

void
InitializeRewindState()
{
  MOZ_RELEASE_ASSERT(gRewindInfo == nullptr);
  void* memory = AllocateMemory(sizeof(RewindInfo), UntrackedMemoryKind::Generic);
  gRewindInfo = new(memory) RewindInfo();

  gMainThreadCallbackMonitor = new Monitor();
}

extern "C" {

MOZ_EXPORT bool
RecordReplayInterface_CanRestoreSnapshots()
{
  return gTakeSnapshots;
}

MOZ_EXPORT void
RecordReplayInterface_RestoreSnapshotAndResume(size_t aSnapshot)
{
  MOZ_RELEASE_ASSERT(Thread::CurrentIsMainThread());
  MOZ_RELEASE_ASSERT(!AreThreadEventsPassedThrough());
  MOZ_RELEASE_ASSERT(aSnapshot <= gRewindInfo->mLastSnapshot);

  // If this restore is part of a main thread callback, clear the callback and
  // notify the replay message loop thread first.
  if (gMainThreadCallback) {
    gMainThreadCallback = std::function<void()>();
    MonitorAutoLock lock(*gMainThreadCallbackMonitor);
    gMainThreadCallbackMonitor->Notify();
  }

  Thread::WaitForIdleThreads();

  if (IsRecording()) {
    PrepareForFirstRecordingRewind();
  }

  // Reset the last ditch restore bit.
  gRewindInfo->mNeedLastDitchRestore = false;

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
static BeforeLastDitchRestoreHook gBeforeLastDitchRestoreHook;

MOZ_EXPORT void
RecordReplayInterface_SetSnapshotHooks(BeforeSnapshotHook aBeforeSnapshot,
                                       AfterSnapshotHook aAfterSnapshot,
                                       BeforeLastDitchRestoreHook aBeforeLastDitchRestore)
{
  gBeforeSnapshotHook = aBeforeSnapshot;
  gAfterSnapshotHook = aAfterSnapshot;
  gBeforeLastDitchRestoreHook = aBeforeLastDitchRestore;
}

} // extern "C"

static double gLastRecordedSnapshot;

static double SecondsBetweenSnapshots = 3.0;

static bool
ShouldRecordSnapshot(size_t aSnapshot)
{
  if (!gTakeSnapshots) {
    return false;
  }

  // The first snapshot is always recorded.
  if (!aSnapshot) {
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
TakeSnapshot(bool aFinal)
{
  MOZ_RELEASE_ASSERT(Thread::CurrentIsMainThread());
  MOZ_RELEASE_ASSERT(!AreThreadEventsPassedThrough());

  gBeforeSnapshotHook();

  // Get the ID of the new snapshot.
  size_t snapshot = gRewindInfo->mTakenSnapshot ? gRewindInfo->mLastSnapshot + 1 : 0;
  if (aFinal) {
    MOZ_RELEASE_ASSERT(!gRewindInfo->mFinalSnapshot || gRewindInfo->mFinalSnapshot == snapshot);
    gRewindInfo->mFinalSnapshot = snapshot;
  }

  if (!gTakeSnapshots) {
    // Setup the dirty memory handler even if we aren't taking snapshots, for
    // help in debugging SEGVs.
    AutoPassThroughThreadEvents pt;
    SetupDirtyMemoryHandler();
  }

  bool justTookSnapshot = true;

  bool recordSnapshot = ShouldRecordSnapshot(snapshot);
  if (recordSnapshot) {
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
      PrintSpew("Took snapshot #%d %.2fs\n", (int) snapshot, (end - start) / 1000000.0);
    } else {
      PrintSpew("Restored snapshot #%d\n", (int) snapshot);

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
  gAfterSnapshotHook(snapshot, final, interim, recordSnapshot);
}

static const size_t MAX_LAST_DITCH_RESTORES = 5;

static StaticMutexNotRecorded gLastDitchRestoreMutex;

bool
NeedLastDitchRestore()
{
  return gRewindInfo->mNeedLastDitchRestore;
}

void
LastDitchRestoreSnapshot()
{
  if (!gRewindInfo->mTakenSnapshot) {
    MOZ_CRASH();
  }

  if (HasDivergedFromRecording()) {
    MOZ_CRASH();
  }

  {
    StaticMutexAutoLock lock(gLastDitchRestoreMutex);
    if (!gRewindInfo->mNeedLastDitchRestore) {
      if (gRewindInfo->mNumLastDitchRestores == MAX_LAST_DITCH_RESTORES) {
        MOZ_CRASH();
      }
      gRewindInfo->mNeedLastDitchRestore = true;
      gRewindInfo->mNumLastDitchRestores++;
    }
  }

  // If we are on the main thread then we can restore to the last recorded
  // snapshot immediately.
  if (Thread::CurrentIsMainThread()) {
    if (!gRewindInfo->mRestoreTargetSnapshot) {
      gBeforeLastDitchRestoreHook();
    }
    RestoreSnapshotAndResume(gRewindInfo->mActiveRecordedSnapshot);
    Unreachable();
  }

  // Otherwise, notify the main thread so that it can wake up from any
  // Thread::Wait it is in. The main thread will check NeedLastDitchRestore()
  // before waiting.
  Thread::Notify(MainThreadId);

  // Wait indefinitely until the main thread rewinds us.
  while (true) {
    Thread::Wait();
  }
  Unreachable();
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

} // extern "C"

void
EnsureNotDivergedFromRecording()
{
  MOZ_RELEASE_ASSERT(!AreThreadEventsPassedThrough());
  if (HasDivergedFromRecording()) {
    MOZ_RELEASE_ASSERT(gUnhandledDivergeAllowed);
    RestoreSnapshotAndResume(gRewindInfo->mActiveRecordedSnapshot);
    Unreachable();
  }
}

bool
HasTakenSnapshot()
{
  return gRewindInfo && gRewindInfo->mTakenSnapshot;
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

void
MaybePauseMainThread()
{
  MOZ_RELEASE_ASSERT(Thread::CurrentIsMainThread());
  MOZ_RELEASE_ASSERT(!AreThreadEventsPassedThrough());
  MOZ_RELEASE_ASSERT(!gRecordingDiverged);
  MonitorAutoLock lock(*gMainThreadCallbackMonitor);

  // Loop and invoke callbacks until one of them unpauses this thread.
  while (gMainThreadShouldPause) {
    if (gMainThreadCallback) {
      {
        MonitorAutoUnlock unlock(*gMainThreadCallbackMonitor);
        AutoDisallowThreadEvents disallow;
        gMainThreadCallback();
      }
      gMainThreadCallback = std::function<void()>();
      gMainThreadCallbackMonitor->Notify();
      continue;
    }
    gMainThreadCallbackMonitor->Wait();
  }

  // If we diverge from the recording the only way we can get back to resuming
  // normal execution is to rewind to a snapshot prior to the divergence.
  MOZ_RELEASE_ASSERT(!gRecordingDiverged);
}

void
PauseMainThreadAndInvokeCallback(const std::function<void()>& aCallback, bool aSynchronous)
{
  MonitorAutoLock lock(*gMainThreadCallbackMonitor);
  gMainThreadShouldPause = true;

  // Clear any existing callback.
  if (Thread::CurrentIsMainThread()) {
    if (gMainThreadCallback) {
      {
        MonitorAutoUnlock unlock(*gMainThreadCallbackMonitor);
        AutoDisallowThreadEvents disallow;
        gMainThreadCallback();
      }
      gMainThreadCallback = std::function<void()>();
    }
  } else {
    while (gMainThreadCallback) {
      gMainThreadCallbackMonitor->Wait();
    }
  }

  gMainThreadCallback = aCallback;

  if (Thread::CurrentIsMainThread()) {
    MonitorAutoUnlock unlock(*gMainThreadCallbackMonitor);
    MaybePauseMainThread();
  } else {
    gMainThreadCallbackMonitor->Notify();
    if (aSynchronous) {
      while (gMainThreadCallback) {
        gMainThreadCallbackMonitor->Wait();
      }
    }
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
