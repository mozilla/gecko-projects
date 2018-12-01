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

using namespace mozilla::net;

namespace mozilla {
namespace net {

NS_IMPL_ISUPPORTS(HttpTransactionChild, nsIRequestObserver, nsIStreamListener,
                  nsITransportEventSink);

//-----------------------------------------------------------------------------
// HttpTransactionChild <public>
//-----------------------------------------------------------------------------

HttpTransactionChild::HttpTransactionChild() {
  LOG(("Creating HttpTransactionChild @%p\n", this));
  mTransaction = new nsHttpTransaction();
  mSelfAddr.raw.family = PR_AF_UNSPEC;
  mPeerAddr.raw.family = PR_AF_UNSPEC;
}

HttpTransactionChild::~HttpTransactionChild() {
  LOG(("Destroying HttpTransactionChild @%p\n", this));
}

nsresult HttpTransactionChild::InitInternal(
    uint32_t caps, const HttpConnectionInfoCloneArgs& infoArgs,
    nsHttpRequestHead* requestHead, nsIInputStream* requestBody,
    uint64_t requestContentLength, bool requestBodyHasHeaders,
    nsIEventTarget* target, uint64_t topLevelOuterContentWindowId) {
  LOG(("HttpTransactionChild::InitInternal [this=%p caps=%x]\n", this, caps));

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
  nsCOMPtr<nsIRequest> request;
  rv = mTransaction->Init(
      caps, cinfo, requestHead, requestBody, requestContentLength,
      requestBodyHasHeaders, target, nullptr,  // TODO: security callback
      this, topLevelOuterContentWindowId, HttpTrafficCategory::eInvalid);
  if (NS_FAILED(rv)) {
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
    const uint64_t& aTopLevelOuterContentWindowId) {
  mRequestHead = aReqHeaders;
  if (aRequestBody) {
    mUploadStream = mozilla::ipc::DeserializeIPCStream(aRequestBody);
  }
  // TODO: let parent process know about the failure
  if (NS_FAILED(InitInternal(aCaps, aArgs, &mRequestHead, mUploadStream,
                             aReqContentLength, aReqBodyIncludesHeaders,
                             GetCurrentThreadEventTarget(),
                             aTopLevelOuterContentWindowId))) {
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
  Unused << SendOnDataAvailable(data, aOffset, aCount);
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
    optionalHead = Some(*head);
  }
  Unused << SendOnStartRequest(status, optionalHead, serializedSecurityInfoOut,
                               mSelfAddr, mPeerAddr,
                               mTransaction->ProxyConnectFailed());
  LOG(("HttpTransactionChild::OnStartRequest end [this=%p]\n", this));
  return NS_OK;
}

NS_IMETHODIMP
HttpTransactionChild::OnStopRequest(nsIRequest* aRequest, nsresult aStatus) {
  LOG(("HttpTransactionChild::OnStopRequest [this=%p]\n", this));
  MOZ_ASSERT(mTransaction);

  Unused << SendOnStopRequest(aStatus, mTransaction->ResponseIsComplete(),
                              mTransaction->GetTransferSize());
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
