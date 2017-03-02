/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { getHeadersURL } = require("devtools/client/netmonitor/utils/mdn-utils");

/**
 * Tests if "Learn More" links are correctly displayed
 * next to headers.
 */
add_task(function* () {
  let { tab, monitor } = yield initNetMonitor(POST_DATA_URL);
  info("Starting test... ");

  let { document, gStore, windowRequire } = monitor.panelWin;
  let Actions = windowRequire("devtools/client/netmonitor/actions/index");
  let {
    getSortedRequests,
  } = windowRequire("devtools/client/netmonitor/selectors/index");

  gStore.dispatch(Actions.batchEnable(false));

  let wait = waitForNetworkEvents(monitor, 0, 2);
  yield ContentTask.spawn(tab.linkedBrowser, {}, function* () {
    content.wrappedJSObject.performRequests();
  });
  yield wait;

  EventUtils.sendMouseEvent({ type: "click" },
    document.querySelectorAll(".request-list-item")[0]);

  testShowLearnMore(getSortedRequests(gStore.getState()).get(0));

  return teardown(monitor);

  /*
   * Tests that a "Learn More" button is only shown if
   * and only if a header is documented in MDN.
   */
  function testShowLearnMore(data) {
    let selector = ".properties-view .treeRow.stringRow";
    document.querySelectorAll(selector).forEach((rowEl, index) => {
      let headerName = rowEl.querySelectorAll(".treeLabelCell .treeLabel")[0]
                            .textContent;
      let headerDocURL = getHeadersURL(headerName);
      let learnMoreEl = rowEl.querySelectorAll(".treeValueCell .learn-more-link");

      if (headerDocURL === null) {
        ok(learnMoreEl.length === 0,
           "undocumented header does not include a \"Learn More\" button");
      } else {
        ok(learnMoreEl[0].getAttribute("title") === headerDocURL,
           "documented header includes a \"Learn More\" button with a link to MDN");
      }
    });
  }
});
