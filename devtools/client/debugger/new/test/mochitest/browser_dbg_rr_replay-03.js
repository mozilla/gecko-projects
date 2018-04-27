/* -*- indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim: set ft=javascript ts=2 et sw=2 tw=80: */
/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

// Test for saving a recording and then replaying it in a new tab, with rewinding disabled.
async function test() {
  waitForExplicitFinish();

  await pushPref("devtools.recordreplay.enableRewinding", false);

  let recordingFile = newRecordingFile();
  let recordingTab = gBrowser.addTab(null, { recordExecution: "*" });

  addMessageListener("RecordingFinished", () => {
    let tabParent = recordingTab.linkedBrowser.frameLoader.tabParent;
    ok(tabParent, "Found recording tab parent");
    addMessageListener("SaveRecordingFinished", () => {
      let replayingTab = gBrowser.addTab(null, { replayExecution: recordingFile });
      addMessageListener("HitRecordingEndpoint", () => {
        ok(true, "Replayed to end of recording");
        finish();
      });
      gBrowser.selectedTab = replayingTab;
    });
    ok(tabParent.saveRecording(recordingFile), "Saved recording");
  });

  gBrowser.selectedTab = recordingTab;
  openUILinkIn(EXAMPLE_URL + "doc_rr_basic.html", "current");
}
