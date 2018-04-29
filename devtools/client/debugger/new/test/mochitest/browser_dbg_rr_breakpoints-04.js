/* -*- indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim: set ft=javascript ts=2 et sw=2 tw=80: */
/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

// Test navigating back to earlier breakpoints while recording, then resuming
// recording.
async function runTest(tab) {
  let client = await attachDebugger(tab);
  await client.interrupt();
  await setBreakpoint(client, "doc_rr_continuous.html", 14);
  await resumeToLine(client, 14);
  let value = await evaluateInTopFrame(client, "number");
  await resumeToLine(client, 14);
  await checkEvaluateInTopFrame(client, "number", value + 1);
  await rewindToLine(client, 14);
  await checkEvaluateInTopFrame(client, "number", value);
  await resumeToLine(client, 14);
  await checkEvaluateInTopFrame(client, "number", value + 1);
  await resumeToLine(client, 14);
  await checkEvaluateInTopFrame(client, "number", value + 2);
  await resumeToLine(client, 14);
  await checkEvaluateInTopFrame(client, "number", value + 3);
  await rewindToLine(client, 14);
  await checkEvaluateInTopFrame(client, "number", value + 2);
  finish();
}

function test() {
  waitForExplicitFinish();

  var tab = gBrowser.addTab(null, { recordExecution: "*" });

  gBrowser.selectedTab = tab;
  openUILinkIn(EXAMPLE_URL + "doc_rr_continuous.html", "current");

  runTest(tab);
}
