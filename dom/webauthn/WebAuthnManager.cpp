/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "hasht.h"
#include "nsICryptoHash.h"
#include "nsNetCID.h"
#include "nsThreadUtils.h"
#include "WebAuthnCoseIdentifiers.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/dom/AuthenticatorAttestationResponse.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/PWebAuthnTransaction.h"
#include "mozilla/dom/U2FUtil.h"
#include "mozilla/dom/WebAuthnCBORUtil.h"
#include "mozilla/dom/WebAuthnManager.h"
#include "mozilla/dom/WebAuthnTransactionChild.h"
#include "mozilla/dom/WebAuthnUtil.h"
#include "mozilla/ipc/BackgroundChild.h"
#include "mozilla/ipc/PBackgroundChild.h"

using namespace mozilla::ipc;

namespace mozilla {
namespace dom {

/***********************************************************************
 * Protocol Constants
 **********************************************************************/

const uint8_t FLAG_TUP = 0x01; // Test of User Presence required
const uint8_t FLAG_AT = 0x40; // Authenticator Data is provided
const uint8_t FLAG_UV = 0x04; // User was Verified (biometrics, etc.); this
                              // flag is not possible with U2F devices

/***********************************************************************
 * Statics
 **********************************************************************/

namespace {
StaticRefPtr<WebAuthnManager> gWebAuthnManager;
static mozilla::LazyLogModule gWebAuthnManagerLog("webauthnmanager");
}

NS_NAMED_LITERAL_STRING(kVisibilityChange, "visibilitychange");

NS_IMPL_ISUPPORTS(WebAuthnManager, nsIDOMEventListener);

/***********************************************************************
 * Utility Functions
 **********************************************************************/

static nsresult
AssembleClientData(const nsAString& aOrigin, const CryptoBuffer& aChallenge,
                   /* out */ nsACString& aJsonOut)
{
  MOZ_ASSERT(NS_IsMainThread());

  nsString challengeBase64;
  nsresult rv = aChallenge.ToJwkBase64(challengeBase64);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return NS_ERROR_FAILURE;
  }

  CollectedClientData clientDataObject;
  clientDataObject.mChallenge.Assign(challengeBase64);
  clientDataObject.mOrigin.Assign(aOrigin);
  clientDataObject.mHashAlgorithm.AssignLiteral(u"SHA-256");

  nsAutoString temp;
  if (NS_WARN_IF(!clientDataObject.ToJSON(temp))) {
    return NS_ERROR_FAILURE;
  }

