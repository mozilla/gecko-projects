/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/PaymentRequest.h"
#include "mozilla/dom/PaymentResponse.h"
#include "nsContentUtils.h"
#include "PaymentRequestManager.h"

namespace mozilla {
namespace dom {

NS_IMPL_CYCLE_COLLECTION_CLASS(PaymentRequest)

NS_IMPL_CYCLE_COLLECTION_TRACE_BEGIN_INHERITED(PaymentRequest,
                                               DOMEventTargetHelper)
  // Don't need NS_IMPL_CYCLE_COLLECTION_TRACE_PRESERVED_WRAPPER because
  // DOMEventTargetHelper does it for us.
NS_IMPL_CYCLE_COLLECTION_TRACE_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(PaymentRequest,
                                                  DOMEventTargetHelper)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mResultPromise)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mAcceptPromise)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mAbortPromise)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mResponse)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mShippingAddress)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(PaymentRequest,
                                                DOMEventTargetHelper)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mResultPromise)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mAcceptPromise)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mAbortPromise)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mResponse)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mShippingAddress)
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION_INHERITED(PaymentRequest)
NS_INTERFACE_MAP_END_INHERITING(DOMEventTargetHelper)

NS_IMPL_ADDREF_INHERITED(PaymentRequest, DOMEventTargetHelper)
NS_IMPL_RELEASE_INHERITED(PaymentRequest, DOMEventTargetHelper)

bool
PaymentRequest::PrefEnabled(JSContext* aCx, JSObject* aObj)
{
  return Preferences::GetBool("dom.payments.request.enabled");
}

bool
PaymentRequest::IsValidMethodData(const Sequence<PaymentMethodData>& aMethodData,
                                  nsAString& aErrorMsg)
{
  if (!aMethodData.Length()) {
    aErrorMsg.AssignLiteral("At least one payment method is required.");
    return false;
  }

  for (const PaymentMethodData& methodData : aMethodData) {
    if (!methodData.mSupportedMethods.Length()) {
      aErrorMsg.AssignLiteral(
        "At least one payment method identifier is required.");
      return false;
    }
  }

  return true;
}

bool
PaymentRequest::IsValidNumber(const nsAString& aItem,
                              const nsAString& aStr,
                              nsAString& aErrorMsg)
{
  nsresult error = NS_ERROR_FAILURE;

  if (!aStr.IsEmpty()) {
    nsAutoString aValue(aStr);

    // If the beginning character is '-', we will check the second one.
    int beginningIndex = (aValue.First() == '-') ? 1 : 0;

    // Ensure
    // - the beginning character is a digit in [0-9], and
    // - the last character is not '.'
    // to follow spec:
    //   https://w3c.github.io/browser-payment-api/#dfn-valid-decimal-monetary-value
    //
    // For example, ".1" is not valid for '.' is not in [0-9],
    // and " 0.1" either for beginning with ' '
    if (aValue.Last() != '.' &&
        aValue.CharAt(beginningIndex) >= '0' &&
        aValue.CharAt(beginningIndex) <= '9') {
      aValue.ToFloat(&error);
    }
  }

  if (NS_FAILED(error)) {
    aErrorMsg.AssignLiteral("The amount.value of \"");
    aErrorMsg.Append(aItem);
    aErrorMsg.AppendLiteral("\"(");
    aErrorMsg.Append(aStr);
    aErrorMsg.AppendLiteral(") must be a valid decimal monetary value.");
    return false;
  }
  return true;
}

bool
PaymentRequest::IsNonNegativeNumber(const nsAString& aItem,
                                    const nsAString& aStr,
                                    nsAString& aErrorMsg)
{
  nsresult error = NS_ERROR_FAILURE;

  if (!aStr.IsEmpty()) {
    nsAutoString aValue(aStr);
    // Ensure
    // - the beginning character is a digit in [0-9], and
    // - the last character is not '.'
    if (aValue.Last() != '.' &&
        aValue.First() >= '0' &&
        aValue.First() <= '9') {
      aValue.ToFloat(&error);
    }
  }

  if (NS_FAILED(error)) {
    aErrorMsg.AssignLiteral("The amount.value of \"");
    aErrorMsg.Append(aItem);
    aErrorMsg.AppendLiteral("\"(");
    aErrorMsg.Append(aStr);
    aErrorMsg.AppendLiteral(") must be a valid and non-negative decimal monetaryvalue.");
    return false;
  }
  return true;
}

bool
PaymentRequest::IsValidDetailsInit(const PaymentDetailsInit& aDetails, nsAString& aErrorMsg)
{
  // Check the amount.value of detail.total
  if (!IsNonNegativeNumber(NS_LITERAL_STRING("details.total"),
                           aDetails.mTotal.mAmount.mValue, aErrorMsg)) {
    return false;
  }

  return IsValidDetailsBase(aDetails, aErrorMsg);
}

