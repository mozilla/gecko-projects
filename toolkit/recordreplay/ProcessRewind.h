/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_toolkit_recordreplay_ProcessRewind_h
#define mozilla_toolkit_recordreplay_ProcessRewind_h

#include "mozilla/Types.h"

#include <functional>

namespace mozilla {
namespace recordreplay {

// This file is responsible for keeping track of and managing the current point
// of execution when replaying an execution, and in allowing the process to
// rewind its state to an earlier point of execution.

///////////////////////////////////////////////////////////////////////////////
// Snapshots Overview.
//
// Snapshots are taken periodically by the main thread of a replaying process.
// Snapshots must be taken at consistent points between different executions of
// the replay. Currently they are taken after XPCOM initialization and every
// time compositor updates are performed. Each snapshot has an ID, which
// monotonically increases during the execution. Snapshots form a basis for
// identifying a particular point in execution, and in allowing the process to
// rewind itself.
//
// A subset of snapshots are recorded: the contents of each thread's stack is
// saved, along with enough information to restore the contents of heap memory
// at the snapshot. The first snapshot is always recorded, and later snapshots
// can be rewound to, even if they weren't recorded, by rewinding to the
// closest earlier snapshot and then running forward from there.
//
// Recorded snapshots are in part represented as diffs vs the following
// recorded snapshot. This requires some different handling for the most recent
// recorded snapshot (whose diff has not been computed) and earlier recorded
// snapshots. See MemorySnapshot.h and Thread.h for more on how recorded
// snapshots are represented.
///////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
// Controlling a Replaying Process.
//
// 1. While performing the replay, execution proceeds until the main thread
//    hits either a breakpoint or a snapshot point.
//
// 2. The main thread then calls a hook (JS::replay::hooks.hitBreakpointReplay
//    or gAfterSnapshotHook), which may decide to pause the main thread and
//    give it a callback to invoke using PauseMainThreadAndInvokeCallback.
//
// 3. Now that the main thread is paused, the replay message loop thread
//    (see ChildIPC.h) can give it additional callbacks to invoke using
//    PauseMainThreadAndInvokeCallback.
//
// 4. These callbacks can inspect the paused state, diverge from the recording
//    by calling TakeSnapshotAndDivergeFromRecording, and eventually can
//    unpause the main thread and allow execution to resume by calling
//    ResumeExecution (if TakeSnapshotAndDivergeFromRecording was not called)
//    or RestoreSnapshotAndResume.
///////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
// Recording Divergence.
//
// Callbacks invoked while debugging (during step 3 of the above comment) might
// try to interact with the system, triggering thread events and attempting to
// replay behaviors that never occurred while recording.
//
// To allow these callbacks the freedom to operate without bringing down the
// entire replay, the DivergeFromRecording API is provided; see RecordReplay.h
// After this is called, some thread events will happen as if events were
// passed through, but other events that require interacting with the system
// will trigger an unhandled divergence from the recording via
// EnsureNotDivergedFromRecording, causing the process to rewind to the most
// recent snapshot. The debugger will recognize this rewind and play back in a
// way that restores the state when DivergeFromRecording() was called, but
// without performing the later operation that triggered the rewind.
///////////////////////////////////////////////////////////////////////////////

// Initialize state needed for rewinding.
void InitializeRewindState();

// Set whether this process should record any of its snapshots.
void SetRecordSnapshots(bool aAllowed);

// Mark a snapshot point. Non-temporary snapshots always occur at the same
// point of execution. The rewind mechanism is not required to actually record
// this snapshot.
void TakeSnapshot(bool aFinal, bool aTemporary = false);

// Return whether we are rewinding and the last snapshot was before the point
// where we are trying to rewind to.
bool LastSnapshotIsInterim();

// Invoke a callback on the main thread, and pause it until ResumeExecution or
// RestoreSnapshotAndResume are called. When the main thread is not paused,
// this must be called on the main thread itself. When the main thread is
// already paused, this may be called from any thread.
void PauseMainThreadAndInvokeCallback(const std::function<void()>& aCallback);

// Return whether the main thread should be paused. This does not necessarily
// mean it is paused, but it will pause at the earliest opportunity.
bool MainThreadShouldPause();

// If necessary, pause the current main thread and service any callbacks until
// the thread no longer needs to pause.
void MaybePauseMainThread();

// Return whether any snapshots have been taken.
bool HasTakenSnapshot();

// Get the ID of the most recent recorded snapshot. The diff between this and
// the following recorded snapshot has not been computed yet.
size_t GetActiveRecordedSnapshot();

// Get the ID of the last snapshot which was recorded and had its diff versus
// the following recorded snapshot computed.
size_t GetLastRecordedDiffSnapshot();

// Whether the last snapshot is a temporary one.
bool HasTemporarySnapshot();

// Make sure that execution has not diverged from the recording after a call to
// TakeSnapshotAndDivergeFromRecording, by rewinding to that snapshot if so.
void EnsureNotDivergedFromRecording();

} // namespace recordreplay
} // namespace mozilla

#endif // mozilla_toolkit_recordreplay_ProcessRewind_h
