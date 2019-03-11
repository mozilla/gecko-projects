/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/* global getCDP */

// Test very basic CDP features.

const TEST_URI = "data:text/html;charset=utf-8,default-test-page";

add_task(async function() {
  try {
    await testCDP();
  } catch (e) {
    // Display better error message with the server side stacktrace
    // if an error happened on the server side:
    if (e.response) {
      throw new Error("CDP Exception:\n" + e.response + "\n");
    } else {
      throw e;
    }
  }
});

async function testCDP() {
  // Open a test page, to prevent debugging the random default page
  const tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, TEST_URI);

  // Start the CDP server
  const RemoteAgent = Cc["@mozilla.org/remote/agent"].getService(Ci.nsISupports).wrappedJSObject;
  RemoteAgent.tabs.start();
  RemoteAgent.listen(Services.io.newURI("http://localhost:9222"));

  // Retrieve the chrome-remote-interface library object
  const CDP = await getCDP();

  // Connect to the server
  const client = await CDP({
    target(list) {
      // Ensure debugging the right target, i.e. the one for our test tab.
      return list.find(target => {
        return target.url == TEST_URI;
      });
    },
  });
  ok(true, "CDP client has been instantiated");

  const {Browser, Log, Page} = client;
  ok("Browser" in client, "Browser domain is available");
  ok("Log" in client, "Log domain is available");
  ok("Page" in client, "Page domain is available");

  const version = await Browser.getVersion();
  is(version.product, "Firefox", "Browser.getVersion works");

  // receive console.log messages and print them
  Log.enable();
  ok(true, "Log domain has been enabled");

  Log.entryAdded(({entry}) => {
    const {timestamp, level, text, args} = entry;
    const msg = text || args.join(" ");
    console.log(`${new Date(timestamp)}\t${level.toUpperCase()}\t${msg}`);
  });

  // turn on navigation related events, such as DOMContentLoaded et al.
  await Page.enable();
  ok(true, "Page domain has been enabled");

  await Page.navigate({url: "data:text/html;charset=utf-8,test-page<script>console.log('foo');</script><script>'</script>"});
  ok(true, "A new page has been loaded");

  await Page.loadEventFired();
  ok(true, "The new page is done loading");

  await client.close();
  ok(true, "The client is closed");

  BrowserTestUtils.removeTab(tab);

  await RemoteAgent.close();
}
