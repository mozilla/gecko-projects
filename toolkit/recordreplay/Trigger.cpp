/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "Trigger.h"

#include "mozilla/StaticMutex.h"
#include "mozilla/RecordReplay.h"
#include "InfallibleVector.h"
#include "ProcessRewind.h"
#include "Thread.h"
#include "ValueIndex.h"

namespace mozilla {
namespace recordreplay {

// Information about each trigger.
struct TriggerInfo
{
  std::function<void()> mCallback;
  size_t mRegisterCount;

  TriggerInfo() { PodZero(this); }

  explicit TriggerInfo(const std::function<void()>& aCallback)
    : mCallback(aCallback), mRegisterCount(1)
  {}
};

// All registered triggers.
static ValueIndex* gTriggers;

typedef std::unordered_map<void*, TriggerInfo> TriggerInfoMap;
static TriggerInfoMap* gTriggerInfoMap;

// Triggers which have been activated. This is protected by the global lock.
static StaticInfallibleVector<size_t> gActivatedTriggers;

static StaticMutexNotRecorded gTriggersMutex;

void
InitializeTriggers()
{
  gTriggers = new ValueIndex();
  gTriggerInfoMap = new TriggerInfoMap();
}

extern "C" {

MOZ_EXPORT void
RecordReplayInterface_RegisterTrigger(void* aObj, const std::function<void()>& aCallback)
{
  MOZ_ASSERT(IsRecordingOrReplaying());
  MOZ_RELEASE_ASSERT(Thread::CurrentIsMainThread());
  MOZ_RELEASE_ASSERT(!AreThreadEventsPassedThrough());
  MOZ_RELEASE_ASSERT(!AreThreadEventsDisallowed());
  MOZ_RELEASE_ASSERT(aObj);

  if (HasDivergedFromRecording()) {
    return;
  }
  Thread* thread = Thread::Current();

  RecordReplayAssert("RegisterTrigger");
  thread->Events().RecordOrReplayThreadEvent(ThreadEvent::RegisterTrigger);

  StaticMutexAutoLock lock(gTriggersMutex);

  TriggerInfoMap::iterator iter = gTriggerInfoMap->find(aObj);
  if (iter != gTriggerInfoMap->end()) {
    iter->second.mCallback = aCallback;
    iter->second.mRegisterCount++;
  } else {
    gTriggers->Insert(aObj);
    gTriggerInfoMap->insert(TriggerInfoMap::value_type(aObj, TriggerInfo(aCallback)));
  }
}

MOZ_EXPORT void
RecordReplayInterface_UnregisterTrigger(void* aObj)
{
  MOZ_ASSERT(IsRecordingOrReplaying());
  MOZ_RELEASE_ASSERT(Thread::CurrentIsMainThread());
  MOZ_RELEASE_ASSERT(!AreThreadEventsPassedThrough());

  StaticMutexAutoLock lock(gTriggersMutex);

  TriggerInfoMap::iterator iter = gTriggerInfoMap->find(aObj);
  MOZ_RELEASE_ASSERT(iter != gTriggerInfoMap->end());
  if (--iter->second.mRegisterCount == 0) {
    gTriggerInfoMap->erase(iter);
    gTriggers->Remove(aObj);
  }
}

MOZ_EXPORT void
RecordReplayInterface_ActivateTrigger(void* aObj)
{
  if (!IsRecording()) {
    return;
  }

  StaticMutexAutoLock lock(gTriggersMutex);

  size_t id = gTriggers->GetIndex(aObj);
  gActivatedTriggers.emplaceBack(id);
}

static void
InvokeTriggerCallback(size_t aId)
{
  void* obj;
  TriggerInfo info;
  {
    StaticMutexAutoLock lock(gTriggersMutex);
    obj = const_cast<void*>(gTriggers->GetValue(aId));
    TriggerInfoMap::iterator iter = gTriggerInfoMap->find(obj);
    MOZ_RELEASE_ASSERT(iter != gTriggerInfoMap->end());
    info = iter->second;
  }

  MOZ_RELEASE_ASSERT(info.mCallback);
  MOZ_RELEASE_ASSERT(info.mRegisterCount);
  info.mCallback();
}

MOZ_EXPORT void
RecordReplayInterface_ExecuteTriggers()
{
  MOZ_ASSERT(IsRecordingOrReplaying());
  MOZ_RELEASE_ASSERT(Thread::CurrentIsMainThread());
  MOZ_RELEASE_ASSERT(!AreThreadEventsPassedThrough());
  MOZ_RELEASE_ASSERT(!AreThreadEventsDisallowed());

  Thread* thread = Thread::Current();

  RecordReplayAssert("ExecuteTriggers");

  if (IsRecording()) {
    // Invoke the callbacks for any triggers waiting for execution, including
    // any whose callbacks are triggered by earlier callback invocations.
    while (true) {
      size_t id;
      {
        StaticMutexAutoLock lock(gTriggersMutex);
        if (gActivatedTriggers.empty()) {
          break;
        }
        id = gActivatedTriggers.popCopy();
      }

      thread->Events().RecordOrReplayThreadEvent(ThreadEvent::ExecuteTrigger);
      thread->Events().WriteScalar(id);
      InvokeTriggerCallback(id);
    }
    thread->Events().RecordOrReplayThreadEvent(ThreadEvent::ExecuteTriggersFinished);
  } else {
    // Execute the same callbacks which were executed at this point while
    // recording.
    while (true) {
      ThreadEvent ev = (ThreadEvent) thread->Events().ReadScalar();
      if (ev != ThreadEvent::ExecuteTrigger) {
        if (ev != ThreadEvent::ExecuteTriggersFinished) {
          child::ReportFatalError("ExecuteTrigger Mismatch");
          Unreachable();
        }
        break;
      }
      size_t id = thread->Events().ReadScalar();
      InvokeTriggerCallback(id);
    }
  }

  RecordReplayAssert("ExecuteTriggers DONE");
}

} // extern "C"

} // namespace recordreplay
} // namespace mozilla
