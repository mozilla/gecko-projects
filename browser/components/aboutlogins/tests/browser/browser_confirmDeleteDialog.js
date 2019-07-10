/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

add_task(async function setup() {
  await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    url: "about:logins",
  });
  registerCleanupFunction(() => {
    BrowserTestUtils.removeTab(gBrowser.selectedTab);
  });
});

add_task(async function test() {
  let browser = gBrowser.selectedBrowser;

  await ContentTask.spawn(browser, null, async () => {
    let dialog = Cu.waiveXrays(
      content.document.querySelector("confirm-delete-dialog")
    );

    let cancelButton = dialog.shadowRoot.querySelector(".cancel-button");
    let confirmDeleteButton = dialog.shadowRoot.querySelector(
      ".confirm-button"
    );
    let dismissButton = dialog.shadowRoot.querySelector(".dismiss-button");
    let message = dialog.shadowRoot.querySelector(".message");
    let title = dialog.shadowRoot.querySelector(".title");

    is(
      title.textContent,
      "Confirm Deletion",
      "Title contents should match l10n attribute set on outer element"
    );
    is(
      message.textContent,
      "Are you sure you want to delete this login?",
      "Message contents should match l10n attribute set on outer element"
    );
    is(
      cancelButton.textContent,
      "Cancel",
      "Cancel button contents should match l10n attribute set on outer element"
    );
    is(
      confirmDeleteButton.textContent,
      "Delete login",
      "Delete button contents should match l10n attribute set on outer element"
    );

    let showPromise = dialog.show();
    cancelButton.click();
    try {
      await showPromise;
      ok(
        false,
        "Promise returned by show() should not resolve after clicking cancel button"
      );
    } catch (ex) {
      ok(
        true,
        "Promise returned by show() should reject after clicking cancel button"
      );
    }
    await ContentTaskUtils.waitForCondition(
      () => dialog.hidden,
      "Waiting for the dialog to be hidden"
    );
    ok(dialog.hidden, "Dialog should be hidden after clicking cancel button");

    showPromise = dialog.show();
    dismissButton.click();
    try {
      await showPromise;
      ok(
        false,
        "Promise returned by show() should not resolve after clicking dismiss button"
      );
    } catch (ex) {
      ok(
        true,
        "Promise returned by show() should reject after clicking dismiss button"
      );
    }
    await ContentTaskUtils.waitForCondition(
      () => dialog.hidden,
      "Waiting for the dialog to be hidden"
    );
    ok(dialog.hidden, "Dialog should be hidden after clicking dismiss button");

    showPromise = dialog.show();
    confirmDeleteButton.click();
    try {
      await showPromise;
      ok(
        true,
        "Promise returned by show() should resolve after clicking confirm button"
      );
    } catch (ex) {
      ok(
        false,
        "Promise returned by show() should not reject after clicking confirm button"
      );
    }
    await ContentTaskUtils.waitForCondition(
      () => dialog.hidden,
      "Waiting for the dialog to be hidden"
    );
    ok(dialog.hidden, "Dialog should be hidden after clicking confirm button");
  });
});
