/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_net_Http2PushStreamManager_h
#define mozilla_net_Http2PushStreamManager_h

#include <functional>
#include <map>
#include "mozilla/StaticPtr.h"
#include "nsDataHashtable.h"
#include "nsString.h"

namespace mozilla {
namespace net {

class Http2PushedStream;

// This class basically saves the mapping of Http2PushedStream and
// PushCallback. The workflow is described below.
// 1. A PushCallback is registered by calling RegisterOnPushCallback().
// 2. When a Http2PushedStream is created, OnPushStreamAdded() is called
//    and the stream is added into mIDToStreamMap.
// 3. CallOnPushCallback() is called to invoke the registered PushCallback.
// 4. The pushed stream's id is saved in nsHttpChannel and passed to
//    nsHttpTransaction.
// 5. nsHttpTransaction will use the saved stream id to call GetStreamById()
//    and finally get the Http2PushedStream object.
class Http2PushStreamManager final {
 public:
  using PushCallback =
      std::function<nsresult(uint32_t, const nsACString&, const nsACString&)>;

  static Http2PushStreamManager* GetSingleton();
  ~Http2PushStreamManager() { MOZ_COUNT_DTOR(Http2PushStreamManager); };

  void OnPushStreamAdded(uint64_t aChannelId, Http2PushedStream* aStream);
  nsresult CallOnPushCallback(uint64_t aChannelId, uint32_t aStreamId,
                              const nsACString& aResourceUrl,
                              const nsACString& aRequestString);
  void RegisterOnPushCallback(uint64_t aChannelId, PushCallback&& aCallback);
  Http2PushedStream* GetStreamById(uint32_t aStreamId);
  void RemoveStreamById(uint32_t aStreamId);

 private:
  static StaticAutoPtr<Http2PushStreamManager> sSingleton;
  nsDataHashtable<nsUint32HashKey, Http2PushedStream*> mIDToStreamMap;
  std::map<uint64_t, PushCallback> mCallbackMap;

  Http2PushStreamManager() { MOZ_COUNT_CTOR(Http2PushStreamManager); };
};

}  // namespace net
}  // namespace mozilla

#endif  // mozilla_net_Http2PushStreamManager_h
