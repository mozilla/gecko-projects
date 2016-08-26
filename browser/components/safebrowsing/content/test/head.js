Components.utils.import("resource://gre/modules/XPCOMUtils.jsm");

XPCOMUtils.defineLazyModuleGetter(this, "Promise",
  "resource://gre/modules/Promise.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "Task",
  "resource://gre/modules/Task.jsm");

/**
 * Waits for a load (or custom) event to finish in a given tab. If provided
 * load an uri into the tab.
 *
 * @param tab
 *        The tab to load into.
 * @param [optional] url
 *        The url to load, or the current url.
 * @param [optional] event
 *        The load event type to wait for.  Defaults to "load".
 * @return {Promise} resolved when the event is handled.
 * @resolves to the received event
 * @rejects if a valid load event is not received within a meaningful interval
 */
function promiseTabLoadEvent(tab, url, eventType="load")
{
  let deferred = Promise.defer();
  info(`Wait tab event: ${eventType}`);

  function handle(loadedUrl) {
    if (loadedUrl === "about:blank" || (url && loadedUrl !== url)) {
      info(`Skipping spurious load event for ${loadedUrl}`);
      return false;
    }

    info("Tab event received: load");
    return true;
  }

  // Create two promises: one resolved from the content process when the page
  // loads and one that is rejected if we take too long to load the url.
  let loaded;
  if (eventType === "load") {
    loaded = BrowserTestUtils.browserLoaded(tab.linkedBrowser, false, handle);
  } else {
    // No need to use handle.
    loaded =
      BrowserTestUtils.waitForContentEvent(tab.linkedBrowser, eventType,
                                           true, undefined, true);
  }

  let timeout = setTimeout(() => {
    deferred.reject(new Error(`Timed out while waiting for a ${eventType} event`));
  }, 30000);

  loaded.then(() => {
    clearTimeout(timeout);
    deferred.resolve()
  });

  if (url)
    BrowserTestUtils.loadURI(tab.linkedBrowser, url);

  // Promise.all rejects if either promise rejects (i.e. if we time out) and
  // if our loaded promise resolves before the timeout, then we resolve the
  // timeout promise as well, causing the all promise to resolve.
  return Promise.all([deferred.promise, loaded]);
}

Services.prefs.setCharPref("urlclassifier.malwareTable", "test-malware-simple,test-unwanted-simple");
Services.prefs.setCharPref("urlclassifier.phishTable", "test-phish-simple");
Services.prefs.setCharPref("urlclassifier.blockedTable", "test-block-simple");
SafeBrowsing.init();
