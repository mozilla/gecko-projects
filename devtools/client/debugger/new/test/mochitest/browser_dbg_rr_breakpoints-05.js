/* -*- indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim: set ft=javascript ts=2 et sw=2 tw=80: */
/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

// Test hitting breakpoints when rewinding past the point where the breakpoint
// script was created.
async function runTest(tab) {
  let client = await attachDebugger(tab);
  await client.interrupt();
  addMessageListener("HitRecordingBeginning", () => runTestFromBeginning(client));
  client.rewind();
}

async function runTestFromBeginning(client) {
  await setBreakpoint(client, "doc_rr_basic.html", 21);
  await resumeToLine(client, 21);
  await checkEvaluateInTopFrame(client, "number", 1);
  await resumeToLine(client, 21);
  await checkEvaluateInTopFrame(client, "number", 2);
  finish();
}

function test() {
  waitForExplicitFinish();

  var tab = gBrowser.addTab(null, { recordExecution: "*" });
  addMessageListener("RecordingFinished", () => runTest(tab));

  gBrowser.selectedTab = tab;
  openUILinkIn(EXAMPLE_URL + "doc_rr_basic.html", "current");
}
