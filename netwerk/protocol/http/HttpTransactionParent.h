/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef HttpTransactionParent_h__
#define HttpTransactionParent_h__

#include "mozilla/net/NeckoChannelParams.h"
#include "mozilla/net/PHttpTransactionParent.h"
#include "nsHttp.h"
#include "nsCOMPtr.h"
#include "HttpTrafficAnalyzer.h"
#include "nsIThreadRetargetableRequest.h"
#include "nsITransport.h"
#include "nsIRequest.h"
#include "TimingStruct.h"

namespace mozilla {
namespace net {

class nsHttpConnectionInfo;

// HttpTransactionParent plays the role of nsHttpTransaction and delegates the
// work to the nsHttpTransport in socket process.
class HttpTransactionParent final : public PHttpTransactionParent,
                                    public nsIRequest,
                                    public nsIThreadRetargetableRequest {
 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIREQUEST
  NS_DECL_NSITHREADRETARGETABLEREQUEST

  explicit HttpTransactionParent();

  // Let socket process init the *real* nsHttpTransaction. See
  // |nsHttpTransaction::Init| for the parameters.
  MOZ_MUST_USE nsresult Init(uint32_t caps, nsHttpConnectionInfo* connInfo,
                             nsHttpRequestHead* reqHeaders,
                             nsIInputStream* reqBody, uint64_t reqContentLength,
                             bool reqBodyIncludesHeaders,
                             nsIEventTarget* consumerTarget,
                             nsIInterfaceRequestor* callbacks,
                             nsITransportEventSink* eventsink,
                             uint64_t topLevelOuterContentWindowId,
                             HttpTrafficCategory trafficCategory,
                             int32_t priority  // a workaround for bug 1485355
  );

  nsHttpResponseHead* TakeResponseHead();
  nsISupports* SecurityInfo() { return mSecurityInfo; }
  bool ProxyConnectFailed() { return mProxyConnectFailed; }

  void GetNetworkAddresses(NetAddr& self, NetAddr& peer);

  NS_IMETHODIMP OnTransportStatus(nsresult aStatus, int64_t aProgress,
                                  int64_t aProgressMax, NetAddr aSelfAddr,
                                  NetAddr aPeerAddr);

  NS_IMETHODIMP OnDataAvailable(nsIRequest* aRequest,
                                nsIInputStream* aInputStream, uint64_t aOffset,
                                uint32_t aCount);

  NS_IMETHODIMP OnStartRequest(nsresult aStatus, nsISupports* aSecurityInfo,
                               bool aProxyConnectFailed,
                               nsHttpResponseHead* aResponseHead);

  NS_IMETHODIMP OnStopRequest(nsresult aStatus, bool aResponseIsComplete,
                              int64_t aTransferSize);

  void SetDNSWasRefreshed() { mCaps &= ~NS_HTTP_REFRESH_DNS; }

  // TODO: serialize the timing. Dummy implementation for compliablity only.
  mozilla::TimeStamp GetDomainLookupStart() { return TimeStamp(); }
  mozilla::TimeStamp GetDomainLookupEnd() { return TimeStamp(); }
  mozilla::TimeStamp GetConnectStart() { return TimeStamp(); }
  mozilla::TimeStamp GetTcpConnectEnd() { return TimeStamp(); }
  mozilla::TimeStamp GetSecureConnectionStart() { return TimeStamp(); }

  mozilla::TimeStamp GetConnectEnd() { return TimeStamp(); }
  mozilla::TimeStamp GetRequestStart() { return TimeStamp(); }
  mozilla::TimeStamp GetResponseStart() { return TimeStamp(); }
  mozilla::TimeStamp GetResponseEnd() { return TimeStamp(); }

  // TODO: need to remove after we move the DNS service to the socket process
  void SetDomainLookupStart(mozilla::TimeStamp timeStamp,
                            bool onlyIfNull = false) {}
  void SetDomainLookupEnd(mozilla::TimeStamp timeStamp,
                          bool onlyIfNull = false) {}

  // TODO: we might have to use mCapsToClear trick
  uint32_t Caps() { return mCaps; }

  // TODO in this bug: get the flags from OnStopRequest
  // Called to set/find out if the transaction generated a complete response.
  bool ResponseIsComplete() { return mResponseIsComplete; }
  int64_t GetTransferSize() { return mTransferSize; }

  void ActorDestroy(ActorDestroyReason aWhy) override;
  void AddIPDLReference();

  mozilla::ipc::IPCResult RecvOnStartRequest(
      const nsresult& aStatus, const Maybe<nsHttpResponseHead>& aResponseHead,
      const nsCString& aSecurityInfoSerialization, const NetAddr& aSelfAddr,
      const NetAddr& aPeerAddr, const bool& aProxyConnectFailed);
  mozilla::ipc::IPCResult RecvOnTransportStatus(const nsresult& aStatus,
                                                const int64_t& aProgress,
                                                const int64_t& aProgressMax);
  mozilla::ipc::IPCResult RecvOnDataAvailable(const nsCString& aData,
                                              const uint64_t& aOffset,
                                              const uint32_t& aCount);
  mozilla::ipc::IPCResult RecvOnStopRequest(const nsresult& aStatus,
                                            const bool& aResponseIsComplete,
                                            const int64_t& aTransferSize);

 private:
  virtual ~HttpTransactionParent();

  void GetStructFromInfo(nsHttpConnectionInfo* aInfo,
                         HttpConnectionInfoCloneArgs& aArgs);

  nsCOMPtr<nsITransportEventSink> mEventsink;
  nsCOMPtr<nsIStreamListener> mChannel;

  nsCOMPtr<nsIEventTarget> mTargetThread;

  nsCOMPtr<nsISupports> mSecurityInfo;
  Atomic<bool, ReleaseAcquire> mResponseIsComplete;
  Atomic<int64_t, ReleaseAcquire> mTransferSize;

  nsAutoPtr<nsHttpResponseHead> mResponseHead;

  nsLoadFlags mLoadFlags = LOAD_NORMAL;
  uint32_t mCaps = 0;
  bool mProxyConnectFailed = false;
  bool mCanceled = false;
  nsresult mStatus = NS_OK;

  NetAddr mSelfAddr;
  NetAddr mPeerAddr;
};

}  // namespace net
}  // namespace mozilla

#endif  // nsHttpTransactionParent_h__
