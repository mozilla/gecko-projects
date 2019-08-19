/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

var Services = require("Services");
loader.lazyRequireGetter(this, "Tools", "devtools/client/definitions", true);
loader.lazyRequireGetter(
  this,
  "DebuggerClient",
  "devtools/shared/client/debugger-client",
  true
);
loader.lazyRequireGetter(this, "l10n", "devtools/client/webconsole/utils/l10n");
loader.lazyRequireGetter(
  this,
  "BrowserConsole",
  "devtools/client/webconsole/browser-console"
);

const BC_WINDOW_FEATURES =
  "chrome,titlebar,toolbar,centerscreen,resizable,dialog=no";

class BrowserConsoleManager {
  constructor() {
    this._browserConsole = null;
    this._browserConsoleInitializing = null;
    this._browerConsoleSessionState = false;
  }

  storeBrowserConsoleSessionState() {
    this._browerConsoleSessionState = !!this.getBrowserConsole();
  }

  getBrowserConsoleSessionState() {
    return this._browerConsoleSessionState;
  }

  /**
   * Open a Browser Console for the given target.
   *
   * @see devtools/framework/target.js for details about targets.
   *
   * @param object target
   *        The target that the browser console will connect to.
   * @param nsIDOMWindow iframeWindow
   *        The window where the browser console UI is already loaded.
   * @param Boolean fissionSupport
   * @return object
   *         A promise object for the opening of the new BrowserConsole instance.
   */
  async openBrowserConsole(target, win, fissionSupport = false) {
    const hud = new BrowserConsole(target, win, win, fissionSupport);
    this._browserConsole = hud;
    hud.once("destroyed", () => {
      this._browserConsole = null;
    });
    await hud.init();
    return hud;
  }

  /**
   * Toggle the Browser Console.
   */
  async toggleBrowserConsole() {
    if (this._browserConsole) {
      const hud = this._browserConsole;
      return hud.destroy();
    }

    if (this._browserConsoleInitializing) {
      return this._browserConsoleInitializing;
    }

    const fissionSupport = Services.prefs.getBoolPref(
      "devtools.browsertoolbox.fission",
      false
    );

    async function connect() {
      // The Browser console ends up using the debugger in autocomplete.
      // Because the debugger can't be running in the same compartment than its debuggee,
      // we have to load the server in a dedicated Loader, flagged with
      // `freshCompartment`, which will force it to be loaded in another compartment.
      // We aren't using `invisibleToDebugger` in order to allow the Browser toolbox to
      // debug the Browser console. This is fine as they will spawn distinct Loaders and
      // so distinct `DebuggerServer` and actor modules.
      const ChromeUtils = require("ChromeUtils");
      const { DevToolsLoader } = ChromeUtils.import(
        "resource://devtools/shared/Loader.jsm"
      );
      const loader = new DevToolsLoader({
        freshCompartment: true,
      });
      const { DebuggerServer } = loader.require(
        "devtools/server/debugger-server"
      );

      DebuggerServer.init();

      // Ensure that the root actor and the target-scoped actors have been registered on
      // the DebuggerServer, so that the Browser Console can retrieve the console actors.
      // (See Bug 1416105 for rationale).
      DebuggerServer.registerActors({ root: true, target: true });

      DebuggerServer.allowChromeProcess = true;

      const client = new DebuggerClient(DebuggerServer.connectPipe());
      await client.connect();
      return client.mainRoot.getMainProcess();
    }

    async function openWindow(t) {
      const win = Services.ww.openWindow(
        null,
        Tools.webConsole.url,
        "_blank",
        BC_WINDOW_FEATURES,
        null
      );

      await new Promise(resolve => {
        win.addEventListener("DOMContentLoaded", resolve, { once: true });
      });

      const title = fissionSupport
        ? `💥 Fission Browser Console 💥`
        : l10n.getStr("browserConsole.title");
      win.document.title = title;
      return win;
    }

    // Temporarily cache the async startup sequence so that if toggleBrowserConsole
    // gets called again we can return this console instead of opening another one.
    this._browserConsoleInitializing = (async () => {
      const target = await connect();
      await target.attach();
      const win = await openWindow(target);
      const browserConsole = await this.openBrowserConsole(
        target,
        win,
        fissionSupport
      );
      return browserConsole;
    })();

    const browserConsole = await this._browserConsoleInitializing;
    this._browserConsoleInitializing = null;
    return browserConsole;
  }

  /**
   * Opens or focuses the Browser Console.
   */
  openBrowserConsoleOrFocus() {
    const hud = this.getBrowserConsole();
    if (hud) {
      hud.iframeWindow.focus();
      return Promise.resolve(hud);
    }

    return this.toggleBrowserConsole();
  }

  /**
   * Get the Browser Console instance, if open.
   *
   * @return object|null
   *         A BrowserConsole instance or null if the Browser Console is not
   *         open.
   */
  getBrowserConsole() {
    return this._browserConsole;
  }
}

exports.BrowserConsoleManager = new BrowserConsoleManager();
