/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim:set ts=4 sw=4 sts=4 et cin: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// HttpLog.h should generally be included first
#include "HttpLog.h"

#include "HttpTransactionParent.h"

using namespace mozilla::net;

namespace mozilla {
namespace net {

NS_IMPL_ISUPPORTS(HttpTransactionParent, nsIRequest,
                  nsIThreadRetargetableRequest);

//-----------------------------------------------------------------------------
// HttpTransactionParent <public>
//-----------------------------------------------------------------------------

HttpTransactionParent::HttpTransactionParent() {
  LOG(("Creating HttpTransactionParent @%p\n", this));
  mChild = new HttpTransactionChild(this);

  this->mSelfAddr.inet = {};
  this->mPeerAddr.inet = {};

#ifdef MOZ_VALGRIND
  memset(&mSelfAddr, 0, sizeof(NetAddr));
  memset(&mPeerAddr, 0, sizeof(NetAddr));
#endif
  mSelfAddr.raw.family = PR_AF_UNSPEC;
  mPeerAddr.raw.family = PR_AF_UNSPEC;
}

HttpTransactionParent::~HttpTransactionParent() {
  LOG(("Destroying HttpTransactionParent @%p\n", this));
}

void HttpTransactionParent::GetStructFromInfo(
    nsHttpConnectionInfo* aInfo, HttpConnectionInfoCloneArgs& aArgs) {
  aArgs.host() = aInfo->GetOrigin();
  aArgs.port() = aInfo->OriginPort();
  aArgs.npnToken() = aInfo->GetNPNToken();
  aArgs.username() = aInfo->GetUsername();
  aArgs.originAttributes() = aInfo->GetOriginAttributes();
  aArgs.endToEndSSL() = aInfo->EndToEndSSL();
  aArgs.routedHost() = aInfo->GetRoutedHost();
  aArgs.routedPort() = aInfo->RoutedPort();
  aArgs.anonymous() = aInfo->GetAnonymous();
  aArgs.aPrivate() = aInfo->GetPrivate();
  aArgs.insecureScheme() = aInfo->GetInsecureScheme();
  aArgs.noSpdy() = aInfo->GetNoSpdy();
  aArgs.beConservative() = aInfo->GetBeConservative();
  aArgs.tlsFlags() = aInfo->GetTlsFlags();
  // aArgs.trrUsed() = aInfo->GetTrrUsed();
  aArgs.trrDisabled() = aInfo->GetTrrDisabled();
}

nsresult HttpTransactionParent::Init(
    uint32_t caps, nsHttpConnectionInfo* cinfo, nsHttpRequestHead* requestHead,
    nsIInputStream* requestBody, uint64_t requestContentLength,
    bool requestBodyHasHeaders, nsIEventTarget* target,
    nsIInterfaceRequestor* callbacks, nsITransportEventSink* eventsink,
    uint64_t topLevelOuterContentWindowId, HttpTrafficCategory trafficCategory,
    int32_t priority) {
  LOG(("HttpTransactionParent::Init [this=%p caps=%x]\n", this, caps));

  // TODO Bug 1547389: support HttpTrafficAnalyzer for socket process
  Unused << trafficCategory;

  mEventsink = eventsink;
  mChannel = do_QueryInterface(eventsink);

  HttpConnectionInfoCloneArgs infoArgs;
  GetStructFromInfo(cinfo, infoArgs);

  nsresult rv = mChild->Init(caps, infoArgs, requestHead, requestBody,
                             requestContentLength, requestBodyHasHeaders,
                             target, topLevelOuterContentWindowId, priority);

  if (NS_FAILED(rv)) {
    return rv;
  }

  mTargetThread = GetCurrentThreadEventTarget();
  mCaps = caps;
  return NS_OK;
}

nsHttpResponseHead* HttpTransactionParent::TakeResponseHead() {
  return mResponseHead.forget();
}

NS_IMETHODIMP
HttpTransactionParent::GetDeliveryTarget(nsIEventTarget** aNewTarget) {
  nsCOMPtr<nsIEventTarget> target = mTargetThread;
  target.forget(aNewTarget);
  return NS_OK;
}

NS_IMETHODIMP HttpTransactionParent::RetargetDeliveryTo(
    nsIEventTarget* aEventTarget) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
HttpTransactionParent::OnTransportStatus(nsresult aStatus, int64_t aProgress,
                                         int64_t aProgressMax,
                                         NetAddr aSelfAddr, NetAddr aPeerAddr) {
  LOG(("HttpTransactionParent::OnTransportStatus [this=%p]\n", this));
  NS_ENSURE_TRUE(mEventsink, NS_ERROR_UNEXPECTED);

  if (aStatus == NS_NET_STATUS_CONNECTED_TO ||
      aStatus == NS_NET_STATUS_WAITING_FOR) {
    mSelfAddr = aSelfAddr;
    mPeerAddr = aPeerAddr;
  }
  return mEventsink->OnTransportStatus(nullptr, aStatus, aProgress,
                                       aProgressMax);
}

void HttpTransactionParent::GetNetworkAddresses(NetAddr& self, NetAddr& peer) {
  self = mSelfAddr;
  peer = mPeerAddr;
}

//-----------------------------------------------------------------------------
// HttpTransactionParent <nsIStreamListener>
//-----------------------------------------------------------------------------

NS_IMETHODIMP
HttpTransactionParent::OnDataAvailable(nsIRequest* aRequest,
                                       nsIInputStream* aInputStream,
                                       uint64_t aOffset, uint32_t aCount) {
  LOG(("HttpTransactionParent::OnDataAvailable [this=%p]\n", this));
  NS_ENSURE_TRUE(mChannel, NS_ERROR_UNEXPECTED);

  if (mCanceled) {
    return mStatus;
  }

  // TODO: need to determine how to send the input stream, we can use
  // OptionalIPCStream first
  nsCOMPtr<nsIStreamListener> chan = mChannel;
  return chan->OnDataAvailable(this, aInputStream, aOffset, aCount);
}

NS_IMETHODIMP
HttpTransactionParent::OnStartRequest(nsresult aStatus,
                                      nsISupports* aSecurityInfo,
                                      bool aProxyConnectFailed,
                                      nsHttpResponseHead* aResponseHead) {
  LOG(("HttpTransactionParent::OnStartRequest [this=%p]\n", this));
  NS_ENSURE_TRUE(mChannel, NS_ERROR_UNEXPECTED);

  if (!mCanceled) {
    mStatus = aStatus;
  }

  mSecurityInfo = aSecurityInfo;
  mProxyConnectFailed = aProxyConnectFailed;
  mResponseHead = aResponseHead;

  nsCOMPtr<nsIStreamListener> chan = mChannel;
  return chan->OnStartRequest(this);
}

NS_IMETHODIMP
HttpTransactionParent::OnStopRequest(nsresult aStatus, bool aResponseIsComplete,
                                     int64_t aTransferSize) {
  LOG(("HttpTransactionParent::OnStopRequest [this=%p]\n", this));
  NS_ENSURE_TRUE(mChannel, NS_ERROR_UNEXPECTED);

  nsresult rv;

  mResponseIsComplete = aResponseIsComplete;
  mTransferSize = aTransferSize;

  nsCOMPtr<nsIStreamListener> chan = mChannel;
  rv = chan->OnStopRequest(this, aStatus);

  return rv;
}

//-----------------------------------------------------------------------------
// HttpTransactionParent <nsIRequest>
//-----------------------------------------------------------------------------

NS_IMETHODIMP
HttpTransactionParent::GetName(nsACString& aResult) {
  aResult.Truncate();
  return NS_OK;
}

NS_IMETHODIMP
HttpTransactionParent::IsPending(bool* aRetval) {
  *aRetval = false;
  return NS_OK;
}

NS_IMETHODIMP
HttpTransactionParent::GetStatus(nsresult* aStatus) {
  *aStatus = mStatus;
  return NS_OK;
}

NS_IMETHODIMP
HttpTransactionParent::Cancel(nsresult aStatus) {
  NS_ENSURE_TRUE(mChild, NS_ERROR_NOT_AVAILABLE);

  if (!mCanceled) {
    mCanceled = true;
    mStatus = aStatus;
    if (NS_FAILED(mChild->Cancel(aStatus))) {
      return NS_ERROR_UNEXPECTED;
    }
  }
  return NS_OK;
}

NS_IMETHODIMP
HttpTransactionParent::Suspend(void) {
  NS_ENSURE_TRUE(mChild, NS_ERROR_NOT_AVAILABLE);

  if (NS_FAILED(mChild->Suspend())) {
    return NS_ERROR_UNEXPECTED;
  }
  return NS_OK;
}

NS_IMETHODIMP
HttpTransactionParent::Resume(void) {
  NS_ENSURE_TRUE(mChild, NS_ERROR_NOT_AVAILABLE);

  if (NS_FAILED(mChild->Resume())) {
    return NS_ERROR_UNEXPECTED;
  }
  return NS_OK;
}

NS_IMETHODIMP
HttpTransactionParent::GetLoadGroup(nsILoadGroup** aLoadGroup) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
HttpTransactionParent::SetLoadGroup(nsILoadGroup* aLoadGroup) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
HttpTransactionParent::GetLoadFlags(nsLoadFlags* aLoadFlags) {
  *aLoadFlags = mLoadFlags;
  return NS_OK;
}

NS_IMETHODIMP
HttpTransactionParent::SetLoadFlags(nsLoadFlags aLoadFlags) {
  mLoadFlags = aLoadFlags;
  return NS_OK;
}

}  // namespace net
}  // namespace mozilla
