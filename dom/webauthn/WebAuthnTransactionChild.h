/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_WebAuthnTransactionChild_h
#define mozilla_dom_WebAuthnTransactionChild_h

#include "mozilla/dom/WebAuthnTransactionChildBase.h"

/*
 * Child process IPC implementation for WebAuthn API. Receives results of
 * WebAuthn transactions from the parent process, and sends them to the
 * WebAuthnManager either cancel the transaction, or be formatted and relayed to
 * content.
 */

namespace mozilla {
namespace dom {

class WebAuthnTransactionChild final : public WebAuthnTransactionChildBase
{
public:
  mozilla::ipc::IPCResult
  RecvConfirmRegister(const uint64_t& aTransactionId,
                      nsTArray<uint8_t>&& aRegBuffer) override;

  mozilla::ipc::IPCResult
  RecvConfirmSign(const uint64_t& aTransactionId,
                  nsTArray<uint8_t>&& aCredentialId,
                  nsTArray<uint8_t>&& aBuffer) override;

  mozilla::ipc::IPCResult
  RecvAbort(const uint64_t& aTransactionId, const nsresult& aError) override;

  void ActorDestroy(ActorDestroyReason why) override;
};

}
}

#endif //mozilla_dom_WebAuthnTransactionChild_h
