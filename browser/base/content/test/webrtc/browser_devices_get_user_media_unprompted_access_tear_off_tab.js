/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

registerCleanupFunction(function() {
  gBrowser.removeCurrentTab();
});

var gTests = [

{
  desc: "getUserMedia: tearing-off a tab",
  run: function* checkAudioVideoWhileLiveTracksExist_TearingOff() {
    let promise = promisePopupNotificationShown("webRTC-shareDevices");
    yield promiseRequestDevice(true, true);
    yield promise;
    yield expectObserverCalled("getUserMedia:request");
    checkDeviceSelectors(true, true);

    let indicator = promiseIndicatorWindow();
    yield promiseMessage("ok", () => {
      PopupNotifications.panel.firstChild.button.click();
    });
    yield expectObserverCalled("getUserMedia:response:allow");
    yield expectObserverCalled("recording-device-events");
    Assert.deepEqual((yield getMediaCaptureState()), {audio: true, video: true},
                     "expected camera and microphone to be shared");

    yield indicator;
    yield checkSharingUI({video: true, audio: true});

    info("tearing off the tab");
    let win = gBrowser.replaceTabWithWindow(gBrowser.selectedTab);
    yield whenDelayedStartupFinished(win);
    yield checkSharingUI({audio: true, video: true}, win);

    gBrowser.selectedBrowser.messageManager.loadFrameScript(CONTENT_SCRIPT_HELPER, true);

    info("request audio+video and check if there is no prompt");
    yield promiseRequestDevice(true, true, null, null, win.gBrowser.selectedBrowser);
    yield promiseObserverCalled("getUserMedia:request");
    let promises = [promiseNoPopupNotification("webRTC-shareDevices"),
                    promiseObserverCalled("getUserMedia:response:allow"),
                    promiseObserverCalled("recording-device-events")];
    yield Promise.all(promises);

    promises = [promiseObserverCalled("recording-device-events"),
                promiseObserverCalled("recording-device-events"),
                promiseObserverCalled("recording-window-ended")];
    yield BrowserTestUtils.closeWindow(win);
    yield Promise.all(promises);

    yield checkNotSharing();
  }
}

];

function test() {
  waitForExplicitFinish();
  SpecialPowers.pushPrefEnv({"set": [["dom.ipc.processCount", 1]]}, runTest);
}

function runTest() {
  // An empty tab where we can load the content script without leaving it
  // behind at the end of the test.
  gBrowser.addTab();

  let tab = gBrowser.addTab();
  gBrowser.selectedTab = tab;
  let browser = tab.linkedBrowser;

  browser.messageManager.loadFrameScript(CONTENT_SCRIPT_HELPER, true);

  browser.addEventListener("load", function() {
    is(PopupNotifications._currentNotifications.length, 0,
       "should start the test without any prior popup notification");
    ok(gIdentityHandler._identityPopup.hidden,
       "should start the test with the control center hidden");

    Task.spawn(function* () {
      yield SpecialPowers.pushPrefEnv({"set": [[PREF_PERMISSION_FAKE, true]]});

      for (let testCase of gTests) {
        info(testCase.desc);
        yield testCase.run();

        // Cleanup before the next test
        yield expectNoObserverCalled();
      }
    }).then(finish, ex => {
     Cu.reportError(ex);
     ok(false, "Unexpected Exception: " + ex);
     finish();
    });
  }, {capture: true, once: true});
  let rootDir = getRootDirectory(gTestPath);
  rootDir = rootDir.replace("chrome://mochitests/content/",
                            "https://example.com/");
  content.location = rootDir + "get_user_media.html";
}
