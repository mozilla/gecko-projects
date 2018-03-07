/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_toolkit_recordreplay_Thread_h
#define mozilla_toolkit_recordreplay_Thread_h

#include "mozilla/Atomics.h"
#include "File.h"
#include "Lock.h"
#include "Monitor.h"

#ifdef XP_MACOSX
#include <pthread.h>
#endif

#include <setjmp.h>

namespace mozilla {
namespace recordreplay {

// Threads Overview.
//
// The main thread and each thread that is spawned when thread events are not
// passed through have their behavior recorded.
//
// While recording, each recorded thread has an associated Thread object which
// can be fetched with Thread::Current and stores the thread's ID, its file for
// storing events that occur in the thread, and some other thread local state.
// Otherwise, threads are spawned and destroyed as usual for the process.
//
// While rewinding, the same Thread structure exists for each recorded thread.
// Several additional changes are needed to facilitate rewinding and IPC:
//
// 1. All recorded threads are spawned early during the process' execution,
//    before any snapshot has been taken. These threads idle until the process
//    calls the system's thread creation API, and then they run with the start
//    routine the process provided. After the start routine finishes they idle
//    indefinitely, potentially running new start routines if their thread ID
//    is reused. This allows the process to rewind itself without needing to
//    spawn or destroy any threads.
//
// 2. Some additional number of threads are spawned for use by the IPC and
//    memory snapshot mechanisms. These have associated Thread
//    structures but are not recorded and always pass through thread events.
//
// 3. All recorded threads and must be able to enter a particular blocking
//    state, under Thread::Wait, when requested by the main thread calling
//    WaitForIdleThreads. For most recorded threads this happens when the
//    thread attempts to take a recorded lock and blocks in Lock::Wait.
//    The only exception is for JS helper threads, which never take recorded
//    locks. For these threads, NotifyUnrecordedWait and MaybeWaitForSnapshot
//    must be used to enter this state.
//
// 4. Once all recorded threads are idle, the main thread is able to record
//    memory snapshots and thread stacks for later rewinding. Additional
//    threads created for #2 above do not idle and do not have their state
//    included in snapshots, but they are designed to avoid interfering with
//    the main thread while it is taking or restoring a snapshot.

// The ID used by the process main thread.
static const size_t MainThreadId = 1;

// The maximum ID useable by recorded threads.
static const size_t MaxRecordedThreadId = 70;

// The maximum number of threads which are not recorded but need a Thread so
// that they can participate in e.g. Wait/Notify calls.
static const size_t MaxNumNonRecordedThreads = 24;

static const size_t MaxThreadId = MaxRecordedThreadId + MaxNumNonRecordedThreads;

// Information about the execution state of a thread.
class Thread
{
public:
  // Signature for the start function of a thread.
  typedef void (*Callback)(void*);

  // Number of recent assertions remembered.
  static const size_t NumRecentAsserts = 128;

private:
  // Monitor used to protect various thread information (see Thread.h) and to
  // wait on or signal progress for a thread.
  static Monitor* gMonitor;

  // Thread ID in the recording, fixed at creation.
  size_t mId;

  // Whether to pass events in the thread through without recording/replaying.
  // This is only used by the associated thread.
  bool mPassThroughEvents;

  // Whether to crash if we try to record/replay thread events. This is only
  // used by the associated thread.
  size_t mDisallowEvents;

  // Whether to capture stack information for events while recording. This is
  // only used by the associated thread.
  size_t mCaptureEventStacks;

  // If record/replay callbacks might execute, this is filled in. Jumping here
  // while replaying will process any remaining callbacks without invoking any
  // of the intervening system code. See Callback.h.
  jmp_buf* mEventCallbackJump;

  // Start routine and argument which the thread is currently executing. This
  // is cleared after the routine finishes and another start routine may be
  // assigned to the thread. This is protected by the thread monitor.
  Callback mStart;
  void* mStartArg;

  // ID used to refer to this thread outside of the current record/replay
  // system. This ID is tied to the start routine/argument and may change over
  // time as this thread is reused for different start routines.
  size_t mVirtualId;

  // Streams with events and assertions for the thread. These are only used by
  // the associated thread.
  Stream* mEvents;
  Stream* mAsserts;

  // Recent assertions that have been encountered, for debugging.
  char* mRecentAsserts[NumRecentAsserts];

  // Buffer for general use. This is only used by the associated thread.
  char* mBuffer;
  size_t mBufferCapacity;

  // Set the passthrough value for this thread in a way that the DLL entry
  // point CheckPassThroughTrampoline can check for it. This is defined in
  // ProcessRedirectWindows.cpp
#if defined(DEBUG) && defined(WIN32)
  static void SetPassThroughInArray(size_t aId, bool aPassThrough);
#endif

  // Stack boundary of the thread, protected by the thread monitor.
  uint8_t* mStackBase;
  size_t mStackSize;

  // File descriptor to block on when the thread is idle, fixed at creation.
  FileHandle mIdlefd;

  // File descriptor to notify to wake the thread up, fixed at creation.
  FileHandle mNotifyfd;

