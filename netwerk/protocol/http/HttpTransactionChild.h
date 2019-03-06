/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef HttpTransactionChild_h__
#define HttpTransactionChild_h__

#include "mozilla/net/NeckoChannelParams.h"
#include "nsHttp.h"
#include "nsCOMPtr.h"
#include "TimingStruct.h"
#include "nsIStreamListener.h"
#include "nsInputStreamPump.h"

#include "HttpTransactionParent.h"

namespace mozilla {
namespace net {

// TODO: remove after inherit ipdl
class HttpTransactionParent;

//-----------------------------------------------------------------------------
// HttpTransactionChild commutes between parent process and socket process,
// manages the real nsHttpTransaction and transaction pump.
//-----------------------------------------------------------------------------
class HttpTransactionChild final : public nsIStreamListener,
                                   public nsITransportEventSink {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSIREQUESTOBSERVER
  NS_DECL_NSISTREAMLISTENER
  NS_DECL_NSITRANSPORTEVENTSINK

  explicit HttpTransactionChild(HttpTransactionParent *);

  // Initialize the *real* nsHttpTransaction. See |nsHttpTransaction::Init| for
  // the parameters.
  MOZ_MUST_USE nsresult
  Init(uint32_t caps,
       const HttpConnectionInfoCloneArgs &aArgs,  // remove proxyInfo first
       nsHttpRequestHead *reqHeaders,
       nsIInputStream *reqBody,  // use the trick in bug 1277681
       uint64_t reqContentLength, bool reqBodyIncludesHeaders,
       nsIEventTarget *consumerTarget,  // Will remove
       uint64_t topLevelOuterContentWindowId, int32_t priority);

  NS_IMETHODIMP Cancel(nsresult aStatus);
  NS_IMETHODIMP Suspend(void);
  NS_IMETHODIMP Resume(void);

  // TODO: move to private after Bug 1485355
  RefPtr<nsHttpTransaction> mTransaction;
  RefPtr<nsInputStreamPump> mTransactionPump;

 private:
  virtual ~HttpTransactionChild();

  RefPtr<HttpTransactionParent> mParent;
};

}  // namespace net
}  // namespace mozilla

#endif  // nsHttpTransactionChild_h__
