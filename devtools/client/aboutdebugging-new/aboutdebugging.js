/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const Services = require("Services");

const { bindActionCreators } = require("devtools/client/shared/vendor/redux");
const { createFactory } = require("devtools/client/shared/vendor/react");
const { render, unmountComponentAtNode } =
  require("devtools/client/shared/vendor/react-dom");
const Provider =
  createFactory(require("devtools/client/shared/vendor/react-redux").Provider);

const FluentReact = require("devtools/client/shared/vendor/fluent-react");
const LocalizationProvider = createFactory(FluentReact.LocalizationProvider);

const actions = require("./src/actions/index");
const { configureStore } = require("./src/create-store");
const {
  setDebugTargetCollapsibilities,
} = require("./src/modules/debug-target-collapsibilities");

const { l10n } = require("./src/modules/l10n");

const {
  addNetworkLocationsObserver,
  getNetworkLocations,
  removeNetworkLocationsObserver,
} = require("./src/modules/network-locations");
const {
  addUSBRuntimesObserver,
  getUSBRuntimes,
  removeUSBRuntimesObserver,
} = require("./src/modules/usb-runtimes");

loader.lazyRequireGetter(this, "adbAddon", "devtools/shared/adb/adb-addon", true);

const Router = createFactory(require("devtools/client/shared/vendor/react-router-dom").HashRouter);
const App = createFactory(require("./src/components/App"));

const AboutDebugging = {
  async init() {
    if (!Services.prefs.getBoolPref("devtools.enabled", true)) {
      // If DevTools are disabled, navigate to about:devtools.
      window.location = "about:devtools?reason=AboutDebugging";
      return;
    }

    this.onAdbAddonUpdated = this.onAdbAddonUpdated.bind(this);
    this.onNetworkLocationsUpdated = this.onNetworkLocationsUpdated.bind(this);
    this.onUSBRuntimesUpdated = this.onUSBRuntimesUpdated.bind(this);

    this.store = configureStore();
    this.actions = bindActionCreators(actions, this.store.dispatch);

    const width = this.getRoundedViewportWidth();
    this.actions.recordTelemetryEvent("open_adbg", { width });

    await l10n.init();

    this.actions.createThisFirefoxRuntime();

    render(
      Provider(
        {
          store: this.store,
        },
        LocalizationProvider(
          { messages: l10n.getBundles() },
          Router(
            {},
            App(
              {}
            )
          )
        )
      ),
      this.mount
    );

    this.onNetworkLocationsUpdated();
    addNetworkLocationsObserver(this.onNetworkLocationsUpdated);

    // Listen to USB runtime updates and retrieve the initial list of runtimes.
    this.onUSBRuntimesUpdated();
    addUSBRuntimesObserver(this.onUSBRuntimesUpdated);

    adbAddon.on("update", this.onAdbAddonUpdated);
    this.onAdbAddonUpdated();

    // Remove deprecated remote debugging extensions.
    await adbAddon.uninstallUnsupportedExtensions();
  },

  onAdbAddonUpdated() {
    this.actions.updateAdbAddonStatus(adbAddon.status);
  },

  onNetworkLocationsUpdated() {
    this.actions.updateNetworkLocations(getNetworkLocations());
  },

  onUSBRuntimesUpdated() {
    this.actions.updateUSBRuntimes(getUSBRuntimes());
  },

  async destroy() {
    const width = this.getRoundedViewportWidth();
    this.actions.recordTelemetryEvent("close_adbg", { width });
    l10n.destroy();

    const state = this.store.getState();
    const currentRuntimeId = state.runtimes.selectedRuntimeId;
    if (currentRuntimeId) {
      await this.actions.unwatchRuntime(currentRuntimeId);
    }

    // Remove all client listeners.
    this.actions.removeRuntimeListeners();

    removeNetworkLocationsObserver(this.onNetworkLocationsUpdated);
    removeUSBRuntimesObserver(this.onUSBRuntimesUpdated);
    adbAddon.off("update", this.onAdbAddonUpdated);
    setDebugTargetCollapsibilities(state.ui.debugTargetCollapsibilities);
    unmountComponentAtNode(this.mount);
  },

  get mount() {
    return document.getElementById("mount");
  },

  /**
   * Computed viewport width, rounded at 50px. Used for telemetry events.
   */
  getRoundedViewportWidth() {
    return Math.ceil(window.outerWidth / 50) * 50;
  },
};

window.addEventListener("DOMContentLoaded", () => {
  AboutDebugging.init();
}, { once: true });

window.addEventListener("unload", () => {
  AboutDebugging.destroy();
}, {once: true});

// Expose AboutDebugging to tests so that they can access to the store.
window.AboutDebugging = AboutDebugging;
