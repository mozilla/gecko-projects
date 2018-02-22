"use strict";

add_task(async function setup() {
  let onChanged = TestUtils.topicObserved("formautofill-storage-changed",
                                          (subject, data) => data == "add");

  let card = {
    "cc-exp-month": 1,
    "cc-exp-year": 9999,
    "cc-name": "John Doe",
    "cc-number": "999999999999",
  };

  profileStorage.creditCards.add(card);
  await onChanged;
});

add_task(async function test_request_shipping_present() {
  await BrowserTestUtils.withNewTab({
    gBrowser,
    url: BLANK_PAGE_URL,
  }, async browser => {
    let dialogReadyPromise = waitForWidgetReady();
    // start by creating a PaymentRequest, and show it
    await ContentTask.spawn(browser,
                            {
                              methodData: [PTU.MethodData.basicCard],
                              details: PTU.Details.twoShippingOptions,
                              options: PTU.Options.requestShippingOption,
                            },
                            PTU.ContentTasks.createAndShowRequest);

    // get a reference to the UI dialog and the requestId
    let [win] = await Promise.all([getPaymentWidget(), dialogReadyPromise]);
    ok(win, "Got payment widget");
    let requestId = paymentUISrv.requestIdForWindow(win);
    ok(requestId, "requestId should be defined");
    is(win.closed, false, "dialog should not be closed");

    let frame = await getPaymentFrame(win);
    ok(frame, "Got payment frame");

    let isShippingOptionsVisible =
      await spawnPaymentDialogTask(frame,
                                   PTU.DialogContentTasks.isElementVisible,
                                   "shipping-option-picker");
    ok(isShippingOptionsVisible, "shipping-option-picker should be visible");
    let isShippingAddressVisible =
      await spawnPaymentDialogTask(frame, PTU.DialogContentTasks.isElementVisible,
                                   "address-picker[selected-state-key='selectedShippingAddress']");
    ok(isShippingAddressVisible, "shipping address picker should be visible");

    spawnPaymentDialogTask(frame, PTU.DialogContentTasks.manuallyClickCancel);
    await BrowserTestUtils.waitForCondition(() => win.closed, "dialog should be closed");
  });
});

add_task(async function test_request_shipping_not_present() {
  await BrowserTestUtils.withNewTab({
    gBrowser,
    url: BLANK_PAGE_URL,
  }, async browser => {
    let dialogReadyPromise = waitForWidgetReady();
    // start by creating a PaymentRequest, and show it
    await ContentTask.spawn(browser,
                            {
                              methodData: [PTU.MethodData.basicCard],
                              details: PTU.Details.twoShippingOptions,
                            },
                            PTU.ContentTasks.createAndShowRequest);

    // get a reference to the UI dialog and the requestId
    let [win] = await Promise.all([getPaymentWidget(), dialogReadyPromise]);
    ok(win, "Got payment widget");
    let requestId = paymentUISrv.requestIdForWindow(win);
    ok(requestId, "requestId should be defined");
    is(win.closed, false, "dialog should not be closed");

    let frame = await getPaymentFrame(win);
    ok(frame, "Got payment frame");

    let isShippingOptionsVisible =
      await spawnPaymentDialogTask(frame,
                                   PTU.DialogContentTasks.isElementVisible,
                                   "shipping-option-picker");
    ok(!isShippingOptionsVisible, "shipping-option-picker should not be visible");
    let isShippingAddressVisible =
      await spawnPaymentDialogTask(frame, PTU.DialogContentTasks.isElementVisible,
                                   "address-picker[selected-state-key='selectedShippingAddress']");
    ok(!isShippingAddressVisible, "shipping address picker should not be visible");

    spawnPaymentDialogTask(frame, PTU.DialogContentTasks.manuallyClickCancel);
    await BrowserTestUtils.waitForCondition(() => win.closed, "dialog should be closed");
  });
});
