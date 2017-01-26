"use strict";

let {AppConstants} = SpecialPowers.Cu.import("resource://gre/modules/AppConstants.jsm", {});

// We run tests under two different configurations, from mochitest.ini and
// mochitest-remote.ini. When running from mochitest-remote.ini, the tests are
// copied to the sub-directory "test-oop-extensions", which we detect here, and
// use to select our configuration.
if (location.pathname.includes("test-oop-extensions")) {
  SpecialPowers.pushPrefEnv({set: [
    ["dom.ipc.processCount.extension", 1],
    ["extensions.webextensions.remote", true],
  ]});
  // We don't want to reset this at the end of the test, so that we don't have
  // to spawn a new extension child process for each test unit.
  SpecialPowers.setIntPref("dom.ipc.keepProcessesAlive.extension", 1);
}

if (AppConstants.MOZ_BUILD_APP === "browser") {
  let chromeScript = SpecialPowers.loadChromeScript(
    SimpleTest.getTestFileURL("chrome_cleanup_script.js"));

  SimpleTest.registerCleanupFunction(async () => {
    chromeScript.sendAsyncMessage("check-cleanup");

    let results = await chromeScript.promiseOneMessage("cleanup-results");
    chromeScript.destroy();

    if (results.extraWindows.length || results.extraTabs.length) {
      ok(false, `Test left extra windows or tabs: ${JSON.stringify(results)}\n`);
    }
  });
}

/* exported waitForLoad */

function waitForLoad(win) {
  return new Promise(resolve => {
    win.addEventListener("load", function() {
      resolve();
    }, {capture: true, once: true});
  });
}

