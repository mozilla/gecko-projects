/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "Callback.h"

#include "ipc/ChildIPC.h"
#include "mozilla/Assertions.h"
#include "mozilla/RecordReplay.h"
#include "mozilla/StaticMutex.h"
#include "ProcessRewind.h"
#include "Thread.h"
#include "ValueIndex.h"

namespace mozilla {
namespace recordreplay {

static ValueIndex* gCallbackData;
static StaticMutexNotRecorded gCallbackMutex;

void
RegisterCallbackData(void* aData)
{
  MOZ_RELEASE_ASSERT(IsRecordingOrReplaying());
  MOZ_RELEASE_ASSERT(!AreThreadEventsPassedThrough());
  if (!aData) {
    return;
  }

  RecordReplayAssert("RegisterCallbackData");

  AutoOrderedAtomicAccess at;
  StaticMutexAutoLock lock(gCallbackMutex);
  if (!gCallbackData) {
    gCallbackData = new ValueIndex();
  }
  gCallbackData->Insert(aData);
}

void
BeginCallback(size_t aCallbackId, jmp_buf** aJump)
{
  MOZ_RELEASE_ASSERT(IsRecording());
  MOZ_RELEASE_ASSERT(!AreThreadEventsDisallowed());
  Thread* thread = Thread::Current();

  *aJump = thread->EventCallbackJump();
  thread->SetEventCallbackJump(nullptr);

  thread->SetPassThrough(false);

  thread->Events().RecordOrReplayThreadEvent(ThreadEvent::ExecuteCallback);
  thread->Events().WriteScalar(aCallbackId);
}

void
EndCallback(jmp_buf** aJump)
{
  MOZ_RELEASE_ASSERT(!AreThreadEventsPassedThrough());
  MOZ_RELEASE_ASSERT(!AreThreadEventsDisallowed());
  Thread* thread = Thread::Current();

  thread->SetEventCallbackJump(*aJump);
  thread->SetPassThrough(true);

  if (IsReplaying()) {
    longjmp(**aJump, 0);
    Unreachable();
  }
}

void
SaveOrRestoreCallbackData(void** aData)
{
  MOZ_RELEASE_ASSERT(IsRecordingOrReplaying());
  MOZ_RELEASE_ASSERT(!AreThreadEventsPassedThrough());
  MOZ_RELEASE_ASSERT(!AreThreadEventsDisallowed());
  MOZ_RELEASE_ASSERT(gCallbackData);

  Thread* thread = Thread::Current();

  RecordReplayAssert("RestoreCallbackData");

  thread->Events().RecordOrReplayThreadEvent(ThreadEvent::RestoreCallbackData);

  size_t index = 0;
  if (IsRecording() && *aData) {
    StaticMutexAutoLock lock(gCallbackMutex);
    index = gCallbackData->GetIndex(*aData);
  }
  thread->Events().RecordOrReplayScalar(&index);

  if (IsReplaying()) {
    *aData = const_cast<void*>(gCallbackData->GetValue(index));
  }
}

void
RemoveCallbackData(void* aData)
{
  MOZ_RELEASE_ASSERT(IsRecordingOrReplaying());

  StaticMutexAutoLock lock(gCallbackMutex);
  gCallbackData->Remove(aData);
}

void
PassThroughThreadEventsAllowCallbacks(const std::function<void()>& aFn)
{
  MOZ_RELEASE_ASSERT(!AreThreadEventsDisallowed());

  Thread* thread = Thread::Current();

  jmp_buf jump;
  thread->SetEventCallbackJump(&jump);

  thread->SetPassThrough(true);

  // We will longjmp to this point if we initially recorded, took a snapshot
  // while inside a Gecko callback, and then rewound to that snapshot. In this
  // case we will end up taking both the IsRecording() and IsReplaying()
  // branches below, and in the latter case will execute all remaining
  // callbacks which occurred while recording under aFn().
  (void) setjmp(jump);

  thread->SetPassThrough(false);

  if (IsRecording()) {
    thread->SetPassThrough(true);
    aFn();
    thread->SetPassThrough(false);
    thread->Events().RecordOrReplayThreadEvent(ThreadEvent::CallbacksFinished);
  }

  thread->SetEventCallbackJump(nullptr);

  // During replay, replay all callbacks that executed while recording until a
  // CallbackFinished event occurs.
  if (IsReplaying()) {
    while (true) {
      ThreadEvent ev = (ThreadEvent) thread->Events().ReadScalar();
      if (ev != ThreadEvent::ExecuteCallback) {
        if (ev != ThreadEvent::CallbacksFinished) {
          child::ReportFatalError("Unexpected event while replaying callback events");
        }
        break;
      }
      size_t id = thread->Events().ReadScalar();
      ReplayInvokeCallback(id);
    }
  }
}

} // namespace recordreplay
} // namespace mozilla
