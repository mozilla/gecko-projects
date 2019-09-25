/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

/**
 * This file initializes the profiler popup UI. It is in charge of initializing
 * the browser specific environment, and then passing those requirements into
 * the UI. The popup is enabled by toggle the following in the browser menu:
 *
 * Tools -> Web Developer -> Enable Profiler Toolbar Icon
 */

/**
 * Initialize the require function through a BrowserLoader. This loader ensures
 * that the popup can use require and has access to the window object.
 */
const { BrowserLoader } = ChromeUtils.import(
  "resource://devtools/client/shared/browser-loader.js"
);
const { require } = BrowserLoader({
  baseURI: "resource://devtools/client/performance-new/popup/",
  window,
});

/**
 * The background.jsm manages the profiler state, and can be loaded multiple time
 * for various components. This pop-up needs a copy, and it is also used by the
 * profiler shortcuts. In order to do this, the background code needs to live in a
 * JSM module, that can be shared with the DevTools keyboard shortcut manager.
 */
const {
  getRecordingPreferencesFromBrowser,
  setRecordingPreferencesOnBrowser,
} = ChromeUtils.import(
  "resource://devtools/client/performance-new/popup/background.jsm"
);

const { receiveProfile } = require("devtools/client/performance-new/browser");

const Perf = require("devtools/client/performance-new/components/Perf");
const ReactDOM = require("devtools/client/shared/vendor/react-dom");
const React = require("devtools/client/shared/vendor/react");
const createStore = require("devtools/client/shared/redux/create-store");
const selectors = require("devtools/client/performance-new/store/selectors");
const reducers = require("devtools/client/performance-new/store/reducers");
const actions = require("devtools/client/performance-new/store/actions");
const { Provider } = require("devtools/client/shared/vendor/react-redux");
const {
  ActorReadyGeckoProfilerInterface,
} = require("devtools/server/performance-new/gecko-profiler-interface");

document.addEventListener("DOMContentLoaded", () => {
  gInit();
});

/**
 * Initialize the panel by creating a redux store, and render the root component.
 *
 * @param perfFront - The Perf actor's front. Used to start and stop recordings.
 * @param preferenceFront - Used to get the recording preferences from the device.
 */
async function gInit(perfFront, preferenceFront) {
  const store = createStore(reducers);

  // Do some initialization, especially with privileged things that are part of the
  // the browser.
  store.dispatch(
    actions.initializeStore({
      perfFront: new ActorReadyGeckoProfilerInterface(),
      receiveProfile,
      // Pull the default recording settings from the reducer, and update them according
      // to what's in the browser's preferences.
      recordingSettingsFromPreferences: getRecordingPreferencesFromBrowser(
        selectors.getRecordingSettings(store.getState())
      ),
      // In the popup, the preferences are stored directly on the current browser.
      setRecordingPreferences: () =>
        setRecordingPreferencesOnBrowser(
          selectors.getRecordingSettings(store.getState())
        ),
      isPopup: true,
    })
  );

  ReactDOM.render(
    React.createElement(Provider, { store }, React.createElement(Perf)),
    document.querySelector("#root")
  );

  resizeWindow();
}

function resizeWindow() {
  window.requestAnimationFrame(() => {
    if (window.gResizePopup) {
      window.gResizePopup(document.body.clientHeight);
    }
  });
}
