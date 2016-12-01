/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests if requests render correct information in the menu UI.
 */

function test() {
  let { L10N } = require("devtools/client/netmonitor/l10n");

  initNetMonitor(SIMPLE_SJS).then(({ tab, monitor }) => {
    info("Starting test... ");

    let { NetMonitorView } = monitor.panelWin;
    let { RequestsMenu } = NetMonitorView;

    RequestsMenu.lazyUpdate = false;

    waitForNetworkEvents(monitor, 1)
      .then(() => teardown(monitor))
      .then(finish);

    monitor.panelWin.once(monitor.panelWin.EVENTS.NETWORK_EVENT, () => {
      is(RequestsMenu.selectedItem, null,
        "There shouldn't be any selected item in the requests menu.");
      is(RequestsMenu.itemCount, 1,
        "The requests menu should not be empty after the first request.");
      is(NetMonitorView.detailsPaneHidden, true,
        "The details pane should still be hidden after the first request.");

      let requestItem = RequestsMenu.getItemAtIndex(0);

      is(typeof requestItem.id, "string",
        "The attached request id is incorrect.");
      isnot(requestItem.id, "",
        "The attached request id should not be empty.");

      is(typeof requestItem.startedMillis, "number",
        "The attached startedMillis is incorrect.");
      isnot(requestItem.startedMillis, 0,
        "The attached startedMillis should not be zero.");

      is(requestItem.requestHeaders, undefined,
        "The requestHeaders should not yet be set.");
      is(requestItem.requestCookies, undefined,
        "The requestCookies should not yet be set.");
      is(requestItem.requestPostData, undefined,
        "The requestPostData should not yet be set.");

      is(requestItem.responseHeaders, undefined,
        "The responseHeaders should not yet be set.");
      is(requestItem.responseCookies, undefined,
        "The responseCookies should not yet be set.");

      is(requestItem.httpVersion, undefined,
        "The httpVersion should not yet be set.");
      is(requestItem.status, undefined,
        "The status should not yet be set.");
      is(requestItem.statusText, undefined,
        "The statusText should not yet be set.");

      is(requestItem.headersSize, undefined,
        "The headersSize should not yet be set.");
      is(requestItem.transferredSize, undefined,
        "The transferredSize should not yet be set.");
      is(requestItem.contentSize, undefined,
        "The contentSize should not yet be set.");

      is(requestItem.mimeType, undefined,
        "The mimeType should not yet be set.");
      is(requestItem.responseContent, undefined,
        "The responseContent should not yet be set.");

      is(requestItem.totalTime, undefined,
        "The totalTime should not yet be set.");
      is(requestItem.eventTimings, undefined,
        "The eventTimings should not yet be set.");

      verifyRequestItemTarget(RequestsMenu, requestItem, "GET", SIMPLE_SJS);
    });

    monitor.panelWin.once(monitor.panelWin.EVENTS.RECEIVED_REQUEST_HEADERS, () => {
      let requestItem = RequestsMenu.getItemAtIndex(0);
      ok(requestItem.requestHeaders,
        "There should be a requestHeaders data available.");
      is(requestItem.requestHeaders.headers.length, 10,
        "The requestHeaders data has an incorrect |headers| property.");
      isnot(requestItem.requestHeaders.headersSize, 0,
        "The requestHeaders data has an incorrect |headersSize| property.");
      // Can't test for the exact request headers size because the value may
      // vary across platforms ("User-Agent" header differs).

      verifyRequestItemTarget(requestItem, "GET", SIMPLE_SJS);
    });

    monitor.panelWin.once(monitor.panelWin.EVENTS.RECEIVED_REQUEST_COOKIES, () => {
      let requestItem = RequestsMenu.getItemAtIndex(0);

      ok(requestItem.requestCookies,
        "There should be a requestCookies data available.");
      is(requestItem.requestCookies.cookies.length, 2,
        "The requestCookies data has an incorrect |cookies| property.");

      verifyRequestItemTarget(RequestsMenu, requestItem, "GET", SIMPLE_SJS);
    });

    monitor.panelWin.once(monitor.panelWin.EVENTS.RECEIVED_REQUEST_POST_DATA, () => {
      ok(false, "Trap listener: this request doesn't have any post data.");
    });

    monitor.panelWin.once(monitor.panelWin.EVENTS.RECEIVED_RESPONSE_HEADERS, () => {
      let requestItem = RequestsMenu.getItemAtIndex(0);

      ok(requestItem.responseHeaders,
        "There should be a responseHeaders data available.");
      is(requestItem.responseHeaders.headers.length, 10,
        "The responseHeaders data has an incorrect |headers| property.");
      is(requestItem.responseHeaders.headersSize, 330,
        "The responseHeaders data has an incorrect |headersSize| property.");

      verifyRequestItemTarget(RequestsMenu, requestItem, "GET", SIMPLE_SJS);
    });

    monitor.panelWin.once(monitor.panelWin.EVENTS.RECEIVED_RESPONSE_COOKIES, () => {
      let requestItem = RequestsMenu.getItemAtIndex(0);

      ok(requestItem.responseCookies,
        "There should be a responseCookies data available.");
      is(requestItem.responseCookies.cookies.length, 2,
        "The responseCookies data has an incorrect |cookies| property.");

      verifyRequestItemTarget(RequestsMenu, requestItem, "GET", SIMPLE_SJS);
    });

    monitor.panelWin.once(monitor.panelWin.EVENTS.STARTED_RECEIVING_RESPONSE, () => {
      let requestItem = RequestsMenu.getItemAtIndex(0);

      is(requestItem.httpVersion, "HTTP/1.1",
        "The httpVersion data has an incorrect value.");
      is(requestItem.status, "200",
        "The status data has an incorrect value.");
      is(requestItem.statusText, "Och Aye",
        "The statusText data has an incorrect value.");
      is(requestItem.headersSize, 330,
        "The headersSize data has an incorrect value.");

      verifyRequestItemTarget(RequestsMenu, requestItem, "GET", SIMPLE_SJS, {
        status: "200",
        statusText: "Och Aye"
      });
    });

    monitor.panelWin.once(monitor.panelWin.EVENTS.UPDATING_RESPONSE_CONTENT, () => {
      let requestItem = RequestsMenu.getItemAtIndex(0);

      is(requestItem.transferredSize, "12",
        "The transferredSize data has an incorrect value.");
      is(requestItem.contentSize, "12",
        "The contentSize data has an incorrect value.");
      is(requestItem.mimeType, "text/plain; charset=utf-8",
        "The mimeType data has an incorrect value.");

      verifyRequestItemTarget(RequestsMenu, requestItem, "GET", SIMPLE_SJS, {
        type: "plain",
        fullMimeType: "text/plain; charset=utf-8",
        transferred: L10N.getFormatStrWithNumbers("networkMenu.sizeB", 12),
        size: L10N.getFormatStrWithNumbers("networkMenu.sizeB", 12),
      });
    });

    monitor.panelWin.once(monitor.panelWin.EVENTS.RECEIVED_RESPONSE_CONTENT, () => {
      let requestItem = RequestsMenu.getItemAtIndex(0);

      ok(requestItem.responseContent,
        "There should be a responseContent data available.");
      is(requestItem.responseContent.content.mimeType,
        "text/plain; charset=utf-8",
        "The responseContent data has an incorrect |content.mimeType| property.");
      is(requestItem.responseContent.content.text,
        "Hello world!",
        "The responseContent data has an incorrect |content.text| property.");
      is(requestItem.responseContent.content.size,
        12,
        "The responseContent data has an incorrect |content.size| property.");

      verifyRequestItemTarget(RequestsMenu, requestItem, "GET", SIMPLE_SJS, {
        type: "plain",
        fullMimeType: "text/plain; charset=utf-8",
        transferred: L10N.getFormatStrWithNumbers("networkMenu.sizeB", 12),
        size: L10N.getFormatStrWithNumbers("networkMenu.sizeB", 12),
      });
    });

    monitor.panelWin.once(monitor.panelWin.EVENTS.UPDATING_EVENT_TIMINGS, () => {
      let requestItem = RequestsMenu.getItemAtIndex(0);

      is(typeof requestItem.totalTime, "number",
        "The attached totalTime is incorrect.");
      ok(requestItem.totalTime >= 0,
        "The attached totalTime should be positive.");

      verifyRequestItemTarget(RequestsMenu, requestItem, "GET", SIMPLE_SJS, {
        time: true
      });
    });

    monitor.panelWin.once(monitor.panelWin.EVENTS.RECEIVED_EVENT_TIMINGS, () => {
      let requestItem = RequestsMenu.getItemAtIndex(0);

      ok(requestItem.eventTimings,
        "There should be a eventTimings data available.");
      is(typeof requestItem.eventTimings.timings.blocked, "number",
        "The eventTimings data has an incorrect |timings.blocked| property.");
      is(typeof requestItem.eventTimings.timings.dns, "number",
        "The eventTimings data has an incorrect |timings.dns| property.");
      is(typeof requestItem.eventTimings.timings.connect, "number",
        "The eventTimings data has an incorrect |timings.connect| property.");
      is(typeof requestItem.eventTimings.timings.send, "number",
        "The eventTimings data has an incorrect |timings.send| property.");
      is(typeof requestItem.eventTimings.timings.wait, "number",
        "The eventTimings data has an incorrect |timings.wait| property.");
      is(typeof requestItem.eventTimings.timings.receive, "number",
        "The eventTimings data has an incorrect |timings.receive| property.");
      is(typeof requestItem.eventTimings.totalTime, "number",
        "The eventTimings data has an incorrect |totalTime| property.");

      verifyRequestItemTarget(RequestsMenu, requestItem, "GET", SIMPLE_SJS, {
        time: true
      });
    });

    tab.linkedBrowser.reload();
  });
}
