/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const TEST_URI = "http://example.com/browser/dom/tests/browser/test_largeAllocation.html";
const TEST_URI_2 = "http://example.com/browser/dom/tests/browser/test_largeAllocation2.html";

function expectProcessCreated() {
  let os = Cc["@mozilla.org/observer-service;1"].getService(Ci.nsIObserverService);
  return new Promise(resolve => {
    let topic = "ipc:content-created";
    function observer() {
      os.removeObserver(observer, topic);
      ok(true, "Expect process created");
      resolve();
    }
    os.addObserver(observer, topic, /* weak = */ false);
  });
}

function expectNoProcess() {
  let os = Cc["@mozilla.org/observer-service;1"].getService(Ci.nsIObserverService);
  let topic = "ipc:content-created";
  function observer() {
    ok(false, "A process was created!");
    os.removeObserver(observer, topic);
  }
  os.addObserver(observer, topic, /* weak = */ false);

  return () => os.removeObserver(observer, topic);
}

function getPID(aBrowser) {
  return ContentTask.spawn(aBrowser, null, () => {
    const appinfo = Components.classes["@mozilla.org/xre/app-info;1"]
            .getService(Components.interfaces.nsIXULRuntime);
    return appinfo.processID;
  });
}

add_task(function*() {
  // I'm terrible and put this set of tests into a single file, so I need a longer timeout
  requestLongerTimeout(2);

  yield SpecialPowers.pushPrefEnv({
    set: [
      ["dom.largeAllocationHeader.enabled", true],
      // Increase processCount.webLargeAllocation to avoid any races where
      // processes aren't being cleaned up quickly enough.
      ["dom.ipc.processCount.webLargeAllocation", 20]
    ]
  });

  // A toplevel tab should be able to navigate cross process!
  yield BrowserTestUtils.withNewTab("about:blank", function*(aBrowser) {
    info("Starting test 0");
    let pid1 = yield getPID(aBrowser);

    let epc = expectProcessCreated();
    yield ContentTask.spawn(aBrowser, TEST_URI, TEST_URI => {
      content.document.location = TEST_URI;
    });

    // Wait for the new process to be created
    yield epc;

    let pid2 = yield getPID(aBrowser);

    isnot(pid1, pid2, "The pids should be different between the initial load and the new load");
  });

  // When a Large-Allocation document is loaded in an iframe, the header should
  // be ignored, and the tab should stay in the current process.
  yield BrowserTestUtils.withNewTab("about:blank", function*(aBrowser) {
    info("Starting test 1");
    let pid1 = yield getPID(aBrowser);

    // Fail the test if we create a process
    let stopExpectNoProcess = expectNoProcess();

    yield ContentTask.spawn(aBrowser, TEST_URI, TEST_URI => {
      content.document.body.innerHTML = `<iframe src='${TEST_URI}'></iframe>`;

      return new Promise(resolve => {
        content.document.body.querySelector('iframe').onload = () => {
          ok(true, "Iframe finished loading");
          resolve();
        };
      });
    });

    let pid2 = yield getPID(aBrowser);

    is(pid1, pid2, "The PID should not have changed");

    stopExpectNoProcess();
  });

  // If you have an opener cross process navigation shouldn't work
  yield BrowserTestUtils.withNewTab("http://example.com", function*(aBrowser) {
    info("Starting test 2");
    let pid1 = yield getPID(aBrowser);

    // Fail the test if we create a process
    let stopExpectNoProcess = expectNoProcess();

    let loaded = ContentTask.spawn(aBrowser, TEST_URI, TEST_URI => {
      content.document.body.innerHTML = '<button>CLICK ME</button>';

      return new Promise(resolve => {
        content.document.querySelector('button').onclick = e => {
          let w = content.window.open(TEST_URI, '_blank');
          w.onload = () => {
            ok(true, "Window finished loading");
            w.close();
            resolve();
          };
        };
      });
    });

    yield BrowserTestUtils.synthesizeMouseAtCenter("button", {}, aBrowser);

    yield loaded;

    let pid2 = yield getPID(aBrowser);

    is(pid1, pid2, "The PID should not have changed");

    stopExpectNoProcess();
  });

  // Load Large-Allocation twice with about:blank load in between
  yield BrowserTestUtils.withNewTab("about:blank", function*(aBrowser) {
    info("Starting test 3");
    let pid1 = yield getPID(aBrowser);

    let epc = expectProcessCreated();

    yield ContentTask.spawn(aBrowser, TEST_URI, TEST_URI => {
      content.document.location = TEST_URI;
    });

    yield epc;

    let pid2 = yield getPID(aBrowser);

    isnot(pid1, pid2);

    epc = expectProcessCreated();

    yield BrowserTestUtils.browserLoaded(aBrowser);

    yield ContentTask.spawn(aBrowser, null, () => content.document.location = "about:blank");

    yield BrowserTestUtils.browserLoaded(aBrowser);

    let pid3 = yield getPID(aBrowser);

    // We should have been kicked out of the large-allocation process by the
    // load, meaning we're back in the first process.
    is(pid1, pid3); // XXX: This may be flakey in multiple content process e10s?

    yield ContentTask.spawn(aBrowser, TEST_URI, TEST_URI => {
      content.document.location = TEST_URI;
    });

    yield epc;

    let pid4 = yield getPID(aBrowser);

    isnot(pid1, pid4);
    isnot(pid2, pid4);
  });

  // Load Large-Allocation then about:blank load, then back button press should load from bfcache.
  yield BrowserTestUtils.withNewTab("about:blank", function*(aBrowser) {
    info("Starting test 4");
    let pid1 = yield getPID(aBrowser);

    let epc = expectProcessCreated();

    yield ContentTask.spawn(aBrowser, TEST_URI, TEST_URI => {
      content.document.location = TEST_URI;
    });

    yield epc;

    let pid2 = yield getPID(aBrowser);

    isnot(pid1, pid2, "PIDs 1 and 2 should not match");

    let stopExpectNoProcess = expectNoProcess();

    yield BrowserTestUtils.browserLoaded(aBrowser);

    // Switch to about:blank, so we can navigate back
    yield ContentTask.spawn(aBrowser, null, () => {
      content.document.location = "about:blank";
    });

    yield BrowserTestUtils.browserLoaded(aBrowser);

    let pid3 = yield getPID(aBrowser);

    // We should have been kicked out of the large-allocation process by the
    // load, meaning we're back in the first process.
    is(pid1, pid3, "PIDs 1 and 3 should match");

    stopExpectNoProcess();

    epc = expectProcessCreated();

    // Navigate back to the previous page. As the large alloation process was
    // left, it won't be in bfcache and will have to be loaded fresh.
    yield ContentTask.spawn(aBrowser, TEST_URI, TEST_URI => {
      content.window.history.back();
    });

    yield epc;

    let pid4 = yield getPID(aBrowser);

    isnot(pid1, pid4, "PID 4 shouldn't match PID 1");
    isnot(pid2, pid4, "PID 4 shouldn't match PID 2");

  });

  // Two consecutive large-allocation loads should create two processes.
  yield BrowserTestUtils.withNewTab("about:blank", function*(aBrowser) {
    info("Starting test 5");
    let pid1 = yield getPID(aBrowser);

    let ready = Promise.all([expectProcessCreated(),
                             BrowserTestUtils.browserLoaded(aBrowser)]);

    yield ContentTask.spawn(aBrowser, TEST_URI, TEST_URI => {
      content.document.location = TEST_URI;
    });

    yield ready;

    let pid2 = yield getPID(aBrowser);

    isnot(pid1, pid2, "PIDs 1 and 2 should not match");

    let epc = expectProcessCreated();

    yield ContentTask.spawn(aBrowser, TEST_URI_2, TEST_URI_2 => {
      content.document.location = TEST_URI_2;
    });

    yield epc;

    let pid3 = yield getPID(aBrowser);

    isnot(pid1, pid3, "PIDs 1 and 3 should not match");
    isnot(pid2, pid3, "PIDs 1 and 3 should not match");
  });

  // Opening a window from the large-allocation window should prevent the process switch.
  yield BrowserTestUtils.withNewTab("about:blank", function*(aBrowser) {
    info("Starting test 6");
    let pid1 = yield getPID(aBrowser);

    let ready = Promise.all([expectProcessCreated(),
                             BrowserTestUtils.browserLoaded(aBrowser)]);

    yield ContentTask.spawn(aBrowser, TEST_URI, TEST_URI => {
      content.document.location = TEST_URI;
    });

    yield ready;

    let pid2 = yield getPID(aBrowser);

    isnot(pid1, pid2, "PIDs 1 and 2 should not match");

    yield BrowserTestUtils.synthesizeMouse("a", 0, 0, {}, aBrowser);

    let stopExpectNoProcess = expectNoProcess();

    yield ContentTask.spawn(aBrowser, null, () => {
      content.document.location = "about:blank";
    });

    yield BrowserTestUtils.browserLoaded(aBrowser);

    let pid3 = yield getPID(aBrowser);

    is(pid3, pid2, "PIDs 2 and 3 should match");

    stopExpectNoProcess();

    is(gBrowser.tabs.length, 3, "There should be 3 tabs");

    // Get rid of that other tab. It should always be the last one.
    gBrowser.removeTab(gBrowser.tabs[2]);
  });
});
