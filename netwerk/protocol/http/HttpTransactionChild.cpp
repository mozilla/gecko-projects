/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim:set ts=4 sw=4 sts=4 et cin: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// HttpLog.h should generally be included first
#include "HttpLog.h"

#include "HttpTransactionChild.h"

#include "Http2PushStreamManager.h"
#include "mozilla/ipc/IPCStreamUtils.h"
#include "mozilla/net/InputChannelThrottleQueueChild.h"
#include "mozilla/net/SocketProcessChild.h"
#include "nsIHttpActivityObserver.h"
#include "nsHttpHandler.h"
#include "nsProxyInfo.h"
#include "mozilla/ipc/BackgroundParent.h"
#include "mozilla/net/BackgroundDataBridgeParent.h"
#include "nsProxyRelease.h"

using namespace mozilla::net;
using mozilla::ipc::BackgroundParent;

namespace mozilla {
namespace net {

NS_IMPL_ISUPPORTS(HttpTransactionChild, nsIRequestObserver, nsIStreamListener,
                  nsITransportEventSink, nsIThrottledInputChannel);

//-----------------------------------------------------------------------------
// HttpTransactionChild <public>
//-----------------------------------------------------------------------------

HttpTransactionChild::HttpTransactionChild(const uint64_t& aChannelId) {
  LOG(("Creating HttpTransactionChild @%p\n", this));
  mTransaction = new nsHttpTransaction();

  mChannelId = aChannelId;
  mStatusCodeIs200 = false;
  mIPCOpen = true;
  mVersionOk = false;
  mAuthOK = false;
  mTransactionCloseReason = NS_OK;
}

HttpTransactionChild::~HttpTransactionChild() {
  LOG(("Destroying HttpTransactionChild @%p\n", this));
}

static already_AddRefed<nsIRequestContext> CreateRequestContext(
    uint64_t aRequestContextID) {
  if (!aRequestContextID) {
    return nullptr;
  }

  nsIRequestContextService* rcsvc = gHttpHandler->GetRequestContextService();
  if (!rcsvc) {
    return nullptr;
  }

  nsCOMPtr<nsIRequestContext> requestContext;
  rcsvc->GetRequestContext(aRequestContextID, getter_AddRefs(requestContext));

  return requestContext.forget();
}

nsresult HttpTransactionChild::InitInternal(
    uint32_t caps, const HttpConnectionInfoCloneArgs& infoArgs,
    nsHttpRequestHead* requestHead, nsIInputStream* requestBody,
    uint64_t requestContentLength, bool requestBodyHasHeaders,
    nsIEventTarget* target, uint64_t topLevelOuterContentWindowId,
    uint8_t httpTrafficCategory, uint64_t requestContextID,
    uint32_t classOfService, uint32_t pushedStreamId,
    bool responseTimeoutEnabled, uint32_t initialRwin) {
  LOG(("HttpTransactionChild::InitInternal [this=%p caps=%x]\n", this, caps));

  nsProxyInfo *pi = nullptr, *first = nullptr, *last = nullptr;
  for (const ProxyInfoCloneArgs& info : infoArgs.proxyInfo()) {
    pi = new nsProxyInfo(info.type(), info.host(), info.port(), info.username(),
                         info.password(), info.flags(), info.timeout(),
                         info.resolveFlags());
    if (last) {
      last->mNext = pi;
    } else {
      first = pi;
    }
    last = pi;
  }

  RefPtr<nsHttpConnectionInfo> cinfo;
  if (infoArgs.routedHost().IsEmpty()) {
    cinfo = new nsHttpConnectionInfo(infoArgs.host(), infoArgs.port(),
                                     infoArgs.npnToken(), infoArgs.username(),
                                     EmptyCString(), first, infoArgs.originAttributes(),
                                     infoArgs.endToEndSSL());
  } else {
    cinfo = new nsHttpConnectionInfo(
        infoArgs.host(), infoArgs.port(), infoArgs.npnToken(),
        infoArgs.username(), EmptyCString(), first, infoArgs.originAttributes(),
        infoArgs.routedHost(), infoArgs.routedPort());
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

  nsCOMPtr<nsIRequestContext> rc = CreateRequestContext(requestContextID);
  LOG(("  CreateRequestContext this=%p id=%" PRIx64, this, requestContextID));

  nsresult rv = mTransaction->Init(
      caps, cinfo, requestHead, requestBody, requestContentLength,
      requestBodyHasHeaders, target, nullptr,  // TODO: security callback
      this, topLevelOuterContentWindowId,
      static_cast<HttpTrafficCategory>(httpTrafficCategory), rc, classOfService,
      pushedStreamId, mChannelId, responseTimeoutEnabled, initialRwin);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    mTransaction = nullptr;
  }

  if (caps & NS_HTTP_ONPUSH_LISTENER) {
    Http2PushStreamManager::GetSingleton()->RegisterOnPushCallback(
        mChannelId,
        [channelId(mChannelId)](uint32_t aStreamId, const nsACString& aUrl,
                                const nsACString& aRequestString) {
          Unused << SocketProcessChild::GetSingleton()->SendOnPushStream(
              channelId, aStreamId, nsCString(aUrl), nsCString(aRequestString));
          return NS_OK;
        });
  }

  nsMainThreadPtrHandle<HttpTransactionChild> handle(
      new nsMainThreadPtrHolder<HttpTransactionChild>("HttpTransactionChild",
                                                      this, false));
  mTransaction->SetTransactionObserver(
      [handle](bool aVersionOK, bool aAuthOK, nsresult aReason) {
        handle->mVersionOk = aVersionOK;
        handle->mAuthOK = aAuthOK;
        handle->mTransactionCloseReason = aReason;
      });

  return rv;
}

mozilla::ipc::IPCResult HttpTransactionChild::RecvCancelPump(
    const nsresult& aStatus) {
  LOG(("HttpTransactionChild::RecvCancelPump start [this=%p]\n", this));

  if (mTransactionPump) {
    mTransactionPump->Cancel(aStatus);
  }

  return IPC_OK();
}

mozilla::ipc::IPCResult HttpTransactionChild::RecvSuspendPump() {
  LOG(("HttpTransactionChild::RecvSuspendPump start [this=%p]\n", this));

  if (mTransactionPump) {
    mTransactionPump->Suspend();
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult HttpTransactionChild::RecvResumePump() {
  LOG(("HttpTransactionChild::RecvResumePump start [this=%p]\n", this));

  if (mTransactionPump) {
    mTransactionPump->Resume();
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult HttpTransactionChild::RecvInit(
    const uint32_t& aCaps, const HttpConnectionInfoCloneArgs& aArgs,
    const nsHttpRequestHead& aReqHeaders, const Maybe<IPCStream>& aRequestBody,
    const uint64_t& aReqContentLength, const bool& aReqBodyIncludesHeaders,
    const uint64_t& aTopLevelOuterContentWindowId,
    const uint8_t& aHttpTrafficCategory, const uint64_t& aRequestContextID,
    const uint32_t& aClassOfService, const uint32_t& aPushedStreamId,
    const bool& aHttpActivityDistributorActivated,
    const bool& aResponseTimeoutEnabled, const uint32_t& aInitialRwin,
    const mozilla::Maybe<PInputChannelThrottleQueueChild*>& aThrottleQueue) {
  mRequestHead = aReqHeaders;
  if (aRequestBody) {
    mUploadStream = mozilla::ipc::DeserializeIPCStream(aRequestBody);
  }

  if (nsCOMPtr<nsIHttpActivityObserver> distributor =
          services::GetActivityDistributor()) {
    distributor->SetIsActive(aHttpActivityDistributorActivated);
  }

  if (aThrottleQueue.isSome()) {
    mThrottleQueue =
        static_cast<InputChannelThrottleQueueChild*>(aThrottleQueue.ref());
  }

  // TODO: let parent process know about the failure
  if (NS_FAILED(InitInternal(
          aCaps, aArgs, &mRequestHead, mUploadStream, aReqContentLength,
          aReqBodyIncludesHeaders, GetCurrentThreadEventTarget(),
          aTopLevelOuterContentWindowId, aRequestContextID, aClassOfService,
          aPushedStreamId, aResponseTimeoutEnabled, aInitialRwin,
          aHttpTrafficCategory))) {
    LOG(("HttpTransactionChild::RecvInit: [this=%p] InitInternal failed!\n",
         this));
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult HttpTransactionChild::RecvRead() {
  LOG(("HttpTransactionChild::RecvRead start [this=%p]\n", this));
  MOZ_ASSERT(mTransaction, "should SentInit first");
  if (mTransaction) {
    Unused << mTransaction->AsyncRead(this, getter_AddRefs(mTransactionPump));
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult HttpTransactionChild::RecvReschedule(
    const int32_t& priority) {
  LOG(("HttpTransactionChild::RecvReschedule start [this=%p]\n", this));
  if (mTransaction) {
    Unused << mTransaction->AsyncReschedule(priority);
  }
  return IPC_OK();
}
mozilla::ipc::IPCResult HttpTransactionChild::RecvUpdateClassOfService(
    const uint32_t& classOfService) {
  LOG(("HttpTransactionChild::RecvUpdateClassOfService start [this=%p]\n",
       this));
  if (mTransaction) {
    mTransaction->AsyncUpdateClassOfService(classOfService);
  }
  return IPC_OK();
}
mozilla::ipc::IPCResult HttpTransactionChild::RecvCancel(
    const nsresult& reason) {
  LOG(("HttpTransactionChild::RecvCancel start [this=%p]\n", this));
  if (mTransaction) {
    Unused << mTransaction->AsyncCancel(reason);
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult HttpTransactionChild::RecvSetDNSWasRefreshed() {
  LOG(("HttpTransactionChild::SetDNSWasRefreshed [this=%p]\n", this));
  if (mTransaction) {
    mTransaction->SetDNSWasRefreshed();
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult HttpTransactionChild::RecvDontReuseConnection() {
  LOG(("HttpTransactionChild::RecvDontReuseConnection [this=%p]\n", this));
  if (mTransaction) {
    mTransaction->DontReuseConnection();
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult HttpTransactionChild::RecvSetH2WSConnRefTaken() {
  LOG(("HttpTransactionChild::RecvSetH2WSConnRefTaken [this=%p]\n", this));
  if (mTransaction) {
    mTransaction->SetH2WSConnRefTaken();
  }
  return IPC_OK();
}

void HttpTransactionChild::ActorDestroy(ActorDestroyReason aWhy) {
  LOG(("HttpTransactionChild::ActorDestroy [this=%p]\n", this));
  mIPCOpen = false;
  mTransaction = nullptr;
  mTransactionPump = nullptr;
}

nsHttpTransaction* HttpTransactionChild::GetTransaction() {
  return mTransaction.get();
}

//-----------------------------------------------------------------------------
// HttpTransactionChild <nsIStreamListener>
//-----------------------------------------------------------------------------

NS_IMETHODIMP
HttpTransactionChild::OnDataAvailable(nsIRequest* aRequest,
                                      nsIInputStream* aInputStream,
                                      uint64_t aOffset, uint32_t aCount) {
  LOG(("HttpTransactionChild::OnDataAvailable [this=%p, aOffset= %" PRIu64
       " aCount=%" PRIu32 "]\n",
       this, aOffset, aCount));
  MOZ_ASSERT(mTransaction);

  // TODO: need to determine how to send the input stream, we use nsCString for
  // now
  // TODO: send by chunks
  // TODO: think of sending this to both content (consumer) and parent (cache)
  // process, content needs to be flow-controlled
  nsCString data;
  nsresult rv = NS_ReadInputStreamToString(aInputStream, data, aCount);
  // TODO: Let parent process know
  if (NS_FAILED(rv)) {
    return rv;
  }
  bool dataSentToContentProcess = false;
  nsCOMPtr<nsIThread> bgThread =
      SocketProcessChild::GetSingleton()->mBackgroundThread;
  if (mStatusCodeIs200 && bgThread) {
    // We can only call bridge->SendOnTransportAndData on the background
    // thread at the moment. So we dispatch the runnable syncly, so we can let
    // the parent channel know not to send the data as well.
    bgThread->Dispatch(
        NS_NewRunnableFunction(
            "Bridge SendOnTransportAndData",
            [&, this]() {
              BackgroundDataBridgeParent* bridge =
                  SocketProcessChild::GetSingleton()->GetDataBridgeForChannel(
                      mChannelId);
              if (!bridge) {
                LOG(
                    ("HttpTransactionChild::OnDataAvailable no "
                     "BackgroundDataBridge found [this=%p]\n",
                     this));
                return;
              }
              LOG(("  Sending data directly to the child (len=%u)\n",
                   data.Length()));
              dataSentToContentProcess =
                  bridge->SendOnTransportAndData(aOffset, aCount, data);
            }),
        NS_DISPATCH_SYNC);
  }

  Unused << SendOnDataAvailable(data, aOffset, aCount,
                                dataSentToContentProcess);
  return NS_OK;
}

// Check ProcessXCTO in nsHttpChannel.cpp for details
bool HttpTransactionChild::IsNoSniff(nsHttpResponseHead* aResponseHead) {
  if (!aResponseHead) {
    return false;
  }

  nsAutoCString contentTypeOptionsHeader;
  Unused << aResponseHead->GetHeader(nsHttp::X_Content_Type_Options,
                                     contentTypeOptionsHeader);
  if (contentTypeOptionsHeader.IsEmpty()) {
    return false;
  }

  int32_t idx = contentTypeOptionsHeader.Find(",");
  if (idx > 0) {
    contentTypeOptionsHeader = Substring(contentTypeOptionsHeader, 0, idx);
  }

  nsHttp::TrimHTTPWhitespace(contentTypeOptionsHeader,
                             contentTypeOptionsHeader);

  return contentTypeOptionsHeader.EqualsIgnoreCase("nosniff");
}

// The maximum number of bytes to consider when attempting to sniff.
static const uint32_t MAX_BYTES_SNIFFED = 1445;

static void GetDataForSniffer(void* aClosure, const uint8_t* aData,
                              uint32_t aCount) {
  nsTArray<uint8_t>* outData = static_cast<nsTArray<uint8_t>*>(aClosure);
  outData->AppendElements(aData, std::min(aCount, MAX_BYTES_SNIFFED));
}

NS_IMETHODIMP
HttpTransactionChild::OnStartRequest(nsIRequest* aRequest) {
  LOG(("HttpTransactionChild::OnStartRequest start [this=%p]\n", this));
  MOZ_ASSERT(mTransaction);

  nsresult status;
  aRequest->GetStatus(&status);

  nsCString serializedSecurityInfoOut;
  nsCOMPtr<nsISupports> secInfoSupp = mTransaction->SecurityInfo();
  if (secInfoSupp) {
    nsCOMPtr<nsISerializable> secInfoSer = do_QueryInterface(secInfoSupp);
    if (secInfoSer) {
      NS_SerializeToString(secInfoSer, serializedSecurityInfoOut);
    }
  }

  nsAutoPtr<nsHttpResponseHead> head(mTransaction->TakeResponseHead());
  Maybe<nsHttpResponseHead> optionalHead;
  if (head) {
    mStatusCodeIs200 = head->Status() == 200;
    optionalHead = Some(*head);
  }

  nsTArray<uint8_t> dataForSniffer;
  if (mTransaction->Caps() & NS_HTTP_CALL_CONTENT_SNIFFER && !IsNoSniff(head)) {
    RefPtr<nsInputStreamPump> pump =
        static_cast<nsInputStreamPump*>(mTransactionPump.get());
    pump->PeekStream(GetDataForSniffer, &dataForSniffer);
  }

  Unused << SendOnStartRequest(status, optionalHead, serializedSecurityInfoOut,
                               mTransaction->ProxyConnectFailed(),
                               mTransaction->Timings(), dataForSniffer);
  LOG(("HttpTransactionChild::OnStartRequest end [this=%p]\n", this));
  return NS_OK;
}

NS_IMETHODIMP
HttpTransactionChild::OnStopRequest(nsIRequest* aRequest, nsresult aStatus) {
  LOG(("HttpTransactionChild::OnStopRequest [this=%p]\n", this));
  MOZ_ASSERT(mTransaction);

  nsAutoPtr<nsHttpHeaderArray> responseTrailer(
      mTransaction->TakeResponseTrailers());

  TransactionObserverResult result;
  result.versionOk() = mVersionOk;
  result.authOk() = mAuthOK;
  result.closeReason() = mTransactionCloseReason;

  Unused << SendOnStopRequest(
      aStatus, mTransaction->ResponseIsComplete(),
      mTransaction->GetTransferSize(), mTransaction->Timings(),
      responseTrailer ? *responseTrailer : nsHttpHeaderArray(),
      mTransaction->HasStickyConnection(), result);

  if (mThrottleQueue) {
    mThrottleQueue->Send__delete__(mThrottleQueue);
    mThrottleQueue = nullptr;
  }
  return NS_OK;
}

//-----------------------------------------------------------------------------
// HttpTransactionChild <nsITransportEventSink>
//-----------------------------------------------------------------------------

NS_IMETHODIMP
HttpTransactionChild::OnTransportStatus(nsITransport* aTransport,
                                        nsresult aStatus, int64_t aProgress,
                                        int64_t aProgressMax) {
  LOG(("HttpTransactionChild::OnTransportStatus [this=%p]\n", this));

  if (!mIPCOpen) {
    return NS_OK;
  }

  if (aStatus == NS_NET_STATUS_CONNECTED_TO ||
      aStatus == NS_NET_STATUS_WAITING_FOR) {
    NetAddr selfAddr;
    NetAddr peerAddr;
    if (mTransaction) {
      mTransaction->GetNetworkAddresses(selfAddr, peerAddr);
    } else {
      nsCOMPtr<nsISocketTransport> socketTransport =
          do_QueryInterface(aTransport);
      if (socketTransport) {
        socketTransport->GetSelfAddr(&selfAddr);
        socketTransport->GetPeerAddr(&peerAddr);
      }
    }
    Unused << SendOnNetAddrUpdate(selfAddr, peerAddr);
  }

  Unused << SendOnTransportStatus(aStatus, aProgress, aProgressMax);
  return NS_OK;
}

//-----------------------------------------------------------------------------
// HttpBaseChannel::nsIThrottledInputChannel
//-----------------------------------------------------------------------------

NS_IMETHODIMP
HttpTransactionChild::SetThrottleQueue(nsIInputChannelThrottleQueue* aQueue) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
HttpTransactionChild::GetThrottleQueue(nsIInputChannelThrottleQueue** aQueue) {
  *aQueue = mThrottleQueue;
  return NS_OK;
}

}  // namespace net
}  // namespace mozilla
