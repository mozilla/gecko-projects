/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim:set ts=4 sw=4 sts=4 et cin: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// HttpLog.h should generally be included first
#include "HttpLog.h"

#include "HttpTransactionChild.h"

#include "mozilla/ipc/IPCStreamUtils.h"
#include "mozilla/net/SocketProcessChild.h"
#include "nsHttpHandler.h"
#include "nsProxyInfo.h"
#include "mozilla/ipc/BackgroundParent.h"
#include "mozilla/net/BackgroundDataBridgeParent.h"

using namespace mozilla::net;
using mozilla::ipc::BackgroundParent;

namespace mozilla {
namespace net {

NS_IMPL_ISUPPORTS(HttpTransactionChild, nsIRequestObserver, nsIStreamListener,
                  nsITransportEventSink);

//-----------------------------------------------------------------------------
// HttpTransactionChild <public>
//-----------------------------------------------------------------------------

HttpTransactionChild::HttpTransactionChild(const uint64_t& aChannelId) {
  LOG(("Creating HttpTransactionChild @%p\n", this));
  mTransaction = new nsHttpTransaction();
  mSelfAddr.raw.family = PR_AF_UNSPEC;
  mPeerAddr.raw.family = PR_AF_UNSPEC;

  mChannelId = aChannelId;
  mStatusCodeIs200 = false;
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
    uint64_t requestContextID, uint32_t classOfService) {
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
      this, topLevelOuterContentWindowId, HttpTrafficCategory::eInvalid, rc,
      classOfService);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    mTransaction = nullptr;
  }

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
    const uint64_t& aRequestContextID, const uint32_t& aClassOfService) {
  mRequestHead = aReqHeaders;
  if (aRequestBody) {
    mUploadStream = mozilla::ipc::DeserializeIPCStream(aRequestBody);
  }
  // TODO: let parent process know about the failure
  if (NS_FAILED(InitInternal(
          aCaps, aArgs, &mRequestHead, mUploadStream, aReqContentLength,
          aReqBodyIncludesHeaders, GetCurrentThreadEventTarget(),
          aTopLevelOuterContentWindowId, aRequestContextID, aClassOfService))) {
    LOG(("HttpTransactionChild::RecvInit: [this=%p] InitInternal failed!\n",
         this));
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult HttpTransactionChild::RecvRead(
    const int32_t& priority) {
  LOG(("HttpTransactionChild::RecvRead start [this=%p]\n", this));
  MOZ_ASSERT(mTransaction, "should SentInit first");
  if (mTransaction) {
    Unused << mTransaction->AsyncRead(this, priority,
                                      getter_AddRefs(mTransactionPump));
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
  Unused << SendOnStartRequest(
      status, optionalHead, serializedSecurityInfoOut, mSelfAddr, mPeerAddr,
      mTransaction->ProxyConnectFailed(), mTransaction->Timings());
  LOG(("HttpTransactionChild::OnStartRequest end [this=%p]\n", this));
  return NS_OK;
}

NS_IMETHODIMP
HttpTransactionChild::OnStopRequest(nsIRequest* aRequest, nsresult aStatus) {
  LOG(("HttpTransactionChild::OnStopRequest [this=%p]\n", this));
  MOZ_ASSERT(mTransaction);

  nsAutoPtr<nsHttpHeaderArray> responseTrailer(
      mTransaction->TakeResponseTrailers());

  Unused << SendOnStopRequest(
      aStatus, mTransaction->ResponseIsComplete(),
      mTransaction->GetTransferSize(), mTransaction->Timings(),
      responseTrailer ? *responseTrailer : nsHttpHeaderArray());
  mTransaction = nullptr;
  mTransactionPump = nullptr;
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

  if (aStatus == NS_NET_STATUS_CONNECTED_TO ||
      aStatus == NS_NET_STATUS_WAITING_FOR) {
    if (mTransaction) {
      mTransaction->GetNetworkAddresses(mSelfAddr, mPeerAddr);
    } else {
      nsCOMPtr<nsISocketTransport> socketTransport =
          do_QueryInterface(aTransport);
      if (socketTransport) {
        socketTransport->GetSelfAddr(&mSelfAddr);
        socketTransport->GetPeerAddr(&mPeerAddr);
      }
    }
  }

  Unused << SendOnTransportStatus(aStatus, aProgress, aProgressMax);
  return NS_OK;
}

}  // namespace net
}  // namespace mozilla
