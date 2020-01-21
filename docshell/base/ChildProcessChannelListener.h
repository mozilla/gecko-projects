/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_ChildProcessChannelListener_h
#define mozilla_dom_ChildProcessChannelListener_h

#include <functional>

#include "mozilla/net/NeckoChannelParams.h"
#include "nsDOMNavigationTiming.h"
#include "nsDataHashtable.h"
#include "nsIChannel.h"

namespace mozilla {
namespace dom {

class ChildProcessChannelListener final {
  NS_INLINE_DECL_REFCOUNTING(ChildProcessChannelListener)

  using Callback =
      std::function<void(nsIChannel*, nsTArray<net::DocumentChannelRedirect>&&,
                         uint32_t, nsDOMNavigationTiming*)>;

  void RegisterCallback(uint64_t aIdentifier, Callback&& aCallback);

  void OnChannelReady(nsIChannel* aChannel, uint64_t aIdentifier,
                      nsTArray<net::DocumentChannelRedirect>&& aRedirects,
                      uint32_t aLoadStateLoadFlags,
                      nsDOMNavigationTiming* aTiming);

  static already_AddRefed<ChildProcessChannelListener> GetSingleton();

 private:
  ChildProcessChannelListener() = default;
  ~ChildProcessChannelListener() = default;
  struct CallbackArgs {
    nsCOMPtr<nsIChannel> mChannel;
    nsTArray<net::DocumentChannelRedirect> mRedirects;
    uint32_t mLoadStateLoadFlags;
    RefPtr<nsDOMNavigationTiming> mTiming;
  };

  // TODO Backtrack.
  nsDataHashtable<nsUint64HashKey, Callback> mCallbacks;
  nsDataHashtable<nsUint64HashKey, CallbackArgs> mChannelArgs;
};

}  // namespace dom
}  // namespace mozilla

#endif  // !defined(mozilla_dom_ChildProcessChannelListener_h)
