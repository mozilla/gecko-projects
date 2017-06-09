/* global runHeuristicsTest */

"use strict";

runHeuristicsTest([
  {
    fixturePath: "ShippingAddress.html",
    expectedResult: [
      [
        {"section": "", "addressType": "", "contactType": "", "fieldName": "given-name"},
        {"section": "", "addressType": "", "contactType": "", "fieldName": "family-name"},
        {"section": "", "addressType": "", "contactType": "", "fieldName": "organization"},
        {"section": "", "addressType": "", "contactType": "", "fieldName": "address-line1"},
        {"section": "", "addressType": "", "contactType": "", "fieldName": "address-line2"},
        {"section": "", "addressType": "", "contactType": "", "fieldName": "postal-code"},
        {"section": "", "addressType": "", "contactType": "", "fieldName": "address-level2"},
        {"section": "", "addressType": "", "contactType": "", "fieldName": "address-level1"}, // state

        // TODO: telphone relative fields should be fixed:
        {"section": "", "addressType": "", "contactType": "", "fieldName": "tel"},
//      {"section": "", "addressType": "", "contactType": "", "fieldName": "tel-area-code"},
//      {"section": "", "addressType": "", "contactType": "", "fieldName": "tel-local-prefix"},
//      {"section": "", "addressType": "", "contactType": "", "fieldName": "tel-local-suffix"},
//      {"section": "", "addressType": "", "contactType": "", "fieldName": "tel-extension"},

        {"section": "", "addressType": "", "contactType": "", "fieldName": "email"},
      ],
      [],
    ],
  }, {
    fixturePath: "Payment.html",
    expectedResult: [
      [
//      {"section": "", "addressType": "", "contactType": "", "fieldName": "cc-exp-month"},
//      {"section": "", "addressType": "", "contactType": "", "fieldName": "cc-exp-year"},
        {"section": "", "addressType": "", "contactType": "", "fieldName": "given-name"},
        {"section": "", "addressType": "", "contactType": "", "fieldName": "family-name"},
        {"section": "", "addressType": "", "contactType": "", "fieldName": "organization"},
        {"section": "", "addressType": "", "contactType": "", "fieldName": "address-line1"},
        {"section": "", "addressType": "", "contactType": "", "fieldName": "address-line2"},
        {"section": "", "addressType": "", "contactType": "", "fieldName": "postal-code"},
        {"section": "", "addressType": "", "contactType": "", "fieldName": "address-level2"},
        {"section": "", "addressType": "", "contactType": "", "fieldName": "address-level1"}, // state

        // TODO: telphone relative fields should be fixed:
        {"section": "", "addressType": "", "contactType": "", "fieldName": "tel"},
//      {"section": "", "addressType": "", "contactType": "", "fieldName": "tel-area-code"},
//      {"section": "", "addressType": "", "contactType": "", "fieldName": "tel-local-prefix"},
//      {"section": "", "addressType": "", "contactType": "", "fieldName": "tel-local-suffix"},
//      {"section": "", "addressType": "", "contactType": "", "fieldName": "tel-extension"},

        {"section": "", "addressType": "", "contactType": "", "fieldName": "email"},
      ],
    ],
  }, {
    fixturePath: "SignIn.html",
    expectedResult: [
      [ // ac-off
//      {"section": "", "addressType": "", "contactType": "", "fieldName": "email"},
      ],
    ],
  },
], "../../../fixtures/third_party/OfficeDepot/");

