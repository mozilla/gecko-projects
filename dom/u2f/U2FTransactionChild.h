/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_U2FTransactionChild_h
#define mozilla_dom_U2FTransactionChild_h

#include "mozilla/dom/PWebAuthnTransactionChild.h"

/*
 * Child process IPC implementation for U2F API. Receives results of U2F
 * transactions from the parent process, and sends them to the U2FManager
 * to either cancel the transaction, or be formatted and relayed to content.
 */

namespace mozilla {
namespace dom {

class U2FTransactionChild final : public PWebAuthnTransactionChild
{
public:
  NS_INLINE_DECL_REFCOUNTING(U2FTransactionChild);
  U2FTransactionChild();
  mozilla::ipc::IPCResult RecvConfirmRegister(nsTArray<uint8_t>&& aRegBuffer) override;
  mozilla::ipc::IPCResult RecvConfirmSign(nsTArray<uint8_t>&& aCredentialId,
                                          nsTArray<uint8_t>&& aBuffer) override;
  mozilla::ipc::IPCResult RecvCancel(const nsresult& aError) override;
  void ActorDestroy(ActorDestroyReason why) override;
private:
  ~U2FTransactionChild() = default;
};

}
}

#endif //mozilla_dom_U2FTransactionChild_h