bool
PaymentRequest::IsValidDetailsBase(const PaymentDetailsBase& aDetails, nsAString& aErrorMsg)
{
  // Check the amount.value of each item in the display items
  if (aDetails.mDisplayItems.WasPassed()) {
    const Sequence<PaymentItem>& displayItems = aDetails.mDisplayItems.Value();
    for (const PaymentItem& displayItem : displayItems) {
      if (!IsValidNumber(displayItem.mLabel,
                         displayItem.mAmount.mValue, aErrorMsg)) {
        return false;
      }
    }
  }

  // Check the shipping option
  if (aDetails.mShippingOptions.WasPassed()) {
    const Sequence<PaymentShippingOption>& shippingOptions = aDetails.mShippingOptions.Value();
    for (const PaymentShippingOption& shippingOption : shippingOptions) {
      if (!IsValidNumber(NS_LITERAL_STRING("details.shippingOptions"),
                         shippingOption.mAmount.mValue, aErrorMsg)) {
        return false;
      }
    }
  }

  // Check payment details modifiers
  if (aDetails.mModifiers.WasPassed()) {
    const Sequence<PaymentDetailsModifier>& modifiers = aDetails.mModifiers.Value();
    for (const PaymentDetailsModifier& modifier : modifiers) {
      if (!IsNonNegativeNumber(NS_LITERAL_STRING("details.modifiers.total"),
                               modifier.mTotal.mAmount.mValue, aErrorMsg)) {
        return false;
      }
      if (modifier.mAdditionalDisplayItems.WasPassed()) {
        const Sequence<PaymentItem>& displayItems = modifier.mAdditionalDisplayItems.Value();
        for (const PaymentItem& displayItem : displayItems) {
          if (!IsValidNumber(displayItem.mLabel,
                             displayItem.mAmount.mValue, aErrorMsg)) {
            return false;
          }
        }
      }
    }
  }

  return true;
}

already_AddRefed<PaymentRequest>
PaymentRequest::Constructor(const GlobalObject& aGlobal,
                            const Sequence<PaymentMethodData>& aMethodData,
                            const PaymentDetailsInit& aDetails,
                            const PaymentOptions& aOptions,
                            ErrorResult& aRv)
{
  nsCOMPtr<nsPIDOMWindowInner> window = do_QueryInterface(aGlobal.GetAsSupports());
  if (!window) {
    aRv.Throw(NS_ERROR_UNEXPECTED);
    return nullptr;
  }

  // [TODO] Bug 1318988 - Implement `allowPaymentRequest` on iframe

  // Check payment methods and details
  nsAutoString message;
  if (!IsValidMethodData(aMethodData, message) ||
      !IsValidDetailsInit(aDetails, message)) {
    aRv.ThrowTypeError<MSG_ILLEGAL_PR_CONSTRUCTOR>(message);
    return nullptr;
  }

  RefPtr<PaymentRequestManager> manager = PaymentRequestManager::GetSingleton();
  if (NS_WARN_IF(!manager)) {
    return nullptr;
  }

  // Create PaymentRequest and set its |mId|
  RefPtr<PaymentRequest> request;
  nsresult rv = manager->CreatePayment(window, aMethodData, aDetails,
                                       aOptions, getter_AddRefs(request));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    aRv.Throw(NS_ERROR_DOM_TYPE_ERR);
    return nullptr;
  }

  return request.forget();
}

already_AddRefed<PaymentRequest>
PaymentRequest::CreatePaymentRequest(nsPIDOMWindowInner* aWindow, nsresult& aRv)
{
  // Generate a unique id for identification
  nsID uuid;
  aRv = nsContentUtils::GenerateUUIDInPlace(uuid);
  if (NS_WARN_IF(NS_FAILED(aRv))) {
    return nullptr;
  }
  char buffer[NSID_LENGTH];
  uuid.ToProvidedString(buffer);
  nsAutoString id;
  CopyASCIItoUTF16(buffer, id);

  RefPtr<PaymentRequest> request = new PaymentRequest(aWindow, id);
  return request.forget();
}

PaymentRequest::PaymentRequest(nsPIDOMWindowInner* aWindow, const nsAString& aInternalId)
  : DOMEventTargetHelper(aWindow)
  , mInternalId(aInternalId)
  , mShippingAddress(nullptr)
  , mUpdating(false)
  , mState(eCreated)
{
  MOZ_ASSERT(aWindow);
}

already_AddRefed<Promise>
PaymentRequest::CanMakePayment(ErrorResult& aRv)
{
  if (mState != eCreated) {
    aRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
    return nullptr;
  }

  if (mResultPromise) {
    aRv.Throw(NS_ERROR_DOM_NOT_ALLOWED_ERR);
    return nullptr;
  }

  nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(GetOwner());
  ErrorResult result;
  RefPtr<Promise> promise = Promise::Create(global, result);
  if (result.Failed()) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  RefPtr<PaymentRequestManager> manager = PaymentRequestManager::GetSingleton();
  if (NS_WARN_IF(!manager)) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }
  nsresult rv = manager->CanMakePayment(mInternalId);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    promise->MaybeReject(NS_ERROR_FAILURE);
    return promise.forget();
  }
  mResultPromise = promise;
  return promise.forget();
}

