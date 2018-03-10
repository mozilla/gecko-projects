/* -*- indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim: set ft=javascript ts=2 et sw=2 tw=80: */
/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

// Test unhandled divergence while evaluating at a breakpoint with Web Replay.
async function runTest(tab) {
  let client = await attachDebugger(tab);
  await client.interrupt();
  await setBreakpoint(client, "doc_rr_basic.html", 21);
  await rewindToLine(client, 21);
  await checkEvaluateInTopFrame(client, "number", 10);
  await checkEvaluateInTopFrameThrows(client, "window.alert(3)");
  await checkEvaluateInTopFrame(client, "number", 10);
  await checkEvaluateInTopFrameThrows(client, "window.alert(3)");
  await checkEvaluateInTopFrame(client, "number", 10);
  finish();
}

function test() {
  waitForExplicitFinish();

  var tab = gBrowser.addTab(null, { recordExecution: "*" });

  const ppmm = Cc["@mozilla.org/parentprocessmessagemanager;1"]
        .getService(Ci.nsIMessageBroadcaster);
  ppmm.addMessageListener("RecordingFinished", () => runTest(tab));

  gBrowser.selectedTab = tab;
  openUILinkIn(EXAMPLE_URL + "doc_rr_basic.html", "current");
}
