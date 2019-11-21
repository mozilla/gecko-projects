/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { RemoteAgentError } = ChromeUtils.import(
  "chrome://remote/content/Error.jsm"
);
const { RemoteAgent } = ChromeUtils.import(
  "chrome://remote/content/RemoteAgent.jsm"
);

/**
 * Override `add_task` in order to translate chrome-remote-interface exceptions
 * into something that logs better errors on stdout
 */
const add_plain_task = add_task.bind(this);
this.add_task = function(taskFn, opts = {}) {
  const { createTab = true } = opts;

  add_plain_task(async function() {
    let client;

    await RemoteAgent.listen(Services.io.newURI("http://localhost:9222"));
    info("CDP server started");

    try {
      const CDP = await getCDP();

      // By default run each test in its own tab
      if (createTab) {
        const tab = await BrowserTestUtils.openNewForegroundTab(gBrowser);
        const browsingContextId = tab.linkedBrowser.browsingContext.id;

        client = await CDP({
          target(list) {
            return list.find(target => target.id === browsingContextId);
          },
        });
        info("CDP client instantiated");

        await taskFn(client, CDP, tab);

        // taskFn may resolve within a tick after opening a new tab.
        // We shouldn't remove the newly opened tab in the same tick.
        // Wait for the next tick here.
        await TestUtils.waitForTick();
        BrowserTestUtils.removeTab(tab);
      } else {
        client = await CDP({});
        info("CDP client instantiated");

        await taskFn(client, CDP);
      }
    } catch (e) {
      // Display better error message with the server side stacktrace
      // if an error happened on the server side:
      if (e.response) {
        throw RemoteAgentError.fromJSON(e.response);
      } else {
        throw e;
      }
    } finally {
      if (client) {
        await client.close();
        info("CDP client closed");
      }

      await RemoteAgent.close();
      info("CDP server stopped");

      // Close any additional tabs, so that only a single tab remains open
      while (gBrowser.tabs.length > 1) {
        gBrowser.removeCurrentTab();
      }
    }
  });
};

/**
 * Create a test document in an invisible window.
 * This window will be automatically closed on test teardown.
 */
function createTestDocument() {
  const browser = Services.appShell.createWindowlessBrowser(true);
  registerCleanupFunction(() => browser.close());

  // Create a system principal content viewer to ensure there is a valid
  // empty document using system principal and avoid any wrapper issues
  // when using document's JS Objects.
  const webNavigation = browser.docShell.QueryInterface(Ci.nsIWebNavigation);
  const system = Services.scriptSecurityManager.getSystemPrincipal();
  webNavigation.createAboutBlankContentViewer(system, system);

  return webNavigation.document;
}

/**
 * Retrieve an intance of CDP object from chrome-remote-interface library
 */
async function getCDP() {
  // Instantiate a background test document in order to load the library
  // as in a web page
  const document = createTestDocument();

  const window = document.defaultView.wrappedJSObject;
  Services.scriptloader.loadSubScript(
    "chrome://mochitests/content/browser/remote/test/browser/chrome-remote-interface.js",
    window
  );

  // Implements `criRequest` to be called by chrome-remote-interface
  // library in order to do the cross-domain http request, which,
  // in a regular Web page, is impossible.
  window.criRequest = (options, callback) => {
    const { host, port, path } = options;
    const url = `http://${host}:${port}${path}`;
    const xhr = new XMLHttpRequest();
    xhr.open("GET", url, true);

    // Prevent "XML Parsing Error: syntax error" error messages
    xhr.overrideMimeType("text/plain");

    xhr.send(null);
    xhr.onload = () => callback(null, xhr.responseText);
    xhr.onerror = e => callback(e, null);
  };

  return window.CDP;
}

function getTargets(CDP) {
  return new Promise((resolve, reject) => {
    CDP.List(null, (err, targets) => {
      if (err) {
        reject(err);
        return;
      }
      resolve(targets);
    });
  });
}

/** Creates a data URL for the given source document. */
function toDataURL(src, doctype = "html") {
  let doc, mime;
  switch (doctype) {
    case "html":
      mime = "text/html;charset=utf-8";
      doc = `<!doctype html>\n<meta charset=utf-8>\n${src}`;
      break;
    default:
      throw new Error("Unexpected doctype: " + doctype);
  }

  return `data:${mime},${encodeURIComponent(doc)}`;
}

/**
 * Load a given URL in the currently selected tab
 */
async function loadURL(url) {
  const browser = gBrowser.selectedTab.linkedBrowser;
  const loaded = BrowserTestUtils.browserLoaded(browser, false, url);

  BrowserTestUtils.loadURI(browser, url);
  await loaded;
}

/**
 * Retrieve the value of a property on the content window.
 */
function getContentProperty(prop) {
  info(`Retrieve ${prop} on the content window`);
  return ContentTask.spawn(
    gBrowser.selectedBrowser,
    prop,
    _prop => content[_prop]
  );
}

/**
 * Return a new promise, which resolves after ms have been elapsed
 */
function timeoutPromise(ms) {
  return new Promise(resolve => {
    window.setTimeout(resolve, ms);
  });
}
