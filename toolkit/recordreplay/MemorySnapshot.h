/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_toolkit_recordreplay_MemorySnapshot_h
#define mozilla_toolkit_recordreplay_MemorySnapshot_h

#include "mozilla/Types.h"
#include "ProcessRecordReplay.h"

namespace mozilla {
namespace recordreplay {

// Memory Snapshots Overview.
//
// As described in ProcessRewind.h, some subset of the snapshots which are
// reached during execution are recorded, so that their state can be restored
// later. Memory snapshots are used to save and restore the contents of all
// heap memory: everything except thread stacks (see ThreadSnapshot.h for
// saving and restoring these) and untracked memory (which is not saved or
// restored, see ProcessRecordReplay.h).
//
// Each memory snapshot is a diff of the heap memory contents compared to the
// next one. See MemorySnapshot.cpp for how diffs are represented and computed.
//
// Rewinding must restore the exact contents of heap memory that existed when
// the target snapshot was reached. Because of this, memory that is allocated
// at a point when a snapshot is taken will never actually be returned to the
// system. We instead keep a set of free blocks that are unused at the current
// point of execution and are available to satisfy new allocations.

// Make sure that a block of memory in a fixed allocation is already allocated.
void CheckFixedMemory(void* aAddress, size_t aSize);

// After marking a block of memory in a fixed allocation as non-writable,
// restore writability to any dirty pages in the range.
void RestoreWritableFixedMemory(void* aAddress, size_t aSize);

// Allocate memory, trying to use a specific address if provided but only if
// it is free.
void* AllocateMemoryTryAddress(void* aAddress, size_t aSize, AllocatedMemoryKind aKind);

// Note a range of memory that was just allocated from the system, and the
// kind of memory allocation that was performed.
void RegisterAllocatedMemory(void* aBaseAddress, size_t aSize, AllocatedMemoryKind aKind);

// Return whether system threads should be suspended and unable to run.
bool SystemThreadsShouldBeSuspended();

// Make sure we know about the current thread, which was created by the system
// and does not participate in the recording.
void NoteCurrentSystemThread();

// Return whether an address belongs to the stack of a known system thread.
bool IsSystemThreadStackAddress(void* aAddress);

// Initialize the memory snapshots system.
void InitializeMemorySnapshots();

// Take the first heap memory snapshot. The ID of this snapshot is zero.
void TakeFirstMemorySnapshot();

// Take a differential heap memory snapshot compared to the last one. The ID of
// this snapshot is that of the active recorded snapshot.
void TakeDiffMemorySnapshot();

// Restore all heap memory to its state when the active recorded snapshot
// (GetActiveRecordedSnapshot) was reached.
void RestoreMemoryToActiveSnapshot();

// Restore all heap memory to its state when the most recent recorded diff
// snapshot (GetLastRecordedDiffSnapshot) was reached. This requires that no
// tracked heap memory has been changed since the active recorded snapshot.
void RestoreMemoryToLastRecordedDiffSnapshot();

// Set whether to allow changes to tracked heap memory at this point. If such
// changes occur when they are not allowed then the process will crash.
void SetMemoryChangesAllowed(bool aAllowed);

// After a SEGV on the specified address, check if the violation occurred due
// to the memory having been write protected by the snapshot mechanism. This
// function returns whether the fault has been handled and execution may
// continue.
bool HandleDirtyMemoryFault(uint8_t* aAddress);

// For debugging, note a point where we hit an unrecoverable failure and try
// to make things easier for the debugger.
void UnrecoverableSnapshotFailure();

// After rewinding, mark all memory that has been allocated since the snapshot
// was taken as free.
void FixupFreeRegionsAfterRewind();

// When converting a recording process into a replaying process for rewinding,
// set or get the file handle to use for reading from the recording.
void PrepareMemoryForFirstRecordingRewind(FileHandle aReplayFd);
FileHandle GetReplayFileAfterRecordingRewind();

// Set whether to allow intentionally crashing in this process via the
// RecordReplayDirective method.
void SetAllowIntentionalCrashes(bool aAllowed);

// When WANT_COUNTDOWN_THREAD is defined (see MemorySnapshot.cpp), set a count
// that, after a thread consumes it, causes the thread to busy-wait. This is
// used for debugging and is a workaround for lldb often being unable to
// interrupt a running process.
void StartCountdown(size_t aCount);

// Per StartCountdown, set a countdown and remove it on destruction.
struct MOZ_RAII AutoCountdown
{
  explicit AutoCountdown(size_t aCount);
  ~AutoCountdown();
};

// Initialize the thread consuming the countdown.
void InitializeCountdownThread();

} // namespace recordreplay
} // namespace mozilla

#endif // mozilla_toolkit_recordreplay_MemorySnapshot_h
