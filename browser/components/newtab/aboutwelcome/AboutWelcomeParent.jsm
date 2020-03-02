/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const EXPORTED_SYMBOLS = ["AboutWelcomeParent"];

const { XPCOMUtils } = ChromeUtils.import(
  "resource://gre/modules/XPCOMUtils.jsm"
);

XPCOMUtils.defineLazyModuleGetters(this, {
  MigrationUtils: "resource:///modules/MigrationUtils.jsm",
});

XPCOMUtils.defineLazyGetter(this, "log", () => {
  const { AboutWelcomeLog } = ChromeUtils.import(
    "resource://activity-stream/aboutwelcome/lib/AboutWelcomeLog.jsm"
  );
  return new AboutWelcomeLog("AboutWelcomeParent.jsm");
});

class AboutWelcomeParent extends JSWindowActorParent {
  /**
   * Handle messages from AboutWelcomeChild.jsm
   *
   * @param {string} type
   * @param {any=} data
   * @param {Browser} browser
   * @param {Window} window
   */
  onContentMessage(type, data, browser, window) {
    log.debug(`Received content event: ${type}`);
    switch (type) {
      case "AWPage:SHOW_MIGRATION_WIZARD":
        MigrationUtils.showMigrationWizard(window, [
          MigrationUtils.MIGRATION_ENTRYPOINT_NEWTAB,
        ]);
        break;
      default:
        log.debug(`Unexpected event ${type} was not handled.`);
    }
  }

  /**
   * @param {{name: string, data?: any}} message
   * @override
   */
  receiveMessage(message) {
    const { name, data } = message;
    let browser;
    let window;

    if (this.manager.rootFrameLoader) {
      browser = this.manager.rootFrameLoader.ownerElement;
      window = browser.ownerGlobal;
      this.onContentMessage(name, data, browser, window);
    } else {
      log.warn(`Not handling ${name} because the browser doesn't exist.`);
    }
  }
}
