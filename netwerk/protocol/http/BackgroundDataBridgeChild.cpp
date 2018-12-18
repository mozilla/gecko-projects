/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/net/BackgroundDataBridgeChild.h"
#include "mozilla/net/HttpBackgroundChannelChild.h"

namespace mozilla {
namespace net {

mozilla::ipc::IPCResult BackgroundDataBridgeChild::RecvOnTransportAndData(
    const uint64_t& offset, const uint32_t& count, const nsCString& data) {
  return mBgChild->RecvOnTransportAndData(NS_OK, NS_NET_STATUS_RECEIVING_FROM,
                                          offset, count, data);
}

}  // namespace net
}  // namespace mozilla
