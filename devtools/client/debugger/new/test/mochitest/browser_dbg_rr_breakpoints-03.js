/* -*- indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim: set ft=javascript ts=2 et sw=2 tw=80: */
/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

// Test some issues when stepping around after hitting a breakpoint while recording.
async function runTest(tab) {
  let client = await attachDebugger(tab);
  await client.interrupt();
  await setBreakpoint(client, "doc_rr_continuous.html", 19);
  await resumeToLine(client, 19);
  await reverseStepOverToLine(client, 18);
  await checkEvaluateInTopFrame(client, "recordReplayDirective(/* AlwaysTakeTemporarySnapshots */ 3)", undefined);
  await stepInToLine(client, 22);
  await setBreakpoint(client, "doc_rr_continuous.html", 24);
  await resumeToLine(client, 24);
  await setBreakpoint(client, "doc_rr_continuous.html", 22);
  await rewindToLine(client, 22);
  finish();
}

function test() {
  waitForExplicitFinish();

  var tab = gBrowser.addTab(null, { recordExecution: "*" });

  gBrowser.selectedTab = tab;
  openTrustedLinkIn(EXAMPLE_URL + "doc_rr_continuous.html", "current");

  runTest(tab);
}
