/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

ChromeUtils.import("resource://gre/modules/XPCOMUtils.jsm");

XPCOMUtils.defineLazyModuleGetters(this, {
  GeckoViewUtils: "resource://gre/modules/GeckoViewUtils.jsm",
  Services: "resource://gre/modules/Services.jsm",
});

function GeckoViewStartup() {
}

GeckoViewStartup.prototype = {
  classID: Components.ID("{8e993c34-fdd6-432c-967e-f995d888777f}"),

  QueryInterface: XPCOMUtils.generateQI([Ci.nsIObserver]),

  /**
   * Register resource://android as the APK root.
   *
   * Consumers can access Android assets using resource://android/assets/FILENAME.
   */
  setResourceSubstitutions: function() {
    let registry = Cc["@mozilla.org/chrome/chrome-registry;1"].getService(Ci.nsIChromeRegistry);
    // Like jar:jar:file:///data/app/org.mozilla.geckoview.test.apk!/assets/omni.ja!/chrome/geckoview/content/geckoview.js
    let url = registry.convertChromeURL(Services.io.newURI("chrome://geckoview/content/geckoview.js")).spec;
    // Like jar:file:///data/app/org.mozilla.geckoview.test.apk!/
    url = url.substring(4, url.indexOf("!/") + 2);

    let protocolHandler = Services.io.getProtocolHandler("resource").QueryInterface(Ci.nsIResProtocolHandler);
    protocolHandler.setSubstitution("android", Services.io.newURI(url));
  },

  /* ----------  nsIObserver  ---------- */
  observe: function(aSubject, aTopic, aData) {
    switch (aTopic) {
      case "app-startup": {
        this.setResourceSubstitutions();

        // Parent and content process.
        Services.obs.addObserver(this, "chrome-document-global-created");
        Services.obs.addObserver(this, "content-document-global-created");

        GeckoViewUtils.addLazyGetter(this, "GeckoViewPermission", {
          service: "@mozilla.org/content-permission/prompt;1",
          observers: [
            "getUserMedia:ask-device-permission",
            "getUserMedia:request",
            "PeerConnection:request",
          ],
        });

        if (Services.appinfo.processType != Services.appinfo.PROCESS_TYPE_DEFAULT) {
          // Content process only.
          GeckoViewUtils.addLazyGetter(this, "GeckoViewPrompt", {
            service: "@mozilla.org/prompter;1",
          });
        }
        break;
      }

      case "profile-after-change": {
        // Parent process only.
        // ContentPrefServiceParent is needed for e10s file picker.
        GeckoViewUtils.addLazyGetter(this, "ContentPrefServiceParent", {
          module: "resource://gre/modules/ContentPrefServiceParent.jsm",
          init: cpsp => cpsp.alwaysInit(),
          ppmm: [
            "ContentPrefs:FunctionCall",
            "ContentPrefs:AddObserverForName",
            "ContentPrefs:RemoveObserverForName",
          ],
        });

        GeckoViewUtils.addLazyGetter(this, "GeckoViewPrompt", {
          service: "@mozilla.org/prompter;1",
          mm: [
            "GeckoView:Prompt",
          ],
        });
        break;
      }

      case "chrome-document-global-created":
      case "content-document-global-created": {
        let win = GeckoViewUtils.getChromeWindow(aSubject);
        if (win !== aSubject) {
          // Only attach to top-level windows.
          return;
        }

        GeckoViewUtils.addLazyEventListener(win, ["click", "contextmenu"], {
          handler: _ => this.GeckoViewPrompt,
          options: {
            capture: false,
            mozSystemGroup: true,
          },
        });
        break;
      }
    }
  },
};

this.NSGetFactory = XPCOMUtils.generateNSGetFactory([GeckoViewStartup]);
