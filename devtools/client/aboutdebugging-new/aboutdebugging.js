/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { BrowserLoader } =
  ChromeUtils.import("resource://devtools/client/shared/browser-loader.js", {});
const { require } = BrowserLoader({
  baseURI: "resource://devtools/client/aboutdebugging-new/",
  window,
});
const Services = require("Services");

const { createFactory } =
  require("devtools/client/shared/vendor/react");
const { render, unmountComponentAtNode } =
  require("devtools/client/shared/vendor/react-dom");

const App = createFactory(require("./src/components/App"));

const AboutDebugging = {
  init() {
    if (!Services.prefs.getBoolPref("devtools.enabled", true)) {
      // If DevTools are disabled, navigate to about:devtools.
      window.location = "about:devtools?reason=AboutDebugging";
      return;
    }

    render(App(), this.mount);
  },

  destroy() {
    unmountComponentAtNode(this.mount);
  },

  get mount() {
    return document.getElementById("mount");
  },
};

window.addEventListener("DOMContentLoaded", () => {
  AboutDebugging.init();
}, { once: true });

window.addEventListener("unload", () => {
  AboutDebugging.destroy();
}, {once: true});
