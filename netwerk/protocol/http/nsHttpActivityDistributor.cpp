/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// HttpLog.h should generally be included first
#include "HttpLog.h"
#include "nsHttpActivityDistributor.h"

#include "mozilla/net/SocketProcessChild.h"
#include "nsCOMPtr.h"
#include "nsAutoPtr.h"
#include "nsIOService.h"
#include "nsThreadUtils.h"
#include "nsHttpHandler.h"
#include "nsIHttpChannel.h"

namespace mozilla {
namespace net {

typedef nsMainThreadPtrHolder<nsIHttpActivityObserver> ObserverHolder;
typedef nsMainThreadPtrHandle<nsIHttpActivityObserver> ObserverHandle;
typedef nsTArray<ObserverHandle> ObserverArray;

class nsHttpActivityEvent : public Runnable {
 public:
  nsHttpActivityEvent(nsISupports *aHttpChannel, uint32_t aActivityType,
                      uint32_t aActivitySubtype, PRTime aTimestamp,
                      uint64_t aExtraSizeData,
                      const nsACString &aExtraStringData,
                      ObserverArray *aObservers)
      : Runnable("net::nsHttpActivityEvent"),
        mHttpChannel(aHttpChannel),
        mActivityType(aActivityType),
        mActivitySubtype(aActivitySubtype),
        mTimestamp(aTimestamp),
        mExtraSizeData(aExtraSizeData),
        mExtraStringData(aExtraStringData),
        mObservers(*aObservers) {}

  NS_IMETHOD Run() override {
    for (size_t i = 0; i < mObservers.Length(); i++) {
      Unused << mObservers[i]->ObserveActivity(
          mHttpChannel, mActivityType, mActivitySubtype, mTimestamp,
          mExtraSizeData, mExtraStringData);
    }
    return NS_OK;
  }

 private:
  virtual ~nsHttpActivityEvent() = default;

  nsCOMPtr<nsISupports> mHttpChannel;
  uint32_t mActivityType;
  uint32_t mActivitySubtype;
  PRTime mTimestamp;
  uint64_t mExtraSizeData;
  nsCString mExtraStringData;

  ObserverArray mObservers;
};

NS_IMPL_ISUPPORTS(nsHttpActivityDistributor, nsIHttpActivityDistributor,
                  nsIHttpActivityObserver)

nsHttpActivityDistributor::nsHttpActivityDistributor()
    : mLock("nsHttpActivityDistributor.mLock"), mActivated(false) {}

NS_IMETHODIMP
nsHttpActivityDistributor::ObserveActivity(nsISupports *aHttpChannel,
                                           uint32_t aActivityType,
                                           uint32_t aActivitySubtype,
                                           PRTime aTimestamp,
                                           uint64_t aExtraSizeData,
                                           const nsACString &aExtraStringData) {
  MOZ_ASSERT(XRE_IsParentProcess() && NS_IsMainThread());

  for (size_t i = 0; i < mObservers.Length(); i++) {
    Unused << mObservers[i]->ObserveActivity(aHttpChannel, aActivityType,
                                             aActivitySubtype, aTimestamp,
                                             aExtraSizeData, aExtraStringData);
  }
  return NS_OK;
}

NS_IMETHODIMP
nsHttpActivityDistributor::ObserveActivityWithChannelId(
    uint64_t aChannelId, uint32_t aActivityType, uint32_t aActivitySubtype,
    PRTime aTimestamp, uint64_t aExtraSizeData,
    const nsACString &aExtraStringData) {
  nsCString extraStringData(aExtraStringData);
  if (XRE_IsSocketProcess()) {
    auto task = [aChannelId, aActivityType, aActivitySubtype, aTimestamp,
                 aExtraSizeData,
                 extraStringData{std::move(extraStringData)}]() {
      SocketProcessChild::GetSingleton()->SendObserveActivity(
          aChannelId, aActivityType, aActivitySubtype, aTimestamp,
          aExtraSizeData, extraStringData);
    };

    if (!NS_IsMainThread()) {
      return NS_DispatchToMainThread(NS_NewRunnableFunction(
          "net::nsHttpActivityDistributor::ObserveActivityWithChannelId",
          task));
    }

    task();
    return NS_OK;
  }

  MOZ_ASSERT(XRE_IsParentProcess());

  RefPtr<nsHttpActivityDistributor> self = this;
  auto task = [aChannelId, aActivityType, aActivitySubtype, aTimestamp,
               aExtraSizeData, extraStringData{std::move(extraStringData)},
               self{std::move(self)}]() {
    nsWeakPtr weakPtr = gHttpHandler->GetWeakHttpChannel(aChannelId);
    if (nsCOMPtr<nsIHttpChannel> channel = do_QueryReferent(weakPtr)) {
      Unused << self->ObserveActivity(channel, aActivityType, aActivitySubtype,
                                      aTimestamp, aExtraSizeData,
                                      extraStringData);
    }
  };

  return NS_DispatchToMainThread(NS_NewRunnableFunction(
      "net::nsHttpActivityDistributor::ObserveActivityWithChannelId", task));
}

NS_IMETHODIMP
nsHttpActivityDistributor::GetIsActive(bool* isActive) {
  NS_ENSURE_ARG_POINTER(isActive);

  if (XRE_IsSocketProcess()) {
    *isActive = mActivated;
    return NS_OK;
  }

  MutexAutoLock lock(mLock);
  *isActive = !!mObservers.Length();
  return NS_OK;
}

NS_IMETHODIMP
nsHttpActivityDistributor::AddObserver(nsIHttpActivityObserver* aObserver) {
  MutexAutoLock lock(mLock);

  ObserverHandle observer(
      new ObserverHolder("nsIHttpActivityObserver", aObserver));
  if (!mObservers.AppendElement(observer)) return NS_ERROR_OUT_OF_MEMORY;

  return NS_OK;
}

NS_IMETHODIMP
nsHttpActivityDistributor::RemoveObserver(nsIHttpActivityObserver* aObserver) {
  MutexAutoLock lock(mLock);

  ObserverHandle observer(
      new ObserverHolder("nsIHttpActivityObserver", aObserver));
  if (!mObservers.RemoveElement(observer)) return NS_ERROR_FAILURE;

  return NS_OK;
}

NS_IMETHODIMP
nsHttpActivityDistributor::SetIsActive(bool aActived) {
  MOZ_RELEASE_ASSERT(XRE_IsSocketProcess());

  mActivated = aActived;
  return NS_OK;
}

}  // namespace net
}  // namespace mozilla
