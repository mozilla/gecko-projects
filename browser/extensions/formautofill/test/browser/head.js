/* exported MANAGE_ADDRESSES_DIALOG_URL, EDIT_ADDRESS_DIALOG_URL, BASE_URL,
            TEST_ADDRESS_1, TEST_ADDRESS_2, TEST_ADDRESS_3, TEST_ADDRESS_4, TEST_ADDRESS_5,
            TEST_CREDIT_CARD_1, TEST_CREDIT_CARD_2, TEST_CREDIT_CARD_3, FORM_URL,
            FTU_PREF, ENABLED_PREF, SYNC_USERNAME_PREF, SYNC_ADDRESSES_PREF,
            sleep, expectPopupOpen, openPopupOn, expectPopupClose, closePopup, clickDoorhangerButton,
            getAddresses, saveAddress, removeAddresses, saveCreditCard,
            getDisplayedPopupItems, getDoorhangerCheckbox */

"use strict";

const MANAGE_ADDRESSES_DIALOG_URL = "chrome://formautofill/content/manageAddresses.xhtml";
const EDIT_ADDRESS_DIALOG_URL = "chrome://formautofill/content/editAddress.xhtml";
const BASE_URL = "http://mochi.test:8888/browser/browser/extensions/formautofill/test/browser/";
const FORM_URL = "http://mochi.test:8888/browser/browser/extensions/formautofill/test/browser/autocomplete_basic.html";
const FTU_PREF = "extensions.formautofill.firstTimeUse";
const ENABLED_PREF = "extensions.formautofill.addresses.enabled";
const SYNC_USERNAME_PREF = "services.sync.username";
const SYNC_ADDRESSES_PREF = "services.sync.engine.addresses";

const TEST_ADDRESS_1 = {
  "given-name": "John",
  "additional-name": "R.",
  "family-name": "Smith",
  organization: "World Wide Web Consortium",
  "street-address": "32 Vassar Street\nMIT Room 32-G524",
  "address-level2": "Cambridge",
  "address-level1": "MA",
  "postal-code": "02139",
  country: "US",
  tel: "+16172535702",
  email: "timbl@w3.org",
};

const TEST_ADDRESS_2 = {
  "street-address": "Some Address",
  country: "US",
};

const TEST_ADDRESS_3 = {
  "street-address": "Other Address",
  "postal-code": "12345",
};

const TEST_ADDRESS_4 = {
  "given-name": "Timothy",
  "family-name": "Berners-Lee",
  organization: "World Wide Web Consortium",
  "street-address": "32 Vassar Street\nMIT Room 32-G524",
  country: "US",
  email: "timbl@w3.org",
};

const TEST_ADDRESS_5 = {
  tel: "+16172535702",
};

const TEST_CREDIT_CARD_1 = {
  "cc-name": "John Doe",
  "cc-number": "1234567812345678",
  // "cc-number-encrypted": "",
  "cc-exp-month": 4,
  "cc-exp-year": 2017,
};

const TEST_CREDIT_CARD_2 = {
  "cc-name": "Timothy Berners-Lee",
  "cc-number": "1111222233334444",
  "cc-exp-month": 12,
  "cc-exp-year": 2022,
};

const TEST_CREDIT_CARD_3 = {
  "cc-number": "9999888877776666",
  "cc-exp-month": 1,
  "cc-exp-year": 2000,
};

const MAIN_BUTTON_INDEX = 0;
const SECONDARY_BUTTON_INDEX = 1;

function getDisplayedPopupItems(browser, selector = ".autocomplete-richlistitem") {
  const {autoCompletePopup: {richlistbox: itemsBox}} = browser;
  const listItemElems = itemsBox.querySelectorAll(selector);

  return [...listItemElems].filter(item => item.getAttribute("collapsed") != "true");
}

async function sleep(ms = 500) {
  await new Promise(resolve => setTimeout(resolve, ms));
}

async function expectPopupOpen(browser) {
  const {autoCompletePopup} = browser;
  const listItemElems = getDisplayedPopupItems(browser);

  await BrowserTestUtils.waitForCondition(() => autoCompletePopup.popupOpen,
                                         "popup should be open");
  await BrowserTestUtils.waitForCondition(() => {
    return [...listItemElems].every(item => {
      return (item.getAttribute("originaltype") == "autofill-profile" ||
             item.getAttribute("originaltype") == "insecureWarning" ||
             item.getAttribute("originaltype") == "autofill-footer") &&
             item.hasAttribute("formautofillattached");
    });
  }, "The popup should be a form autofill one");
}

