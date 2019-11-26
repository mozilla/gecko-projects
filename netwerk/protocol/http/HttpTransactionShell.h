/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef HttpTransactionShell_h__
#define HttpTransactionShell_h__

#include "nsISupports.h"
#include "TimingStruct.h"
#include "nsInputStreamPump.h"

class nsIEventTraget;
class nsIInputStream;
class nsIInterfaceRequestor;
class nsIRequest;
class nsIRequestContext;
class nsITransportEventSink;
enum HttpTrafficCategory : uint8_t;

namespace mozilla {
namespace net {

class Http2PushedStreamWrapper;
class nsHttpConnectionInfo;
class nsHttpHeaderArray;
class nsHttpRequestHead;
class nsHttpTransaction;
class TransactionObserver;

//----------------------------------------------------------------------------
// Abstract base class for a HTTP transaction in the chrome process
//----------------------------------------------------------------------------

// 95e5a5b7-6aa2-4011-920a-0908b52f95d4
#define HTTPTRANSACTIONSHELL_IID                     \
  {                                                  \
    0x95e5a5b7, 0x6aa2, 0x4011, {                    \
      0x92, 0x0a, 0x09, 0x08, 0xb5, 0x2f, 0x95, 0xd4 \
    }                                                \
  }

class HttpTransactionShell : public nsISupports {
 public:
  NS_DECLARE_STATIC_IID_ACCESSOR(HTTPTRANSACTIONSHELL_IID)

  //
  // called to initialize the transaction
  //
  // @param caps
  //        the transaction capabilities (see nsHttp.h)
  // @param connInfo
  //        the connection type for this transaction.
  // @param reqHeaders
  //        the request header struct
  // @param reqBody
  //        the request body (POST or PUT data stream)
  // @param reqBodyIncludesHeaders
  //        fun stuff to support NPAPI plugins.
  // @param target
  //        the dispatch target were notifications should be sent.
  // @param callbacks
  //        the notification callbacks to be given to PSM.
  // @param topLevelOuterContentWindowId
  //        indicate the top level outer content window in which
  //        this transaction is being loaded.
  // @param responseBody
  //        the input stream that will contain the response data.  async
  //        wait on this input stream for data.  on first notification,
  //        headers should be available (check transaction status).
  //
  MOZ_MUST_USE nsresult virtual Init(
      uint32_t caps, nsHttpConnectionInfo* connInfo,
      nsHttpRequestHead* reqHeaders, nsIInputStream* reqBody,
      uint64_t reqContentLength, bool reqBodyIncludesHeaders,
      nsIEventTarget* consumerTarget, nsIInterfaceRequestor* callbacks,
      nsITransportEventSink* eventsink, uint64_t topLevelOuterContentWindowId,
      HttpTrafficCategory trafficCategory,
      nsIAsyncInputStream** responseBody) = 0;

  virtual void SetClassOfService(uint32_t classOfService) = 0;

  // Called to take ownership of the response headers; the transaction
  // will drop any reference to the response headers after this call.
  virtual nsHttpResponseHead* TakeResponseHead() = 0;

  // Called to take ownership of the trailer headers.
  // Returning null if there is no trailer.
  virtual nsHttpHeaderArray* TakeResponseTrailers() = 0;

  virtual nsISupports* SecurityInfo() = 0;
  virtual void SetSecurityCallbacks(nsIInterfaceRequestor* aCallbacks) = 0;

  virtual void GetNetworkAddresses(NetAddr& self, NetAddr& peer) = 0;

  // Functions for Timing interface
  virtual mozilla::TimeStamp GetDomainLookupStart() = 0;
  virtual mozilla::TimeStamp GetDomainLookupEnd() = 0;
  virtual mozilla::TimeStamp GetConnectStart() = 0;
  virtual mozilla::TimeStamp GetTcpConnectEnd() = 0;
  virtual mozilla::TimeStamp GetSecureConnectionStart() = 0;

  virtual mozilla::TimeStamp GetConnectEnd() = 0;
  virtual mozilla::TimeStamp GetRequestStart() = 0;
  virtual mozilla::TimeStamp GetResponseStart() = 0;
  virtual mozilla::TimeStamp GetResponseEnd() = 0;

  virtual void SetDomainLookupStart(mozilla::TimeStamp timeStamp,
                                    bool onlyIfNull = false) = 0;
  virtual void SetDomainLookupEnd(mozilla::TimeStamp timeStamp,
                                  bool onlyIfNull = false) = 0;

