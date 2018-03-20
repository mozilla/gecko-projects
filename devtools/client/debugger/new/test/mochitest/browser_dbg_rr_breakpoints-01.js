/* -*- indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim: set ft=javascript ts=2 et sw=2 tw=80: */
/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

// Test basic breakpoint functionality in web replay.
async function runTest(tab) {
  let client = await attachDebugger(tab);
  await client.interrupt();
  await setBreakpoint(client, "doc_rr_basic.html", 21);
  await rewindToLine(client, 21);
  await checkEvaluateInTopFrame(client, "number", 10);
  await rewindToLine(client, 21);
  await checkEvaluateInTopFrame(client, "number", 9);
  await resumeToLine(client, 21);
  await checkEvaluateInTopFrame(client, "number", 10);
  PromiseTestUtils.removeUncaughtRejections(/No such actor for ID/);
  finish();
}

function test() {
  waitForExplicitFinish();

  var tab = gBrowser.addTab(null, { recordExecution: "*" });
  addRecordingFinishedListener(() => runTest(tab));

  gBrowser.selectedTab = tab;
  openUILinkIn(EXAMPLE_URL + "doc_rr_basic.html", "current");
}
