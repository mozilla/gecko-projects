"use strict";

add_task(async function test_cancelEditAddressDialog() {
  await new Promise(resolve => {
    let win = window.openDialog(EDIT_ADDRESS_DIALOG_URL);
    win.addEventListener("load", () => {
      win.addEventListener("unload", () => {
        ok(true, "Edit address dialog is closed");
        resolve();
      }, {once: true});
      win.document.querySelector("#cancel").click();
    }, {once: true});
  });
});

add_task(async function test_cancelEditAddressDialogWithESC() {
  await new Promise(resolve => {
    let win = window.openDialog(EDIT_ADDRESS_DIALOG_URL);
    win.addEventListener("load", () => {
      win.addEventListener("unload", () => {
        ok(true, "Edit address dialog is closed with ESC key");
        resolve();
      }, {once: true});
      EventUtils.synthesizeKey("VK_ESCAPE", {}, win);
    }, {once: true});
  });
});

add_task(async function test_saveAddress() {
  await new Promise(resolve => {
    let win = window.openDialog(EDIT_ADDRESS_DIALOG_URL);
    win.addEventListener("load", () => {
      win.addEventListener("unload", () => {
        ok(true, "Edit address dialog is closed");
        resolve();
      }, {once: true});
      let doc = win.document;
      // Verify labels
      is(doc.querySelector("#address-level1-container > span").textContent, "State",
                           "US address-level1 label should be 'State'");
      is(doc.querySelector("#postal-code-container > span").textContent, "Zip Code",
                           "US postal-code label should be 'Zip Code'");
      // Input address info and verify move through form with tab keys
      const keyInputs = [
        "VK_TAB",
        TEST_ADDRESS_1["given-name"],
        "VK_TAB",
        TEST_ADDRESS_1["additional-name"],
        "VK_TAB",
        TEST_ADDRESS_1["family-name"],
        "VK_TAB",
        TEST_ADDRESS_1.organization,
        "VK_TAB",
        TEST_ADDRESS_1["street-address"],
        "VK_TAB",
        TEST_ADDRESS_1["address-level2"],
        "VK_TAB",
        TEST_ADDRESS_1["address-level1"],
        "VK_TAB",
        TEST_ADDRESS_1["postal-code"],
        "VK_TAB",
        TEST_ADDRESS_1.country,
        "VK_TAB",
        TEST_ADDRESS_1.email,
        "VK_TAB",
        TEST_ADDRESS_1.tel,
        "VK_TAB",
        "VK_TAB",
        "VK_RETURN",
      ];
      keyInputs.forEach(input => EventUtils.synthesizeKey(input, {}, win));
    }, {once: true});
  });
  let addresses = await getAddresses();

  is(addresses.length, 1, "only one address is in storage");
  is(Object.keys(TEST_ADDRESS_1).length, 11, "Sanity check number of properties");
  for (let [fieldName, fieldValue] of Object.entries(TEST_ADDRESS_1)) {
    is(addresses[0][fieldName], fieldValue, "check " + fieldName);
  }
});

add_task(async function test_editAddress() {
  let addresses = await getAddresses();
  await new Promise(resolve => {
    let win = window.openDialog(EDIT_ADDRESS_DIALOG_URL, null, null, addresses[0]);
    win.addEventListener("FormReady", () => {
      win.addEventListener("unload", () => {
        ok(true, "Edit address dialog is closed");
        resolve();
      }, {once: true});
      EventUtils.synthesizeKey("VK_TAB", {}, win);
      EventUtils.synthesizeKey("VK_RIGHT", {}, win);
      EventUtils.synthesizeKey("test", {}, win);
      win.document.querySelector("#save").click();
    }, {once: true});
  });
  addresses = await getAddresses();

  is(addresses.length, 1, "only one address is in storage");
  is(addresses[0]["given-name"], TEST_ADDRESS_1["given-name"] + "test", "given-name changed");
  await removeAddresses([addresses[0].guid]);

  addresses = await getAddresses();
  is(addresses.length, 0, "Address storage is empty");
});