  aJsonOut.Assign(NS_ConvertUTF16toUTF8(temp));
  return NS_OK;
}

nsresult
GetOrigin(nsPIDOMWindowInner* aParent,
          /*out*/ nsAString& aOrigin, /*out*/ nsACString& aHost)
{
  MOZ_ASSERT(aParent);
  nsCOMPtr<nsIDocument> doc = aParent->GetDoc();
  MOZ_ASSERT(doc);

  nsCOMPtr<nsIPrincipal> principal = doc->NodePrincipal();
  nsresult rv = nsContentUtils::GetUTFOrigin(principal, aOrigin);
  if (NS_WARN_IF(NS_FAILED(rv)) ||
      NS_WARN_IF(aOrigin.IsEmpty())) {
    return NS_ERROR_FAILURE;
  }

  if (aOrigin.EqualsLiteral("null")) {
    // 4.1.1.3 If callerOrigin is an opaque origin, reject promise with a
    // DOMException whose name is "NotAllowedError", and terminate this
    // algorithm
    MOZ_LOG(gWebAuthnManagerLog, LogLevel::Debug, ("Rejecting due to opaque origin"));
    return NS_ERROR_DOM_NOT_ALLOWED_ERR;
  }

  nsCOMPtr<nsIURI> originUri;
  if (NS_FAILED(principal->GetURI(getter_AddRefs(originUri)))) {
    return NS_ERROR_FAILURE;
  }
  if (NS_FAILED(originUri->GetAsciiHost(aHost))) {
    return NS_ERROR_FAILURE;
  }

  return NS_OK;
}

nsresult
RelaxSameOrigin(nsPIDOMWindowInner* aParent,
                const nsAString& aInputRpId,
                /* out */ nsACString& aRelaxedRpId)
{
  MOZ_ASSERT(aParent);
  nsCOMPtr<nsIDocument> doc = aParent->GetDoc();
  MOZ_ASSERT(doc);

  nsCOMPtr<nsIPrincipal> principal = doc->NodePrincipal();
  nsCOMPtr<nsIURI> uri;
  if (NS_FAILED(principal->GetURI(getter_AddRefs(uri)))) {
    return NS_ERROR_FAILURE;
  }
  nsAutoCString originHost;
  if (NS_FAILED(uri->GetAsciiHost(originHost))) {
    return NS_ERROR_FAILURE;
  }
  nsCOMPtr<nsIDocument> document = aParent->GetDoc();
  if (!document || !document->IsHTMLDocument()) {
    return NS_ERROR_FAILURE;
  }
  nsHTMLDocument* html = document->AsHTMLDocument();
  if (NS_WARN_IF(!html)) {
    return NS_ERROR_FAILURE;
  }

  if (!html->IsRegistrableDomainSuffixOfOrEqualTo(aInputRpId, originHost)) {
    return NS_ERROR_DOM_SECURITY_ERR;
  }

  aRelaxedRpId.Assign(NS_ConvertUTF16toUTF8(aInputRpId));
  return NS_OK;
}

static void
ListenForVisibilityEvents(nsPIDOMWindowInner* aParent,
                          WebAuthnManager* aListener)
{
  MOZ_ASSERT(aParent);
  MOZ_ASSERT(aListener);

  nsCOMPtr<nsIDocument> doc = aParent->GetExtantDoc();
  if (NS_WARN_IF(!doc)) {
    return;
  }

  nsresult rv = doc->AddSystemEventListener(kVisibilityChange, aListener,
                                            /* use capture */ true,
                                            /* wants untrusted */ false);
  Unused << NS_WARN_IF(NS_FAILED(rv));
}

static void
StopListeningForVisibilityEvents(nsPIDOMWindowInner* aParent,
                                 WebAuthnManager* aListener)
{
  MOZ_ASSERT(aParent);
  MOZ_ASSERT(aListener);

  nsCOMPtr<nsIDocument> doc = aParent->GetExtantDoc();
  if (NS_WARN_IF(!doc)) {
    return;
  }

  nsresult rv = doc->RemoveSystemEventListener(kVisibilityChange, aListener,
                                               /* use capture */ true);
  Unused << NS_WARN_IF(NS_FAILED(rv));
}

/***********************************************************************
 * WebAuthnManager Implementation
 **********************************************************************/

WebAuthnManager::WebAuthnManager()
{
  MOZ_ASSERT(NS_IsMainThread());
}

void
WebAuthnManager::ClearTransaction()
{
  if (!NS_WARN_IF(mTransaction.isNothing())) {
    StopListeningForVisibilityEvents(mTransaction.ref().mParent, this);
  }

  mTransaction.reset();
}

void
WebAuthnManager::RejectTransaction(const nsresult& aError)
{
  if (!NS_WARN_IF(mTransaction.isNothing())) {
    mTransaction.ref().mPromise->MaybeReject(aError);
  }

  ClearTransaction();
}

void
WebAuthnManager::CancelTransaction(const nsresult& aError)
{
  if (!NS_WARN_IF(!mChild || mTransaction.isNothing())) {
    mChild->SendRequestCancel(mTransaction.ref().mId);
  }

  RejectTransaction(aError);
}

WebAuthnManager::~WebAuthnManager()
{
  MOZ_ASSERT(NS_IsMainThread());

  if (mTransaction.isSome()) {
    RejectTransaction(NS_ERROR_ABORT);
  }

  if (mChild) {
    RefPtr<WebAuthnTransactionChild> c;
    mChild.swap(c);
    c->Send__delete__(c);
  }
}

bool
WebAuthnManager::MaybeCreateBackgroundActor()
{
  MOZ_ASSERT(NS_IsMainThread());

  if (mChild) {
    return true;
  }

  PBackgroundChild* actor = BackgroundChild::GetOrCreateForCurrentThread();
  if (NS_WARN_IF(!actor)) {
    return false;
  }

  RefPtr<WebAuthnTransactionChild> mgr(new WebAuthnTransactionChild());
  PWebAuthnTransactionChild* constructedMgr =
    actor->SendPWebAuthnTransactionConstructor(mgr);

  if (NS_WARN_IF(!constructedMgr)) {
    return false;
  }

  MOZ_ASSERT(constructedMgr == mgr);
  mChild = mgr.forget();

  return true;
}

//static
WebAuthnManager*
WebAuthnManager::GetOrCreate()
{
  MOZ_ASSERT(NS_IsMainThread());

  if (gWebAuthnManager) {
    return gWebAuthnManager;
  }

  gWebAuthnManager = new WebAuthnManager();
  ClearOnShutdown(&gWebAuthnManager);
  return gWebAuthnManager;
}

//static
WebAuthnManager*
WebAuthnManager::Get()
{
  MOZ_ASSERT(NS_IsMainThread());
  return gWebAuthnManager;
}

already_AddRefed<Promise>
WebAuthnManager::MakeCredential(nsPIDOMWindowInner* aParent,
                                const MakePublicKeyCredentialOptions& aOptions)
{
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aParent);

