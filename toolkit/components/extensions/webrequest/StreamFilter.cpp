/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "StreamFilter.h"

#include "jsapi.h"
#include "jsfriendapi.h"

#include "mozilla/HoldDropJSObjects.h"
#include "mozilla/SystemGroup.h"
#include "mozilla/extensions/StreamFilterChild.h"
#include "mozilla/extensions/StreamFilterEvents.h"
#include "mozilla/ipc/BackgroundChild.h"
#include "mozilla/ipc/PBackgroundChild.h"
#include "nsContentUtils.h"
#include "nsCycleCollectionParticipant.h"
#include "nsLiteralString.h"
#include "nsThreadUtils.h"
#include "nsTArray.h"

using namespace JS;
using namespace mozilla::dom;

using mozilla::ipc::BackgroundChild;
using mozilla::ipc::PBackgroundChild;

namespace mozilla {
namespace extensions {

/*****************************************************************************
 * Initialization
 *****************************************************************************/

StreamFilter::StreamFilter(nsIGlobalObject* aParent,
                           uint64_t aRequestId,
                           const nsAString& aAddonId)
  : mParent(aParent)
  , mChannelId(aRequestId)
  , mAddonId(NS_Atomize(aAddonId))
{
  MOZ_ASSERT(aParent);

  ConnectToPBackground();
};

StreamFilter::~StreamFilter()
{
  ForgetActor();
}

void
StreamFilter::ForgetActor()
{
  if (mActor) {
    mActor->Cleanup();
    mActor->SetStreamFilter(nullptr);
  }
}


/* static */ already_AddRefed<StreamFilter>
StreamFilter::Create(GlobalObject& aGlobal, uint64_t aRequestId, const nsAString& aAddonId)
{
  nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(aGlobal.GetAsSupports());
  MOZ_ASSERT(global);

  RefPtr<StreamFilter> filter = new StreamFilter(global, aRequestId, aAddonId);
  return filter.forget();
}

/*****************************************************************************
 * Actor allocation
 *****************************************************************************/

void
StreamFilter::ConnectToPBackground()
{
  PBackgroundChild* background = BackgroundChild::GetForCurrentThread();
  if (background) {
    ActorCreated(background);
  } else {
    bool ok = BackgroundChild::GetOrCreateForCurrentThread(this);
    MOZ_RELEASE_ASSERT(ok);
  }
}

void
StreamFilter::ActorFailed()
{
  MOZ_CRASH("Failed to create a PBackgroundChild actor");
}

void
StreamFilter::ActorCreated(PBackgroundChild* aBackground)
{
  MOZ_ASSERT(aBackground);
  MOZ_ASSERT(!mActor);

  nsAutoString addonId;
  mAddonId->ToString(addonId);

  PStreamFilterChild* actor = aBackground->SendPStreamFilterConstructor(mChannelId, addonId);
  MOZ_ASSERT(actor);

  mActor = static_cast<StreamFilterChild*>(actor);
  mActor->SetStreamFilter(this);
}

/*****************************************************************************
 * Binding methods
 *****************************************************************************/

template <typename T>
static inline bool
ReadTypedArrayData(nsTArray<uint8_t>& aData, const T& aArray, ErrorResult& aRv)
{
  aArray.ComputeLengthAndData();
  if (!aData.SetLength(aArray.Length(), fallible)) {
    aRv.Throw(NS_ERROR_OUT_OF_MEMORY);
    return false;
  }
  memcpy(aData.Elements(), aArray.Data(), aArray.Length());
  return true;
}

void
StreamFilter::Write(const ArrayBufferOrUint8Array& aData, ErrorResult& aRv)
{
  if (!mActor) {
    aRv.Throw(NS_ERROR_NOT_INITIALIZED);
    return;
  }

  nsTArray<uint8_t> data;

  bool ok;
  if (aData.IsArrayBuffer()) {
    ok = ReadTypedArrayData(data, aData.GetAsArrayBuffer(), aRv);
  } else if (aData.IsUint8Array()) {
    ok = ReadTypedArrayData(data, aData.GetAsUint8Array(), aRv);
  } else {
    MOZ_ASSERT_UNREACHABLE("Argument should be ArrayBuffer or Uint8Array");
    return;
  }

  if (ok) {
    mActor->Write(Move(data), aRv);
  }
}

StreamFilterStatus
StreamFilter::Status() const
{
  if (!mActor) {
    return StreamFilterStatus::Uninitialized;
  }
  return mActor->Status();
}

void
StreamFilter::Suspend(ErrorResult& aRv)
{
  if (mActor) {
    mActor->Suspend(aRv);
  } else {
    aRv.Throw(NS_ERROR_NOT_INITIALIZED);
  }
}

void
StreamFilter::Resume(ErrorResult& aRv)
{
  if (mActor) {
    mActor->Resume(aRv);
  } else {
    aRv.Throw(NS_ERROR_NOT_INITIALIZED);
  }
}

void
StreamFilter::Disconnect(ErrorResult& aRv)
{
  if (mActor) {
    mActor->Disconnect(aRv);
  } else {
    aRv.Throw(NS_ERROR_NOT_INITIALIZED);
  }
}

void
StreamFilter::Close(ErrorResult& aRv)
{
  if (mActor) {
    mActor->Close(aRv);
  } else {
    aRv.Throw(NS_ERROR_NOT_INITIALIZED);
  }
}

/*****************************************************************************
 * Event emitters
 *****************************************************************************/

void
StreamFilter::FireEvent(const nsAString& aType)
{
  EventInit init;
  init.mBubbles = false;
  init.mCancelable = false;

  RefPtr<Event> event = Event::Constructor(this, aType, init);
  event->SetTrusted(true);

  bool defaultPrevented;
  DispatchEvent(event, &defaultPrevented);
}

void
StreamFilter::FireDataEvent(const nsTArray<uint8_t>& aData)
{
  AutoEntryScript aes(mParent, "StreamFilter data event");
  JSContext* cx = aes.cx();

  RootedDictionary<StreamFilterDataEventInit> init(cx);
  init.mBubbles = false;
  init.mCancelable = false;

  auto buffer = ArrayBuffer::Create(cx, aData.Length(), aData.Elements());
  if (!buffer) {
    // TODO: There is no way to recover from this. This chunk of data is lost.
    FireErrorEvent(NS_LITERAL_STRING("Out of memory"));
    return;
  }

  init.mData.Init(buffer);

  RefPtr<StreamFilterDataEvent> event =
    StreamFilterDataEvent::Constructor(this, NS_LITERAL_STRING("data"), init);
  event->SetTrusted(true);

  bool defaultPrevented;
  DispatchEvent(event, &defaultPrevented);
}

void
StreamFilter::FireErrorEvent(const nsAString& aError)
{
  MOZ_ASSERT(mError.IsEmpty());

  mError = aError;
  FireEvent(NS_LITERAL_STRING("error"));
}

/*****************************************************************************
 * Glue
 *****************************************************************************/

/* static */ bool
StreamFilter::IsAllowedInContext(JSContext* aCx, JSObject* /* unused */)
{
  return nsContentUtils::CallerHasPermission(aCx, nsGkAtoms::webRequestBlocking);
}

JSObject*
StreamFilter::WrapObject(JSContext* aCx, HandleObject aGivenProto)
{
  return StreamFilterBinding::Wrap(aCx, this, aGivenProto);
}

NS_IMPL_CYCLE_COLLECTION_CLASS(StreamFilter)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(StreamFilter)
  NS_INTERFACE_MAP_ENTRY(nsIIPCBackgroundChildCreateCallback)
NS_INTERFACE_MAP_END_INHERITING(DOMEventTargetHelper)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(StreamFilter, DOMEventTargetHelper)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mParent)
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(StreamFilter, DOMEventTargetHelper)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mParent)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_TRACE_BEGIN_INHERITED(StreamFilter, DOMEventTargetHelper)
NS_IMPL_CYCLE_COLLECTION_TRACE_END

NS_IMPL_ADDREF_INHERITED(StreamFilter, DOMEventTargetHelper)
NS_IMPL_RELEASE_INHERITED(StreamFilter, DOMEventTargetHelper)

} // namespace extensions
} // namespace mozilla
