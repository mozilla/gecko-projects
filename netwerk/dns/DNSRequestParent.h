/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=2 ts=8 et tw=80 : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_net_DNSRequestParent_h
#define mozilla_net_DNSRequestParent_h

#include "mozilla/net/DNSRequestBase.h"
#include "mozilla/net/PDNSRequestParent.h"
#include "nsIDNSService.h"

namespace mozilla {
namespace net {

class DNSRequestParent : public DNSRequestActor, public PDNSRequestParent {
 public:
  explicit DNSRequestParent(DNSRequestBase* aRequest)
      : DNSRequestActor(aRequest) {}

  // Pass args here rather than storing them in the parent; they are only
  // needed if the request is to be canceled.
  mozilla::ipc::IPCResult RecvCancelDNSRequest(
      const nsCString& hostName, const uint16_t& type,
      const OriginAttributes& originAttributes, const uint32_t& flags,
      const nsresult& reason);
  mozilla::ipc::IPCResult Recv__delete__() override;
  mozilla::ipc::IPCResult RecvLookupCompleted(const DNSRequestResponse& reply);

  DNSRequestChild* AsDNSRequestChild() override { return nullptr; }
  DNSRequestParent* AsDNSRequestParent() override { return this; }

 protected:
  virtual void ActorDestroy(ActorDestroyReason why) override;

 private:
  virtual ~DNSRequestParent() = default;
};

}  // namespace net
}  // namespace mozilla

#endif  // mozilla_net_DNSRequestParent_h