  if (mTransaction.isSome()) {
    CancelTransaction(NS_ERROR_ABORT);
  }

  nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(aParent);

  ErrorResult rv;
  RefPtr<Promise> promise = Promise::Create(global, rv);
  if (rv.Failed()) {
    return nullptr;
  }

  nsString origin;
  nsCString rpId;
  rv = GetOrigin(aParent, origin, rpId);
  if (NS_WARN_IF(rv.Failed())) {
    promise->MaybeReject(rv);
    return promise.forget();
  }

  // Enforce 4.4.3 User Account Parameters for Credential Generation
  if (aOptions.mUser.mId.WasPassed()) {
    // When we add UX, we'll want to do more with this value, but for now
    // we just have to verify its correctness.
    CryptoBuffer userId;
    userId.Assign(aOptions.mUser.mId.Value());
    if (userId.Length() > 64) {
      promise->MaybeReject(NS_ERROR_DOM_TYPE_ERR);
      return promise.forget();
    }
  }

  // If timeoutSeconds was specified, check if its value lies within a
  // reasonable range as defined by the platform and if not, correct it to the
  // closest value lying within that range.

  uint32_t adjustedTimeout = 30000;
  if (aOptions.mTimeout.WasPassed()) {
    adjustedTimeout = aOptions.mTimeout.Value();
    adjustedTimeout = std::max(15000u, adjustedTimeout);
    adjustedTimeout = std::min(120000u, adjustedTimeout);
  }

  if (aOptions.mRp.mId.WasPassed()) {
    // If rpId is specified, then invoke the procedure used for relaxing the
    // same-origin restriction by setting the document.domain attribute, using
    // rpId as the given value but without changing the current document’s
    // domain. If no errors are thrown, set rpId to the value of host as
    // computed by this procedure, and rpIdHash to the SHA-256 hash of rpId.
    // Otherwise, reject promise with a DOMException whose name is
    // "SecurityError", and terminate this algorithm.

    if (NS_FAILED(RelaxSameOrigin(aParent, aOptions.mRp.mId.Value(), rpId))) {
      promise->MaybeReject(NS_ERROR_DOM_SECURITY_ERR);
      return promise.forget();
    }
  }

  CryptoBuffer rpIdHash;
  if (!rpIdHash.SetLength(SHA256_LENGTH, fallible)) {
    promise->MaybeReject(NS_ERROR_OUT_OF_MEMORY);
    return promise.forget();
  }

  nsresult srv;
  nsCOMPtr<nsICryptoHash> hashService =
    do_CreateInstance(NS_CRYPTO_HASH_CONTRACTID, &srv);
  if (NS_WARN_IF(NS_FAILED(srv))) {
    promise->MaybeReject(NS_ERROR_DOM_SECURITY_ERR);
    return promise.forget();
  }

