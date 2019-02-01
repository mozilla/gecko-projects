/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef HttpTransactionParent_h__
#define HttpTransactionParent_h__

#include "mozilla/net/nsAHttpTransactionShell.h"
#include "mozilla/net/NeckoChannelParams.h"
#include "mozilla/net/PHttpTransactionParent.h"
#include "nsHttp.h"
#include "nsCOMPtr.h"
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
                                    public nsAHttpTransactionShell,
                                    public nsIRequest,
                                    public nsIThreadRetargetableRequest {
 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSAHTTPTRANSACTIONSHELL
  NS_DECL_NSIREQUEST
  NS_DECL_NSITHREADRETARGETABLEREQUEST

  explicit HttpTransactionParent();

  void ActorDestroy(ActorDestroyReason aWhy) override;
  void AddIPDLReference();

  mozilla::ipc::IPCResult RecvOnStartRequest(
      const nsresult& aStatus, const Maybe<nsHttpResponseHead>& aResponseHead,
      const nsCString& aSecurityInfoSerialization, const NetAddr& aSelfAddr,
      const NetAddr& aPeerAddr, const bool& aProxyConnectFailed,
      const TimingStruct& aTimings);
  mozilla::ipc::IPCResult RecvOnTransportStatus(const nsresult& aStatus,
                                                const int64_t& aProgress,
                                                const int64_t& aProgressMax);
  mozilla::ipc::IPCResult RecvOnDataAvailable(
      const nsCString& aData, const uint64_t& aOffset, const uint32_t& aCount,
      const bool& dataSentToChildProcess);
  mozilla::ipc::IPCResult RecvOnStopRequest(
      const nsresult& aStatus, const bool& aResponseIsComplete,
      const int64_t& aTransferSize, const TimingStruct& aTimings,
      const nsHttpHeaderArray& responseTrailers);

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
  nsAutoPtr<nsHttpHeaderArray> mResponseTrailers;

  nsLoadFlags mLoadFlags = LOAD_NORMAL;
  bool mProxyConnectFailed = false;
  bool mCanceled = false;
  nsresult mStatus = NS_OK;
  bool mDataAlreadySent = false;

  NetAddr mSelfAddr;
  NetAddr mPeerAddr;

  TimingStruct mTimings;
  bool mIPCOpen;
  bool mResponseHeadTaken;
  bool mResponseTrailersTaken;
};

}  // namespace net
}  // namespace mozilla

#endif  // nsHttpTransactionParent_h__
