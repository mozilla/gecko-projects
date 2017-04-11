/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

var gTests = [

{
  desc: "device sharing animation on background tabs",
  run: function* checkAudioVideo() {
    function* getStreamAndCheckBackgroundAnim(aAudio, aVideo, aSharing) {
      // Get a stream
      let popupPromise = promisePopupNotificationShown("webRTC-shareDevices");
      yield promiseRequestDevice(aAudio, aVideo);
      yield popupPromise;
      yield expectObserverCalled("getUserMedia:request");

      yield promiseMessage("ok", () => {
        PopupNotifications.panel.firstChild.button.click();
      });
      yield expectObserverCalled("getUserMedia:response:allow");
      yield expectObserverCalled("recording-device-events");
      let expected = {};
      if (aVideo)
        expected.video = true;
      if (aAudio)
        expected.audio = true;
      Assert.deepEqual((yield getMediaCaptureState()), expected,
                       "expected " + Object.keys(expected).join(" and ") +
                       " to be shared");


      // Check the attribute on the tab, and check there's no visible
      // sharing icon on the tab
      let tab = gBrowser.selectedTab;
      is(tab.getAttribute("sharing"), aSharing,
         "the tab has the attribute to show the " + aSharing + " icon");
      let icon =
        document.getAnonymousElementByAttribute(tab, "anonid", "sharing-icon");
      is(window.getComputedStyle(icon).display, "none",
         "the animated sharing icon of the tab is hidden");

      // After selecting a new tab, check the attribute is still there,
      // and the icon is now visible.
      yield BrowserTestUtils.switchTab(gBrowser, gBrowser.addTab());
      is(gBrowser.selectedTab.getAttribute("sharing"), "",
         "the new tab doesn't have the 'sharing' attribute");
      is(tab.getAttribute("sharing"), aSharing,
         "the tab still has the 'sharing' attribute");
      isnot(window.getComputedStyle(icon).display, "none",
            "the animated sharing icon of the tab is now visible");

      // Ensure the icon disappears when selecting the tab.
      yield BrowserTestUtils.removeTab(gBrowser.selectedTab);
      ok(tab.selected, "the tab with ongoing sharing is selected again");
      is(window.getComputedStyle(icon).display, "none",
         "the animated sharing icon is gone after selecting the tab again");

      // And finally verify the attribute is removed when closing the stream.
      yield closeStream();

      // TODO(Bug 1304997): Fix the race in closeStream() and remove this
      // promiseWaitForCondition().
      yield promiseWaitForCondition(() => !tab.getAttribute("sharing"));
      is(tab.getAttribute("sharing"), "",
         "the tab no longer has the 'sharing' attribute after closing the stream");
    }

    yield getStreamAndCheckBackgroundAnim(true, true, "camera");
    yield getStreamAndCheckBackgroundAnim(false, true, "camera");
    yield getStreamAndCheckBackgroundAnim(true, false, "microphone");
  }
}

];

add_task(async function test() {
  await runTests(gTests);
});
