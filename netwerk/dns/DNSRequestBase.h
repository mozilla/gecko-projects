/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=2 ts=8 et tw=80 : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_net_DNSRequestBase_h
#define mozilla_net_DNSRequestBase_h

#include "mozilla/net/PDNSRequestParent.h"
#include "nsICancelable.h"
#include "nsIDNSRecord.h"
#include "nsIDNSListener.h"
#include "nsIDNSByTypeRecord.h"
#include "nsIEventTarget.h"

namespace mozilla {
namespace net {

class DNSRequestActor;
class DNSRequestChild;
class DNSRequestParent;

// A base class for DNSRequestSender and DNSRequestHandler.
// Provide interfaces for processing DNS requests.
class DNSRequestBase : public nsISupports {
 public:
  explicit DNSRequestBase() = default;

  void SetIPCActor(DNSRequestActor* aActor) { mIPCActor = aActor; }

  virtual bool OnRecvCancelDNSRequest(const nsCString& hostName,
                                      const uint16_t& type,
                                      const OriginAttributes& originAttributes,
                                      const uint32_t& flags,
                                      const nsresult& reason) = 0;
  virtual bool OnRecvLookupCompleted(const DNSRequestResponse& reply) = 0;
  virtual void OnIPCActorReleased() = 0;

 protected:
  virtual ~DNSRequestBase() = default;

  RefPtr<DNSRequestActor> mIPCActor;
};

// DNSRequestSender is used to send an IPC request to DNSRequestHandler and
// deliver the result to nsIDNSListener.
// Note this class could be used both in content process and parent process.
class DNSRequestSender final : public DNSRequestBase, public nsICancelable {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSICANCELABLE

  DNSRequestSender(const nsACString& aHost, const uint16_t& aType,
                   const OriginAttributes& aOriginAttributes,
                   const uint32_t& aFlags, nsIDNSListener* aListener,
                   nsIEventTarget* target);

  bool OnRecvCancelDNSRequest(const nsCString& hostName, const uint16_t& type,
                              const OriginAttributes& originAttributes,
                              const uint32_t& flags,
                              const nsresult& reason) override;
  bool OnRecvLookupCompleted(const DNSRequestResponse& reply) override;
  void OnIPCActorReleased() override;

  // Sends IPDL request to DNSRequestHandler
  void StartRequest();
  void CallOnLookupComplete();
  void CallOnLookupByTypeComplete();

 private:
  friend class ChildDNSService;
  virtual ~DNSRequestSender() = default;

  nsCOMPtr<nsIDNSListener> mListener;
  nsCOMPtr<nsIEventTarget> mTarget;
  nsCOMPtr<nsIDNSRecord> mResultRecord;
  nsCOMPtr<nsIDNSByTypeRecord>
      mResultByTypeRecords;  // the result of a by-type
                             // query (mType must not be
                             // equal to
                             // nsIDNSService::RESOLVE_TYPE_DEFAULT
                             // (this is reserved for
                             // the standard A/AAAA query)).
  nsresult mResultStatus;
  nsCString mHost;
  uint16_t mType;
  const OriginAttributes mOriginAttributes;
  uint16_t mFlags;
};

// DNSRequestHandler handles the dns request and sends the result back via IPC.
// Note this class could be used both in parent process and socket process.
class DNSRequestHandler final : public DNSRequestBase, public nsIDNSListener {
 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIDNSLISTENER

  DNSRequestHandler();

  void DoAsyncResolve(const nsACString& hostname,
                      const OriginAttributes& originAttributes, uint32_t flags);

  bool OnRecvCancelDNSRequest(const nsCString& hostName, const uint16_t& type,
                              const OriginAttributes& originAttributes,
                              const uint32_t& flags,
                              const nsresult& reason) override;
  bool OnRecvLookupCompleted(const DNSRequestResponse& reply) override;
  void OnIPCActorReleased() override;

 private:
  virtual ~DNSRequestHandler() = default;

  uint32_t mFlags;
};

// Provides some common methods for DNSRequestChild and DNSRequestParent.
class DNSRequestActor {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(DNSRequestActor)

  explicit DNSRequestActor(DNSRequestBase* aRequest)
      : mIPCOpen(false), mDNSRequest(aRequest) {
    mDNSRequest->SetIPCActor(this);
  }

  void AddIPDLReference() {
    mIPCOpen = true;
    AddRef();
  }

  void ReleaseIPDLReference() {
    mDNSRequest->OnIPCActorReleased();
    mDNSRequest = nullptr;
    mIPCOpen = false;
    Release();
  }

  bool IPCOpen() const { return mIPCOpen; }
  void SetIPCOpen(bool aIPCOpen) { mIPCOpen = aIPCOpen; }

  virtual DNSRequestChild* AsDNSRequestChild() = 0;
  virtual DNSRequestParent* AsDNSRequestParent() = 0;

 protected:
  virtual ~DNSRequestActor() = default;

  bool mIPCOpen;
  RefPtr<DNSRequestBase> mDNSRequest;
};

}  // namespace net
}  // namespace mozilla

#endif  // mozilla_net_DNSRequestBase_h
