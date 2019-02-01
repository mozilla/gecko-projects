/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim:set ts=4 sw=4 sts=4 et cin: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// HttpLog.h should generally be included first
#include "HttpLog.h"

#include "HttpTransactionParent.h"
#include "mozilla/ipc/IPCStreamUtils.h"
#include "mozilla/net/SocketProcessParent.h"

using namespace mozilla::net;

namespace mozilla {
namespace net {

NS_IMPL_ISUPPORTS(HttpTransactionParent, nsIRequest,
                  nsIThreadRetargetableRequest);

//-----------------------------------------------------------------------------
// HttpTransactionParent <public>
//-----------------------------------------------------------------------------

HttpTransactionParent::HttpTransactionParent()
    : mIPCOpen(false),
      mResponseHeadTaken(false),
      mResponseTrailersTaken(false) {
  LOG(("Creating HttpTransactionParent @%p\n", this));

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

  if (!aInfo->ProxyInfo()) {
    return;
  }

  nsTArray<ProxyInfoCloneArgs> proxyInfoArray;
  nsProxyInfo* head = aInfo->ProxyInfo();
  for (nsProxyInfo* iter = head; iter; iter = iter->mNext) {
    ProxyInfoCloneArgs* arg = proxyInfoArray.AppendElement();
    arg->type() = nsCString(iter->Type());
    arg->host() = aInfo->ProxyInfo()->Host();
    arg->port() = aInfo->ProxyInfo()->Port();
    arg->username() = aInfo->ProxyInfo()->Username();
    arg->password() = aInfo->ProxyInfo()->Password();
    arg->flags() = aInfo->ProxyInfo()->Flags();
    arg->timeout() = aInfo->ProxyInfo()->Timeout();
    arg->resolveFlags() = aInfo->ProxyInfo()->ResolveFlags();
  }
  aArgs.proxyInfo() = proxyInfoArray;
}

//-----------------------------------------------------------------------------
// HttpTransactionParent <nsAHttpTransactionShell>
//-----------------------------------------------------------------------------

// Let socket process init the *real* nsHttpTransaction.
nsresult HttpTransactionParent::Init(
    uint32_t caps, nsHttpConnectionInfo* cinfo, nsHttpRequestHead* requestHead,
    nsIInputStream* requestBody, uint64_t requestContentLength,
    bool requestBodyHasHeaders, nsIEventTarget* target,
    nsIInterfaceRequestor* callbacks, nsITransportEventSink* eventsink,
    uint64_t topLevelOuterContentWindowId, HttpTrafficCategory trafficCategory,
    nsIRequestContext* requestContext, uint32_t classOfService) {
  LOG(("HttpTransactionParent::Init [this=%p caps=%x]\n", this, caps));

  // TODO Bug 1547389: support HttpTrafficAnalyzer for socket process
  Unused << trafficCategory;

  if (!mIPCOpen) {
    return NS_ERROR_FAILURE;
  }

  mEventsink = eventsink;

  HttpConnectionInfoCloneArgs infoArgs;
  GetStructFromInfo(cinfo, infoArgs);

  mozilla::ipc::AutoIPCStream autoStream;
  if (requestBody &&
      !autoStream.Serialize(requestBody, SocketProcessParent::GetSingleton())) {
    return NS_ERROR_FAILURE;
  }

  uint64_t requestContextID = requestContext ? requestContext->GetID() : 0;

  // TODO: handle |target| later
  if (!SendInit(caps, infoArgs, *requestHead,
                requestBody ? Some(autoStream.TakeValue()) : Nothing(),
                requestContentLength, requestBodyHasHeaders,
                topLevelOuterContentWindowId, requestContextID,
                classOfService)) {
    return NS_ERROR_FAILURE;
  }

  mTargetThread = GetCurrentThreadEventTarget();
  return NS_OK;
}

nsresult HttpTransactionParent::AsyncRead(nsIStreamListener* listener,
                                          int32_t priority, nsIRequest** pump) {
  MOZ_ASSERT(pump);

  if (!SendRead(priority)) {
    return NS_ERROR_FAILURE;
  }

  NS_ADDREF(*pump = this);
  mChannel = listener;
  return NS_OK;
}

nsresult HttpTransactionParent::AsyncReschedule(int32_t priority) {
  if (!SendReschedule(priority)) {
    return NS_ERROR_FAILURE;
  }
  return NS_OK;
}

void HttpTransactionParent::AsyncUpdateClassOfService(uint32_t classOfService) {
  Unused << SendUpdateClassOfService(classOfService);
}

nsresult HttpTransactionParent::AsyncCancel(nsresult reason) {
  if (!SendCancel(reason)) {
    return NS_ERROR_FAILURE;
  }
  return NS_OK;
}

nsHttpResponseHead* HttpTransactionParent::TakeResponseHead() {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(!mResponseHeadTaken, "TakeResponseHead called 2x");

  mResponseHeadTaken = true;
  return mResponseHead.forget();
}

nsHttpHeaderArray* HttpTransactionParent::TakeResponseTrailers() {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(!mResponseTrailersTaken, "TakeResponseTrailers called 2x");

  mResponseTrailersTaken = true;
  return mResponseTrailers.forget();
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

void HttpTransactionParent::SetDNSWasRefreshed() {
  MOZ_ASSERT(NS_IsMainThread(), "SetDNSWasRefreshed on main thread only!");
  Unused << SendSetDNSWasRefreshed();
}

void HttpTransactionParent::GetNetworkAddresses(NetAddr& self, NetAddr& peer) {
  self = mSelfAddr;
  peer = mPeerAddr;
}

// TODO: propogate the sticky information from socket process
bool HttpTransactionParent::IsStickyConnection() { return false; }

mozilla::TimeStamp HttpTransactionParent::GetDomainLookupStart() {
  return mTimings.domainLookupStart;
}

mozilla::TimeStamp HttpTransactionParent::GetDomainLookupEnd() {
  return mTimings.domainLookupEnd;
}

mozilla::TimeStamp HttpTransactionParent::GetConnectStart() {
  return mTimings.connectStart;
}

mozilla::TimeStamp HttpTransactionParent::GetTcpConnectEnd() {
  return mTimings.tcpConnectEnd;
}

mozilla::TimeStamp HttpTransactionParent::GetSecureConnectionStart() {
  return mTimings.secureConnectionStart;
}

mozilla::TimeStamp HttpTransactionParent::GetConnectEnd() {
  return mTimings.connectEnd;
}

mozilla::TimeStamp HttpTransactionParent::GetRequestStart() {
  return mTimings.requestStart;
}

mozilla::TimeStamp HttpTransactionParent::GetResponseStart() {
  return mTimings.responseStart;
}

mozilla::TimeStamp HttpTransactionParent::GetResponseEnd() {
  return mTimings.responseEnd;
}

bool HttpTransactionParent::ResponseIsComplete() { return mResponseIsComplete; }

int64_t HttpTransactionParent::GetTransferSize() { return mTransferSize; }

bool HttpTransactionParent::DataAlreadySent() { return mDataAlreadySent; }

nsISupports* HttpTransactionParent::SecurityInfo() { return mSecurityInfo; }

bool HttpTransactionParent::ProxyConnectFailed() { return mProxyConnectFailed; }

void HttpTransactionParent::AddIPDLReference() {
  mIPCOpen = true;
  AddRef();
}

mozilla::ipc::IPCResult HttpTransactionParent::RecvOnStartRequest(
    const nsresult& aStatus, const Maybe<nsHttpResponseHead>& aResponseHead,
    const nsCString& aSecurityInfoSerialization, const NetAddr& aSelfAddr,
    const NetAddr& aPeerAddr, const bool& aProxyConnectFailed,
    const TimingStruct& aTimings) {
  LOG(("HttpTransactionParent::RecvOnStartRequest [this=%p aStatus=%" PRIx32
       "]\n",
       this, static_cast<uint32_t>(aStatus)));

  if (!mCanceled && NS_SUCCEEDED(mStatus)) {
    mStatus = aStatus;
  }

  if (!aSecurityInfoSerialization.IsEmpty()) {
    NS_DeserializeObject(aSecurityInfoSerialization,
                         getter_AddRefs(mSecurityInfo));
  }

  if (aResponseHead.isSome()) {
    mResponseHead = new nsHttpResponseHead(aResponseHead.ref());
  }
  mProxyConnectFailed = aProxyConnectFailed;
  mSelfAddr = aSelfAddr;
  mPeerAddr = aPeerAddr;
  mTimings = aTimings;

  nsCOMPtr<nsIStreamListener> chan = mChannel;

  nsresult rv = chan->OnStartRequest(this);
  if (NS_FAILED(rv)) {
    Cancel(rv);
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult HttpTransactionParent::RecvOnTransportStatus(
    const nsresult& aStatus, const int64_t& aProgress,
    const int64_t& aProgressMax) {
  LOG(("HttpTransactionParent::OnTransportStatus [this=%p]\n", this));

  mEventsink->OnTransportStatus(nullptr, aStatus, aProgress, aProgressMax);
  return IPC_OK();
}

mozilla::ipc::IPCResult HttpTransactionParent::RecvOnDataAvailable(
    const nsCString& aData, const uint64_t& aOffset, const uint32_t& aCount,
    const bool& dataSentToChildProcess) {
  LOG(("HttpTransactionParent::RecvOnDataAvailable [this=%p, aOffset= %" PRIu64
       " aCount=%" PRIu32 " alreadySentToChild=%d]\n",
       this, aOffset, aCount, dataSentToChildProcess));

  if (mCanceled) {
    return IPC_OK();
  }

  // TODO: need to determine how to send the input stream, we use nsCString for
  // now
  nsCOMPtr<nsIStreamListener> chan = mChannel;
  nsCOMPtr<nsIInputStream> stringStream;
  nsresult rv = NS_NewByteInputStream(getter_AddRefs(stringStream),
                                      MakeSpan(aData.get(), aCount),
                                      NS_ASSIGNMENT_DEPEND);

  if (NS_FAILED(rv)) {
    Cancel(rv);
    return IPC_OK();
  }

  mDataAlreadySent = dataSentToChildProcess;
  rv = chan->OnDataAvailable(this, stringStream, aOffset, aCount);
  if (NS_FAILED(rv)) {
    Cancel(rv);
  }

  return IPC_OK();
}

mozilla::ipc::IPCResult HttpTransactionParent::RecvOnStopRequest(
    const nsresult& aStatus, const bool& aResponseIsComplete,
    const int64_t& aTransferSize, const TimingStruct& aTimings,
    const nsHttpHeaderArray& responseTrailers) {
  LOG(("HttpTransactionParent::RecvOnStopRequest [this=%p status=%" PRIx32
       "]\n",
       this, static_cast<uint32_t>(aStatus)));

  if (!mCanceled && NS_SUCCEEDED(mStatus)) {
    mStatus = aStatus;
  }

  nsCOMPtr<nsIRequest> deathGrip = this;

  mResponseIsComplete = aResponseIsComplete;
  mTransferSize = aTransferSize;
  mTimings = aTimings;
  mResponseTrailers = new nsHttpHeaderArray(responseTrailers);

  nsCOMPtr<nsIStreamListener> chan = mChannel;
  Unused << chan->OnStopRequest(this, aStatus);

  // We are done with this transaction after OnStopRequest.
  if (mIPCOpen) {
    Unused << Send__delete__(this);
  }
  return IPC_OK();
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
  LOG(("HttpTransactionParent::Cancel [this=%p status=%" PRIx32 "]\n", this,
       static_cast<uint32_t>(aStatus)));

  if (!mCanceled) {
    mCanceled = true;
    mStatus = aStatus;
    Unused << SendCancelPump(mStatus);
  }
  return NS_OK;
}

NS_IMETHODIMP
HttpTransactionParent::Suspend(void) {
  Unused << SendSuspendPump();
  return NS_OK;
}

NS_IMETHODIMP
HttpTransactionParent::Resume(void) {
  Unused << SendResumePump();
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

void HttpTransactionParent::ActorDestroy(ActorDestroyReason aWhy) {
  LOG(("HttpTransactionParent::ActorDestroy [this=%p]\n", this));
  mIPCOpen = false;
  // TODO: we (probably?) have to notify OnStopReq on the channel
}

}  // namespace net
}  // namespace mozilla
