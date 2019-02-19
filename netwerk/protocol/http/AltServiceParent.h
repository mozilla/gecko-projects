/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef AltServiceParent_h__
#define AltServiceParent_h__

#include "mozilla/net/PAltServiceParent.h"

namespace mozilla {
namespace net {

class AltServiceParent final : public PAltServiceParent {
 public:
  explicit AltServiceParent();
  virtual ~AltServiceParent();

  mozilla::ipc::IPCResult RecvClearHostMapping(
      const nsCString& aHost, const int32_t& aPort,
      const OriginAttributes& aOriginAttributes,
      const nsCString& aTopWindowOrigin);

  void ActorDestroy(ActorDestroyReason aWhy) override;

 private:
  DISALLOW_COPY_AND_ASSIGN(AltServiceParent);
};

}  // namespace net
}  // namespace mozilla

#endif  // AltServiceParent_h__