  srv = HashCString(hashService, rpId, rpIdHash);
  if (NS_WARN_IF(NS_FAILED(srv))) {
    promise->MaybeReject(NS_ERROR_DOM_SECURITY_ERR);
    return promise.forget();
  }


  // TODO: Move this logic into U2FTokenManager in Bug 1409220.

  // Process each element of mPubKeyCredParams using the following steps, to
  // produce a new sequence acceptableParams.
  nsTArray<PublicKeyCredentialParameters> acceptableParams;
  for (size_t a = 0; a < aOptions.mPubKeyCredParams.Length(); ++a) {
    // Let current be the currently selected element of
    // mPubKeyCredParams.

    // If current.type does not contain a PublicKeyCredentialType
    // supported by this implementation, then stop processing current and move
    // on to the next element in mPubKeyCredParams.
    if (aOptions.mPubKeyCredParams[a].mType != PublicKeyCredentialType::Public_key) {
      continue;
    }

    nsString algName;
    if (NS_FAILED(CoseAlgorithmToWebCryptoId(aOptions.mPubKeyCredParams[a].mAlg,
                                             algName))) {
      continue;
    }

    if (!acceptableParams.AppendElement(aOptions.mPubKeyCredParams[a],
                                        mozilla::fallible)){
      promise->MaybeReject(NS_ERROR_OUT_OF_MEMORY);
      return promise.forget();
    }
  }

  // If acceptableParams is empty and mPubKeyCredParams was not empty, cancel
  // the timer started in step 2, reject promise with a DOMException whose name
  // is "NotSupportedError", and terminate this algorithm.
  if (acceptableParams.IsEmpty() && !aOptions.mPubKeyCredParams.IsEmpty()) {
    promise->MaybeReject(NS_ERROR_DOM_NOT_SUPPORTED_ERR);
    return promise.forget();
  }

  // If excludeList is undefined, set it to the empty list.
  //
  // If extensions was specified, process any extensions supported by this
  // client platform, to produce the extension data that needs to be sent to the
  // authenticator. If an error is encountered while processing an extension,
  // skip that extension and do not produce any extension data for it. Call the
  // result of this processing clientExtensions.
  //
  // Currently no extensions are supported
  //
  // Use attestationChallenge, callerOrigin and rpId, along with the token
  // binding key associated with callerOrigin (if any), to create a ClientData
  // structure representing this request. Choose a hash algorithm for hashAlg
  // and compute the clientDataJSON and clientDataHash.

  CryptoBuffer challenge;
  if (!challenge.Assign(aOptions.mChallenge)) {
    promise->MaybeReject(NS_ERROR_DOM_SECURITY_ERR);
    return promise.forget();
  }

  nsAutoCString clientDataJSON;
  srv = AssembleClientData(origin, challenge, clientDataJSON);
  if (NS_WARN_IF(NS_FAILED(srv))) {
    promise->MaybeReject(NS_ERROR_DOM_SECURITY_ERR);
    return promise.forget();
  }

  CryptoBuffer clientDataHash;
  if (!clientDataHash.SetLength(SHA256_LENGTH, fallible)) {
    promise->MaybeReject(NS_ERROR_DOM_SECURITY_ERR);
    return promise.forget();
  }

  srv = HashCString(hashService, clientDataJSON, clientDataHash);
  if (NS_WARN_IF(NS_FAILED(srv))) {
    promise->MaybeReject(NS_ERROR_DOM_SECURITY_ERR);
    return promise.forget();
  }

  nsTArray<WebAuthnScopedCredentialDescriptor> excludeList;
  for (const auto& s: aOptions.mExcludeCredentials) {
    WebAuthnScopedCredentialDescriptor c;
    CryptoBuffer cb;
    cb.Assign(s.mId);
    c.id() = cb;
    excludeList.AppendElement(c);
  }

  if (!MaybeCreateBackgroundActor()) {
    promise->MaybeReject(NS_ERROR_DOM_OPERATION_ERR);
    return promise.forget();
  }

