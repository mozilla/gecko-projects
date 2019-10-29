/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim:set ts=4 sw=4 sts=4 et cin: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// HttpLog.h should generally be included first
#include "HttpLog.h"

#include "HttpTransactionParent.h"
#include "mozilla/ipc/IPCStreamUtils.h"
#include "mozilla/net/ChannelEventQueue.h"
#include "mozilla/net/InputChannelThrottleQueueParent.h"
#include "mozilla/net/SocketProcessParent.h"
#include "nsHttpHandler.h"
#include "nsIHttpActivityObserver.h"
#include "nsStreamUtils.h"

using namespace mozilla::net;

namespace mozilla {
namespace net {

NS_IMPL_ADDREF(HttpTransactionParent)
NS_IMPL_QUERY_INTERFACE(HttpTransactionParent, nsIRequest,
                        nsIThreadRetargetableRequest)

NS_IMETHODIMP_(MozExternalRefCountType) HttpTransactionParent::Release(void) {
  MOZ_ASSERT(int32_t(mRefCnt) > 0, "dup release");

  if (!nsAutoRefCnt::isThreadSafe) {
    NS_ASSERT_OWNINGTHREAD(HttpTransactionParent);
  }

  nsrefcnt count = --mRefCnt;
  NS_LOG_RELEASE(this, count, "HttpTransactionParent");

  if (count == 0) {
    if (!nsAutoRefCnt::isThreadSafe) {
      NS_ASSERT_OWNINGTHREAD(HttpTransactionParent);
    }

    mRefCnt = 1; /* stabilize */
    delete (this);
    return 0;
  }

  // When ref count goes down to 1 (held internally by IPDL), it means that
  // we are done with this transaction. We should send a delete message
  // to delete the transaction child in socket process.
  if (count == 1 && mIPCOpen) {
    mozilla::Unused << Send__delete__(this);
    return 1;
  }
  return count;
}

//-----------------------------------------------------------------------------
// HttpTransactionParent <public>
//-----------------------------------------------------------------------------

HttpTransactionParent::HttpTransactionParent()
    : mIPCOpen(false),
      mResponseHeadTaken(false),
      mResponseTrailersTaken(false),
      mHasStickyConnection(false),
      mChannelId(0),
      mOnStartRequestCalled(false),
      mOnStopRequestCalled(false) {
  LOG(("Creating HttpTransactionParent @%p\n", this));

  this->mSelfAddr.inet = {};
  this->mPeerAddr.inet = {};

#ifdef MOZ_VALGRIND
  memset(&mSelfAddr, 0, sizeof(NetAddr));
  memset(&mPeerAddr, 0, sizeof(NetAddr));
#endif
  mSelfAddr.raw.family = PR_AF_UNSPEC;
  mPeerAddr.raw.family = PR_AF_UNSPEC;

  mEventQ = new ChannelEventQueue(static_cast<nsIRequest*>(this));
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
    nsIRequestContext* requestContext, uint32_t classOfService,
    uint32_t pushedStreamId, uint64_t aChannelId, bool responseTimeoutEnabled,
    uint32_t initialRwin) {
  LOG(("HttpTransactionParent::Init [this=%p caps=%x]\n", this, caps));

  if (!mIPCOpen) {
    return NS_ERROR_FAILURE;
  }

  mEventsink = eventsink;
  mTargetThread = GetCurrentThreadEventTarget();
  mChannelId = aChannelId;

  nsCOMPtr<nsIThrottledInputChannel> throttled = do_QueryInterface(mEventsink);
  nsIInputChannelThrottleQueue* queue;
  Maybe<PInputChannelThrottleQueueParent*> throttleQueue;
  if (throttled) {
    nsresult rv = throttled->GetThrottleQueue(&queue);
    // In case of failure, just carry on without throttling.
    if (NS_SUCCEEDED(rv) && queue) {
      LOG(("HttpTransactionParent::Init %p using throttle queue %p\n", this,
           queue));
      uint32_t meanBytesPerSecond = 0;
      uint32_t maxBytesPerSecond = 0;
      Unused << queue->GetMaxBytesPerSecond(&maxBytesPerSecond);
      Unused << queue->GetMeanBytesPerSecond(&meanBytesPerSecond);
      InputChannelThrottleQueueParent* tqParent =
          queue->ToInputChannelThrottleQueueParent();
      MOZ_RELEASE_ASSERT(tqParent);

      Unused << SocketProcessParent::GetSingleton()
                    ->SendPInputChannelThrottleQueueConstructor(
                        tqParent, meanBytesPerSecond, maxBytesPerSecond);

      // The Release() is called by IPDL when the actor is deleted.
      tqParent->AddRef();
      throttleQueue = Some(tqParent);
    }
  }

  HttpConnectionInfoCloneArgs infoArgs;
  GetStructFromInfo(cinfo, infoArgs);

  mozilla::ipc::AutoIPCStream autoStream;
  if (requestBody &&
      !autoStream.Serialize(requestBody, SocketProcessParent::GetSingleton())) {
    return NS_ERROR_FAILURE;
  }

  uint64_t requestContextID = requestContext ? requestContext->GetID() : 0;
  bool activityDistributorActivated = false;
  if (nsCOMPtr<nsIHttpActivityObserver> distributor =
          services::GetActivityDistributor()) {
    Unused << distributor->GetIsActive(&activityDistributorActivated);
  }

  // TODO: handle |target| later
  if (!SendInit(caps, infoArgs, *requestHead,
                requestBody ? Some(autoStream.TakeValue()) : Nothing(),
                requestContentLength, requestBodyHasHeaders,
                topLevelOuterContentWindowId,
                static_cast<uint8_t>(trafficCategory), requestContextID,
                classOfService, pushedStreamId, activityDistributorActivated,
                responseTimeoutEnabled, initialRwin, throttleQueue)) {
    return NS_ERROR_FAILURE;
  }

  return NS_OK;
}

nsresult HttpTransactionParent::AsyncRead(nsIStreamListener* listener,
                                          nsIRequest** pump) {
  MOZ_ASSERT(pump);

  if (!SendRead()) {
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
  if (!mIPCOpen) {
    return NS_OK;
  }

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

nsresult HttpTransactionParent::SetSniffedTypeToChannel(
    nsIRequest* aPump, nsIChannel* aChannel,
    nsInputStreamPump::PeekSegmentFun aCallTypeSniffers) {
  Unused << aPump;
  if (!mDataForSniffer.IsEmpty()) {
    aCallTypeSniffers(aChannel, mDataForSniffer.Elements(),
                      mDataForSniffer.Length());
  }
  return NS_OK;
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

bool HttpTransactionParent::HasStickyConnection() {
  return mHasStickyConnection;
}

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

const TimingStruct HttpTransactionParent::Timings() { return mTimings; }

bool HttpTransactionParent::ResponseIsComplete() { return mResponseIsComplete; }

int64_t HttpTransactionParent::GetTransferSize() { return mTransferSize; }

bool HttpTransactionParent::DataAlreadySent() { return mDataAlreadySent; }

nsISupports* HttpTransactionParent::SecurityInfo() { return mSecurityInfo; }

bool HttpTransactionParent::ProxyConnectFailed() { return mProxyConnectFailed; }

void HttpTransactionParent::AddIPDLReference() {
  mIPCOpen = true;
  AddRef();
}

void HttpTransactionParent::SetTransactionObserver(TransactionObserver&& obs) {
  mTransactionObserver = std::move(obs);
}

namespace {

class OnStartRequestEvent : public MainThreadChannelEvent {
 public:
  explicit OnStartRequestEvent(HttpTransactionParent* aParent,
                               const nsresult& aStatus,
                               const Maybe<nsHttpResponseHead>& aResponseHead,
                               const nsCString& aSecurityInfoSerialization,
                               const bool& aProxyConnectFailed,
                               const TimingStruct& aTimings,
                               nsTArray<uint8_t>&& aDataForSniffer)
      : mParent(aParent),
        mStatus(aStatus),
        mResponseHead(aResponseHead),
        mSecurityInfoSerialization(aSecurityInfoSerialization),
        mProxyConnectFailed(aProxyConnectFailed),
        mTimings(aTimings),
        mDataForSniffer(std::move(aDataForSniffer)) {}

  void Run() override {
    LOG(("HttpTransactionParent::OnStartRequestEvent [this=%p]\n", mParent));
    mParent->DoOnStartRequest(mStatus, mResponseHead,
                              mSecurityInfoSerialization, mProxyConnectFailed,
                              mTimings, std::move(mDataForSniffer));
  }

 private:
  HttpTransactionParent* mParent;
  nsresult mStatus;
  Maybe<nsHttpResponseHead> mResponseHead;
  nsCString mSecurityInfoSerialization;
  bool mProxyConnectFailed;
  TimingStruct mTimings;
  nsTArray<uint8_t> mDataForSniffer;
};

};  // anonymous namespace

mozilla::ipc::IPCResult HttpTransactionParent::RecvOnStartRequest(
    const nsresult& aStatus, const Maybe<nsHttpResponseHead>& aResponseHead,
    const nsCString& aSecurityInfoSerialization,
    const bool& aProxyConnectFailed, const TimingStruct& aTimings,
    nsTArray<uint8_t>&& aDataForSniffer) {
  mEventQ->RunOrEnqueue(new OnStartRequestEvent(
      this, aStatus, aResponseHead, aSecurityInfoSerialization,
      aProxyConnectFailed, aTimings, std::move(aDataForSniffer)));
  return IPC_OK();
}

void HttpTransactionParent::DoOnStartRequest(
    const nsresult& aStatus, const Maybe<nsHttpResponseHead>& aResponseHead,
    const nsCString& aSecurityInfoSerialization,
    const bool& aProxyConnectFailed, const TimingStruct& aTimings,
    nsTArray<uint8_t>&& aDataForSniffer) {
  LOG(("HttpTransactionParent::DoOnStartRequest [this=%p aStatus=%" PRIx32
       "]\n",
       this, static_cast<uint32_t>(aStatus)));

  if (mOnStartRequestCalled) {
    return;
  }

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
  mTimings = aTimings;
  mDataForSniffer = std::move(aDataForSniffer);

  AutoEventEnqueuer ensureSerialDispatch(mEventQ);
  nsresult rv = mChannel->OnStartRequest(this);
  mOnStartRequestCalled = true;
  if (NS_FAILED(rv)) {
    Cancel(rv);
  }
}

namespace {

class OnTransportStatusEvent : public MainThreadChannelEvent {
 public:
  explicit OnTransportStatusEvent(HttpTransactionParent* aParent,
                                  const nsresult& aStatus,
                                  const int64_t& aProgress,
                                  const int64_t& aProgressMax)
      : mParent(aParent),
        mStatus(aStatus),
        mProgress(aProgress),
        mProgressMax(aProgressMax) {}

  void Run() override {
    LOG(("HttpTransactionParent::OnTransportStatusEvent [this=%p]\n", mParent));
    mParent->DoOnTransportStatus(mStatus, mProgress, mProgressMax);
  }

 private:
  HttpTransactionParent* mParent;
  nsresult mStatus;
  int64_t mProgress;
  int64_t mProgressMax;
};

};  // anonymous namespace

mozilla::ipc::IPCResult HttpTransactionParent::RecvOnTransportStatus(
    const nsresult& aStatus, const int64_t& aProgress,
    const int64_t& aProgressMax) {
  LOG(("HttpTransactionParent::OnTransportStatus [this=%p]\n", this));

  mEventQ->RunOrEnqueue(
      new OnTransportStatusEvent(this, aStatus, aProgress, aProgressMax));
  return IPC_OK();
}

void HttpTransactionParent::DoOnTransportStatus(const nsresult& aStatus,
                                                const int64_t& aProgress,
                                                const int64_t& aProgressMax) {
  AutoEventEnqueuer ensureSerialDispatch(mEventQ);
  mEventsink->OnTransportStatus(nullptr, aStatus, aProgress, aProgressMax);
}

namespace {

class OnDataAvailableEvent : public MainThreadChannelEvent {
 public:
  explicit OnDataAvailableEvent(HttpTransactionParent* aParent,
                                const nsCString& aData, const uint64_t& aOffset,
                                const uint32_t& aCount,
                                const bool& aDataSentToChildProcess)
      : mParent(aParent),
        mData(aData),
        mOffset(aOffset),
        mCount(aCount),
        mDataSentToChildProcess(aDataSentToChildProcess) {}

  void Run() override {
    LOG(("HttpTransactionParent::OnDataAvailableEvent [this=%p]\n", mParent));
    mParent->DoOnDataAvailable(mData, mOffset, mCount, mDataSentToChildProcess);
  }

 private:
  HttpTransactionParent* mParent;
  nsCString mData;
  uint64_t mOffset;
  uint32_t mCount;
  bool mDataSentToChildProcess;
};

};  // anonymous namespace

mozilla::ipc::IPCResult HttpTransactionParent::RecvOnDataAvailable(
    const nsCString& aData, const uint64_t& aOffset, const uint32_t& aCount,
    const bool& dataSentToChildProcess) {
  LOG(("HttpTransactionParent::RecvOnDataAvailable [this=%p, aOffset= %" PRIu64
       " aCount=%" PRIu32 " alreadySentToChild=%d]\n",
       this, aOffset, aCount, dataSentToChildProcess));

  if (mCanceled) {
    return IPC_OK();
  }

  mEventQ->RunOrEnqueue(new OnDataAvailableEvent(this, aData, aOffset, aCount,
                                                 dataSentToChildProcess));
  return IPC_OK();
}

void HttpTransactionParent::DoOnDataAvailable(
    const nsCString& aData, const uint64_t& aOffset, const uint32_t& aCount,
    const bool& dataSentToChildProcess) {
  nsCOMPtr<nsIInputStream> stringStream;
  nsresult rv = NS_NewByteInputStream(getter_AddRefs(stringStream),
                                      MakeSpan(aData.get(), aCount),
                                      NS_ASSIGNMENT_DEPEND);

  if (NS_FAILED(rv)) {
    Cancel(rv);
    return;
  }

  AutoEventEnqueuer ensureSerialDispatch(mEventQ);
  mDataAlreadySent = dataSentToChildProcess;
  rv = mChannel->OnDataAvailable(this, stringStream, aOffset, aCount);
  if (NS_FAILED(rv)) {
    Cancel(rv);
  }
}

namespace {

class OnStopRequestEvent : public MainThreadChannelEvent {
 public:
  explicit OnStopRequestEvent(
      HttpTransactionParent* aParent, const nsresult& aStatus,
      const bool& aResponseIsComplete, const int64_t& aTransferSize,
      const TimingStruct& aTimings, const nsHttpHeaderArray& aResponseTrailers,
      const bool& aHasStickyConn, const TransactionObserverResult& aResult)
      : mParent(aParent),
        mStatus(aStatus),
        mResponseIsComplete(aResponseIsComplete),
        mTransferSize(aTransferSize),
        mTimings(aTimings),
        mResponseTrailers(aResponseTrailers),
        mHasStickyConn(aHasStickyConn),
        mResult(aResult) {}

  void Run() override {
    LOG(("HttpTransactionParent::OnStopRequestEvent [this=%p]\n", mParent));
    mParent->DoOnStopRequest(mStatus, mResponseIsComplete, mTransferSize,
                             mTimings, mResponseTrailers, mHasStickyConn,
                             mResult);
  }

 private:
  HttpTransactionParent* mParent;
  nsresult mStatus;
  bool mResponseIsComplete;
  int64_t mTransferSize;
  TimingStruct mTimings;
  nsHttpHeaderArray mResponseTrailers;
  bool mHasStickyConn;
  TransactionObserverResult mResult;
};

};  // anonymous namespace

mozilla::ipc::IPCResult HttpTransactionParent::RecvOnStopRequest(
    const nsresult& aStatus, const bool& aResponseIsComplete,
    const int64_t& aTransferSize, const TimingStruct& aTimings,
    const nsHttpHeaderArray& aResponseTrailers, const bool& aHasStickyConn,
    const TransactionObserverResult& aResult) {
  LOG(("HttpTransactionParent::RecvOnStopRequest [this=%p status=%" PRIx32
       "]\n",
       this, static_cast<uint32_t>(aStatus)));
  mEventQ->RunOrEnqueue(new OnStopRequestEvent(
      this, aStatus, aResponseIsComplete, aTransferSize, aTimings,
      aResponseTrailers, aHasStickyConn, aResult));
  return IPC_OK();
}

void HttpTransactionParent::DoOnStopRequest(
    const nsresult& aStatus, const bool& aResponseIsComplete,
    const int64_t& aTransferSize, const TimingStruct& aTimings,
    const nsHttpHeaderArray& aResponseTrailers, const bool& aHasStickyConn,
    const TransactionObserverResult& aResult) {
  if (mOnStopRequestCalled) {
    return;
  }

  if (!mCanceled && NS_SUCCEEDED(mStatus)) {
    mStatus = aStatus;
  }

  nsCOMPtr<nsIRequest> deathGrip = this;

  mResponseIsComplete = aResponseIsComplete;
  mTransferSize = aTransferSize;
  mTimings = aTimings;
  mResponseTrailers = new nsHttpHeaderArray(aResponseTrailers);
  mHasStickyConnection = aHasStickyConn;

  if (mTransactionObserver) {
    mTransactionObserver(aResult.versionOk(), aResult.authOk(),
                         aResult.closeReason());
    mTransactionObserver = nullptr;
  }

  AutoEventEnqueuer ensureSerialDispatch(mEventQ);
  Unused << mChannel->OnStopRequest(this, mStatus);
  mOnStopRequestCalled = true;
  gHttpHandler->RemoveHttpChannel(mChannelId);
}

mozilla::ipc::IPCResult HttpTransactionParent::RecvOnNetAddrUpdate(
    const NetAddr& aSelfAddr, const NetAddr& aPeerAddr) {
  mSelfAddr = aSelfAddr;
  mPeerAddr = aPeerAddr;
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

namespace {

class NotifyListenerEvent : public MainThreadChannelEvent {
 public:
  explicit NotifyListenerEvent(HttpTransactionParent* aParent)
      : mParent(aParent) {}

  void Run() override {
    LOG(("HttpTransactionParent::NotifyListenerEvent [this=%p]\n", mParent));
    mParent->DoNotifyListener();
  }

 private:
  HttpTransactionParent* mParent;
};

};  // anonymous namespace

NS_IMETHODIMP
HttpTransactionParent::Cancel(nsresult aStatus) {
  MOZ_ASSERT(NS_IsMainThread());

  LOG(("HttpTransactionParent::Cancel [this=%p status=%" PRIx32 "]\n", this,
       static_cast<uint32_t>(aStatus)));

  if (mCanceled) {
    LOG(("  already canceled\n"));
    return NS_OK;
  }

  MOZ_ASSERT(NS_FAILED(aStatus), "cancel with non-failure status code");

  mCanceled = true;
  mStatus = aStatus;
  if (mIPCOpen) {
    Unused << SendCancelPump(mStatus);
  }

  // Leverage ChannelEventQueue::Suspend() to call DoNotifyListener()
  // asynchronously.
  mEventQ->Suspend();
  mEventQ->RunOrEnqueue(new NotifyListenerEvent(this));
  mEventQ->Resume();
  return NS_OK;
}

void HttpTransactionParent::DoNotifyListener() {
  MOZ_ASSERT(NS_IsMainThread());

  if (!mOnStartRequestCalled) {
    mChannel->OnStartRequest(this);
    mOnStartRequestCalled = true;
  }

  if (!mOnStopRequestCalled) {
    mChannel->OnStopRequest(this, mStatus);
    mOnStopRequestCalled = true;
  }
}

NS_IMETHODIMP
HttpTransactionParent::Suspend() {
  MOZ_ASSERT(NS_IsMainThread());

  // SendSuspend only once, when suspend goes from 0 to 1.
  if (!mSuspendCount++ && mIPCOpen) {
    Unused << SendSuspendPump();
  }
  mEventQ->Suspend();
  return NS_OK;
}

NS_IMETHODIMP
HttpTransactionParent::Resume() {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(mSuspendCount, "Resume called more than Suspend");

  // SendResume only once, when suspend count drops to 0.
  if (mSuspendCount && !--mSuspendCount && mIPCOpen) {
    Unused << SendResumePump();
  }
  mEventQ->Resume();
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
  if (aWhy != Deletion) {
    Cancel(NS_ERROR_FAILURE);
  }
}

void HttpTransactionParent::DontReuseConnection() {
  MOZ_ASSERT(NS_IsMainThread());
  Unused << SendDontReuseConnection();
}

void HttpTransactionParent::SetH2WSConnRefTaken() {
  MOZ_ASSERT(NS_IsMainThread());
  Unused << SendSetH2WSConnRefTaken();
}

HttpTransactionParent* HttpTransactionParent::AsHttpTransactionParent() {
  return this;
}

nsHttpTransaction* HttpTransactionParent::AsHttpTransaction() {
  return nullptr;
}

}  // namespace net
}  // namespace mozilla
