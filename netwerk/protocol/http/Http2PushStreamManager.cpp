/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "Http2PushStreamManager.h"

#include "mozilla/ClearOnShutdown.h"
#include "nsThreadUtils.h"

namespace mozilla {
namespace net {

/* static */
StaticAutoPtr<Http2PushStreamManager> Http2PushStreamManager::sSingleton;

/* static */
Http2PushStreamManager* Http2PushStreamManager::GetSingleton() {
  MOZ_ASSERT(NS_IsMainThread());

  if (!sSingleton) {
    sSingleton = new Http2PushStreamManager();
    ClearOnShutdown(&sSingleton);
  }
  return sSingleton;
}

void Http2PushStreamManager::OnPushStreamAdded(uint64_t aChannelId,
                                               Http2PushedStream* aStream) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aStream);

  if (mCallbackMap.find(aChannelId) == mCallbackMap.end()) {
    aStream->OnPushFailed();
    return;
  }

  uint32_t streamId = aStream->StreamID();
  if (!mIDToStreamMap.Lookup(streamId)) {
    mIDToStreamMap.Put(streamId, aStream);
  }

  if (NS_FAILED(CallOnPushCallback(aChannelId, streamId,
                                   aStream->GetResourceUrl(),
                                   aStream->GetRequestString()))) {
    aStream->OnPushFailed();
    mIDToStreamMap.Remove(streamId);
  }
}

nsresult Http2PushStreamManager::CallOnPushCallback(
    uint64_t aChannelId, uint32_t aStreamId, const nsACString& aResourceUrl,
    const nsACString& aRequestString) {
  MOZ_ASSERT(NS_IsMainThread());

  if (mCallbackMap.find(aChannelId) == mCallbackMap.end()) {
    MOZ_ASSERT(false, "Callback is not registered!");
    return NS_ERROR_FAILURE;
  }

  return mCallbackMap[aChannelId](aStreamId, aResourceUrl, aRequestString);
}

void Http2PushStreamManager::RegisterOnPushCallback(uint64_t aChannelId,
                                                    PushCallback&& aCallback) {
  MOZ_ASSERT(NS_IsMainThread());

  mCallbackMap[aChannelId] = std::move(aCallback);
}

Http2PushedStream* Http2PushStreamManager::GetStreamById(uint32_t aStreamId) {
  MOZ_ASSERT(NS_IsMainThread());

  return mIDToStreamMap.Get(aStreamId);
}

void Http2PushStreamManager::RemoveStreamById(uint32_t aStreamId) {
  MOZ_ASSERT(NS_IsMainThread());

  mIDToStreamMap.Remove(aStreamId);
}

}  // namespace net
}  // namespace mozilla