  // Whether the thread is waiting on idlefd.
  Atomic<bool, SequentiallyConsistent, Behavior::DontPreserve> mIdle;

  // Any lock/cvar which the thread is waiting on.
  Atomic<Lock*, SequentiallyConsistent, Behavior::DontPreserve> mWaitLock;
  Atomic<void*, SequentiallyConsistent, Behavior::DontPreserve> mWaitCvar;

  // Any callback which should be invoked so the thread can make progress,
  // and whether the callback has been invoked yet while the main thread is
  // waiting for threads to become idle. Protected by the thread monitor.
  std::function<void()> mUnrecordedWaitCallback;
  bool mUnrecordedWaitNotified;

  // Any weak pointers associated with this thread which need to be fixed up
  // due to the process just having rewound to a point when it was recording.
  InfallibleVector<const void*> mPendingWeakPointerFixups;

public:
///////////////////////////////////////////////////////////////////////////////
// Public Routines
///////////////////////////////////////////////////////////////////////////////

  // Accessors for some members that never change.
  size_t Id() { return mId; }
  size_t VirtualId() { return mVirtualId; }
  Stream& Events() { return *mEvents; }
  Stream& Asserts() { return *mAsserts; }
  uint8_t* StackBase() { return mStackBase; }
  size_t StackSize() { return mStackSize; }

  inline bool IsMainThread() { return mId == MainThreadId; }
  inline bool IsRecordedThread() { return mId <= MaxRecordedThreadId; }
  inline bool IsNonMainRecordedThread() { return IsRecordedThread() && !IsMainThread(); }

  // Access the flag for whether this thread is passing events through.
  void SetPassThrough(bool aPassThrough) {
    MOZ_RELEASE_ASSERT(mPassThroughEvents == !aPassThrough);
    mPassThroughEvents = aPassThrough;
#if defined(DEBUG) && defined(WIN32)
    SetPassThroughInArray(mId, aPassThrough);
#endif
  }
  bool PassThroughEvents() {
    return mPassThroughEvents;
  }

  // Access the buffer for setjmp/longjmps related to callback processing.
  void SetEventCallbackJump(jmp_buf* aJump) {
    MOZ_RELEASE_ASSERT(!!mEventCallbackJump != !!aJump);
    mEventCallbackJump = aJump;
  }
  jmp_buf* EventCallbackJump() {
    MOZ_RELEASE_ASSERT(mEventCallbackJump);
    return mEventCallbackJump;
  }

  // Access the counter for whether events are disallowed in this thread.
  void BeginDisallowEvents() {
    mDisallowEvents++;
  }
  void EndDisallowEvents() {
    MOZ_RELEASE_ASSERT(mDisallowEvents);
    mDisallowEvents--;
  }
  bool AreEventsDisallowed() {
    return mDisallowEvents != 0;
  }

  // Access the counter for whether event stacks are captured while recording.
  void BeginCaptureEventStacks() {
    mCaptureEventStacks++;
  }
  void EndCaptureEventStacks() {
    MOZ_RELEASE_ASSERT(mCaptureEventStacks);
    mCaptureEventStacks--;
  }
  bool ShouldCaptureEventStacks() {
    return mCaptureEventStacks != 0;
  }

  // Access the array of recent assertions in the thread.
  char*& RecentAssert(size_t i) {
    MOZ_ASSERT(i < NumRecentAsserts);
    return mRecentAsserts[i];
  }

  // Access a thread local buffer of a guaranteed size. The buffer must be
  // restored before it can be taken again.
  char* TakeBuffer(size_t aSize);
  void RestoreBuffer(char* aBuf);

  // The actual start routine at the root of all recorded threads, and of all
  // threads when replaying.
  static void ThreadMain(void* aArgument);

  // Bind this Thread to the current system thread, setting Thread::Current()
  // and some other basic state.
  void BindToCurrent();

  // Initialize thread state.
  static void InitializeThreads();

  // Get the current thread, or null if this is a system thread.
  static Thread* Current();

  // Helper to test if this is the process main thread.
  static bool CurrentIsMainThread();

  // Lookup a Thread by various methods.
  static Thread* GetById(size_t aId);
  static Thread* GetByVirtualId(size_t aId);
  static Thread* GetByStackPointer(void* aSp);

  // Spawn all non-main recorded threads used for recording/replaying.
  static void SpawnAllThreads();

  // Spawn the specified thread.
  static void SpawnThread(Thread* aThread);

  // Spawn a non-recorded thread with the specified start routine/argument.
  static Thread* SpawnNonRecordedThread(Callback aStart, void* aArgument);

  // Wait until a thread has initialized its stack and other state.
  static void WaitUntilInitialized(Thread* aThread);

  // Start an existing thread, for use when the process has called a thread
  // creation system API when events were not passed through. The return value
  // is the virtual ID of the result.
  static size_t StartThread(Callback aStart, void* aArgument);

  // Wait until a thread finishes executing its start routine.
  static void JoinThread(size_t aVirtualId);

  // Return whether a virtual id appears to be valid.
  static bool IsValidVirtualId(size_t aVirtualId);

