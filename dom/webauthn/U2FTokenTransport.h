/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_U2FTokenTransport_h
#define mozilla_dom_U2FTokenTransport_h

#include "mozilla/dom/PWebAuthnTransaction.h"

/*
 * Abstract class representing a transport manager for U2F Keys (software,
 * bluetooth, usb, etc.). Hides the implementation details for specific key
 * transport types.
 */

namespace mozilla {
namespace dom {

typedef MozPromise<WebAuthnMakeCredentialResult, nsresult, true> U2FRegisterPromise;
typedef MozPromise<WebAuthnGetAssertionResult, nsresult, true> U2FSignPromise;

class U2FTokenTransport
{
public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(U2FTokenTransport);
  U2FTokenTransport() {}

  virtual RefPtr<U2FRegisterPromise>
  Register(const nsTArray<WebAuthnScopedCredential>& aCredentials,
           const WebAuthnAuthenticatorSelection &aAuthenticatorSelection,
           const nsTArray<uint8_t>& aApplication,
           const nsTArray<uint8_t>& aChallenge,
           uint32_t aTimeoutMS) = 0;

  virtual RefPtr<U2FSignPromise>
  Sign(const nsTArray<WebAuthnScopedCredential>& aCredentials,
       const nsTArray<uint8_t>& aApplication,
       const nsTArray<uint8_t>& aChallenge,
       bool aRequireUserVerification,
       uint32_t aTimeoutMS) = 0;

  virtual void Cancel() = 0;

protected:
  virtual ~U2FTokenTransport() = default;
};

} // namespace dom
} // namespace mozilla

#endif // mozilla_dom_U2FTokenTransport_h
