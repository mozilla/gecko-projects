/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/net/BackgroundDataBridgeParent.h"
#include "mozilla/net/SocketProcessChild.h"

namespace mozilla {
namespace net {

BackgroundDataBridgeParent::BackgroundDataBridgeParent(uint64_t channelID)
    : mChannelID(channelID) {
  MOZ_COUNT_CTOR(BackgroundDataBridgeParent);
  SocketProcessChild::GetSingleton()->AddDataBridgeToMap(channelID, this);
  SocketProcessChild::GetSingleton()->SaveBackgroundThread();
}

void BackgroundDataBridgeParent::ActorDestroy(ActorDestroyReason aWhy) {
  SocketProcessChild::GetSingleton()->RemoveDataBridgeFromMap(mChannelID);
}

BackgroundDataBridgeParent::~BackgroundDataBridgeParent() {
  SocketProcessChild::GetSingleton()->RemoveDataBridgeFromMap(mChannelID);
  MOZ_COUNT_DTOR(BackgroundDataBridgeParent);
}

}  // namespace net
}  // namespace mozilla