  // Note a weak pointer that needs to be fixed on this thread after it
  // restores its stack during a recording rewind.
  void AddPendingWeakPointerFixup(const void* aPtr) {
    mPendingWeakPointerFixups.append(aPtr);
  }

///////////////////////////////////////////////////////////////////////////////
// Thread Coordination
///////////////////////////////////////////////////////////////////////////////

  // Basic API for threads to coordinate activity with each other, for use
  // during replay. Each Notify() on a thread ID will cause that thread to
  // return from one call to Wait(). Thus, if a thread Wait()'s and then
  // another thread Notify()'s its ID, the first thread will wake up afterward.
  // Similarly, if a thread Notify()'s another thread which is not waiting,
  // that second thread will return from its next Wait() without needing
  // another Notify().
  //
  // If the main thread has called WaitForIdleThreads, then calling
  // Wait() will put this thread in the desired idle state. WaitNoIdle() will
  // never cause the thread to enter the idle state, and should be used
  // carefully to avoid deadlocks with the main thread.
  static void Wait();
  static void WaitNoIdle();
  static void Notify(size_t aId);

  // Wait indefinitely, until the process is rewound.
  static void WaitForever();

  // Wait indefinitely, without allowing this thread to be rewound.
  static void WaitForeverNoIdle();

  // Set the lock which this thread is waiting for.
  void SetWaitLock(Lock* aLock) {
    MOZ_RELEASE_ASSERT(!!aLock == !mWaitLock);
    mWaitLock = aLock;
  }

  // Wake up all threads which are waiting for a lock.
  static void NotifyThreadsWaitingForLock(Lock* aLock);

  // Release the lock and block this thread until the given cvar is notified.
  static void WaitForCvar(void* aCvar, const std::function<void()>& aReleaseLock);

  // Release the lock and block this thread until the given cvar is notified or
  // aCallback returns true, returning whether the cvar was notified.
  static bool WaitForCvarUntil(void* aCvar, const std::function<void()>& aReleaseLock,
                               const std::function<bool()>& aCallback);

  // Wake up one or all threads waiting on a cvar.
  static void SignalCvar(void* aCvar, bool aBroadcast);

  // Perform initialization related to off thread call events.
  static void InitializeOffThreadCallEvents();

  // Synchronously execute a callback on another thread, allowing snapshots to
  // be taken/restored while doing so.
  static void ExecuteCallEventOffThread(const std::function<void()>& aCallback, bool* aCompleted);

  // Note a buffer used by an off thread call event. This is called twice for
  // each buffer, before and after the call itself. The first time an untracked
  // buffer is created for this one, and the second time the untracked buffer's
  // contents are copied to this one.
  static void NoteOffThreadCallEventBuffer(void* aBuf, size_t aSize, bool aFirst);

  // Get any untracked buffer associated with an off thread call event.
  static void* MaybeUntrackedOffThreadCallEventBuffer(void* aBuf);

  // Mark a region where an off thread call event is being performed. Between
  // these calls the current thread may not write to tracked memory.
  static void StartOffThreadCallEvent();
  static void EndOffThreadCallEvent();

  // See RecordReplay.h.
  void NotifyUnrecordedWait(const std::function<void()>& aCallback);
  static void MaybeWaitForSnapshot();

  // Wait for all other threads to enter the idle state necessary for recording
  // or restoring a snapshot. This may only be called on the main thread.
  static void WaitForIdleThreads();

  // After rewinding to an earlier snapshot, the main thread will call this to
  // ensure that each thread has woken up and restored its own stack contents.
  // The main thread does not itself write to the stacks of other threads.
  static void WaitForIdleThreadsToRestoreTheirStacks();

  // After WaitForIdleThreads(), the main thread will call this to allow
  // other threads to resume execution.
  static void ResumeIdleThreads();

  // When all other threads are idle, the main thread may call this to save its
  // own stack and the stacks of all other threads. The return value is true if
  // the stacks were just saved, or false if they were just restored due to a
  // rewind from a later point of execution.
  static bool SaveAllThreads(size_t aSnapshot);

  // Restore the saved stacks for a snapshot and rewind state to that point.
  // This function does not return.
  static void RestoreAllThreads(size_t aSnapshot);
};

// This uses a stack pointer instead of TLS to make sure events are passed
// through, for avoiding thorny reentrance issues.
class AutoEnsurePassThroughThreadEventsUseStackPointer
{
  Thread* mThread;
  bool mPassedThrough;

public:
  AutoEnsurePassThroughThreadEventsUseStackPointer()
    : mThread(Thread::GetByStackPointer(this))
    , mPassedThrough(!mThread || mThread->PassThroughEvents())
  {
    if (!mPassedThrough) {
      mThread->SetPassThrough(true);
    }
  }

  ~AutoEnsurePassThroughThreadEventsUseStackPointer()
  {
    if (!mPassedThrough) {
      mThread->SetPassThrough(false);
    }
  }
};

} // namespace recordreplay
} // namespace mozilla

#endif // mozilla_toolkit_recordreplay_Thread_h
