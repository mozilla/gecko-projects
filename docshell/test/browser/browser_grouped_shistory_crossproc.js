add_task(function* () {
  yield SpecialPowers.pushPrefEnv({
    set: [["browser.groupedhistory.enabled", true]]
  });

  // Wait for a process change and then fulfil the promise.
  function awaitProcessChange(browser) {
    return new Promise(resolve => {
      browser.addEventListener("BrowserChangedProcess", function bcp(e) {
        browser.removeEventListener("BrowserChangedProcess", bcp);
        ok(true, "The browser changed process!");
        resolve();
      });
    });
  }

  yield BrowserTestUtils.withNewTab({ gBrowser, url: "data:text/html,a" }, function* (browser1) {
    // Set up the grouped SHEntry setup
    let tab2 = gBrowser.loadOneTab("data:text/html,b", {
      referrerPolicy: Ci.nsIHttpChannel.REFERRER_POLICY_DEFAULT,
      allowThirdPartyFixup: true,
      relatedToCurrent: true,
      isPrerendered: true,
    });
    yield BrowserTestUtils.browserLoaded(tab2.linkedBrowser);
    browser1.frameLoader.appendPartialSessionHistoryAndSwap(
      tab2.linkedBrowser.frameLoader);
    yield awaitProcessChange(browser1);

    // Load a URI which will involve loading in the parent process
    browser1.loadURI("about:config", Ci.nsIWebNavigation.LOAD_FLAGS_NONE, null, null, null);
    yield BrowserTestUtils.browserLoaded(browser1);
    let docshell = browser1.frameLoader.docShell.QueryInterface(Ci.nsIWebNavigation);
    ok(docshell, "The browser should be loaded in the chrome process");
    is(docshell.canGoForward, false, "canGoForward is correct");
    is(docshell.canGoBack, true, "canGoBack is correct");
    is(docshell.sessionHistory.count, 3, "Count is correct");
    is(browser1.frameLoader.groupedSessionHistory, null,
       "browser1's session history is now complete");
  });
});
