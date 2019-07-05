/*
 * Test for keeping the valid fields information in sharedData.
 */

"use strict";

let FormAutofillParent;

add_task(async function setup() {
  ({ FormAutofillParent } = ChromeUtils.import(
    "resource://formautofill/FormAutofillParent.jsm",
    null
  ));
});

add_task(async function test_profileSavedFieldNames_init() {
  let formAutofillParent = new FormAutofillParent();
  sinon.stub(formAutofillParent, "_updateSavedFieldNames");

  await formAutofillParent.init();
  await formAutofillParent.formAutofillStorage.initialize();
  Assert.equal(formAutofillParent._updateSavedFieldNames.called, true);

  formAutofillParent._uninit();
});

add_task(async function test_profileSavedFieldNames_observe() {
  let formAutofillParent = new FormAutofillParent();
  sinon.stub(formAutofillParent, "_updateSavedFieldNames");

  await formAutofillParent.init();

  // profile changed => Need to trigger updateValidFields
  ["add", "update", "remove", "reconcile", "removeAll"].forEach(event => {
    formAutofillParent.observe(null, "formautofill-storage-changed", event);
    Assert.equal(formAutofillParent._updateSavedFieldNames.called, true);
  });

  // profile metadata updated => no need to trigger updateValidFields
  formAutofillParent._updateSavedFieldNames.resetHistory();
  formAutofillParent.observe(
    null,
    "formautofill-storage-changed",
    "notifyUsed"
  );
  Assert.equal(formAutofillParent._updateSavedFieldNames.called, false);
});

add_task(async function test_profileSavedFieldNames_update() {
  let formAutofillParent = new FormAutofillParent();
  await formAutofillParent.init();
  registerCleanupFunction(function cleanup() {
    Services.prefs.clearUserPref("extensions.formautofill.addresses.enabled");
  });

  Object.defineProperty(
    formAutofillParent.formAutofillStorage.addresses,
    "_data",
    { writable: true }
  );

  formAutofillParent.formAutofillStorage.addresses._data = [];

  // The set is empty if there's no profile in the store.
  formAutofillParent._updateSavedFieldNames();
  Assert.equal(
    Services.ppmm.sharedData.get("FormAutofill:savedFieldNames").size,
    0
  );

  // 2 profiles with 4 valid fields.
  formAutofillParent.formAutofillStorage.addresses._data = [
    {
      guid: "test-guid-1",
      organization: "Sesame Street",
      "street-address": "123 Sesame Street.",
      tel: "1-345-345-3456",
      email: "",
      timeCreated: 0,
      timeLastUsed: 0,
      timeLastModified: 0,
      timesUsed: 0,
    },
    {
      guid: "test-guid-2",
      organization: "Mozilla",
      "street-address": "331 E. Evelyn Avenue",
      tel: "1-650-903-0800",
      country: "US",
      timeCreated: 0,
      timeLastUsed: 0,
      timeLastModified: 0,
      timesUsed: 0,
    },
  ];

  formAutofillParent._updateSavedFieldNames();

  let autofillSavedFieldNames = Services.ppmm.sharedData.get(
    "FormAutofill:savedFieldNames"
  );
  Assert.equal(autofillSavedFieldNames.size, 4);
  Assert.equal(autofillSavedFieldNames.has("organization"), true);
  Assert.equal(autofillSavedFieldNames.has("street-address"), true);
  Assert.equal(autofillSavedFieldNames.has("tel"), true);
  Assert.equal(autofillSavedFieldNames.has("email"), false);
  Assert.equal(autofillSavedFieldNames.has("guid"), false);
  Assert.equal(autofillSavedFieldNames.has("timeCreated"), false);
  Assert.equal(autofillSavedFieldNames.has("timeLastUsed"), false);
  Assert.equal(autofillSavedFieldNames.has("timeLastModified"), false);
  Assert.equal(autofillSavedFieldNames.has("timesUsed"), false);
});
