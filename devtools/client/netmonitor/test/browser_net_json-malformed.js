/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests if malformed JSON responses are handled correctly.
 */

add_task(function* () {
  let { L10N } = require("devtools/client/netmonitor/utils/l10n");
  let { tab, monitor } = yield initNetMonitor(JSON_MALFORMED_URL);
  info("Starting test... ");

  let { document, gStore, windowRequire } = monitor.panelWin;
  let Actions = windowRequire("devtools/client/netmonitor/actions/index");
  let {
    getDisplayedRequests,
    getSortedRequests,
  } = windowRequire("devtools/client/netmonitor/selectors/index");

  gStore.dispatch(Actions.batchEnable(false));

  let wait = waitForNetworkEvents(monitor, 1);
  yield ContentTask.spawn(tab.linkedBrowser, {}, function* () {
    content.wrappedJSObject.performRequests();
  });
  yield wait;

  verifyRequestItemTarget(
    document,
    getDisplayedRequests(gStore.getState()),
    getSortedRequests(gStore.getState()).get(0),
    "GET",
    CONTENT_TYPE_SJS + "?fmt=json-malformed",
    {
      status: 200,
      statusText: "OK",
      type: "json",
      fullMimeType: "text/json; charset=utf-8"
    });

  wait = waitForDOM(document, "#response-panel .editor-mount iframe");
  EventUtils.sendMouseEvent({ type: "click" },
    document.querySelector(".network-details-panel-toggle"));
  EventUtils.sendMouseEvent({ type: "click" },
    document.querySelector("#response-tab"));
  let [editor] = yield wait;
  yield once(editor, "DOMContentLoaded");
  yield waitForDOM(editor.contentDocument, ".CodeMirror-code");

  let tabpanel = document.querySelector("#response-panel");
  is(tabpanel.querySelector(".response-error-header") === null, false,
    "The response error header doesn't have the intended visibility.");
  is(tabpanel.querySelector(".response-error-header").textContent,
    "SyntaxError: JSON.parse: unexpected non-whitespace character after JSON data" +
      " at line 1 column 40 of the JSON data",
    "The response error header doesn't have the intended text content.");
  is(tabpanel.querySelector(".response-error-header").getAttribute("title"),
    "SyntaxError: JSON.parse: unexpected non-whitespace character after JSON data" +
      " at line 1 column 40 of the JSON data",
    "The response error header doesn't have the intended tooltiptext attribute.");
  let jsonView = tabpanel.querySelector(".tree-section .treeLabel") || {};
  is(jsonView.textContent === L10N.getStr("jsonScopeName"), false,
    "The response json view doesn't have the intended visibility.");
  is(tabpanel.querySelector(".editor-mount iframe") === null, false,
    "The response editor doesn't have the intended visibility.");
  is(tabpanel.querySelector(".response-image-box") === null, true,
    "The response image box doesn't have the intended visibility.");

  // Strip CodeMirror line number through slice(1)
  let text = editor.contentDocument
    .querySelector(".CodeMirror-line").textContent;

  is(text, "{ \"greeting\": \"Hello malformed JSON!\" },",
    "The text shown in the source editor is incorrect.");

  yield teardown(monitor);
});
