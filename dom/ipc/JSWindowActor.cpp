/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/JSWindowActor.h"
#include "mozilla/dom/JSWindowActorBinding.h"
#include "mozilla/dom/MessageManagerBinding.h"
#include "mozilla/dom/PWindowGlobal.h"
#include "mozilla/dom/Promise.h"
#include "js/Promise.h"
#include "xpcprivate.h"
#include "nsIXPConnect.h"

namespace mozilla {
namespace dom {

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(JSWindowActor)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(JSWindowActor)
NS_IMPL_CYCLE_COLLECTING_RELEASE(JSWindowActor)

NS_IMPL_CYCLE_COLLECTION_CLASS(JSWindowActor)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(JSWindowActor)
  tmp->RejectPendingQueries();  // Clear out & reject mPendingQueries
  NS_IMPL_CYCLE_COLLECTION_UNLINK_PRESERVED_WRAPPER
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(JSWindowActor)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mPendingQueries)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mWrappedJS)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_TRACE_WRAPPERCACHE(JSWindowActor)

JSWindowActor::JSWindowActor() : mNextQueryId(0) {}

void JSWindowActor::StartDestroy() {
  InvokeCallback(CallbackFunction::WillDestroy);
}

void JSWindowActor::AfterDestroy() {
  InvokeCallback(CallbackFunction::DidDestroy);
}

void JSWindowActor::InvokeCallback(CallbackFunction callback) {
  AutoEntryScript aes(GetParentObject(), "JSWindowActor destroy callback");
  JSContext* cx = aes.cx();
  MozJSWindowActorCallbacks callbacksHolder;
  NS_ENSURE_TRUE_VOID(GetWrapper());
  JS::Rooted<JS::Value> val(cx, JS::ObjectValue(*GetWrapper()));
  if (NS_WARN_IF(!callbacksHolder.Init(cx, val))) {
    return;
  }

  // Destroy callback is optional.
  if (callback == CallbackFunction::WillDestroy) {
    if (callbacksHolder.mWillDestroy.WasPassed()) {
      callbacksHolder.mWillDestroy.Value()->Call(this);
    }
  } else if (callback == CallbackFunction::DidDestroy) {
    if (callbacksHolder.mDidDestroy.WasPassed()) {
      callbacksHolder.mDidDestroy.Value()->Call(this);
    }
  } else {
    if (callbacksHolder.mActorCreated.WasPassed()) {
      callbacksHolder.mActorCreated.Value()->Call(this);
    }
  }
}

nsresult JSWindowActor::QueryInterfaceActor(const nsIID& aIID, void** aPtr) {
  if (!mWrappedJS) {
    AutoEntryScript aes(GetParentObject(), "JSWindowActor query interface");
    JSContext* cx = aes.cx();

    JS::Rooted<JSObject*> self(cx, GetWrapper());
    JSAutoRealm ar(cx, self);

    RefPtr<nsXPCWrappedJS> wrappedJS;
    nsresult rv = nsXPCWrappedJS::GetNewOrUsed(
        cx, self, NS_GET_IID(nsISupports), getter_AddRefs(wrappedJS));
    NS_ENSURE_SUCCESS(rv, rv);

    mWrappedJS = do_QueryInterface(wrappedJS);
    MOZ_ASSERT(mWrappedJS);
  }

  return mWrappedJS->QueryInterface(aIID, aPtr);
}

void JSWindowActor::RejectPendingQueries() {
  // Take our queries out, in case somehow rejecting promises can trigger
  // additions or removals.
  nsRefPtrHashtable<nsUint64HashKey, Promise> pendingQueries;
  mPendingQueries.SwapElements(pendingQueries);
  for (auto iter = pendingQueries.Iter(); !iter.Done(); iter.Next()) {
    iter.Data()->MaybeReject(NS_ERROR_NOT_AVAILABLE);
  }
}

void JSWindowActor::SetName(const nsAString& aName) {
  MOZ_ASSERT(mName.IsEmpty(), "Cannot set name twice!");
  mName = aName;
}

void JSWindowActor::SendAsyncMessage(JSContext* aCx,
                                     const nsAString& aMessageName,
                                     JS::Handle<JS::Value> aObj,
                                     ErrorResult& aRv) {
  ipc::StructuredCloneData data;
  if (!nsFrameMessageManager::GetParamsForMessage(
          aCx, aObj, JS::UndefinedHandleValue, data)) {
    aRv.Throw(NS_ERROR_DOM_DATA_CLONE_ERR);
    return;
  }

  JSWindowActorMessageMeta meta;
  meta.actorName() = mName;
  meta.messageName() = aMessageName;
  meta.kind() = JSWindowActorMessageKind::Message;

  SendRawMessage(meta, std::move(data), aRv);
}

