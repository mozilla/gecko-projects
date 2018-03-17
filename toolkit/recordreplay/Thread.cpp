/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "Thread.h"

#include "mozilla/Atomics.h"
#include "mozilla/StaticMutex.h"
#include "mozilla/ThreadLocal.h"
#include "ChunkAllocator.h"
#include "MemorySnapshot.h"
#include "ProcessRewind.h"
#include "SpinLock.h"
#include "ThreadSnapshot.h"

namespace mozilla {
namespace recordreplay {

///////////////////////////////////////////////////////////////////////////////
// Thread Organization
///////////////////////////////////////////////////////////////////////////////

static MOZ_THREAD_LOCAL(Thread*) gTlsThreadKey;

/* static */ Monitor* Thread::gMonitor;

/* static */ Thread*
Thread::Current()
{
  MOZ_ASSERT(IsRecordingOrReplaying());
  Thread* thread = gTlsThreadKey.get();
  if (!thread) {
    NoteCurrentSystemThread();
  }
  return thread;
}

/* static */ bool
Thread::CurrentIsMainThread()
{
  Thread* thread = Current();
  return thread->IsMainThread();
}

void
Thread::BindToCurrent()
{
  MOZ_ASSERT(!mStackBase);
  gTlsThreadKey.set(this);

  size_t size;
  uint8_t* base;
#if defined(XP_MACOSX)
  mNativeId = pthread_self();
  size = pthread_get_stacksize_np(mNativeId);
  base = (uint8_t*)pthread_get_stackaddr_np(mNativeId) - size;
#elif defined(WIN32)
  GetAllocatedRegionInfo(&size, &base, &size);
  MOZ_CRASH();
#else
#error "Unknown platform"
#endif

  // Lock if we will be notifying later on. We don't do this for the main
  // thread because we haven't initialized enough state yet that we can use
  // a monitor.
  Maybe<MonitorAutoLock> lock;
  if (mId != MainThreadId) {
    lock.emplace(*gMonitor);
  }

  mStackBase = base;
  mStackSize = size;

  // Notify WaitUntilInitialized if it is waiting for this thread to start.
  if (mId != MainThreadId) {
    gMonitor->NotifyAll();
  }
}

// All threads, indexed by the thread ID.
static Thread* gThreads;

/* static */ Thread*
Thread::GetById(size_t aId)
{
  MOZ_ASSERT(aId);
  MOZ_ASSERT(aId <= MaxThreadId);
  return &gThreads[aId];
}

/* static */ Thread*
Thread::GetByNativeId(NativeThreadId aNativeId)
{
  for (size_t id = MainThreadId; id <= MaxRecordedThreadId; id++) {
    Thread* thread = GetById(id);
    if (thread->mNativeId == aNativeId) {
      return thread;
    }
  }
  return nullptr;
}

/* static */ Thread*
Thread::GetByStackPointer(void* aSp)
{
  if (!gThreads) {
    return nullptr;
  }
  for (size_t i = MainThreadId; i <= MaxThreadId; i++) {
    Thread* thread = &gThreads[i];
    if (MemoryContains(thread->mStackBase, thread->mStackSize, aSp)) {
      return thread;
    }
  }
  return nullptr;
}

/* static */ void
Thread::InitializeThreads()
{
  gThreads = new Thread[MaxThreadId + 1];
  for (size_t i = MainThreadId; i <= MaxThreadId; i++) {
    Thread* thread = &gThreads[i];
    PodZero(thread);
    new(thread) Thread();

    thread->mId = i;

    if (i <= MaxRecordedThreadId) {
      thread->mEvents = gRecordingFile->OpenStream(StreamName::Event, i);
      thread->mAsserts = gRecordingFile->OpenStream(StreamName::Assert, i);
    }

    DirectCreatePipe(&thread->mNotifyfd, &thread->mIdlefd);
  }

  if (!gTlsThreadKey.init()) {
    MOZ_CRASH();
  }
}

/* static */ void
Thread::WaitUntilInitialized(Thread* aThread)
{
  MonitorAutoLock lock(*gMonitor);
  while (!aThread->mStackBase) {
    gMonitor->Wait();
  }
}

/* static */ void
Thread::ThreadMain(void* aArgument)
{
  MOZ_ASSERT(IsRecordingOrReplaying());

  Thread* thread = (Thread*) aArgument;
  MOZ_ASSERT(thread->mId > MainThreadId);

  thread->BindToCurrent();

  while (true) {
    // Wait until this thread has been given a start routine.
    while (true) {
      {
        MonitorAutoLock lock(*gMonitor);
        if (thread->mStart) {
          break;
        }
      }
      Wait();
    }

    {
      Maybe<AutoPassThroughThreadEvents> pt;
      if (!thread->IsRecordedThread())
        pt.emplace();
      thread->mStart(thread->mStartArg);
    }

    MonitorAutoLock lock(*gMonitor);

    // Clear the start routine to indicate to other threads that this one has
    // finished executing.
    thread->mStart = nullptr;
    thread->mStartArg = nullptr;

    // Notify any other thread waiting for this to finish in JoinThread.
    gMonitor->NotifyAll();
  }

  Unreachable();
}

static void
SpawnCallEventHelperThreads();

/* static */ void
Thread::SpawnAllThreads()
{
  MOZ_ASSERT(AreThreadEventsPassedThrough());

  InitializeThreadSnapshots(MaxRecordedThreadId + 1);

  gMonitor = new Monitor();

  // All Threads are spawned up front. This allows threads to be scanned
  // (e.g. in ReplayUnlock) without worrying about racing with other threads
  // being spawned.
  for (size_t i = MainThreadId + 1; i <= MaxRecordedThreadId; i++) {
    SpawnThread(GetById(i));
  }

  SpawnCallEventHelperThreads();
}

// The number of non-recorded threads that have been spawned.
static Atomic<size_t, SequentiallyConsistent, Behavior::DontPreserve> gNumNonRecordedThreads;

/* static */ Thread*
Thread::SpawnNonRecordedThread(Callback aStart, void* aArgument)
{
  if (gInitializationFailureMessage) {
    DirectSpawnThread(aStart, aArgument);
    return nullptr;
  }

  size_t id = MaxRecordedThreadId + ++gNumNonRecordedThreads;
  MOZ_RELEASE_ASSERT(id <= MaxThreadId);

  Thread* thread = GetById(id);
  thread->mStart = aStart;
  thread->mStartArg = aArgument;

  SpawnThread(thread);
  return thread;
}

/* static */ void
Thread::SpawnThread(Thread* aThread)
{
  DirectSpawnThread(ThreadMain, aThread);
  WaitUntilInitialized(aThread);
}

/* static */ NativeThreadId
Thread::StartThread(Callback aStart, void* aArgument, bool aNeedsJoin)
{
  MOZ_ASSERT(IsRecordingOrReplaying());
  MOZ_ASSERT(!AreThreadEventsPassedThrough());
  MOZ_ASSERT(!AreThreadEventsDisallowed());

  EnsureNotDivergedFromRecording();
  Thread* thread = Thread::Current();

  RecordReplayAssert("StartThread");

  MonitorAutoLock lock(*gMonitor);

  size_t id = 0;
  if (IsRecording()) {
    // Look for an idle thread.
    for (id = MainThreadId + 1; id <= MaxRecordedThreadId; id++) {
      Thread* targetThread = Thread::GetById(id);
      if (!targetThread->mStart && !targetThread->mNeedsJoin) {
        break;
      }
    }
    if (id >= MaxRecordedThreadId) {
      child::ReportFatalError("Too many threads");
    }
    MOZ_RELEASE_ASSERT(id <= MaxRecordedThreadId);
  }
  thread->Events().RecordOrReplayThreadEvent(ThreadEvent::CreateThread);
  thread->Events().RecordOrReplayScalar(&id);

  Thread* targetThread = GetById(id);

  // Block until the thread is ready for a new start routine.
  while (targetThread->mStart) {
    MOZ_RELEASE_ASSERT(IsReplaying());
    gMonitor->Wait();
  }

  targetThread->mStart = aStart;
  targetThread->mStartArg = aArgument;
  targetThread->mNeedsJoin = aNeedsJoin;

  // Notify the thread in case it is waiting for a start routine under
  // ThreadMain.
  Notify(id);

  return targetThread->mNativeId;
}

void
Thread::Join()
{
  MOZ_ASSERT(!AreThreadEventsPassedThrough());

  EnsureNotDivergedFromRecording();

  while (true) {
    MonitorAutoLock lock(*gMonitor);
    if (!mStart) {
      MOZ_RELEASE_ASSERT(mNeedsJoin);
      mNeedsJoin = false;
      break;
    }
    gMonitor->Wait();
  }
}

///////////////////////////////////////////////////////////////////////////////
// Thread Buffers
///////////////////////////////////////////////////////////////////////////////

char*
Thread::TakeBuffer(size_t aSize)
{
  MOZ_ASSERT(mBuffer != (char*) 0x1);
  if (aSize > mBufferCapacity) {
    mBufferCapacity = aSize;
    mBuffer = (char*) realloc(mBuffer, aSize);
  }
  char* buf = mBuffer;

  // Poison the buffer in case this thread tries to use it again reentrantly.
  mBuffer = (char*) 0x1;

  return buf;
}

void
Thread::RestoreBuffer(char* aBuf)
{
  MOZ_ASSERT(mBuffer == (char*) 0x1);
  mBuffer = aBuf;
}

///////////////////////////////////////////////////////////////////////////////
// Thread Public API Accessors
///////////////////////////////////////////////////////////////////////////////

extern "C" {

MOZ_EXPORT void
RecordReplayInterface_InternalBeginPassThroughThreadEvents()
{
  MOZ_ASSERT(IsRecordingOrReplaying());
  if (!gInitializationFailureMessage) {
    Thread::Current()->SetPassThrough(true);
  }
}

MOZ_EXPORT void
RecordReplayInterface_InternalEndPassThroughThreadEvents()
{
  MOZ_ASSERT(IsRecordingOrReplaying());
  if (!gInitializationFailureMessage) {
    Thread::Current()->SetPassThrough(false);
  }
}

MOZ_EXPORT bool
RecordReplayInterface_InternalAreThreadEventsPassedThrough()
{
  MOZ_ASSERT(IsRecordingOrReplaying());
  Thread* thread = Thread::Current();
  return !thread || thread->PassThroughEvents();
}

MOZ_EXPORT void
RecordReplayInterface_InternalBeginDisallowThreadEvents()
{
  MOZ_ASSERT(IsRecordingOrReplaying());
  Thread::Current()->BeginDisallowEvents();
}

MOZ_EXPORT void
RecordReplayInterface_InternalEndDisallowThreadEvents()
{
  MOZ_ASSERT(IsRecordingOrReplaying());
  Thread::Current()->EndDisallowEvents();
}

MOZ_EXPORT bool
RecordReplayInterface_InternalAreThreadEventsDisallowed()
{
  MOZ_ASSERT(IsRecordingOrReplaying());
  Thread* thread = Thread::Current();
  return thread && thread->AreEventsDisallowed();
}

MOZ_EXPORT void
RecordReplayInterface_InternalBeginCaptureEventStacks()
{
  MOZ_ASSERT(IsRecordingOrReplaying());
  Thread::Current()->BeginCaptureEventStacks();
}

MOZ_EXPORT void
RecordReplayInterface_InternalEndCaptureEventStacks()
{
  MOZ_ASSERT(IsRecordingOrReplaying());
  Thread::Current()->EndCaptureEventStacks();
}

} // extern "C"

///////////////////////////////////////////////////////////////////////////////
// Thread Coordination
///////////////////////////////////////////////////////////////////////////////

// Whether all threads should attempt to idle.
static Atomic<bool, SequentiallyConsistent, Behavior::DontPreserve> gThreadsShouldIdle;

// Whether all threads are considered to be idle.
static bool gThreadsAreIdle;

// The number of call events which are currently executing and permitted to
// write to tracked memory, including from non-recorded call event helper
// threads.
static Atomic<size_t, SequentiallyConsistent, Behavior::DontPreserve> gNumActiveCallEvents;

static void
AddActiveCallEvent()
{
  ++gNumActiveCallEvents;
}

static void
ReleaseActiveCallEvent()
{
  // The main thread may be blocked under WaitForIdleThreads if there are
  // active call events.
  if (--gNumActiveCallEvents == 0 && gThreadsShouldIdle) {
    Thread::Notify(MainThreadId);
  }
}

/* static */ void
Thread::WaitForIdleThreads()
{
  MOZ_RELEASE_ASSERT(CurrentIsMainThread());

  MOZ_RELEASE_ASSERT(!gThreadsShouldIdle);
  MOZ_RELEASE_ASSERT(!gThreadsAreIdle);
  gThreadsShouldIdle = true;

  MonitorAutoLock lock(*gMonitor);
  for (size_t i = MainThreadId + 1; i <= MaxRecordedThreadId; i++) {
    GetById(i)->mUnrecordedWaitNotified = false;
  }
  while (true) {
    bool done = !gNumActiveCallEvents;
    for (size_t i = MainThreadId + 1; i <= MaxRecordedThreadId; i++) {
      Thread* thread = GetById(i);
      if (!thread->mIdle) {
        done = false;
        if (thread->mUnrecordedWaitCallback && !thread->mUnrecordedWaitNotified) {
          // Set this flag before releasing the idle lock. Otherwise it's
          // possible the thread could call NotifyUnrecordedWait while we
          // aren't holding the lock, and we would set the flag afterwards
          // without first invoking the callback.
          thread->mUnrecordedWaitNotified = true;

          // Release the idle lock here to avoid any risk of deadlock.
          {
            MonitorAutoUnlock unlock(*gMonitor);
            AutoPassThroughThreadEvents pt;
            thread->mUnrecordedWaitCallback();
          }

          // Releasing the global lock means that we need to start over
          // checking whether there are any idle threads. By marking this
          // thread as having been notified we have made progress, however.
          done = true;
          i = MainThreadId;
        }
      }
    }
    if (done) {
      break;
    }
    MonitorAutoUnlock unlock(*gMonitor);
    WaitNoIdle();
  }

  gThreadsAreIdle = true;
}

/* static */ void
Thread::ResumeIdleThreads()
{
  MOZ_RELEASE_ASSERT(CurrentIsMainThread());

  {
    MonitorAutoLock lock(*gMonitor);

    MOZ_RELEASE_ASSERT(gThreadsAreIdle);
    gThreadsAreIdle = false;

    MOZ_RELEASE_ASSERT(gThreadsShouldIdle);
    gThreadsShouldIdle = false;

    // Helper threads might be waiting under EndOffThreadCallEvent.
    gMonitor->NotifyAll();
  }

  for (size_t i = MainThreadId + 1; i <= MaxRecordedThreadId; i++) {
    Notify(i);
  }
}

void
Thread::NotifyUnrecordedWait(const std::function<void()>& aCallback)
{
  MonitorAutoLock lock(*gMonitor);
  if (mUnrecordedWaitCallback) {
    // Per the documentation for NotifyUnrecordedWait, we need to call the
    // routine after a notify, even if the routine has been called already
    // since the main thread started to wait for idle replay threads.
    mUnrecordedWaitNotified = false;
  } else {
    MOZ_RELEASE_ASSERT(!mUnrecordedWaitNotified);
  }

  mUnrecordedWaitCallback = aCallback;

  // The main thread might be able to make progress now by calling the routine
  // if it is waiting for idle replay threads.
  if (gThreadsShouldIdle) {
    Notify(MainThreadId);
  }
}

/* static */ void
Thread::MaybeWaitForSnapshot()
{
  MonitorAutoLock lock(*gMonitor);
  while (gThreadsShouldIdle) {
    MonitorAutoUnlock unlock(*gMonitor);
    Wait();
  }
}

extern "C" {

MOZ_EXPORT void
RecordReplayInterface_NotifyUnrecordedWait(const std::function<void()>& aCallback)
{
  Thread::Current()->NotifyUnrecordedWait(aCallback);
}

MOZ_EXPORT void
RecordReplayInterface_MaybeWaitForSnapshot()
{
  Thread::MaybeWaitForSnapshot();
}

} // extern "C"

/* static */ void
Thread::WaitNoIdle()
{
  Thread* thread = Current();
  uint8_t data = 0;
  size_t read = DirectRead(thread->mIdlefd, &data, 1);
  MOZ_RELEASE_ASSERT(read == 1);
}

/* static */ void
Thread::Wait()
{
  Thread* thread = Current();
  MOZ_ASSERT(!thread->mIdle);
  MOZ_ASSERT(thread->IsRecordedThread() && !thread->PassThroughEvents());

  if (thread->IsMainThread()) {
    WaitNoIdle();
    return;
  }

  // The state saved for a thread needs to match up with the most recent
  // point at which it became idle, so that when the main thread saves the
  // stacks from all threads it saves those stacks at the right point.
  // SaveThreadState might trigger thread events, so make sure they are
  // passed through.
  thread->SetPassThrough(true);
  int stackSeparator = 0;
  if (!SaveThreadState(thread->Id(), &stackSeparator)) {
    // We just restored a snapshot, fixup any weak pointers and notify the main
    // thread since it is waiting for all threads to restore their stacks.
    for (const void* ptr : thread->mPendingWeakPointerFixups) {
      thread->SetPassThrough(false);
      FixupOffThreadWeakPointerAfterRecordingRewind(ptr);
      thread->SetPassThrough(true);
    }
    thread->mPendingWeakPointerFixups.clear();
    Notify(MainThreadId);
  }

  thread->mIdle = true;
  if (gThreadsShouldIdle) {
    // Notify the main thread that we just became idle.
    Notify(MainThreadId);
  }

  do {
    // Do the actual waiting for another thread to notify this one.
    WaitNoIdle();

    // Rewind this thread if the main thread told us to do so. The main
    // thread is responsible for rewinding its own stack.
    if (ShouldRestoreThreadStack(thread->Id())) {
      RestoreThreadStack(thread->Id());
      Unreachable();
    }
  } while (gThreadsShouldIdle);

  thread->mIdle = false;
  thread->SetPassThrough(false);
}

/* static */ void
Thread::WaitForever()
{
  if (CurrentIsMainThread()) {
    TakeSnapshot(/* aFinal = */ true);
  }

  while (true) {
    Wait();
  }
  Unreachable();
}

/* static */ void
Thread::WaitForeverNoIdle()
{
  FileHandle writeFd, readFd;
  DirectCreatePipe(&writeFd, &readFd);
  while (true) {
    uint8_t data;
    DirectRead(readFd, &data, 1);
  }
}

/* static */ void
Thread::Notify(size_t aId)
{
  uint8_t data = 0;
  DirectWrite(GetById(aId)->mNotifyfd, &data, 1);
}

/* static */ void
Thread::NotifyThreadsWaitingForLock(Lock* aLock)
{
  for (size_t i = MainThreadId; i <= MaxThreadId; i++) {
    if (GetById(i)->mWaitLock == aLock) {
      Notify(i);
    }
  }
}

/* static */ void
Thread::WaitForCvar(void* aCvar, const std::function<void()>& aReleaseLock)
{
  if (IsRecording()) {
    Thread* thread = Current();
    MOZ_ASSERT(!thread->mWaitCvar);
    thread->mWaitCvar = aCvar;
    aReleaseLock();

    while (IsRecording() && thread->mWaitCvar) {
      if (thread->PassThroughEvents()) {
        WaitNoIdle();
      } else {
        Wait();
      }
    }
  } else {
    aReleaseLock();
  }
}

/* static */ bool
Thread::WaitForCvarUntil(void* aCvar, const std::function<void()>& aReleaseLock,
                         const std::function<bool()>& aCallback)
{
  RecordReplayAssert("WaitForCvarUntil");

  Thread* thread = Current();
  thread->Events().RecordOrReplayThreadEvent(ThreadEvent::WaitForCvarUntil);

  bool notified = true;
  if (IsRecording()) {
    MOZ_ASSERT(!thread->mWaitCvar);
    thread->mWaitCvar = aCvar;
    aReleaseLock();

    while (thread->mWaitCvar) {
      if (gThreadsShouldIdle && !thread->PassThroughEvents()) {
        Wait();
        if (IsReplaying()) {
          break;
        }
      }
      if (aCallback()) {
        notified = false;
        thread->mWaitCvar = nullptr;
        break;
      }
      ThreadYield(); // Busy-wait :(
    }
  } else {
    aReleaseLock();
  }

  thread->Events().RecordOrReplayScalar(&notified);
  return notified;
}

/* static */ void
Thread::SignalCvar(void* aCvar, bool aBroadcast)
{
  if (IsRecording()) {
    for (size_t i = MainThreadId; i <= MaxThreadId; i++) {
      Thread* thread = GetById(i);
      if (thread->mWaitCvar == aCvar) {
        thread->mWaitCvar = nullptr;
        Notify(i);
        if (!aBroadcast) {
          return;
        }
      }
    }
  }
}

/* static */ bool
Thread::SaveAllThreads(size_t aSnapshot)
{
  MOZ_RELEASE_ASSERT(CurrentIsMainThread());

#ifdef WIN32
  MOZ_CRASH();
#endif

  AutoPassThroughThreadEvents pt; // setjmp may perform system calls.
  SetMemoryChangesAllowed(false);

  int stackSeparator = 0;
  if (!SaveThreadState(MainThreadId, &stackSeparator)) {
    // We just restored this state from a later point of execution.
    SetMemoryChangesAllowed(true);
    return false;
  }

  UntrackedFile file;
  file.Open(gSnapshotStackPrefix, aSnapshot, UntrackedFile::WRITE);

  UntrackedStream* stream = file.OpenStream(StreamName::Main, 0);

  for (size_t i = MainThreadId; i <= MaxRecordedThreadId; i++) {
    SaveThreadStack(*stream, i);
  }

  SetMemoryChangesAllowed(true);
  return true;
}

/* static */ void
Thread::RestoreAllThreads(size_t aSnapshot)
{
  MOZ_RELEASE_ASSERT(CurrentIsMainThread());

  BeginPassThroughThreadEvents();
  SetMemoryChangesAllowed(false);

  UntrackedFile file;
  file.Open(gSnapshotStackPrefix, aSnapshot, UntrackedFile::READ);

  UntrackedStream* stream = file.OpenStream(StreamName::Main, 0);

  for (size_t i = MainThreadId; i <= MaxRecordedThreadId; i++) {
    RestoreStackForLoadingByThread(*stream, i);
  }

  file.Close();

  RestoreThreadStack(MainThreadId);
  MOZ_CRASH(); // RestoreThreadState does not return.
}

/* static */ void
Thread::WaitForIdleThreadsToRestoreTheirStacks()
{
  // Wait for all other threads to restore their stack before resuming execution.
  while (true) {
    bool done = true;
    for (size_t i = MainThreadId + 1; i <= MaxRecordedThreadId; i++) {
      if (ShouldRestoreThreadStack(i)) {
        Notify(i);
        done = false;
      }
    }
    if (done) {
      break;
    }
    WaitNoIdle();
  }
}

///////////////////////////////////////////////////////////////////////////////
// Off Thread Call Events
///////////////////////////////////////////////////////////////////////////////

// While recording, threads may call APIs that block indefinitely. We want the
// main thread to be able to take snapshots and rewind the process while these
// threads are blocked, so need mechanisms to allow the thread to enter its
// special idle state. Locks and condition variables are emulated so that they
// are considered idle while blocked, but other APIs need different handling.
//
// Call event helper threads are non-recorded threads that can take over work
// from a recorded thread, calling the blocking API themselves so that the
// recorded thread can enter its idle state.
//
// The main difficulty here is ensuring the helper thread doesn't interfere
// with the snapshot/rewind mechanism by writing to memory it shouldn't. The
// blocking API can return at any time, leading to heap writes through
// pointers that are probably invalid after rewinding.
//
// To avoid these problems, we maintain a core invariant. Calls to
// Add/ReleaseActiveCallEvent define an execution region where call event
// helper threads are allowed to write to tracked memory. Whenever there is
// an active call event the main thread will not consider recorded threads to
// be idle.

static const size_t CallEventHelperThreadCount = 12;

struct CallEventHelperThreadInfo
{
  size_t mHelperThreadId;
  std::function<void()> mCallback;
  Atomic<size_t, SequentiallyConsistent, Behavior::DontPreserve> mRequestorThreadId;
};

static CallEventHelperThreadInfo* gCallEventHelperThreads;

static void
CallEventHelperThreadMain(void* aArgument)
{
  CallEventHelperThreadInfo& info = gCallEventHelperThreads[(size_t) aArgument];
  info.mHelperThreadId = Thread::Current()->Id();

  while (true) {
    while (!info.mRequestorThreadId) {
      Thread::WaitNoIdle();
    }

    info.mCallback();

    size_t id = info.mRequestorThreadId;
    info.mCallback = std::function<void()>();
    info.mRequestorThreadId = 0;

    Thread::Notify(id);

    // Release the active call event added by ExecuteCallEventOffThread.
    ReleaseActiveCallEvent();
  }
}

static void
SpawnCallEventHelperThreads()
{
  gCallEventHelperThreads = new CallEventHelperThreadInfo[CallEventHelperThreadCount];
  PodZero(gCallEventHelperThreads, CallEventHelperThreadCount);

  for (size_t i = 0; i < CallEventHelperThreadCount; i++) {
    Thread::SpawnNonRecordedThread(CallEventHelperThreadMain, (void*) i);
  }
}

/* static */ void
Thread::ExecuteCallEventOffThread(const std::function<void()>& aCallback, bool* aCompleted)
{
  MOZ_RELEASE_ASSERT(IsRecording());
  MOZ_RELEASE_ASSERT(!gThreadsAreIdle);

  // Allow call event helper threads to write to tracked memory. This will be
  // released by the helper thread after the callback finishes.
  AddActiveCallEvent();

  Thread* thread = Current();

  CallEventHelperThreadInfo* info = nullptr;
  {
    MonitorAutoLock lock(*gMonitor);

    for (size_t i = 0; i < CallEventHelperThreadCount; i++) {
      if (!gCallEventHelperThreads[i].mRequestorThreadId) {
        info = &gCallEventHelperThreads[i];
        break;
      }
    }

    MOZ_RELEASE_ASSERT(info);

    info->mCallback = aCallback;
    info->mRequestorThreadId = thread->Id();
  }

  Notify(info->mHelperThreadId);

  thread->SetPassThrough(false);
  while (IsRecording() && !*aCompleted) {
    Wait();
  }
  thread->SetPassThrough(true);
}

// Information about an output buffer in use by an off thread call event.
struct OffThreadCallEventBuffer
{
  // Address of the original output buffer supplied by the caller.
  void* mOriginal;

