/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "U2FTransactionChild.h"

namespace mozilla {
namespace dom {

mozilla::ipc::IPCResult
U2FTransactionChild::RecvConfirmRegister(const uint64_t& aTransactionId,
                                         nsTArray<uint8_t>&& aRegBuffer)
{
  RefPtr<U2FManager> mgr = U2FManager::Get();
  MOZ_ASSERT(mgr);
  mgr->FinishRegister(aTransactionId, aRegBuffer);
  return IPC_OK();
}

mozilla::ipc::IPCResult
U2FTransactionChild::RecvConfirmSign(const uint64_t& aTransactionId,
                                     nsTArray<uint8_t>&& aCredentialId,
                                     nsTArray<uint8_t>&& aBuffer)
{
  RefPtr<U2FManager> mgr = U2FManager::Get();
  MOZ_ASSERT(mgr);
  mgr->FinishSign(aTransactionId, aCredentialId, aBuffer);
  return IPC_OK();
}

mozilla::ipc::IPCResult
U2FTransactionChild::RecvAbort(const uint64_t& aTransactionId,
                               const nsresult& aError)
{
  RefPtr<U2FManager> mgr = U2FManager::Get();
  MOZ_ASSERT(mgr);
  mgr->RequestAborted(aTransactionId, aError);
  return IPC_OK();
}

void
U2FTransactionChild::ActorDestroy(ActorDestroyReason why)
{
  RefPtr<U2FManager> mgr = U2FManager::Get();
  // This could happen after the U2FManager has been shut down.
  if (mgr) {
    mgr->ActorDestroyed();
  }
}

} // namespace dom
} // namespace mozilla
