/* -*- indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim: set ft=javascript ts=2 et sw=2 tw=80: */
/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

function test() {
  waitForExplicitFinish();

  addMessageListener("RecordingFinished", () => {
    ok(true, "Finished");
    finish();
  });

  var recordingTab = gBrowser.addTab(null, { recordExecution: "*" });
  gBrowser.selectedTab = recordingTab;
  openTrustedLinkIn(EXAMPLE_URL + "doc_rr_basic.html", "current");
}
