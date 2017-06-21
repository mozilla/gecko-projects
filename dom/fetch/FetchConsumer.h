/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_FetchConsumer_h
#define mozilla_dom_FetchConsumer_h

#include "Fetch.h"

class nsIThread;

namespace mozilla {
namespace dom {

class Promise;

namespace workers {
class WorkerPrivate;
class WorkerHolder;
}

template <class Derived> class FetchBody;

// FetchBody is not thread-safe but we need to move it around threads.
// In order to keep it alive all the time, we use a WorkerHolder, if created on
// workers, plus a this consumer.
template <class Derived>
class FetchBodyConsumer final
{
public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(FetchBodyConsumer<Derived>)

  static already_AddRefed<Promise>
  Create(nsIGlobalObject* aGlobal,
         nsIEventTarget* aMainThreadEventTarget,
         FetchBody<Derived>* aBody,
         FetchConsumeType aType,
         ErrorResult& aRv);

  void
  ReleaseObject();

  FetchBody<Derived>*
  Body() const
  {
    return mBody;
  }

  void
  BeginConsumeBodyMainThread();

  void
  ContinueConsumeBody(nsresult aStatus, uint32_t aLength, uint8_t* aResult);

  void
  ContinueConsumeBlobBody(BlobImpl* aBlobImpl);

  void
  CancelPump();

  workers::WorkerPrivate*
  GetWorkerPrivate() const
  {
    return mWorkerPrivate;
  }

private:
  FetchBodyConsumer(nsIEventTarget* aMainThreadEventTarget,
                    workers::WorkerPrivate* aWorkerPrivate,
                    FetchBody<Derived>* aBody,
                    Promise* aPromise,
                    FetchConsumeType aType);

  ~FetchBodyConsumer();

  void
  AssertIsOnTargetThread() const;

  bool
  RegisterWorkerHolder(workers::WorkerPrivate* aWorkerPrivate);

  nsCOMPtr<nsIThread> mTargetThread;
  nsCOMPtr<nsIEventTarget> mMainThreadEventTarget;
  RefPtr<FetchBody<Derived>> mBody;

  // Set when consuming the body is attempted on a worker.
  // Unset when consumption is done/aborted.
  // This WorkerHolder keeps alive the consumer via a cycle.
  UniquePtr<workers::WorkerHolder> mWorkerHolder;

  // Always set whenever the FetchBodyConsumer is created on the worker thread.
  workers::WorkerPrivate* mWorkerPrivate;

  nsMainThreadPtrHandle<nsIInputStreamPump> mConsumeBodyPump;

  // Only ever set once, always on target thread.
  FetchConsumeType mConsumeType;
  RefPtr<Promise> mConsumePromise;

#ifdef DEBUG
  bool mReadDone;
#endif
};

} // namespace dom
} // namespace mozilla

#endif // mozilla_dom_FetchConsumer_h