void
PaymentRequest::RespondCanMakePayment(bool aResult)
{
  MOZ_ASSERT(mResultPromise);
  mResultPromise->MaybeResolve(aResult);
  mResultPromise = nullptr;
}

already_AddRefed<Promise>
PaymentRequest::Show(ErrorResult& aRv)
{
  if (mState != eCreated) {
    aRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
    return nullptr;
  }

  nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(GetOwner());
  ErrorResult result;
  RefPtr<Promise> promise = Promise::Create(global, result);
  if (result.Failed()) {
    mState = eClosed;
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  RefPtr<PaymentRequestManager> manager = PaymentRequestManager::GetSingleton();
  if (NS_WARN_IF(!manager)) {
    mState = eClosed;
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }
  nsresult rv = manager->ShowPayment(mInternalId);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    promise->MaybeReject(NS_ERROR_FAILURE);
    mState = eClosed;
    return promise.forget();
  }

  mAcceptPromise = promise;
  mState = eInteractive;
  return promise.forget();
}

void
PaymentRequest::RejectShowPayment(nsresult aRejectReason)
{
  MOZ_ASSERT(mAcceptPromise);
  MOZ_ASSERT(ReadyForUpdate());

  mAcceptPromise->MaybeReject(aRejectReason);
  mState = eClosed;
  mAcceptPromise = nullptr;
}

void
PaymentRequest::RespondShowPayment(bool aAccept,
                                   const nsAString& aMethodName,
                                   const nsAString& aDetails,
                                   const nsAString& aPayerName,
                                   const nsAString& aPayerEmail,
                                   const nsAString& aPayerPhone,
                                   nsresult aRv)
{
  MOZ_ASSERT(mAcceptPromise);
  MOZ_ASSERT(ReadyForUpdate());
  MOZ_ASSERT(mState == eInteractive);

  if (!aAccept) {
    RejectShowPayment(aRv);
    return;
  }

  RefPtr<PaymentResponse> paymentResponse =
    new PaymentResponse(GetOwner(), mInternalId, mId, aMethodName,
                        mShippingOption, mShippingAddress, aDetails,
                        aPayerName, aPayerEmail, aPayerPhone);
  mResponse = paymentResponse;
  mAcceptPromise->MaybeResolve(paymentResponse);

  mState = eClosed;
  mAcceptPromise = nullptr;
}

void
PaymentRequest::RespondComplete()
{
  MOZ_ASSERT(mResponse);
  mResponse->RespondComplete();
}

already_AddRefed<Promise>
PaymentRequest::Abort(ErrorResult& aRv)
{
  if (mState != eInteractive) {
    aRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
    return nullptr;
  }

  if (mAbortPromise) {
    aRv.Throw(NS_ERROR_DOM_NOT_ALLOWED_ERR);
    return nullptr;
  }

  nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(GetOwner());
  ErrorResult result;
  RefPtr<Promise> promise = Promise::Create(global, result);
  if (result.Failed()) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  RefPtr<PaymentRequestManager> manager = PaymentRequestManager::GetSingleton();
  if (NS_WARN_IF(!manager)) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }
  nsresult rv = manager->AbortPayment(mInternalId);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  mAbortPromise = promise;
  return promise.forget();
}

void
PaymentRequest::RespondAbortPayment(bool aSuccess)
{
  MOZ_ASSERT(mAbortPromise);
  MOZ_ASSERT(mState == eInteractive);

  if (aSuccess) {
    mAbortPromise->MaybeResolve(JS::UndefinedHandleValue);
    mAbortPromise = nullptr;
    RejectShowPayment(NS_ERROR_DOM_ABORT_ERR);
  } else {
    mAbortPromise->MaybeReject(NS_ERROR_DOM_INVALID_STATE_ERR);
    mAbortPromise = nullptr;
  }
}

void
PaymentRequest::GetId(nsAString& aRetVal) const
{
  aRetVal = mId;
}

void
PaymentRequest::GetInternalId(nsAString& aRetVal)
{
  aRetVal = mInternalId;
}

void
PaymentRequest::SetId(const nsAString& aId)
{
  mId = aId;
}

bool
PaymentRequest::Equals(const nsAString& aInternalId) const
{
  return mInternalId.Equals(aInternalId);
}

bool
PaymentRequest::ReadyForUpdate()
{
  return mState == eInteractive && !mUpdating;
}

void
PaymentRequest::SetUpdating(bool aUpdating)
{
  mUpdating = aUpdating;
}

already_AddRefed<PaymentAddress>
PaymentRequest::GetShippingAddress() const
{
  RefPtr<PaymentAddress> address = mShippingAddress;
  return address.forget();
}

void
PaymentRequest::GetShippingOption(nsAString& aRetVal) const
{
  aRetVal = mShippingOption;
}

Nullable<PaymentShippingType>
PaymentRequest::GetShippingType() const
{
  return nullptr;
}

PaymentRequest::~PaymentRequest()
{
}

JSObject*
PaymentRequest::WrapObject(JSContext* aCx, JS::Handle<JSObject*> aGivenProto)
{
  return PaymentRequestBinding::Wrap(aCx, this, aGivenProto);
}

} // namespace dom
} // namespace mozilla