  // TODO: Add extension list building
  nsTArray<WebAuthnExtension> extensions;

  WebAuthnTransactionInfo info(rpIdHash,
                               clientDataHash,
                               adjustedTimeout,
                               excludeList,
                               extensions);

  ListenForVisibilityEvents(aParent, this);

  MOZ_ASSERT(mTransaction.isNothing());
  mTransaction = Some(WebAuthnTransaction(aParent,
                                          promise,
                                          Move(info),
                                          Move(clientDataJSON)));

  mChild->SendRequestRegister(mTransaction.ref().mId, mTransaction.ref().mInfo);
  return promise.forget();
}

already_AddRefed<Promise>
WebAuthnManager::GetAssertion(nsPIDOMWindowInner* aParent,
                              const PublicKeyCredentialRequestOptions& aOptions)
{
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aParent);

  if (mTransaction.isSome()) {
    CancelTransaction(NS_ERROR_ABORT);
  }

  nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(aParent);

  ErrorResult rv;
  RefPtr<Promise> promise = Promise::Create(global, rv);
  if (rv.Failed()) {
    return nullptr;
  }

  nsString origin;
  nsCString rpId;
  rv = GetOrigin(aParent, origin, rpId);
  if (NS_WARN_IF(rv.Failed())) {
    promise->MaybeReject(rv);
    return promise.forget();
  }

  // If timeoutSeconds was specified, check if its value lies within a
  // reasonable range as defined by the platform and if not, correct it to the
  // closest value lying within that range.

  uint32_t adjustedTimeout = 30000;
  if (aOptions.mTimeout.WasPassed()) {
    adjustedTimeout = aOptions.mTimeout.Value();
    adjustedTimeout = std::max(15000u, adjustedTimeout);
    adjustedTimeout = std::min(120000u, adjustedTimeout);
  }

  if (aOptions.mRpId.WasPassed()) {
    // If rpId is specified, then invoke the procedure used for relaxing the
    // same-origin restriction by setting the document.domain attribute, using
    // rpId as the given value but without changing the current document’s
    // domain. If no errors are thrown, set rpId to the value of host as
    // computed by this procedure, and rpIdHash to the SHA-256 hash of rpId.
    // Otherwise, reject promise with a DOMException whose name is
    // "SecurityError", and terminate this algorithm.

    if (NS_FAILED(RelaxSameOrigin(aParent, aOptions.mRpId.Value(), rpId))) {
      promise->MaybeReject(NS_ERROR_DOM_SECURITY_ERR);
      return promise.forget();
    }
  }

  CryptoBuffer rpIdHash;
  if (!rpIdHash.SetLength(SHA256_LENGTH, fallible)) {
    promise->MaybeReject(NS_ERROR_OUT_OF_MEMORY);
    return promise.forget();
  }

  nsresult srv;
  nsCOMPtr<nsICryptoHash> hashService =
    do_CreateInstance(NS_CRYPTO_HASH_CONTRACTID, &srv);
  if (NS_WARN_IF(NS_FAILED(srv))) {
    promise->MaybeReject(NS_ERROR_DOM_SECURITY_ERR);
    return promise.forget();
  }

  srv = HashCString(hashService, rpId, rpIdHash);
  if (NS_WARN_IF(NS_FAILED(srv))) {
    promise->MaybeReject(NS_ERROR_DOM_SECURITY_ERR);
    return promise.forget();
  }

  // Use assertionChallenge, callerOrigin and rpId, along with the token binding
  // key associated with callerOrigin (if any), to create a ClientData structure
  // representing this request. Choose a hash algorithm for hashAlg and compute
  // the clientDataJSON and clientDataHash.
  CryptoBuffer challenge;
  if (!challenge.Assign(aOptions.mChallenge)) {
    promise->MaybeReject(NS_ERROR_DOM_SECURITY_ERR);
    return promise.forget();
  }

  nsAutoCString clientDataJSON;
  srv = AssembleClientData(origin, challenge, clientDataJSON);
  if (NS_WARN_IF(NS_FAILED(srv))) {
    promise->MaybeReject(NS_ERROR_DOM_SECURITY_ERR);
    return promise.forget();
  }

  CryptoBuffer clientDataHash;
  if (!clientDataHash.SetLength(SHA256_LENGTH, fallible)) {
    promise->MaybeReject(NS_ERROR_DOM_SECURITY_ERR);
    return promise.forget();
  }

  srv = HashCString(hashService, clientDataJSON, clientDataHash);
  if (NS_WARN_IF(NS_FAILED(srv))) {
    promise->MaybeReject(NS_ERROR_DOM_SECURITY_ERR);
    return promise.forget();
  }

  // Note: we only support U2F-style authentication for now, so we effectively
  // require an AllowList.
  if (aOptions.mAllowCredentials.Length() < 1) {
    promise->MaybeReject(NS_ERROR_DOM_NOT_ALLOWED_ERR);
    return promise.forget();
  }

  nsTArray<WebAuthnScopedCredentialDescriptor> allowList;
  for (const auto& s: aOptions.mAllowCredentials) {
    WebAuthnScopedCredentialDescriptor c;
    CryptoBuffer cb;
    cb.Assign(s.mId);
    c.id() = cb;
    allowList.AppendElement(c);
  }

  if (!MaybeCreateBackgroundActor()) {
    promise->MaybeReject(NS_ERROR_DOM_OPERATION_ERR);
    return promise.forget();
  }

  // TODO: Add extension list building
  // If extensions was specified, process any extensions supported by this
  // client platform, to produce the extension data that needs to be sent to the
  // authenticator. If an error is encountered while processing an extension,
  // skip that extension and do not produce any extension data for it. Call the
  // result of this processing clientExtensions.
  nsTArray<WebAuthnExtension> extensions;

  WebAuthnTransactionInfo info(rpIdHash,
                               clientDataHash,
                               adjustedTimeout,
                               allowList,
                               extensions);

  ListenForVisibilityEvents(aParent, this);

  MOZ_ASSERT(mTransaction.isNothing());
  mTransaction = Some(WebAuthnTransaction(aParent,
                                          promise,
                                          Move(info),
                                          Move(clientDataJSON)));

  mChild->SendRequestSign(mTransaction.ref().mId, mTransaction.ref().mInfo);
  return promise.forget();
}

