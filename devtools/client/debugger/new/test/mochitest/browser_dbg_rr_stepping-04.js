/* -*- indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim: set ft=javascript ts=2 et sw=2 tw=80: */
/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

// Stepping past the beginning or end of a frame should act like a step-out.
async function runTest(tab) {
  let client = await attachDebugger(tab);
  await client.interrupt();
  await setBreakpoint(client, "doc_rr_basic.html", 21);
  await rewindToLine(client, 21);
  await checkEvaluateInTopFrame(client, "number", 10);
  await reverseStepOverToLine(client, 20);
  await reverseStepOverToLine(client, 12);

  // After reverse-stepping out of the topmost frame we should rewind to the
  // last breakpoint hit.
  await reverseStepOverToLine(client, 21);
  await checkEvaluateInTopFrame(client, "number", 9);

  await stepOverToLine(client, 22);
  await stepOverToLine(client, 23);
  await stepOverToLine(client, 23);
  await stepOverToLine(client, 13);
  await stepOverToLine(client, 17);
  await stepOverToLine(client, 18);
  await stepOverToLine(client, 18);

  // After forward-stepping out of the topmost frame we should run forward to
  // the next breakpoint hit.
  await stepOverToLine(client, 21);
  await checkEvaluateInTopFrame(client, "number", 10);

  finish();
}

function test() {
  waitForExplicitFinish();

  var tab = gBrowser.addTab(null, { recordExecution: "*" });
  addMessageListener("RecordingFinished", () => runTest(tab));

  gBrowser.selectedTab = tab;
  openUILinkIn(EXAMPLE_URL + "doc_rr_basic.html", "current");
}