  // Copy of the output buffer located in untracked memory.
  void* mUntracked;

  // Size of the buffer.
  size_t mSize;

  OffThreadCallEventBuffer(void* aOriginal, void* aUntracked, size_t aSize)
    : mOriginal(aOriginal), mUntracked(aUntracked), mSize(aSize)
  {}
};

struct OffThreadCallEventInfo {
  InfallibleVector<OffThreadCallEventBuffer, 4, AllocPolicy<UntrackedMemoryKind::Generic>> mBuffers;
  SpinLock mLock;
};
static OffThreadCallEventInfo* gOffThreadCallEventInfo;

/* static */ void
Thread::InitializeOffThreadCallEvents()
{
  // Off thread call event data is allocated in untracked memory to avoid
  // deadlocks when the dirty memory handler accesses it.
  gOffThreadCallEventInfo = (OffThreadCallEventInfo*)
    AllocateMemory(sizeof(OffThreadCallEventInfo), UntrackedMemoryKind::Generic);
}

/* static */ void
Thread::NoteOffThreadCallEventBuffer(void* aBuf, size_t aSize, bool aFirst)
{
  // This method is called twice for each output buffer used by an off thread
  // call event. During the first call the buffer is replaced with a fresh
  // region of untracked memory. During the second call the contents of the
  // untracked region are copied to the target output buffer.
  MOZ_RELEASE_ASSERT(gNumActiveCallEvents);

  if (!aSize) {
    return;
  }

  void* untracked = nullptr;
  if (aFirst) {
    untracked = AllocateMemory(aSize, UntrackedMemoryKind::Generic);
    memcpy(untracked, aBuf, aSize);
  }

  Maybe<AutoSpinLock> lock;
  lock.emplace(gOffThreadCallEventInfo->mLock);

  if (aFirst) {
    for (OffThreadCallEventBuffer& info : gOffThreadCallEventInfo->mBuffers) {
      MOZ_RELEASE_ASSERT(info.mOriginal != aBuf);
    }
    gOffThreadCallEventInfo->mBuffers.emplaceBack(aBuf, untracked, aSize);
  } else {
    for (OffThreadCallEventBuffer& info : gOffThreadCallEventInfo->mBuffers) {
      if (info.mOriginal == aBuf) {
        MOZ_RELEASE_ASSERT(aSize == info.mSize);
        OffThreadCallEventBuffer copy = info;
        gOffThreadCallEventInfo->mBuffers.erase(&info);
        lock.reset();

        memcpy(copy.mOriginal, copy.mUntracked, aSize);
        DeallocateMemory(copy.mUntracked, aSize, UntrackedMemoryKind::Generic);
        return;
      }
    }
    MOZ_CRASH();
  }
}

/* static */ void*
Thread::MaybeUntrackedOffThreadCallEventBuffer(void* aBuf)
{
  if (!gOffThreadCallEventInfo) {
    return aBuf;
  }
  AutoSpinLock lock(gOffThreadCallEventInfo->mLock);
  for (const OffThreadCallEventBuffer& info : gOffThreadCallEventInfo->mBuffers) {
    if (info.mOriginal == aBuf) {
      MOZ_RELEASE_ASSERT(!Thread::Current()->IsRecordedThread());
      return info.mUntracked;
    }
  }
  return aBuf;
}

/* static */ void
Thread::StartOffThreadCallEvent()
{
  // By releasing the call event the main thread will be allowed to take
  // snapshots or rewind to an earlier snapshot. The call event cannot touch
  MOZ_RELEASE_ASSERT(IsRecording());
  MOZ_RELEASE_ASSERT(!gThreadsAreIdle);
  ReleaseActiveCallEvent();
}

/* static */ void
Thread::EndOffThreadCallEvent()
{
  {
    MonitorAutoLock lock(*gMonitor);

    // Wait if recorded threads are supposed to be idle.
    while (gThreadsShouldIdle) {
      gMonitor->Wait();
    }

    AddActiveCallEvent();
  }

  // Stop execution in this thread if we are no longer recording.
  if (IsReplaying()) {
    ReleaseActiveCallEvent();
    WaitForeverNoIdle();
  }
}

} // namespace recordreplay
} // namespace mozilla
