/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim:set ts=4 sw=4 sts=4 et cin: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// HttpLog.h should generally be included first
#include "HttpLog.h"

#include "HttpTransactionChild.h"
#include "nsHttpHandler.h"

using namespace mozilla::net;

namespace mozilla {
namespace net {

NS_IMPL_ISUPPORTS(HttpTransactionChild, nsIRequestObserver, nsIStreamListener,
                  nsITransportEventSink);

//-----------------------------------------------------------------------------
// HttpTransactionChild <public>
//-----------------------------------------------------------------------------

HttpTransactionChild::HttpTransactionChild(HttpTransactionParent* aParent)
    : mParent(aParent) {
  LOG(("Creating HttpTransactionChild @%p\n", this));
  mTransaction = new nsHttpTransaction();
}

HttpTransactionChild::~HttpTransactionChild() {
  LOG(("Destroying HttpTransactionChild @%p\n", this));
}

nsresult HttpTransactionChild::Init(
    uint32_t caps, const HttpConnectionInfoCloneArgs& infoArgs,
    nsHttpRequestHead* requestHead, nsIInputStream* requestBody,
    uint64_t requestContentLength, bool requestBodyHasHeaders,
    nsIEventTarget* target, uint64_t topLevelOuterContentWindowId,
    int32_t priority) {
  LOG(("HttpTransactionChild::Init [this=%p caps=%x]\n", this, caps));

  RefPtr<nsHttpConnectionInfo> cinfo;
  if (infoArgs.routedHost().IsEmpty()) {
    cinfo = new nsHttpConnectionInfo(
        infoArgs.host(), infoArgs.port(), infoArgs.npnToken(),
        infoArgs.username(), EmptyCString(), nullptr,
        infoArgs.originAttributes(), infoArgs.endToEndSSL());
  } else {
    cinfo = new nsHttpConnectionInfo(
        infoArgs.host(), infoArgs.port(), infoArgs.npnToken(),
        infoArgs.username(), EmptyCString(), nullptr,
        infoArgs.originAttributes(), infoArgs.routedHost(),
        infoArgs.routedPort());
  }

  // Make sure the anonymous, insecure-scheme, and private flags are transferred
  cinfo->SetAnonymous(infoArgs.anonymous());
  cinfo->SetPrivate(infoArgs.aPrivate());
  cinfo->SetInsecureScheme(infoArgs.insecureScheme());
  cinfo->SetNoSpdy(infoArgs.noSpdy());
  cinfo->SetBeConservative(infoArgs.beConservative());
  cinfo->SetTlsFlags(infoArgs.tlsFlags());
  // cinfo->SetTrrUsed(infoArgs.trrUsed());
  cinfo->SetTrrDisabled(infoArgs.trrDisabled());

  nsresult rv;
  nsCOMPtr<nsIAsyncInputStream> responseStream;
  rv = mTransaction->Init(
      caps, cinfo, requestHead, requestBody, requestContentLength,
      requestBodyHasHeaders, target, nullptr,  // TODO: security callback
      this, topLevelOuterContentWindowId, HttpTrafficCategory::eInvalid,
      getter_AddRefs(responseStream));
  if (NS_FAILED(rv)) {
    mTransaction = nullptr;
    return rv;
  }

  rv = nsInputStreamPump::Create(getter_AddRefs(mTransactionPump),
                                 responseStream);

  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  rv = gHttpHandler->InitiateTransaction(mTransaction, priority);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  rv = mTransactionPump->AsyncRead(this, nullptr);
  Unused << NS_WARN_IF(NS_FAILED(rv));
  return rv;
}

NS_IMETHODIMP
HttpTransactionChild::Cancel(nsresult aStatus) {
  if (mTransactionPump) {
    mTransactionPump->Cancel(aStatus);
  }

  return NS_OK;
}

NS_IMETHODIMP
HttpTransactionChild::Suspend(void) { return mTransactionPump->Suspend(); }

NS_IMETHODIMP
HttpTransactionChild::Resume(void) { return mTransactionPump->Resume(); }

//-----------------------------------------------------------------------------
// HttpTransactionChild <nsIStreamListener>
//-----------------------------------------------------------------------------

NS_IMETHODIMP
HttpTransactionChild::OnDataAvailable(nsIRequest* aRequest,
                                      nsIInputStream* aInputStream,
                                      uint64_t aOffset, uint32_t aCount) {
  LOG(("HttpTransactionChild::OnDataAvailable [this=%p]\n", this));
  MOZ_ASSERT(mTransaction);

  // TODO: need to determine how to send the input stream, we can use
  // OptionalIPCStream first
  return mParent->OnDataAvailable(aRequest, aInputStream, aOffset, aCount);
}

NS_IMETHODIMP
HttpTransactionChild::OnStartRequest(nsIRequest* aRequest) {
  LOG(("HttpTransactionChild::OnStartRequest [this=%p]\n", this));
  MOZ_ASSERT(mTransaction);
  nsresult status;
  aRequest->GetStatus(&status);

  nsresult rv;
  nsAutoPtr<nsHttpResponseHead> trans(mTransaction->TakeResponseHead());
  rv = mParent->OnStartRequest(status, mTransaction->SecurityInfo(),
                               mTransaction->ProxyConnectFailed(),
                               trans.forget());

  if (NS_SUCCEEDED(rv)) {
    trans = nullptr;
  }
  return rv;
}

NS_IMETHODIMP
HttpTransactionChild::OnStopRequest(nsIRequest* aRequest, nsresult aStatus) {
  LOG(("HttpTransactionChild::OnStopRequest [this=%p]\n", this));
  MOZ_ASSERT(mTransaction);
  return mParent->OnStopRequest(aStatus, mTransaction->ResponseIsComplete(),
                                mTransaction->GetTransferSize());
}

//-----------------------------------------------------------------------------
// HttpTransactionChild <nsITransportEventSink>
//-----------------------------------------------------------------------------

NS_IMETHODIMP
HttpTransactionChild::OnTransportStatus(nsITransport* aTransport,
                                        nsresult aStatus, int64_t aProgress,
                                        int64_t aProgressMax) {
  LOG(("HttpTransactionChild::OnTransportStatus [this=%p]\n", this));
  MOZ_ASSERT(mTransaction);

  NetAddr selfAddr;
  NetAddr peerAddr;
  if (aStatus == NS_NET_STATUS_CONNECTED_TO ||
      aStatus == NS_NET_STATUS_WAITING_FOR) {
    mTransaction->GetNetworkAddresses(selfAddr, peerAddr);
  }

  mParent->OnTransportStatus(aStatus, aProgress, aProgressMax, selfAddr,
                             peerAddr);
  return NS_OK;
}

}  // namespace net
}  // namespace mozilla
