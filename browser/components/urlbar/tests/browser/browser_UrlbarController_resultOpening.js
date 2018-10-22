/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * These tests unit test the result/url loading functionality of UrlbarController.
 */

"use strict";

let controller;

add_task(async function setup() {
  sandbox = sinon.sandbox.create();

  controller = new UrlbarController({
    window,
  });

  registerCleanupFunction(async () => {
    sandbox.restore();
  });
});

add_task(function test_handleEnteredText_url() {
  sandbox.stub(window, "openTrustedLinkIn");

  const event = new KeyboardEvent("keyup", {key: "Enter"});
  // Additional spaces in the url to check that we trim correctly.
  controller.handleEnteredText(event, " https://example.com ");

  Assert.ok(window.openTrustedLinkIn.calledOnce,
    "Should have triggered opening the url.");

  let args = window.openTrustedLinkIn.args[0];

  Assert.equal(args[0], "https://example.com",
    "Should have triggered opening with the correct url");
  Assert.equal(args[1], "current",
    "Should be opening in the current browser");
  Assert.deepEqual(args[2], {
    allowInheritPrincipal: false,
    allowPinnedTabHostChange: true,
    allowPopups: false,
    allowThirdPartyFixup: true,
    indicateErrorPageLoad: true,
    postData: null,
    targetBrowser: gBrowser.selectedBrowser,
  }, "Should have the correct additional parameters for opening");

  sandbox.restore();
});

add_task(function test_resultSelected_switchtab() {
  sandbox.stub(window, "switchToTabHavingURI").returns(true);
  sandbox.stub(window, "isTabEmpty").returns(false);
  sandbox.stub(window.gBrowser, "removeTab");

  const event = new MouseEvent("click", {button: 0});
  const url = "https://example.com/1";
  const result = new UrlbarMatch(UrlbarUtils.MATCH_TYPE.TAB_SWITCH, {url});

  controller.resultSelected(event, result);

  Assert.ok(window.switchToTabHavingURI.calledOnce,
    "Should have triggered switching to the tab");

  let args = window.switchToTabHavingURI.args[0];

  Assert.equal(args[0], url, "Should have passed the expected url");
  Assert.ok(!args[1], "Should not attempt to open a new tab");
  Assert.deepEqual(args[2], {
    adoptIntoActiveWindow: UrlbarPrefs.get("switchTabs.adoptIntoActiveWindow"),
  }, "Should have the correct additional parameters for opening");

  sandbox.restore();
});
