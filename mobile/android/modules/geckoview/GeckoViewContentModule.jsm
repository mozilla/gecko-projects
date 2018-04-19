/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

var EXPORTED_SYMBOLS = ["GeckoViewContentModule"];

ChromeUtils.import("resource://gre/modules/XPCOMUtils.jsm");

ChromeUtils.defineModuleGetter(this, "EventDispatcher",
  "resource://gre/modules/Messaging.jsm");

XPCOMUtils.defineLazyGetter(this, "dump", () =>
    ChromeUtils.import("resource://gre/modules/AndroidLog.jsm",
                       {}).AndroidLog.d.bind(null, "ViewContentModule"));

// function debug(aMsg) {
//   dump(aMsg);
// }

class GeckoViewContentModule {
  constructor(aModuleName, aMessageManager) {
    this.moduleName = aModuleName;
    this.messageManager = aMessageManager;
    this.eventDispatcher = EventDispatcher.forMessageManager(aMessageManager);

    this.messageManager.addMessageListener(
      "GeckoView:UpdateSettings",
      aMsg => {
        this.settings = aMsg.data;
        this.onSettingsUpdate();
      }
    );
    this.messageManager.addMessageListener(
      "GeckoView:Register",
      aMsg => {
        if (aMsg.data.module == this.moduleName) {
          this.settings = aMsg.data.settings;
          this.register();
        }
      }
    );
    this.messageManager.addMessageListener(
      "GeckoView:Unregister",
      aMsg => {
        if (aMsg.data.module == this.moduleName) {
          this.unregister();
        }
      }
    );

    this.init();
  }

  init() {}
  register() {}
  unregister() {}
  onSettingsUpdate() {}
}
