var { UrlClassifierTestUtils } = ChromeUtils.import(
  "resource://testing-common/UrlClassifierTestUtils.jsm"
);

ChromeUtils.defineModuleGetter(
  this,
  "ContentBlockingAllowList",
  "resource://gre/modules/ContentBlockingAllowList.jsm"
);

/**
 * Waits for a load (or custom) event to finish in a given tab. If provided
 * load an uri into the tab.
 *
 * @param tab
 *        The tab to load into.
 * @param [optional] url
 *        The url to load, or the current url.
 * @return {Promise} resolved when the event is handled.
 * @resolves to the received event
 * @rejects if a valid load event is not received within a meaningful interval
 */
function promiseTabLoadEvent(tab, url) {
  info("Wait tab event: load");

  function handle(loadedUrl) {
    if (loadedUrl === "about:blank" || (url && loadedUrl !== url)) {
      info(`Skipping spurious load event for ${loadedUrl}`);
      return false;
    }

    info("Tab event received: load");
    return true;
  }

  let loaded = BrowserTestUtils.browserLoaded(tab.linkedBrowser, false, handle);

  if (url) {
    BrowserTestUtils.loadURI(tab.linkedBrowser, url);
  }

  return loaded;
}

function openIdentityPopup() {
  let mainView = document.getElementById("identity-popup-mainView");
  let viewShown = BrowserTestUtils.waitForEvent(mainView, "ViewShown");
  gIdentityHandler._identityBox.click();
  return viewShown;
}

function openProtectionsPopup() {
  let mainView = document.getElementById("protections-popup-mainView");
  let viewShown = BrowserTestUtils.waitForEvent(mainView, "ViewShown");
  // TODO: This should click on the icon once we have it.
  gIdentityHandler._trackingProtectionIconContainer.click();
  return viewShown;
}

function waitForSecurityChange(numChanges = 1, win = null) {
  if (!win) {
    win = window;
  }
  return new Promise(resolve => {
    let n = 0;
    let listener = {
      onSecurityChange() {
        n = n + 1;
        info("Received onSecurityChange event " + n + " of " + numChanges);
        if (n >= numChanges) {
          win.gBrowser.removeProgressListener(listener);
          resolve(n);
        }
      },
    };
    win.gBrowser.addProgressListener(listener);
  });
}

function waitForContentBlockingEvent(numChanges = 1, win = null) {
  if (!win) {
    win = window;
  }
  return new Promise(resolve => {
    let n = 0;
    let listener = {
      onContentBlockingEvent(webProgress, request, event) {
        n = n + 1;
        info(
          `Received onContentBlockingEvent event: ${event} (${n} of ${numChanges})`
        );
        if (n >= numChanges) {
          win.gBrowser.removeProgressListener(listener);
          resolve(n);
        }
      },
    };
    win.gBrowser.addProgressListener(listener);
  });
}
