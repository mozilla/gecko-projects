/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

var EXPORTED_SYMBOLS = ["PictureInPicture"];

const {Services} = ChromeUtils.import("resource://gre/modules/Services.jsm");

const PLAYER_URI = "chrome://global/content/pictureinpicture/player.xhtml";
const PLAYER_FEATURES = `chrome,titlebar=no,alwaysontop,lockaspectratio,resizable`;
const WINDOW_TYPE = "Toolkit:PictureInPicture";

/**
 * This module is responsible for creating a Picture in Picture window to host
 * a clone of a video element running in web content.
 */

var PictureInPicture = {
  // Listeners are added in nsBrowserGlue.js lazily
  receiveMessage(aMessage) {
    let browser = aMessage.target;

    switch (aMessage.name) {
      case "PictureInPicture:Request": {
        let videoData = aMessage.data;
        this.handlePictureInPictureRequest(browser, videoData);
        break;
      }
      case "PictureInPicture:Close": {
        /**
         * Content has requested that its Picture in Picture window go away.
         */
        this.closePipWindow();
        break;
      }
      case "PictureInPicture:Playing": {
        let controls = this.weakPipControls && this.weakPipControls.get();
        if (controls) {
          controls.classList.add("playing");
        }
        break;
      }
      case "PictureInPicture:Paused": {
        let controls = this.weakPipControls && this.weakPipControls.get();
        if (controls) {
          controls.classList.remove("playing");
        }
        break;
      }
    }
  },

  focusTabAndClosePip() {
    let gBrowser = this.browser.ownerGlobal.gBrowser;
    let tab = gBrowser.getTabForBrowser(this.browser);
    gBrowser.selectedTab = tab;
    this.unload();
    this.closePipWindow();
  },

  /**
   * Find and close any pre-existing Picture in Picture windows.
   */
  closePipWindow() {
    // This uses an enumerator, but there really should only be one of
    // these things.
    for (let win of Services.wm.getEnumerator(WINDOW_TYPE)) {
      if (win.closed) {
        continue;
      }
      win.close();
    }
  },

  /**
   * A request has come up from content to open a Picture in Picture
   * window.
   *
   * @param browser (xul:browser)
   *   The browser that is requesting the Picture in Picture window.
   *
   * @param videoData (object)
   *   An object containing the following properties:
   *
   *   videoHeight (int):
   *     The preferred height of the video.
   *
   *   videoWidth (int):
   *     The preferred width of the video.
   *
   * @returns Promise
   *   Resolves once the Picture in Picture window has been created, and
   *   the player component inside it has finished loading.
   */
  async handlePictureInPictureRequest(browser, videoData) {
    this.browser = browser;
    let parentWin = browser.ownerGlobal;
    this.closePipWindow();
    let win = await this.openPipWindow(parentWin, videoData);
    let controls = win.document.getElementById("controls");
    this.weakPipControls = Cu.getWeakReference(controls);
    if (videoData.playing) {
      controls.classList.add("playing");
    }
    win.setupPlayer(browser, videoData);
  },

  /**
   * unload event has been called in player.js, cleanup our preserved
   * browser object.
   */
  unload() {
    delete this.weakPipControls;
    delete this.browser;
  },

  /**
   * Open a Picture in Picture window on the same screen as parentWin,
   * sized based on the information in videoData.
   *
   * @param parentWin (chrome window)
   *   The window hosting the browser that requested the Picture in
   *   Picture window.
   *
   * @param videoData (object)
   *   An object containing the following properties:
   *
   *   videoHeight (int):
   *     The preferred height of the video.
   *
   *   videoWidth (int):
   *     The preferred width of the video.
   *
   * @returns Promise
   *   Resolves once the window has opened and loaded the player component.
   */
  async openPipWindow(parentWin, videoData) {
    let { videoHeight, videoWidth } = videoData;

    // The Picture in Picture window will open on the same display as the
    // originating window, and anchor to the bottom right.
    let screenManager = Cc["@mozilla.org/gfx/screenmanager;1"]
                          .getService(Ci.nsIScreenManager);
    let screen = screenManager.screenForRect(parentWin.screenX,
                                             parentWin.screenY, 1, 1);

    // Now that we have the right screen, let's see how much available
    // real-estate there is for us to work with.
    let screenLeft = {}, screenTop = {}, screenWidth = {}, screenHeight = {};
    screen.GetAvailRectDisplayPix(screenLeft, screenTop, screenWidth,
                                  screenHeight);

    // We have to divide these dimensions by the CSS scale factor for the
    // display in order for the video to be positioned correctly on displays
    // that are not at a 1.0 scaling.
    screenWidth.value = screenWidth.value / screen.defaultCSSScaleFactor;
    screenHeight.value = screenHeight.value / screen.defaultCSSScaleFactor;

    // For now, the Picture in Picture window will be a maximum of a quarter
    // of the screen height, and a third of the screen width.
    const MAX_HEIGHT = screenHeight.value / 4;
    const MAX_WIDTH = screenWidth.value / 3;

    let resultWidth = videoWidth;
    let resultHeight = videoHeight;

    if (videoHeight > MAX_HEIGHT || videoWidth > MAX_WIDTH) {
      let aspectRatio = videoWidth / videoHeight;
      // We're bigger than the max - take the largest dimension and clamp
      // it to the associated max. Recalculate the other dimension to maintain
      // aspect ratio.
      if (videoWidth >= videoHeight) {
        // We're clamping the width, so the height must be adjusted to match
        // the original aspect ratio. Since aspect ratio is width over height,
        // that means we need to _divide_ the MAX_WIDTH by the aspect ratio to
        // calculate the appropriate height.
        resultWidth = MAX_WIDTH;
        resultHeight = Math.round(MAX_WIDTH / aspectRatio);
      } else {
        // We're clamping the height, so the width must be adjusted to match
        // the original aspect ratio. Since aspect ratio is width over height,
        // this means we need to _multiply_ the MAX_HEIGHT by the aspect ratio
        // to calculate the appropriate width.
        resultHeight = MAX_HEIGHT;
        resultWidth = Math.round(MAX_HEIGHT * aspectRatio);
      }
    }

    // Now that we have the dimensions of the video, we need to figure out how
    // to position it in the bottom right corner. Since we know the width of the
    // available rect, we need to subtract the dimensions of the window we're
    // opening to get the top left coordinates that openWindow expects.
    let pipLeft = screenWidth.value - resultWidth;
    let pipTop = screenHeight.value - resultHeight;
    let features = `${PLAYER_FEATURES},top=${pipTop},left=${pipLeft},` +
                   `outerWidth=${resultWidth},outerHeight=${resultHeight}`;

    let pipWindow =
      Services.ww.openWindow(parentWin, PLAYER_URI, null, features, null);

    return new Promise(resolve => {
      pipWindow.addEventListener("load", () => {
        resolve(pipWindow);
      }, { once: true });
    });
  },
};