already_AddRefed<Promise> JSWindowActor::SendQuery(
    JSContext* aCx, const nsAString& aMessageName, JS::Handle<JS::Value> aObj,
    ErrorResult& aRv) {
  ipc::StructuredCloneData data;
  if (!nsFrameMessageManager::GetParamsForMessage(
          aCx, aObj, JS::UndefinedHandleValue, data)) {
    aRv.Throw(NS_ERROR_DOM_DATA_CLONE_ERR);
    return nullptr;
  }

  nsIGlobalObject* global = xpc::CurrentNativeGlobal(aCx);
  if (NS_WARN_IF(!global)) {
    aRv.Throw(NS_ERROR_UNEXPECTED);
    return nullptr;
  }

  RefPtr<Promise> promise = Promise::Create(global, aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }

  JSWindowActorMessageMeta meta;
  meta.actorName() = mName;
  meta.messageName() = aMessageName;
  meta.queryId() = mNextQueryId++;
  meta.kind() = JSWindowActorMessageKind::Query;

  mPendingQueries.Put(meta.queryId(), promise);

  SendRawMessage(meta, std::move(data), aRv);
  return promise.forget();
}

void JSWindowActor::ReceiveRawMessage(const JSWindowActorMessageMeta& aMetadata,
                                      ipc::StructuredCloneData&& aData) {
  AutoEntryScript aes(GetParentObject(), "JSWindowActor message handler");
  JSContext* cx = aes.cx();

  // Read the message into a JS object from IPC.
  ErrorResult error;
  JS::Rooted<JS::Value> data(cx);
  aData.Read(cx, &data, error);
  if (NS_WARN_IF(error.Failed())) {
    if (XRE_IsParentProcess()) {
      MOZ_ASSERT(false, "Should not receive non-decodable data");
    } else {
      MOZ_DIAGNOSTIC_ASSERT(false, "Should not receive non-decodable data");
    }
    MOZ_ALWAYS_TRUE(error.MaybeSetPendingException(cx));
    return;
  }

  switch (aMetadata.kind()) {
    case JSWindowActorMessageKind::QueryResolve:
    case JSWindowActorMessageKind::QueryReject:
      ReceiveQueryReply(cx, aMetadata, data, error);
      break;

    case JSWindowActorMessageKind::Message:
    case JSWindowActorMessageKind::Query:
      ReceiveMessageOrQuery(cx, aMetadata, data, error);
      break;

    default:
      MOZ_ASSERT_UNREACHABLE();
  }

  if (NS_WARN_IF(error.Failed())) {
    MOZ_ALWAYS_TRUE(error.MaybeSetPendingException(cx));
  }
}

void JSWindowActor::ReceiveMessageOrQuery(
    JSContext* aCx, const JSWindowActorMessageMeta& aMetadata,
    JS::Handle<JS::Value> aData, ErrorResult& aRv) {
  // The argument which we want to pass to IPC.
  RootedDictionary<ReceiveMessageArgument> argument(aCx);
  argument.mObjects = JS_NewPlainObject(aCx);
  argument.mTarget = this;
  argument.mName = aMetadata.messageName();
  argument.mData = aData;
  argument.mJson = aData;
  argument.mSync = false;

  JS::Rooted<JSObject*> self(aCx, GetWrapper());
  JS::Rooted<JSObject*> global(aCx, JS::GetNonCCWObjectGlobal(self));

  // We only need to create a promise if we're dealing with a query here. It
  // will be resolved or rejected once the listener has been called. Our
  // listener on this promise will then send the reply.
  RefPtr<Promise> promise;
  if (aMetadata.kind() == JSWindowActorMessageKind::Query) {
    promise = Promise::Create(xpc::NativeGlobal(global), aRv);
    if (NS_WARN_IF(aRv.Failed())) {
      return;
    }

    RefPtr<QueryHandler> handler = new QueryHandler(this, aMetadata);
    promise->AppendNativeHandler(handler);
  }

  // Invoke the actual callback.
  JS::Rooted<JS::Value> retval(aCx);
  RefPtr<MessageListener> messageListener =
      new MessageListener(self, global, nullptr, nullptr);
  messageListener->ReceiveMessage(argument, &retval, aRv,
                                  "JSWindowActor receive message");

  // If we have a promise, resolve or reject it respectively.
  if (promise) {
    if (aRv.Failed()) {
      promise->MaybeReject(aRv);
    } else {
      promise->MaybeResolve(retval);
    }
  }
}

