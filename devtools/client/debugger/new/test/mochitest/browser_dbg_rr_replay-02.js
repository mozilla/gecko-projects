/* -*- indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim: set ft=javascript ts=2 et sw=2 tw=80: */
/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

// Test ending a recording at a breakpoint and then separately replaying to the end.
var recordingFile;
var lastNumberValue;

async function runRecordingTest(tab) {
  let client = await attachDebugger(tab);
  await client.interrupt();
  await setBreakpoint(client, "doc_rr_continuous.html", 14);
  await resumeToLine(client, 14);
  await resumeToLine(client, 14);
  await reverseStepOverToLine(client, 13);
  lastNumberValue = await evaluateInTopFrame(client, "number");
  let tabParent = tab.linkedBrowser.frameLoader.tabParent;
  ok(tabParent, "Found recording tab parent");
  addMessageListener("SaveRecordingFinished", () => {
    let replayingTab = gBrowser.addTab(null, { replayExecution: recordingFile });
    addMessageListener("HitRecordingEndpoint", () => runReplayingTest(replayingTab));
    gBrowser.selectedTab = replayingTab;
  });
  ok(tabParent.saveRecording(recordingFile), "Saved recording");
}

async function runReplayingTest(tab) {
  let client = await attachDebugger(tab);
  await client.interrupt();
  await checkEvaluateInTopFrame(client, "number", lastNumberValue);
  await reverseStepOverToLine(client, 13);
  await setBreakpoint(client, "doc_rr_continuous.html", 14);
  await rewindToLine(client, 14);
  await checkEvaluateInTopFrame(client, "number", lastNumberValue - 1);
  await resumeToLine(client, 14);
  await checkEvaluateInTopFrame(client, "number", lastNumberValue);
  finish();
}

function test() {
  waitForExplicitFinish();

  recordingFile = newRecordingFile();

  let recordingTab = gBrowser.addTab(null, { recordExecution: "*" });
  gBrowser.selectedTab = recordingTab;
  openTrustedLinkIn(EXAMPLE_URL + "doc_rr_continuous.html", "current");

  runRecordingTest(recordingTab);
}