add_task(async function test_saveAddressCA() {
  await new Promise(resolve => {
    let win = window.openDialog(EDIT_ADDRESS_DIALOG_URL);
    win.addEventListener("load", () => {
      win.addEventListener("unload", () => {
        ok(true, "Edit address dialog is closed");
        resolve();
      }, {once: true});
      let doc = win.document;
      // Change country to verify labels
      doc.querySelector("#country").focus();
      EventUtils.synthesizeKey("Canada", {}, win);
      is(doc.querySelector("#address-level1-container > span").textContent, "Province",
                           "CA address-level1 label should be 'Province'");
      is(doc.querySelector("#postal-code-container > span").textContent, "Postal Code",
                           "CA postal-code label should be 'Postal Code'");
      // Input address info and verify move through form with tab keys
      doc.querySelector("#given-name").focus();
      const keyInputs = [
        TEST_ADDRESS_CA_1["given-name"],
        "VK_TAB",
        TEST_ADDRESS_CA_1["additional-name"],
        "VK_TAB",
        TEST_ADDRESS_CA_1["family-name"],
        "VK_TAB",
        TEST_ADDRESS_CA_1.organization,
        "VK_TAB",
        TEST_ADDRESS_CA_1["street-address"],
        "VK_TAB",
        TEST_ADDRESS_CA_1["address-level2"],
        "VK_TAB",
        TEST_ADDRESS_CA_1["address-level1"],
        "VK_TAB",
        TEST_ADDRESS_CA_1["postal-code"],
        "VK_TAB",
        TEST_ADDRESS_CA_1.country,
        "VK_TAB",
        TEST_ADDRESS_CA_1.email,
        "VK_TAB",
        TEST_ADDRESS_CA_1.tel,
        "VK_TAB",
        "VK_TAB",
        "VK_RETURN",
      ];
      keyInputs.forEach(input => EventUtils.synthesizeKey(input, {}, win));
    }, {once: true});
  });
  let addresses = await getAddresses();
  for (let [fieldName, fieldValue] of Object.entries(TEST_ADDRESS_CA_1)) {
    is(addresses[0][fieldName], fieldValue, "check " + fieldName);
  }
  await removeAllRecords();
});

add_task(async function test_saveAddressDE() {
  await new Promise(resolve => {
    let win = window.openDialog(EDIT_ADDRESS_DIALOG_URL);
    win.addEventListener("load", () => {
      win.addEventListener("unload", () => {
        ok(true, "Edit address dialog is closed");
        resolve();
      }, {once: true});
      let doc = win.document;
      // Change country to verify labels
      doc.querySelector("#country").focus();
      EventUtils.synthesizeKey("Germany", {}, win);
      is(doc.querySelector("#postal-code-container > span").textContent, "Postal Code",
                           "DE postal-code label should be 'Postal Code'");
      is(doc.querySelector("#address-level1-container").style.display, "none",
                           "DE address-level1 should be hidden");
      // Input address info and verify move through form with tab keys
      doc.querySelector("#given-name").focus();
      const keyInputs = [
        TEST_ADDRESS_DE_1["given-name"],
        "VK_TAB",
        TEST_ADDRESS_DE_1["additional-name"],
        "VK_TAB",
        TEST_ADDRESS_DE_1["family-name"],
        "VK_TAB",
        TEST_ADDRESS_DE_1.organization,
        "VK_TAB",
        TEST_ADDRESS_DE_1["street-address"],
        "VK_TAB",
        TEST_ADDRESS_DE_1["postal-code"],
        "VK_TAB",
        TEST_ADDRESS_DE_1["address-level2"],
        "VK_TAB",
        TEST_ADDRESS_DE_1.country,
        "VK_TAB",
        TEST_ADDRESS_DE_1.email,
        "VK_TAB",
        TEST_ADDRESS_DE_1.tel,
        "VK_TAB",
        "VK_TAB",
        "VK_RETURN",
      ];
      keyInputs.forEach(input => EventUtils.synthesizeKey(input, {}, win));
    }, {once: true});
  });
  let addresses = await getAddresses();
  for (let [fieldName, fieldValue] of Object.entries(TEST_ADDRESS_DE_1)) {
    is(addresses[0][fieldName], fieldValue, "check " + fieldName);
  }
  await removeAllRecords();
});
