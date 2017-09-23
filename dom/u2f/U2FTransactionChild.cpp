/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "U2FTransactionChild.h"

namespace mozilla {
namespace dom {

U2FTransactionChild::U2FTransactionChild()
{
  // Retain a reference so the task object isn't deleted without IPDL's
  // knowledge. The reference will be released by
  // mozilla::ipc::BackgroundChildImpl::DeallocPWebAuthnTransactionChild.
  NS_ADDREF_THIS();
}

mozilla::ipc::IPCResult
U2FTransactionChild::RecvConfirmRegister(nsTArray<uint8_t>&& aRegBuffer)
{
  RefPtr<U2FManager> mgr = U2FManager::Get();
  MOZ_ASSERT(mgr);
  mgr->FinishRegister(aRegBuffer);
  return IPC_OK();
}

mozilla::ipc::IPCResult
U2FTransactionChild::RecvConfirmSign(nsTArray<uint8_t>&& aCredentialId,
                                     nsTArray<uint8_t>&& aBuffer)
{
  RefPtr<U2FManager> mgr = U2FManager::Get();
  MOZ_ASSERT(mgr);
  mgr->FinishSign(aCredentialId, aBuffer);
  return IPC_OK();
}

mozilla::ipc::IPCResult
U2FTransactionChild::RecvCancel(const nsresult& aError)
{
  RefPtr<U2FManager> mgr = U2FManager::Get();
  MOZ_ASSERT(mgr);
  mgr->Cancel(aError);
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
