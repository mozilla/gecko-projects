/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=2 ts=8 et tw=80 : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/net/DNSRequestParent.h"
#include "mozilla/net/DNSRequestChild.h"
#include "nsIDNSService.h"
#include "nsNetCID.h"
#include "nsThreadUtils.h"
#include "nsIServiceManager.h"
#include "nsICancelable.h"
#include "nsIDNSRecord.h"
#include "nsHostResolver.h"
#include "mozilla/Unused.h"

using namespace mozilla::ipc;

namespace mozilla {
namespace net {

DNSRequestHandler::DNSRequestHandler() : mFlags(0) {}

//-----------------------------------------------------------------------------
// DNSRequestParent::nsISupports
//-----------------------------------------------------------------------------

NS_IMPL_ISUPPORTS(DNSRequestHandler, nsIDNSListener)

static void SendLookupCompletedHelper(DNSRequestActor* aActor,
                                      const DNSRequestResponse& aReply) {
  if (DNSRequestParent* parent = aActor->AsDNSRequestParent()) {
    Unused << parent->SendLookupCompleted(aReply);
  } else if (DNSRequestChild* child = aActor->AsDNSRequestChild()) {
    Unused << child->SendLookupCompleted(aReply);
  }
}

void DNSRequestHandler::DoAsyncResolve(const nsACString& hostname,
                                       const OriginAttributes& originAttributes,
                                       uint32_t flags) {
  nsresult rv;
  mFlags = flags;
  nsCOMPtr<nsIDNSService> dns = do_GetService(NS_DNSSERVICE_CONTRACTID, &rv);
  if (NS_SUCCEEDED(rv)) {
    nsCOMPtr<nsIEventTarget> main = GetMainThreadEventTarget();
    nsCOMPtr<nsICancelable> unused;
    rv = dns->AsyncResolveNative(hostname, flags, this, main, originAttributes,
                                 getter_AddRefs(unused));
  }

  if (NS_FAILED(rv) && mIPCActor->IPCOpen()) {
    SendLookupCompletedHelper(mIPCActor, DNSRequestResponse(rv));
    mIPCActor->SetIPCOpen(false);
  }
}

bool DNSRequestHandler::OnRecvCancelDNSRequest(
    const nsCString& hostName, const uint16_t& type,
    const OriginAttributes& originAttributes, const uint32_t& flags,
    const nsresult& reason) {
  nsresult rv;
  nsCOMPtr<nsIDNSService> dns = do_GetService(NS_DNSSERVICE_CONTRACTID, &rv);
  if (NS_SUCCEEDED(rv)) {
    if (type == nsIDNSService::RESOLVE_TYPE_DEFAULT) {
      rv = dns->CancelAsyncResolveNative(hostName, flags, this, reason,
                                         originAttributes);
    } else {
      rv = dns->CancelAsyncResolveByTypeNative(hostName, type, flags, this,
                                               reason, originAttributes);
    }
  }
  return true;
}

bool DNSRequestHandler::OnRecvLookupCompleted(const DNSRequestResponse& reply) {
  return true;
}

void DNSRequestHandler::OnIPCActorReleased() { mIPCActor = nullptr; }

//-----------------------------------------------------------------------------
// nsIDNSListener functions
//-----------------------------------------------------------------------------

NS_IMETHODIMP
DNSRequestHandler::OnLookupComplete(nsICancelable* request, nsIDNSRecord* rec,
                                    nsresult status) {
  if (!mIPCActor || !mIPCActor->IPCOpen()) {
    // nothing to do: child probably crashed
    return NS_OK;
  }

  if (NS_SUCCEEDED(status)) {
    MOZ_ASSERT(rec);

    nsAutoCString cname;
    if (mFlags & nsHostResolver::RES_CANON_NAME) {
      rec->GetCanonicalName(cname);
    }

    // Get IP addresses for hostname (use port 80 as dummy value for NetAddr)
    NetAddrArray array;
    NetAddr addr;
    while (NS_SUCCEEDED(rec->GetNextAddr(80, &addr))) {
      array.AppendElement(addr);
    }

    SendLookupCompletedHelper(mIPCActor, DNSRecord(cname, array));
  } else {
    SendLookupCompletedHelper(mIPCActor, DNSRequestResponse(status));
  }

  mIPCActor->SetIPCOpen(false);
  return NS_OK;
}

NS_IMETHODIMP
DNSRequestHandler::OnLookupByTypeComplete(nsICancelable* aRequest,
                                          nsIDNSByTypeRecord* aRes,
                                          nsresult aStatus) {
  if (!mIPCActor || !mIPCActor->IPCOpen()) {
    // nothing to do: child probably crashed
    return NS_OK;
  }

  if (NS_SUCCEEDED(aStatus)) {
    nsTArray<nsCString> rec;
    aRes->GetRecords(rec);
    SendLookupCompletedHelper(mIPCActor, DNSRequestResponse(rec));
  } else {
    SendLookupCompletedHelper(mIPCActor, DNSRequestResponse(aStatus));
  }
  mIPCActor->SetIPCOpen(false);
  return NS_OK;
}

//-----------------------------------------------------------------------------
// DNSRequestParent functions
//-----------------------------------------------------------------------------

mozilla::ipc::IPCResult DNSRequestParent::RecvCancelDNSRequest(
    const nsCString& hostName, const uint16_t& type,
    const OriginAttributes& originAttributes, const uint32_t& flags,
    const nsresult& reason) {
  return mDNSRequest->OnRecvCancelDNSRequest(hostName, type, originAttributes,
                                             flags, reason)
             ? IPC_OK()
             : IPC_FAIL_NO_REASON(this);
}

mozilla::ipc::IPCResult DNSRequestParent::RecvLookupCompleted(
    const DNSRequestResponse& reply) {
  return mDNSRequest->OnRecvLookupCompleted(reply) ? IPC_OK()
                                                   : IPC_FAIL_NO_REASON(this);
}

mozilla::ipc::IPCResult DNSRequestParent::Recv__delete__() {
  mIPCOpen = false;
  return IPC_OK();
}

void DNSRequestParent::ActorDestroy(ActorDestroyReason why) {
  // We may still have refcount>0 if DNS hasn't called our OnLookupComplete
  // yet, but child process has crashed.  We must not send any more msgs
  // to child, or IPDL will kill chrome process, too.
  mIPCOpen = false;
}

}  // namespace net
}  // namespace mozilla
