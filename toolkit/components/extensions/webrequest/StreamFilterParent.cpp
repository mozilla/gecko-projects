/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "StreamFilterParent.h"

#include "mozilla/ScopeExit.h"
#include "mozilla/Unused.h"
#include "mozilla/dom/ContentParent.h"
#include "nsHttpChannel.h"
#include "nsIChannel.h"
#include "nsIHttpChannelInternal.h"
#include "nsIInputStream.h"
#include "nsITraceableChannel.h"
#include "nsProxyRelease.h"
#include "nsQueryObject.h"
#include "nsSocketTransportService2.h"
#include "nsStringStream.h"

namespace mozilla {
namespace extensions {

/*****************************************************************************
 * Initialization
 *****************************************************************************/

StreamFilterParent::StreamFilterParent()
  : mMainThread(GetCurrentThreadEventTarget())
  , mIOThread(mMainThread)
  , mBufferMutex("StreamFilter buffer mutex")
  , mReceivedStop(false)
  , mSentStop(false)
  , mContext(nullptr)
  , mOffset(0)
  , mState(State::Uninitialized)
{
}

StreamFilterParent::~StreamFilterParent()
{
  NS_ReleaseOnMainThreadSystemGroup("StreamFilterParent::mOrigListener",
                                    mOrigListener.forget());
  NS_ReleaseOnMainThreadSystemGroup("StreamFilterParent::mContext",
                                    mContext.forget());
}

bool
StreamFilterParent::Create(dom::ContentParent* aContentParent, uint64_t aChannelId, const nsAString& aAddonId,
                           Endpoint<PStreamFilterChild>* aEndpoint)
{
  AssertIsMainThread();

  auto& webreq = WebRequestService::GetSingleton();

  nsCOMPtr<nsIAtom> addonId = NS_Atomize(aAddonId);
  nsCOMPtr<nsIChannel> channel = webreq.GetTraceableChannel(aChannelId, addonId, aContentParent);

  RefPtr<nsHttpChannel> chan = do_QueryObject(channel);
  NS_ENSURE_TRUE(chan, false);

  Endpoint<PStreamFilterParent> parent;
  Endpoint<PStreamFilterChild> child;
  nsresult rv = PStreamFilter::CreateEndpoints(chan->ProcessId(),
                                               aContentParent ? aContentParent->OtherPid()
                                                              : base::GetCurrentProcId(),
                                               &parent, &child);
  NS_ENSURE_SUCCESS(rv, false);

  if (!chan->AttachStreamFilter(Move(parent))) {
    return false;
  }

  *aEndpoint = Move(child);
  return true;
}

/* static */ void
StreamFilterParent::Attach(nsIChannel* aChannel, ParentEndpoint&& aEndpoint)
{
  auto self = MakeRefPtr<StreamFilterParent>();

  self->ActorThread()->Dispatch(
    NewRunnableMethod<ParentEndpoint&&>("StreamFilterParent::Bind",
                                        self,
                                        &StreamFilterParent::Bind,
                                        Move(aEndpoint)),
    NS_DISPATCH_NORMAL);

  self->Init(aChannel);

  // IPC owns this reference now.
  Unused << self.forget();
}

void
StreamFilterParent::Bind(ParentEndpoint&& aEndpoint)
{
  aEndpoint.Bind(this);
}

void
StreamFilterParent::Init(nsIChannel* aChannel)
{
  mChannel = aChannel;

  nsCOMPtr<nsITraceableChannel> traceable = do_QueryInterface(aChannel);
  MOZ_RELEASE_ASSERT(traceable);

  nsresult rv = traceable->SetNewListener(this, getter_AddRefs(mOrigListener));
  MOZ_RELEASE_ASSERT(NS_SUCCEEDED(rv));
}

/*****************************************************************************
 * nsIThreadRetargetableStreamListener
 *****************************************************************************/

NS_IMETHODIMP
StreamFilterParent::CheckListenerChain()
{
  AssertIsMainThread();

  nsCOMPtr<nsIThreadRetargetableStreamListener> trsl =
    do_QueryInterface(mOrigListener);
  if (trsl) {
    return trsl->CheckListenerChain();
  }
  return NS_ERROR_FAILURE;
}

/*****************************************************************************
 * Error handling
 *****************************************************************************/

void
StreamFilterParent::Broken()
{
  AssertIsActorThread();

  mState = State::Disconnecting;

  RefPtr<StreamFilterParent> self(this);
  RunOnIOThread(FUNC, [=] {
    self->FlushBufferedData();

    RunOnActorThread(FUNC, [=] {
      if (self->IPCActive()) {
        self->mState = State::Disconnected;
      }
    });
  });
}

/*****************************************************************************
 * State change requests
 *****************************************************************************/

IPCResult
StreamFilterParent::RecvClose()
{
  AssertIsActorThread();

  mState = State::Closed;

  if (!mSentStop) {
    RefPtr<StreamFilterParent> self(this);
    // Make a trip through the IO thread to be sure OnStopRequest is emitted
    // after the last OnDataAvailable event.
    RunOnIOThread(FUNC, [=] {
      RunOnMainThread(FUNC, [=] {
        nsresult rv = self->EmitStopRequest(NS_OK);
        Unused << NS_WARN_IF(NS_FAILED(rv));
      });
    });
  }

  Unused << SendClosed();
  Destroy();
  return IPC_OK();
}

void
StreamFilterParent::Destroy()
{
  // Close the channel asynchronously so the actor is never destroyed before
  // this message is fully processed.
  ActorThread()->Dispatch(
    NewRunnableMethod("StreamFilterParent::Close",
                      this,
                      &StreamFilterParent::Close),
    NS_DISPATCH_NORMAL);
}

IPCResult
StreamFilterParent::RecvSuspend()
{
  AssertIsActorThread();

  if (mState == State::TransferringData) {
    RefPtr<StreamFilterParent> self(this);
    RunOnMainThread(FUNC, [=] {
      self->mChannel->Suspend();

      RunOnActorThread(FUNC, [=] {
        if (self->IPCActive()) {
          self->mState = State::Suspended;
          self->CheckResult(self->SendSuspended());
        }
      });
    });
  }
  return IPC_OK();
}

IPCResult
StreamFilterParent::RecvResume()
{
  AssertIsActorThread();

  if (mState == State::Suspended) {
    // Change state before resuming so incoming data is handled correctly
    // immediately after resuming.
    mState = State::TransferringData;

    RefPtr<StreamFilterParent> self(this);
    RunOnMainThread(FUNC, [=] {
      self->mChannel->Resume();

      RunOnActorThread(FUNC, [=] {
        if (self->IPCActive()) {
          self->CheckResult(self->SendResumed());
        }
      });
    });
  }
  return IPC_OK();
}

IPCResult
StreamFilterParent::RecvDisconnect()
{
  AssertIsActorThread();

  if (mState == State::Suspended) {
  RefPtr<StreamFilterParent> self(this);
    RunOnMainThread(FUNC, [=] {
      self->mChannel->Resume();
    });
  } else if (mState != State::TransferringData) {
    return IPC_OK();
  }

  mState = State::Disconnecting;
  CheckResult(SendFlushData());
  return IPC_OK();
}

IPCResult
StreamFilterParent::RecvFlushedData()
{
  AssertIsActorThread();

  MOZ_ASSERT(mState == State::Disconnecting);

  Destroy();

  RefPtr<StreamFilterParent> self(this);
  RunOnIOThread(FUNC, [=] {
    self->FlushBufferedData();

    RunOnActorThread(FUNC, [=] {
      self->mState = State::Disconnected;
    });
  });
  return IPC_OK();
}

/*****************************************************************************
 * Data output
 *****************************************************************************/

IPCResult
StreamFilterParent::RecvWrite(Data&& aData)
{
  AssertIsActorThread();

  if (IsIOThread()) {
    Write(aData);
  } else {
    IOThread()->Dispatch(
      NewRunnableMethod<Data&&>("StreamFilterParent::WriteMove",
                                this,
                                &StreamFilterParent::WriteMove,
                                Move(aData)),
      NS_DISPATCH_NORMAL);
  }
  return IPC_OK();
}

void
StreamFilterParent::WriteMove(Data&& aData)
{
  nsresult rv = Write(aData);
  Unused << NS_WARN_IF(NS_FAILED(rv));
}

nsresult
StreamFilterParent::Write(Data& aData)
{
  AssertIsIOThread();

  nsCOMPtr<nsIInputStream> stream;
  nsresult rv = NS_NewByteInputStream(getter_AddRefs(stream),
                                      reinterpret_cast<char*>(aData.Elements()),
                                      aData.Length());
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mOrigListener->OnDataAvailable(mChannel, mContext, stream,
                                      mOffset, aData.Length());
  NS_ENSURE_SUCCESS(rv, rv);

  mOffset += aData.Length();
  return NS_OK;
}

/*****************************************************************************
 * nsIStreamListener
 *****************************************************************************/

NS_IMETHODIMP
StreamFilterParent::OnStartRequest(nsIRequest* aRequest, nsISupports* aContext)
{
  AssertIsMainThread();

  mContext = aContext;

  if (mState != State::Disconnected) {
    RefPtr<StreamFilterParent> self(this);
    RunOnActorThread(FUNC, [=] {
      if (self->IPCActive()) {
        self->mState = State::TransferringData;
        self->CheckResult(self->SendStartRequest());
      }
    });
  }

  return mOrigListener->OnStartRequest(aRequest, aContext);
}

NS_IMETHODIMP
StreamFilterParent::OnStopRequest(nsIRequest* aRequest,
                                  nsISupports* aContext,
                                  nsresult aStatusCode)
{
  AssertIsMainThread();

  mReceivedStop = true;
  if (mState == State::Disconnected) {
    return EmitStopRequest(aStatusCode);
  }

  RefPtr<StreamFilterParent> self(this);
  RunOnActorThread(FUNC, [=] {
    if (self->IPCActive()) {
      self->CheckResult(self->SendStopRequest(aStatusCode));
    }
  });
  return NS_OK;
}

nsresult
StreamFilterParent::EmitStopRequest(nsresult aStatusCode)
{
  AssertIsMainThread();
  MOZ_ASSERT(!mSentStop);

  mSentStop = true;
  return mOrigListener->OnStopRequest(mChannel, mContext, aStatusCode);
}

/*****************************************************************************
 * Incoming data handling
 *****************************************************************************/

void
StreamFilterParent::DoSendData(Data&& aData)
{
  AssertIsActorThread();

  if (mState == State::TransferringData) {
    CheckResult(SendData(aData));
  }
}

NS_IMETHODIMP
StreamFilterParent::OnDataAvailable(nsIRequest* aRequest,
                                    nsISupports* aContext,
                                    nsIInputStream* aInputStream,
                                    uint64_t aOffset,
                                    uint32_t aCount)
{
  // Note: No AssertIsIOThread here. Whatever thread we're on now is, by
  // definition, the IO thread.
  if (OnSocketThread()) {
    mIOThread = nullptr;
  } else {
    mIOThread = NS_GetCurrentThread();
  }

  if (mState == State::Disconnected) {
    // If we're offloading data in a thread pool, it's possible that we'll
    // have buffered some additional data while waiting for the buffer to
    // flush. So, if there's any buffered data left, flush that before we
    // flush this incoming data.
    //
    // Note: When in the eDisconnected state, the buffer list is guaranteed
    // never to be accessed by another thread during an OnDataAvailable call.
    if (!mBufferedData.isEmpty()) {
      FlushBufferedData();
    }

    mOffset += aCount;
    return mOrigListener->OnDataAvailable(aRequest, aContext, aInputStream,
                                          mOffset - aCount, aCount);
  }

  Data data;
  data.SetLength(aCount);

  uint32_t count;
  nsresult rv = aInputStream->Read(reinterpret_cast<char*>(data.Elements()),
                                   aCount, &count);
  NS_ENSURE_SUCCESS(rv, rv);
  NS_ENSURE_TRUE(count == aCount, NS_ERROR_UNEXPECTED);

  if (mState == State::Disconnecting) {
    MutexAutoLock al(mBufferMutex);
    BufferData(Move(data));
  } else if (mState == State::Closed) {
    return NS_ERROR_FAILURE;
  } else {
    ActorThread()->Dispatch(
      NewRunnableMethod<Data&&>("StreamFilterParent::DoSendData",
                                this,
                                &StreamFilterParent::DoSendData,
                                Move(data)),
      NS_DISPATCH_NORMAL);
  }
  return NS_OK;
}

nsresult
StreamFilterParent::FlushBufferedData()
{
  AssertIsIOThread();

  // When offloading data to a thread pool, OnDataAvailable isn't guaranteed
  // to always run in the same thread, so it's possible for this function to
  // run in parallel with OnDataAvailable.
  MutexAutoLock al(mBufferMutex);

  while (!mBufferedData.isEmpty()) {
    UniquePtr<BufferedData> data(mBufferedData.popFirst());

    nsresult rv = Write(data->mData);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  if (mReceivedStop && !mSentStop) {
    RefPtr<StreamFilterParent> self(this);
    RunOnMainThread(FUNC, [=] {
      if (!mSentStop) {
        nsresult rv = self->EmitStopRequest(NS_OK);
        Unused << NS_WARN_IF(NS_FAILED(rv));
      }
    });
  }

  return NS_OK;
}

/*****************************************************************************
 * Thread helpers
 *****************************************************************************/

void
StreamFilterParent::AssertIsActorThread()
{
  MOZ_ASSERT(OnSocketThread());
}

nsIEventTarget*
StreamFilterParent::ActorThread()
{
  return gSocketTransportService;
}

nsIEventTarget*
StreamFilterParent::IOThread()
{
  if (mIOThread) {
    return mIOThread;
  }
  return gSocketTransportService;
}

bool
StreamFilterParent::IsIOThread()
{
  return (mIOThread ? NS_GetCurrentThread() == mIOThread
                    : OnSocketThread());
}

void
StreamFilterParent::AssertIsIOThread()
{
  MOZ_ASSERT(IsIOThread());
}

template<typename Function>
void
StreamFilterParent::RunOnActorThread(const char* aName, Function&& aFunc)
{
  if (OnSocketThread()) {
    aFunc();
  } else {
    gSocketTransportService->Dispatch(
      Move(NS_NewRunnableFunction(aName, aFunc)),
      NS_DISPATCH_NORMAL);
  }
}

template<typename Function>
void
StreamFilterParent::RunOnIOThread(const char* aName, Function&& aFunc)
{
  if (mIOThread) {
    mIOThread->Dispatch(Move(NS_NewRunnableFunction(aName, aFunc)),
                        NS_DISPATCH_NORMAL);
  } else {
    RunOnActorThread(aName, Move(aFunc));
  }
}

/*****************************************************************************
 * Glue
 *****************************************************************************/

void
StreamFilterParent::ActorDestroy(ActorDestroyReason aWhy)
{
  AssertIsActorThread();

  if (mState != State::Disconnected && mState != State::Closed) {
    Broken();
  }
}

void
StreamFilterParent::DeallocPStreamFilterParent()
{
  RefPtr<StreamFilterParent> self = dont_AddRef(this);
}

NS_IMPL_ISUPPORTS(StreamFilterParent, nsIStreamListener, nsIRequestObserver, nsIThreadRetargetableStreamListener)

} // namespace extensions
} // namespace mozilla