async function openPopupOn(browser, selector) {
  await SimpleTest.promiseFocus(browser);
  /* eslint no-shadow: ["error", { "allow": ["selector"] }] */
  const identified = await ContentTask.spawn(browser, {selector}, async function({selector}) {
    const input = content.document.querySelector(selector);
    const forms = content.document.getElementsByTagName("form");
    const rootElement = [...forms].find(form => form.contains(input)) || content.document.body;

    input.focus();
    if (rootElement.hasAttribute("test-formautofill-identified")) {
      return true;
    }
    rootElement.setAttribute("test-formautofill-identified", "true");
    return false;
  });
  // Wait 2 seconds for identifyAutofillFields if the form hasn't been identified yet.
  if (!identified) {
    await sleep(2000);
  }
  await BrowserTestUtils.synthesizeKey("VK_DOWN", {}, browser);
  await expectPopupOpen(browser);
}

async function expectPopupClose(browser) {
  await BrowserTestUtils.waitForCondition(() => !browser.autoCompletePopup.popupOpen,
    "popup should have closed");
}

async function closePopup(browser) {
  await ContentTask.spawn(browser, {}, async function() {
    content.document.activeElement.blur();
  });

  await expectPopupClose(browser);
}

function getRecords(data) {
  return new Promise(resolve => {
    Services.cpmm.addMessageListener("FormAutofill:Records", function getResult(result) {
      Services.cpmm.removeMessageListener("FormAutofill:Records", getResult);
      resolve(result.data);
    });
    Services.cpmm.sendAsyncMessage("FormAutofill:GetRecords", data);
  });
}

function getAddresses() {
  return getRecords({collectionName: "addresses"});
}

function saveAddress(address) {
  Services.cpmm.sendAsyncMessage("FormAutofill:SaveAddress", {address});
  return TestUtils.topicObserved("formautofill-storage-changed");
}

function saveCreditCard(creditcard) {
  let creditcardClone = Object.assign({}, creditcard);
  Services.cpmm.sendAsyncMessage("FormAutofill:SaveCreditCard", {
    creditcard: creditcardClone,
  });
  return TestUtils.topicObserved("formautofill-storage-changed");
}
function removeAddresses(guids) {
  Services.cpmm.sendAsyncMessage("FormAutofill:RemoveAddresses", {guids});
  return TestUtils.topicObserved("formautofill-storage-changed");
}

/**
 * Clicks the popup notification button and wait for popup hidden.
 *
 * @param {number} buttonIndex Number indicating which button to click.
 *                             See the constants in this file.
 */
async function clickDoorhangerButton(buttonIndex) {
  let popuphidden = BrowserTestUtils.waitForEvent(PopupNotifications.panel, "popuphidden");
  let notifications = PopupNotifications.panel.childNodes;
  ok(notifications.length > 0, "at least one notification displayed");
  ok(true, notifications.length + " notification(s)");
  let notification = notifications[0];

  if (buttonIndex == MAIN_BUTTON_INDEX) {
    ok(true, "Triggering main action");
    EventUtils.synthesizeMouseAtCenter(notification.button, {});
  } else if (buttonIndex == SECONDARY_BUTTON_INDEX) {
    ok(true, "Triggering secondary action");
    EventUtils.synthesizeMouseAtCenter(notification.secondaryButton, {});
  } else if (notification.childNodes[buttonIndex - 1]) {
    ok(true, "Triggering secondary action with index " + buttonIndex);
    EventUtils.synthesizeMouseAtCenter(notification.childNodes[buttonIndex - 1], {});
  }
  await popuphidden;
}

function getDoorhangerCheckbox() {
  let notifications = PopupNotifications.panel.childNodes;
  ok(notifications.length > 0, "at least one notification displayed");
  ok(true, notifications.length + " notification(s)");
  return notifications[0].checkbox;
}

registerCleanupFunction(async function() {
  let addresses = await getAddresses();
  if (addresses.length) {
    await removeAddresses(addresses.map(address => address.guid));
  }
});
