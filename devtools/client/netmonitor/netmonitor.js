/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { BrowserLoader } = Components.utils.import("resource://devtools/client/shared/browser-loader.js", {});

function Netmonitor(toolbox) {
  const require = window.windowRequire = BrowserLoader({
    baseURI: "resource://devtools/client/netmonitor/",
    window,
    commonLibRequire: toolbox.browserRequire,
  }).require;

  // Inject EventEmitter into netmonitor window.
  const EventEmitter = require("devtools/shared/event-emitter");
  EventEmitter.decorate(window);

  window.NetMonitorController = require("./netmonitor-controller").NetMonitorController;
  window.NetMonitorController._toolbox = toolbox;
  window.NetMonitorController._target = toolbox.target;
}

Netmonitor.prototype = {
  init() {
    const require = window.windowRequire;
    const { createFactory } = require("devtools/client/shared/vendor/react");
    const { render } = require("devtools/client/shared/vendor/react-dom");
    const Provider = createFactory(require("devtools/client/shared/vendor/react-redux").Provider);

    // Components
    const NetworkMonitor = createFactory(require("./components/network-monitor"));

    this.networkMonitor = document.querySelector(".root");

    render(Provider(
      { store: window.gStore },
      NetworkMonitor({ toolbox: window.NetMonitorController._toolbox }),
    ), this.networkMonitor);

    return window.NetMonitorController.startupNetMonitor();
  },

  destroy() {
    const require = window.windowRequire;
    const { unmountComponentAtNode } = require("devtools/client/shared/vendor/react-dom");

    unmountComponentAtNode(this.networkMonitor);

    return window.NetMonitorController.shutdownNetMonitor();
  }
};
