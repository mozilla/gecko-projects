/* -*- indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim: set ft=javascript ts=2 et sw=2 tw=80: */
/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

// Basic test for saving a recording and then replaying it in a new tab.
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
  finish();
}

function test() {
  waitForExplicitFinish();

  let recordingFile = newRecordingFile();
  let recordingTab = gBrowser.addTab(null, { recordExecution: "*" });

  addMessageListener("RecordingFinished", () => {
    let tabParent = recordingTab.linkedBrowser.frameLoader.tabParent;
    ok(tabParent, "Found recording tab parent");
    addMessageListener("SaveRecordingFinished", () => {
      let replayingTab = gBrowser.addTab(null, { replayExecution: recordingFile });
      addMessageListener("HitRecordingEndpoint", () => runTest(replayingTab));
      gBrowser.selectedTab = replayingTab;
    });
    ok(tabParent.saveRecording(recordingFile), "Saved recording");
  });

  gBrowser.selectedTab = recordingTab;
  openTrustedLinkIn(EXAMPLE_URL + "doc_rr_basic.html", "current");
}