already_AddRefed<Promise>
WebAuthnManager::Store(nsPIDOMWindowInner* aParent,
                       const Credential& aCredential)
{
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aParent);

  if (mTransaction.isSome()) {
    CancelTransaction(NS_ERROR_ABORT);
  }

  nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(aParent);

  ErrorResult rv;
  RefPtr<Promise> promise = Promise::Create(global, rv);
  if (rv.Failed()) {
    return nullptr;
  }

  promise->MaybeReject(NS_ERROR_DOM_NOT_SUPPORTED_ERR);
  return promise.forget();
}

void
WebAuthnManager::FinishMakeCredential(const uint64_t& aTransactionId,
                                      nsTArray<uint8_t>& aRegBuffer)
{
  MOZ_ASSERT(NS_IsMainThread());

  // Check for a valid transaction.
  if (mTransaction.isNothing() || mTransaction.ref().mId != aTransactionId) {
    return;
  }

  CryptoBuffer regData;
  if (NS_WARN_IF(!regData.Assign(aRegBuffer.Elements(), aRegBuffer.Length()))) {
    RejectTransaction(NS_ERROR_OUT_OF_MEMORY);
    return;
  }

  mozilla::dom::CryptoBuffer aaguidBuf;
  if (NS_WARN_IF(!aaguidBuf.SetCapacity(16, mozilla::fallible))) {
    RejectTransaction(NS_ERROR_OUT_OF_MEMORY);
    return;
  }
  // TODO: Adjust the AAGUID from all zeroes in Bug 1381575 (if needed)
  // See https://github.com/w3c/webauthn/issues/506
  for (int i=0; i<16; i++) {
    aaguidBuf.AppendElement(0x00, mozilla::fallible);
  }

  // Decompose the U2F registration packet
  CryptoBuffer pubKeyBuf;
  CryptoBuffer keyHandleBuf;
  CryptoBuffer attestationCertBuf;
  CryptoBuffer signatureBuf;

  // Only handles attestation cert chains of length=1.
  nsresult rv = U2FDecomposeRegistrationResponse(regData, pubKeyBuf, keyHandleBuf,
                                                 attestationCertBuf, signatureBuf);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    RejectTransaction(rv);
    return;
  }
  MOZ_ASSERT(keyHandleBuf.Length() <= 0xFFFF);

  nsAutoString keyHandleBase64Url;
  rv = keyHandleBuf.ToJwkBase64(keyHandleBase64Url);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    RejectTransaction(rv);
    return;
  }

  CryptoBuffer clientDataBuf;
  if (!clientDataBuf.Assign(mTransaction.ref().mClientData)) {
    RejectTransaction(NS_ERROR_OUT_OF_MEMORY);
    return;
  }

  CryptoBuffer rpIdHashBuf;
  if (!rpIdHashBuf.Assign(mTransaction.ref().mInfo.RpIdHash())) {
    RejectTransaction(NS_ERROR_OUT_OF_MEMORY);
    return;
  }

  // Construct the public key object
  CryptoBuffer pubKeyObj;
  rv = CBOREncodePublicKeyObj(pubKeyBuf, pubKeyObj);
  if (NS_FAILED(rv)) {
    RejectTransaction(rv);
    return;
  }

  // During create credential, counter is always 0 for U2F
  // See https://github.com/w3c/webauthn/issues/507
  mozilla::dom::CryptoBuffer counterBuf;
  if (NS_WARN_IF(!counterBuf.SetCapacity(4, mozilla::fallible))) {
    RejectTransaction(NS_ERROR_OUT_OF_MEMORY);
    return;
  }
  counterBuf.AppendElement(0x00, mozilla::fallible);
  counterBuf.AppendElement(0x00, mozilla::fallible);
  counterBuf.AppendElement(0x00, mozilla::fallible);
  counterBuf.AppendElement(0x00, mozilla::fallible);

  // Construct the Attestation Data, which slots into the end of the
  // Authentication Data buffer.
  CryptoBuffer attDataBuf;
  rv = AssembleAttestationData(aaguidBuf, keyHandleBuf, pubKeyObj, attDataBuf);
  if (NS_FAILED(rv)) {
    RejectTransaction(rv);
    return;
  }

  mozilla::dom::CryptoBuffer authDataBuf;
  rv = AssembleAuthenticatorData(rpIdHashBuf, FLAG_TUP, counterBuf, attDataBuf,
                                 authDataBuf);
  if (NS_FAILED(rv)) {
    RejectTransaction(rv);
    return;
  }

  // The Authentication Data buffer gets CBOR-encoded with the Cert and
  // Signature to build the Attestation Object.
  CryptoBuffer attObj;
  rv = CBOREncodeAttestationObj(authDataBuf, attestationCertBuf, signatureBuf,
                                attObj);
  if (NS_FAILED(rv)) {
    RejectTransaction(rv);
    return;
  }

  // Create a new PublicKeyCredential object and populate its fields with the
  // values returned from the authenticator as well as the clientDataJSON
  // computed earlier.
  RefPtr<AuthenticatorAttestationResponse> attestation =
      new AuthenticatorAttestationResponse(mTransaction.ref().mParent);
  attestation->SetClientDataJSON(clientDataBuf);
  attestation->SetAttestationObject(attObj);

  RefPtr<PublicKeyCredential> credential =
      new PublicKeyCredential(mTransaction.ref().mParent);
  credential->SetId(keyHandleBase64Url);
  credential->SetType(NS_LITERAL_STRING("public-key"));
  credential->SetRawId(keyHandleBuf);
  credential->SetResponse(attestation);

  mTransaction.ref().mPromise->MaybeResolve(credential);
  ClearTransaction();
}

