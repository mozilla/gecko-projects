/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsAHttpTransactionShell_h__
#define nsAHttpTransactionShell_h__

#include "nsISupports.h"

class nsIEventTraget;
class nsIInputStream;
class nsIInterfaceRequestor;
class nsIRequest;
class nsIRequestContext;
class nsITransportEventSink;
enum HttpTrafficCategory : uint8_t;

namespace mozilla {
namespace net {

class HttpTransactionParent;
class nsHttpConnectionInfo;
class nsHttpHeaderArray;
class nsHttpRequestHead;
class nsHttpTransaction;

//----------------------------------------------------------------------------
// Abstract base class for a HTTP transaction in the chrome process
//----------------------------------------------------------------------------

// 95e5a5b7-6aa2-4011-920a-0908b52f95d4
#define NS_AHTTPTRANSACTIONSHELL_IID                 \
  {                                                  \
    0x95e5a5b7, 0x6aa2, 0x4011, {                    \
      0x92, 0x0a, 0x09, 0x08, 0xb5, 0x2f, 0x95, 0xd4 \
    }                                                \
  }

class nsAHttpTransactionShell : public nsISupports {
 public:
  NS_DECLARE_STATIC_IID_ACCESSOR(NS_AHTTPTRANSACTIONSHELL_IID)

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

  MOZ_MUST_USE virtual nsresult Init(
      uint32_t caps, nsHttpConnectionInfo *connInfo,
      nsHttpRequestHead *reqHeaders, nsIInputStream *reqBody,
      uint64_t reqContentLength, bool reqBodyIncludesHeaders,
      nsIEventTarget *consumerTarget, nsIInterfaceRequestor *callbacks,
      nsITransportEventSink *eventsink, uint64_t topLevelOuterContentWindowId,
      HttpTrafficCategory trafficCategory, nsIRequestContext *requestContext,
      uint32_t classOfService) = 0;

  // @param aListener
  //        receives notifications.
  // @param pump
  //        the pump that will contain the response data. async wait on this
  //        input stream for data. On first notification, headers should be
  //        available (check transaction status).
  virtual nsresult AsyncRead(nsIStreamListener *listener,
                             nsIRequest **pump) = 0;
  virtual nsresult AsyncReschedule(int32_t priority) = 0;
  virtual void AsyncUpdateClassOfService(uint32_t classOfService) = 0;
  virtual nsresult AsyncCancel(nsresult reason) = 0;

  // Called to take ownership of the response headers; the transaction
  // will drop any reference to the response headers after this call.
  virtual nsHttpResponseHead *TakeResponseHead() = 0;

  virtual nsISupports *SecurityInfo() = 0;
  virtual bool ProxyConnectFailed() = 0;

  virtual void GetNetworkAddresses(NetAddr &self, NetAddr &peer) = 0;

  virtual mozilla::TimeStamp GetDomainLookupStart() = 0;
  virtual mozilla::TimeStamp GetDomainLookupEnd() = 0;
  virtual mozilla::TimeStamp GetConnectStart() = 0;
  virtual mozilla::TimeStamp GetTcpConnectEnd() = 0;
  virtual mozilla::TimeStamp GetSecureConnectionStart() = 0;

  virtual mozilla::TimeStamp GetConnectEnd() = 0;
  virtual mozilla::TimeStamp GetRequestStart() = 0;
  virtual mozilla::TimeStamp GetResponseStart() = 0;
  virtual mozilla::TimeStamp GetResponseEnd() = 0;

  virtual bool HasStickyConnection() = 0;

  // Called to set/find out if the transaction generated a complete response.
  virtual bool ResponseIsComplete() = 0;
  virtual int64_t GetTransferSize() = 0;
  virtual bool DataAlreadySent() = 0;

  // Called to notify that a requested DNS cache entry was refreshed.
  virtual void SetDNSWasRefreshed() = 0;

  // Called to take ownership of the trailer headers.
  // Returning null if there is no trailer.
  virtual nsHttpHeaderArray *TakeResponseTrailers() = 0;

  virtual void DontReuseConnection() = 0;
  virtual void SetH2WSConnRefTaken() = 0;

  virtual HttpTransactionParent *AsHttpTransactionParent() = 0;
  virtual nsHttpTransaction *AsHttpTransaction() = 0;
};

NS_DEFINE_STATIC_IID_ACCESSOR(nsAHttpTransactionShell,
                              NS_AHTTPTRANSACTIONSHELL_IID)

#define NS_DECL_NSAHTTPTRANSACTIONSHELL                                        \
  virtual nsresult Init(                                                       \
      uint32_t caps, nsHttpConnectionInfo *connInfo,                           \
      nsHttpRequestHead *reqHeaders, nsIInputStream *reqBody,                  \
      uint64_t reqContentLength, bool reqBodyIncludesHeaders,                  \
      nsIEventTarget *consumerTarget, nsIInterfaceRequestor *callbacks,        \
      nsITransportEventSink *eventsink, uint64_t topLevelOuterContentWindowId, \
      HttpTrafficCategory trafficCategory, nsIRequestContext *requestContext,  \
      uint32_t classOfService) override;                                       \
  virtual nsresult AsyncRead(nsIStreamListener *listener, nsIRequest **pump)   \
      override;                                                                \
  virtual nsresult AsyncReschedule(int32_t priority) override;                 \
  virtual void AsyncUpdateClassOfService(uint32_t classOfService) override;    \
  virtual nsresult AsyncCancel(nsresult reason) override;                      \
  virtual nsHttpResponseHead *TakeResponseHead() override;                     \
  virtual nsISupports *SecurityInfo() override;                                \
  virtual bool ProxyConnectFailed() override;                                  \
  virtual void GetNetworkAddresses(NetAddr &self, NetAddr &peer) override;     \
  virtual mozilla::TimeStamp GetDomainLookupStart() override;                  \
  virtual mozilla::TimeStamp GetDomainLookupEnd() override;                    \
  virtual mozilla::TimeStamp GetConnectStart() override;                       \
  virtual mozilla::TimeStamp GetTcpConnectEnd() override;                      \
  virtual mozilla::TimeStamp GetSecureConnectionStart() override;              \
  virtual mozilla::TimeStamp GetConnectEnd() override;                         \
  virtual mozilla::TimeStamp GetRequestStart() override;                       \
  virtual mozilla::TimeStamp GetResponseStart() override;                      \
  virtual mozilla::TimeStamp GetResponseEnd() override;                        \
  virtual bool HasStickyConnection() override;                                 \
  virtual bool ResponseIsComplete() override;                                  \
  virtual bool DataAlreadySent() override;                                     \
  virtual int64_t GetTransferSize() override;                                  \
  virtual void SetDNSWasRefreshed() override;                                  \
  virtual nsHttpHeaderArray *TakeResponseTrailers() override;                  \
  virtual void DontReuseConnection() override;                                 \
  virtual void SetH2WSConnRefTaken() override;                                 \
  virtual HttpTransactionParent *AsHttpTransactionParent() override;           \
  virtual nsHttpTransaction *AsHttpTransaction() override;
}  // namespace net
}  // namespace mozilla

#endif  // nsAHttpTransactionShell_h__
