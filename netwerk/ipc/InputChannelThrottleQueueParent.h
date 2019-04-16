/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef InputChannelThrottleQueueParent_h__
#define InputChannelThrottleQueueParent_h__

#include "nsISupportsImpl.h"
#include "nsIThrottledInputChannel.h"
#include "mozilla/net/PInputChannelThrottleQueueParent.h"

namespace mozilla {
namespace net {

class InputChannelThrottleQueueParent final
    : public PInputChannelThrottleQueueParent,
      public nsIInputChannelThrottleQueue {
 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIINPUTCHANNELTHROTTLEQUEUE

  friend class PInputChannelThrottleQueueParent;

  explicit InputChannelThrottleQueueParent();
  mozilla::ipc::IPCResult RecvRecordRead(const uint32_t& aBytesRead);
  void ActorDestroy(ActorDestroyReason aWhy) override {}
  virtual InputChannelThrottleQueueParent* ToInputChannelThrottleQueueParent()
      override {
    return this;
  }

 private:
  virtual ~InputChannelThrottleQueueParent() = default;

  uint64_t mBytesProcessed;
  uint32_t mMeanBytesPerSecond;
  uint32_t mMaxBytesPerSecond;
};

}  // namespace net
}  // namespace mozilla

#endif  // InputChannelThrottleQueueParent_h__