  virtual const TimingStruct Timings() = 0;

  // Called to set/find out if the transaction generated a complete response.
  virtual bool ResponseIsComplete() = 0;
  virtual int64_t GetTransferSize() = 0;
  virtual int64_t GetRequestSize() = 0;

  // Called to notify that a requested DNS cache entry was refreshed.
  virtual void SetDNSWasRefreshed() = 0;

  virtual void DontReuseConnection() = 0;
  virtual bool HasStickyConnection() const = 0;

  virtual void SetH2WSConnRefTaken() = 0;
  virtual void SetTransactionObserver(TransactionObserver* arg) = 0;
  virtual void SetRequestContext(nsIRequestContext* aRequestContext) = 0;
  virtual void SetPushedStream(Http2PushedStreamWrapper* push) = 0;

  virtual bool ProxyConnectFailed() = 0;
  virtual int32_t GetProxyConnectResponseCode() = 0;
  virtual bool ResolvedByTRR() = 0;

  virtual nsHttpTransaction* AsHttpTransaction() = 0;
};

NS_DEFINE_STATIC_IID_ACCESSOR(HttpTransactionShell, HTTPTRANSACTIONSHELL_IID)

#define NS_DECL_HTTPTRANSACTIONSHELL                                           \
  virtual nsresult Init(                                                       \
      uint32_t caps, nsHttpConnectionInfo* connInfo,                           \
      nsHttpRequestHead* reqHeaders, nsIInputStream* reqBody,                  \
      uint64_t reqContentLength, bool reqBodyIncludesHeaders,                  \
      nsIEventTarget* consumerTarget, nsIInterfaceRequestor* callbacks,        \
      nsITransportEventSink* eventsink, uint64_t topLevelOuterContentWindowId, \
      HttpTrafficCategory trafficCategory, nsIAsyncInputStream** responseBody) \
      override;                                                                \
  virtual void SetClassOfService(uint32_t classOfService) override;            \
  virtual nsHttpResponseHead* TakeResponseHead() override;                     \
  virtual nsHttpHeaderArray* TakeResponseTrailers() override;                  \
  virtual nsISupports* SecurityInfo() override;                                \
  virtual void SetSecurityCallbacks(nsIInterfaceRequestor* aCallbacks)         \
      override;                                                                \
  virtual void GetNetworkAddresses(NetAddr& self, NetAddr& peer) override;     \
  virtual mozilla::TimeStamp GetDomainLookupStart() override;                  \
  virtual mozilla::TimeStamp GetDomainLookupEnd() override;                    \
  virtual mozilla::TimeStamp GetConnectStart() override;                       \
  virtual mozilla::TimeStamp GetTcpConnectEnd() override;                      \
  virtual mozilla::TimeStamp GetSecureConnectionStart() override;              \
  virtual mozilla::TimeStamp GetConnectEnd() override;                         \
  virtual mozilla::TimeStamp GetRequestStart() override;                       \
  virtual mozilla::TimeStamp GetResponseStart() override;                      \
  virtual mozilla::TimeStamp GetResponseEnd() override;                        \
  virtual void SetDomainLookupStart(mozilla::TimeStamp timeStamp,              \
                                    bool onlyIfNull = false) override;         \
  virtual void SetDomainLookupEnd(mozilla::TimeStamp timeStamp,                \
                                  bool onlyIfNull = false) override;           \
  virtual const TimingStruct Timings() override;                               \
  virtual bool ResponseIsComplete() override;                                  \
  virtual int64_t GetTransferSize() override;                                  \
  virtual int64_t GetRequestSize() override;                                   \
  virtual void SetDNSWasRefreshed() override;                                  \
  virtual void DontReuseConnection() override;                                 \
  virtual bool HasStickyConnection() const override;                           \
  virtual void SetH2WSConnRefTaken() override;                                 \
  virtual void SetTransactionObserver(TransactionObserver* arg) override;      \
  virtual void SetRequestContext(nsIRequestContext* aRequestContext) override; \
  virtual void SetPushedStream(Http2PushedStreamWrapper* push) override;       \
  virtual bool ProxyConnectFailed() override;                                  \
  virtual int32_t GetProxyConnectResponseCode() override;                      \
  virtual bool ResolvedByTRR() override;                                       \
  virtual nsHttpTransaction* AsHttpTransaction() override;
}  // namespace net
}  // namespace mozilla

#endif  // HttpTransactionShell_h__
