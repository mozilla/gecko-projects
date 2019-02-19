/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim:set ts=4 sw=4 sts=4 et cin: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// HttpLog.h should generally be included first
#include "HttpLog.h"

#include "AltServiceParent.h"
#include "nsHttpHandler.h"

namespace mozilla {
namespace net {

AltServiceParent::AltServiceParent() { MOZ_COUNT_CTOR(AltServiceParent); }

AltServiceParent::~AltServiceParent() { MOZ_COUNT_DTOR(AltServiceParent); }

mozilla::ipc::IPCResult AltServiceParent::RecvClearHostMapping(
    const nsCString& aHost, const int32_t& aPort,
    const OriginAttributes& aOriginAttributes,
    const nsCString& aTopWindowOrigin) {
  gHttpHandler->AltServiceCache()->ClearHostMapping(
      aHost, aPort, aOriginAttributes, aTopWindowOrigin);
  return IPC_OK();
}

void AltServiceParent::ActorDestroy(ActorDestroyReason aWhy) {
  LOG(("AltServiceParent::ActorDestroy [this=%p]\n", this));
}

}  // namespace net
}  // namespace mozilla