void
WebAuthnManager::FinishGetAssertion(const uint64_t& aTransactionId,
                                    nsTArray<uint8_t>& aCredentialId,
                                    nsTArray<uint8_t>& aSigBuffer)
{
  MOZ_ASSERT(NS_IsMainThread());

  // Check for a valid transaction.
  if (mTransaction.isNothing() || mTransaction.ref().mId != aTransactionId) {
    return;
  }

  CryptoBuffer tokenSignatureData;
  if (NS_WARN_IF(!tokenSignatureData.Assign(aSigBuffer.Elements(),
                                            aSigBuffer.Length()))) {
    RejectTransaction(NS_ERROR_OUT_OF_MEMORY);
    return;
  }

  CryptoBuffer clientDataBuf;
  if (!clientDataBuf.Assign(mTransaction.ref().mClientData)) {
    RejectTransaction(NS_ERROR_OUT_OF_MEMORY);
    return;
  }

  CryptoBuffer rpIdHashBuf;
  if (!rpIdHashBuf.Assign(mTransaction.ref().mInfo.RpIdHash())) {
    RejectTransaction(NS_ERROR_OUT_OF_MEMORY);
    return;
  }

  CryptoBuffer signatureBuf;
  CryptoBuffer counterBuf;
  uint8_t flags = 0;
  nsresult rv = U2FDecomposeSignResponse(tokenSignatureData, flags, counterBuf,
                                         signatureBuf);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    RejectTransaction(rv);
    return;
  }

  CryptoBuffer attestationDataBuf;
  CryptoBuffer authenticatorDataBuf;
  rv = AssembleAuthenticatorData(rpIdHashBuf, FLAG_TUP, counterBuf,
                                 /* deliberately empty */ attestationDataBuf,
                                 authenticatorDataBuf);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    RejectTransaction(rv);
    return;
  }

  CryptoBuffer credentialBuf;
  if (!credentialBuf.Assign(aCredentialId)) {
    RejectTransaction(NS_ERROR_OUT_OF_MEMORY);
    return;
  }

  nsAutoString credentialBase64Url;
  rv = credentialBuf.ToJwkBase64(credentialBase64Url);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    RejectTransaction(rv);
    return;
  }

  // If any authenticator returns success:

  // Create a new PublicKeyCredential object named value and populate its fields
  // with the values returned from the authenticator as well as the
  // clientDataJSON computed earlier.
  RefPtr<AuthenticatorAssertionResponse> assertion =
    new AuthenticatorAssertionResponse(mTransaction.ref().mParent);
  assertion->SetClientDataJSON(clientDataBuf);
  assertion->SetAuthenticatorData(authenticatorDataBuf);
  assertion->SetSignature(signatureBuf);

  RefPtr<PublicKeyCredential> credential =
    new PublicKeyCredential(mTransaction.ref().mParent);
  credential->SetId(credentialBase64Url);
  credential->SetType(NS_LITERAL_STRING("public-key"));
  credential->SetRawId(credentialBuf);
  credential->SetResponse(assertion);

  mTransaction.ref().mPromise->MaybeResolve(credential);
  ClearTransaction();
}

void
WebAuthnManager::RequestAborted(const uint64_t& aTransactionId,
                                const nsresult& aError)
{
  MOZ_ASSERT(NS_IsMainThread());

  if (mTransaction.isSome() && mTransaction.ref().mId == aTransactionId) {
    RejectTransaction(aError);
  }
}

NS_IMETHODIMP
WebAuthnManager::HandleEvent(nsIDOMEvent* aEvent)
{
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aEvent);

  nsAutoString type;
  aEvent->GetType(type);
  if (!type.Equals(kVisibilityChange)) {
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsIDocument> doc =
    do_QueryInterface(aEvent->InternalDOMEvent()->GetTarget());
  if (NS_WARN_IF(!doc)) {
    return NS_ERROR_FAILURE;
  }

  if (doc->Hidden()) {
    MOZ_LOG(gWebAuthnManagerLog, LogLevel::Debug,
            ("Visibility change: WebAuthn window is hidden, cancelling job."));

    CancelTransaction(NS_ERROR_ABORT);
  }

  return NS_OK;
}

void
WebAuthnManager::ActorDestroyed()
{
  MOZ_ASSERT(NS_IsMainThread());
  mChild = nullptr;
}

}
}