void JSWindowActor::ReceiveQueryReply(JSContext* aCx,
                                      const JSWindowActorMessageMeta& aMetadata,
                                      JS::Handle<JS::Value> aData,
                                      ErrorResult& aRv) {
  if (NS_WARN_IF(aMetadata.actorName() != mName)) {
    aRv.Throw(NS_ERROR_UNEXPECTED);
    return;
  }

  RefPtr<Promise> promise;
  if (NS_WARN_IF(!mPendingQueries.Remove(aMetadata.queryId(),
                                         getter_AddRefs(promise)))) {
    aRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
    return;
  }

  JSAutoRealm ar(aCx, promise->PromiseObj());
  JS::RootedValue data(aCx, aData);
  if (NS_WARN_IF(!JS_WrapValue(aCx, &data))) {
    aRv.Throw(NS_ERROR_FAILURE);
    return;
  }

  if (aMetadata.kind() == JSWindowActorMessageKind::QueryResolve) {
    promise->MaybeResolve(data);
  } else {
    promise->MaybeReject(NS_ERROR_DOM_OPERATION_ERR);
  }
}

// Native handler for our generated promise which is used to handle Queries and
// send the reply when their promises have been resolved.
JSWindowActor::QueryHandler::QueryHandler(
    JSWindowActor* aActor, const JSWindowActorMessageMeta& aMetadata)
    : mActor(aActor),
      mMessageName(aMetadata.messageName()),
      mQueryId(aMetadata.queryId()) {}

void JSWindowActor::QueryHandler::RejectedCallback(
    JSContext* aCx, JS::Handle<JS::Value> aValue) {
  if (!mActor) {
    return;
  }

  // Make sure that this rejection is reported, despite being "handled". This
  // is done by creating a new promise in the rejected state, and throwing it
  // away. This will be reported as an unhandled rejected promise.
  Unused << JS::CallOriginalPromiseReject(aCx, aValue);

  // The exception probably isn't cloneable, so just send down undefined.
  ipc::StructuredCloneData data;
  data.Write(aCx, JS::UndefinedHandleValue, IgnoredErrorResult());
  SendReply(aCx, JSWindowActorMessageKind::QueryReject, std::move(data));
}

void JSWindowActor::QueryHandler::ResolvedCallback(
    JSContext* aCx, JS::Handle<JS::Value> aValue) {
  if (!mActor) {
    return;
  }

  ipc::StructuredCloneData data;
  data.InitScope(JS::StructuredCloneScope::DifferentProcess);

  IgnoredErrorResult error;
  data.Write(aCx, aValue, error);
  if (NS_WARN_IF(error.Failed())) {
    // We failed to serialize the message over IPC. Report this error to the
    // console, and send a reject reply.
    nsAutoString msg;
    msg.Append(mActor->Name());
    msg.Append(':');
    msg.Append(mMessageName);
    msg.Append(NS_LITERAL_STRING(": message reply cannot be cloned."));
    nsContentUtils::LogSimpleConsoleError(msg, "chrome", false, true);

    JS_ClearPendingException(aCx);

    ipc::StructuredCloneData data;
    data.Write(aCx, JS::UndefinedHandleValue, IgnoredErrorResult());
    SendReply(aCx, JSWindowActorMessageKind::QueryReject, std::move(data));
    return;
  }

  SendReply(aCx, JSWindowActorMessageKind::QueryResolve, std::move(data));
}

void JSWindowActor::QueryHandler::SendReply(JSContext* aCx,
                                            JSWindowActorMessageKind aKind,
                                            ipc::StructuredCloneData&& aData) {
  MOZ_ASSERT(mActor);

  JSWindowActorMessageMeta meta;
  meta.actorName() = mActor->Name();
  meta.messageName() = mMessageName;
  meta.queryId() = mQueryId;
  meta.kind() = aKind;

  mActor->SendRawMessage(meta, std::move(aData), IgnoreErrors());
  mActor = nullptr;
}

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(JSWindowActor::QueryHandler)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(JSWindowActor::QueryHandler)
NS_IMPL_CYCLE_COLLECTING_RELEASE(JSWindowActor::QueryHandler)

NS_IMPL_CYCLE_COLLECTION(JSWindowActor::QueryHandler, mActor)

}  // namespace dom
}  // namespace mozilla
