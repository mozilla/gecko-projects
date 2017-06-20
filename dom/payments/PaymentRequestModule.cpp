/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/ModuleUtils.h"
#include "PaymentActionRequest.h"
#include "PaymentActionResponse.h"
#include "PaymentRequestService.h"

using mozilla::dom::PaymentActionRequest;
using mozilla::dom::PaymentCreateActionRequest;
using mozilla::dom::PaymentCompleteActionRequest;
using mozilla::dom::PaymentCanMakeActionResponse;
using mozilla::dom::PaymentAbortActionResponse;
using mozilla::dom::PaymentShowActionResponse;
using mozilla::dom::PaymentCompleteActionResponse;
using mozilla::dom::PaymentRequestService;

NS_GENERIC_FACTORY_CONSTRUCTOR(PaymentActionRequest)
NS_GENERIC_FACTORY_CONSTRUCTOR(PaymentCreateActionRequest)
NS_GENERIC_FACTORY_CONSTRUCTOR(PaymentCompleteActionRequest)
NS_GENERIC_FACTORY_CONSTRUCTOR(PaymentCanMakeActionResponse)
NS_GENERIC_FACTORY_CONSTRUCTOR(PaymentAbortActionResponse)
NS_GENERIC_FACTORY_CONSTRUCTOR(PaymentShowActionResponse)
NS_GENERIC_FACTORY_CONSTRUCTOR(PaymentCompleteActionResponse)
NS_GENERIC_FACTORY_SINGLETON_CONSTRUCTOR(PaymentRequestService,
                                         PaymentRequestService::GetSingleton)

NS_DEFINE_NAMED_CID(NS_PAYMENT_ACTION_REQUEST_CID);
NS_DEFINE_NAMED_CID(NS_PAYMENT_CREATE_ACTION_REQUEST_CID);
NS_DEFINE_NAMED_CID(NS_PAYMENT_COMPLETE_ACTION_REQUEST_CID);
NS_DEFINE_NAMED_CID(NS_PAYMENT_CANMAKE_ACTION_RESPONSE_CID);
NS_DEFINE_NAMED_CID(NS_PAYMENT_ABORT_ACTION_RESPONSE_CID);
NS_DEFINE_NAMED_CID(NS_PAYMENT_SHOW_ACTION_RESPONSE_CID);
NS_DEFINE_NAMED_CID(NS_PAYMENT_COMPLETE_ACTION_RESPONSE_CID);
NS_DEFINE_NAMED_CID(NS_PAYMENT_REQUEST_SERVICE_CID);

static const mozilla::Module::CIDEntry kPaymentRequestCIDs[] = {
  { &kNS_PAYMENT_ACTION_REQUEST_CID, false, nullptr, PaymentActionRequestConstructor},
  { &kNS_PAYMENT_CREATE_ACTION_REQUEST_CID, false, nullptr, PaymentCreateActionRequestConstructor},
  { &kNS_PAYMENT_COMPLETE_ACTION_REQUEST_CID, false, nullptr, PaymentCompleteActionRequestConstructor},
  { &kNS_PAYMENT_CANMAKE_ACTION_RESPONSE_CID, false, nullptr, PaymentCanMakeActionResponseConstructor},
  { &kNS_PAYMENT_ABORT_ACTION_RESPONSE_CID, false, nullptr, PaymentAbortActionResponseConstructor},
  { &kNS_PAYMENT_SHOW_ACTION_RESPONSE_CID, false, nullptr, PaymentShowActionResponseConstructor},
  { &kNS_PAYMENT_COMPLETE_ACTION_RESPONSE_CID, false, nullptr, PaymentCompleteActionResponseConstructor},
  { &kNS_PAYMENT_REQUEST_SERVICE_CID, true, nullptr, PaymentRequestServiceConstructor },
  { nullptr }
};

static const mozilla::Module::ContractIDEntry kPaymentRequestContracts[] = {
  { NS_PAYMENT_ACTION_REQUEST_CONTRACT_ID, &kNS_PAYMENT_ACTION_REQUEST_CID },
  { NS_PAYMENT_CREATE_ACTION_REQUEST_CONTRACT_ID, &kNS_PAYMENT_CREATE_ACTION_REQUEST_CID },
  { NS_PAYMENT_COMPLETE_ACTION_REQUEST_CONTRACT_ID, &kNS_PAYMENT_COMPLETE_ACTION_REQUEST_CID },
  { NS_PAYMENT_CANMAKE_ACTION_RESPONSE_CONTRACT_ID, &kNS_PAYMENT_CANMAKE_ACTION_RESPONSE_CID },
  { NS_PAYMENT_ABORT_ACTION_RESPONSE_CONTRACT_ID, &kNS_PAYMENT_ABORT_ACTION_RESPONSE_CID },
  { NS_PAYMENT_SHOW_ACTION_RESPONSE_CONTRACT_ID, &kNS_PAYMENT_SHOW_ACTION_RESPONSE_CID },
  { NS_PAYMENT_COMPLETE_ACTION_RESPONSE_CONTRACT_ID, &kNS_PAYMENT_COMPLETE_ACTION_RESPONSE_CID },
  { NS_PAYMENT_REQUEST_SERVICE_CONTRACT_ID, &kNS_PAYMENT_REQUEST_SERVICE_CID },
  { nullptr }
};

static const mozilla::Module::CategoryEntry kPaymentRequestCategories[] = {
  { "payment-request", "PaymentActionRequest", NS_PAYMENT_ACTION_REQUEST_CONTRACT_ID },
  { "payment-request", "PaymentCreateActionRequest", NS_PAYMENT_CREATE_ACTION_REQUEST_CONTRACT_ID },
  { "payment-request", "PaymentCompleteActionRequest", NS_PAYMENT_COMPLETE_ACTION_REQUEST_CONTRACT_ID },
  { "payment-request", "PaymentCanMakeActionResponse", NS_PAYMENT_CANMAKE_ACTION_RESPONSE_CONTRACT_ID },
  { "payment-request", "PaymentAbortActionResponse", NS_PAYMENT_ABORT_ACTION_RESPONSE_CONTRACT_ID },
  { "payment-request", "PaymentShowActionResponse", NS_PAYMENT_SHOW_ACTION_RESPONSE_CONTRACT_ID },
  { "payment-request", "PaymentCompleteActionResponse", NS_PAYMENT_COMPLETE_ACTION_RESPONSE_CONTRACT_ID },
  { "payment-request", "PaymentRequestService", NS_PAYMENT_REQUEST_SERVICE_CONTRACT_ID },
  { nullptr }
};

static const mozilla::Module kPaymentRequestModule = {
  mozilla::Module::kVersion,
  kPaymentRequestCIDs,
  kPaymentRequestContracts,
  kPaymentRequestCategories
};

NSMODULE_DEFN(PaymentRequestModule) = &kPaymentRequestModule;
