/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests for the paste and go functionality of the urlbar.
 */

let clipboardHelper = Cc["@mozilla.org/widget/clipboardhelper;1"].getService(
  Ci.nsIClipboardHelper
);

add_task(async function() {
  const kURLs = [
    "http://example.com/1",
    "http://example.org/2\n",
    "http://\nexample.com/3\n",
  ];
  for (let url of kURLs) {
    await BrowserTestUtils.withNewTab("about:blank", async function(browser) {
      gURLBar.focus();
      await new Promise((resolve, reject) => {
        waitForClipboard(
          url,
          function() {
            clipboardHelper.copyString(url);
          },
          resolve,
          () => reject(new Error(`Failed to copy string '${url}' to clipboard`))
        );
      });
      let textBox = document.getAnonymousElementByAttribute(
        gURLBar.textbox,
        "anonid",
        "moz-input-box"
      );
      let cxmenu = textBox.menupopup;
      let cxmenuPromise = BrowserTestUtils.waitForEvent(cxmenu, "popupshown");
      EventUtils.synthesizeMouseAtCenter(gURLBar.inputField, {
        type: "contextmenu",
        button: 2,
      });
      await cxmenuPromise;
      let menuitem = textBox.getMenuItem("paste-and-go");
      let browserLoadedPromise = BrowserTestUtils.browserLoaded(
        browser,
        false,
        url.replace(/\n/g, "")
      );
      EventUtils.synthesizeMouseAtCenter(menuitem, {});
      // Using toSource in order to get the newlines escaped:
      info("Paste and go, loading " + url.toSource());
      await browserLoadedPromise;
      ok(true, "Successfully loaded " + url);
    });
  }
});

add_task(async function() {
  const url = "http://example.com/4\u2028";
  await BrowserTestUtils.withNewTab("about:blank", async function(browser) {
    gURLBar.focus();
    await new Promise((resolve, reject) => {
      waitForClipboard(
        url,
        function() {
          clipboardHelper.copyString(url);
        },
        resolve,
        () => reject(new Error(`Failed to copy string '${url}' to clipboard`))
      );
    });
    let textBox = document.getAnonymousElementByAttribute(
      gURLBar.textbox,
      "anonid",
      "moz-input-box"
    );
    let cxmenu = textBox.menupopup;
    let cxmenuPromise = BrowserTestUtils.waitForEvent(cxmenu, "popupshown");
    EventUtils.synthesizeMouseAtCenter(gURLBar.inputField, {
      type: "contextmenu",
      button: 2,
    });
    await cxmenuPromise;
    let menuitem = textBox.getMenuItem("paste-and-go");
    let browserLoadedPromise = BrowserTestUtils.browserLoaded(
      browser,
      false,
      url.replace(/\u2028/g, "")
    );
    EventUtils.synthesizeMouseAtCenter(menuitem, {});
    // Using toSource in order to get the newlines escaped:
    info("Paste and go, loading " + url.toSource());
    await browserLoadedPromise;
    ok(true, "Successfully loaded " + url);
  });
});
