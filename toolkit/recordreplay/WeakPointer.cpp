/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WeakPointer.h"

#include "mozilla/dom/ScriptSettings.h"
#include "InfallibleVector.h"
#include "Monitor.h"
#include "ProcessRewind.h"
#include "Thread.h"
#include "ValueIndex.h"
#include "jsapi.h"

namespace mozilla {
namespace recordreplay {

static ValueIndex* gWeakPointers;

struct WeakPointerInfo {
  size_t mThreadId;
  std::function<void(bool)> mCallback;
  JS::PersistentRootedObject* mRoot;

  WeakPointerInfo(size_t aThreadId, const std::function<void(bool)>& aCallback)
    : mThreadId(aThreadId), mCallback(aCallback), mRoot(nullptr)
  {}

  ~WeakPointerInfo() {
    delete mRoot;
  }
};

typedef std::unordered_map<const void*, WeakPointerInfo> WeakPointerInfoMap;
static WeakPointerInfoMap* gWeakPointerInfoMap;

// For efficiency, we don't count each weak pointer access as a separate event,
// but instead count how many hits there were for each weak pointer index.
// Each time there is a miss we generate a new weak pointer index for the
// associated pointer value.
//
// While recording, these reflect the number of hits so far for each weak
// pointer index. While replaying, these reflect the *remaining* number of hits
// for each weak pointer index.
static StaticInfallibleVector<size_t> gWeakPointerHits;

static StaticMutexNotRecorded gWeakPointerMutex;

extern "C" {

MOZ_EXPORT void
RecordReplayInterface_InternalRegisterWeakPointer(const void* aPtr,
                                                  const std::function<void(bool)>& aCallback)
{
  MOZ_RELEASE_ASSERT(!AreThreadEventsPassedThrough());

  if (HasDivergedFromRecording()) {
    return;
  }

  MOZ_RELEASE_ASSERT(!AreThreadEventsDisallowed());

  size_t id;
  {
    AutoOrderedAtomicAccess order;
    StaticMutexAutoLock lock(gWeakPointerMutex);

    id = gWeakPointers->Insert(aPtr);
    WeakPointerInfo info(Thread::Current()->Id(), aCallback);
    gWeakPointerInfoMap->insert(WeakPointerInfoMap::value_type(aPtr, info));

    if (IsRecording()) {
      MOZ_RELEASE_ASSERT(id == gWeakPointerHits.length());
      gWeakPointerHits.append(0);
    }
  }

  RecordReplayAssert("RegisterWeakPointer %zu", id);
}

MOZ_EXPORT void
RecordReplayInterface_InternalUnregisterWeakPointer(const void* aPtr)
{
  MOZ_RELEASE_ASSERT(!AreThreadEventsPassedThrough());
  MOZ_RELEASE_ASSERT(!AreThreadEventsDisallowed());
  MOZ_RELEASE_ASSERT(gWeakPointers->Contains(aPtr));

  StaticMutexAutoLock lock(gWeakPointerMutex);

  size_t id = gWeakPointers->GetIndex(aPtr);
  RecordReplayAssert("UnregisterWeakPointer %d", (int) id);

  size_t threadId = Thread::Current()->Id();
  MOZ_RELEASE_ASSERT(gWeakPointerInfoMap->find(aPtr)->second.mThreadId == threadId);

  gWeakPointerInfoMap->erase(aPtr);
  gWeakPointers->Remove(aPtr);
}

MOZ_EXPORT void
RecordReplayInterface_InternalWeakPointerAccess(const void* aPtr, bool aSuccess)
{
  MOZ_RELEASE_ASSERT(!AreThreadEventsPassedThrough());

  if (HasDivergedFromRecording()) {
    return;
  }

  MOZ_RELEASE_ASSERT(!AreThreadEventsDisallowed());

  Maybe<StaticMutexAutoLock> lock;
  lock.emplace(gWeakPointerMutex);

  size_t id = gWeakPointers->GetIndex(aPtr);

  // The caller should use the weak pointer API to ensure that success values
  // are the same between recording and replay (e.g. see the example in
  // RecordReplay.h)
  if (IsReplaying() && aSuccess != !!gWeakPointerHits[id]) {
    EnsureNotDivergedFromRecording();
    child::ReportFatalError("Inconsistent weak pointer success values during replay");
    Unreachable();
  }

  auto iter = gWeakPointerInfoMap->find(aPtr);
  MOZ_RELEASE_ASSERT(iter != gWeakPointerInfoMap->end());
  const WeakPointerInfo& info = iter->second;
  MOZ_RELEASE_ASSERT(info.mThreadId == Thread::Current()->Id());

  RecordReplayAssert("WeakPointerAccess %d", (int) id);

  if (aSuccess) {
    if (IsRecording()) {
      gWeakPointerHits[id]++;
    } else {
      gWeakPointerHits[id]--;
    }
  } else {
    // Any JS root for this weak pointer should have been cleared already.
    MOZ_RELEASE_ASSERT(info.mRoot == nullptr);

    // Generate a new index for the pointer, per gWeakPointerHits above.
    gWeakPointers->Remove(aPtr);
    size_t id = gWeakPointers->Insert(aPtr);

    RecordReplayAssert("WeakPointerAccess Miss %d", (int) id);

    if (IsRecording()) {
      MOZ_RELEASE_ASSERT(id == gWeakPointerHits.length());
      gWeakPointerHits.append(0);
    }
  }

  // When replaying, invoke the callback associated with the weak pointer,
  // specifying whether there are any remaining hits on this pointer.
  if (IsReplaying()) {
    // Release lock before invoking the callback, which may call back into
    // e.g. SetWeakPointerJSRoot.
    std::function<void(bool)> callback = info.mCallback;
    bool success = !!gWeakPointerHits[id];
    lock.reset();

    callback(success);
  }
}

MOZ_EXPORT void
RecordReplayInterface_SetWeakPointerJSRoot(const void* aPtr, JSObject* aJSObj)
{
  MOZ_RELEASE_ASSERT(IsReplaying());
  MOZ_RELEASE_ASSERT(!AreThreadEventsPassedThrough());

  if (HasDivergedFromRecording()) {
    return;
  }

  MOZ_RELEASE_ASSERT(!AreThreadEventsDisallowed());

  StaticMutexAutoLock lock(gWeakPointerMutex);

  auto iter = gWeakPointerInfoMap->find(aPtr);
  MOZ_RELEASE_ASSERT(iter != gWeakPointerInfoMap->end());
  WeakPointerInfo& info = iter->second;
  MOZ_RELEASE_ASSERT(info.mThreadId == Thread::Current()->Id());

  if (info.mRoot) {
    delete info.mRoot;
    info.mRoot = nullptr;
  }

  if (aJSObj) {
    JSContext* cx = dom::danger::GetJSContext();
    info.mRoot = new JS::PersistentRootedObject(cx);
    *info.mRoot = aJSObj;
  }
}

} // extern "C"

void
InitializeWeakPointers()
{
  gWeakPointers = new ValueIndex();
  gWeakPointerInfoMap = new WeakPointerInfoMap();
}

void
WriteWeakPointers(File* aFile)
{
  Stream* stream = aFile->OpenStream(StreamName::WeakPointer, 0);

  stream->WriteScalar(gWeakPointerHits.length());
  for (size_t hits : gWeakPointerHits) {
    stream->WriteScalar(hits);
  }
}

void
ReadWeakPointers()
{
  Stream* stream = gRecordingFile->OpenStream(StreamName::WeakPointer, 0);

  size_t count = stream->ReadScalar();
  MOZ_RELEASE_ASSERT(count >= gWeakPointerHits.length());

  gWeakPointerHits.appendN(0, count - gWeakPointerHits.length());

  for (size_t i = 0; i < count; i++) {
    size_t totalHits = stream->ReadScalar();

    // If we just rewound to a place where we were originally recording,
    // adjust the remaining number of hits according to how many hits have
    // occurred so far.
    MOZ_RELEASE_ASSERT(gWeakPointerHits[i] <= totalHits);
    gWeakPointerHits[i] = totalHits - gWeakPointerHits[i];
  }
}

void
FixupWeakPointersAfterRecordingRewind()
{
  Thread* thread = Thread::Current();
  MOZ_RELEASE_ASSERT(thread->IsMainThread());

  ReadWeakPointers();

  // Invoke the callback for every weak pointer that currently exists. Invokes
  // need to be done on the thread associated with the weak pointer, so note
  // those on lists associated with those threads to be consumed after the
  // thread restores its stack and resumes execution.
  for (auto& iter : *gWeakPointerInfoMap) {
    if (iter.second.mThreadId == thread->Id()) {
      size_t id = gWeakPointers->GetIndex(iter.first);
      iter.second.mCallback(!!gWeakPointerHits[id]);
    } else {
      Thread* other = Thread::GetById(iter.second.mThreadId);
      other->AddPendingWeakPointerFixup(iter.first);
    }
  }
}

void
FixupOffThreadWeakPointerAfterRecordingRewind(const void* aPtr)
{
  Maybe<StaticMutexAutoLock> lock;
  lock.emplace(gWeakPointerMutex);

  auto iter = gWeakPointerInfoMap->find(aPtr);
  MOZ_RELEASE_ASSERT(iter != gWeakPointerInfoMap->end());
  WeakPointerInfo& info = iter->second;
  MOZ_RELEASE_ASSERT(info.mThreadId == Thread::Current()->Id());

  size_t id = gWeakPointers->GetIndex(iter->first);

  std::function<void(bool)> callback = info.mCallback;
  bool success = !!gWeakPointerHits[id];
  lock.reset();

  callback(success);
}

} // namespace recordreplay
} // namespace mozilla
