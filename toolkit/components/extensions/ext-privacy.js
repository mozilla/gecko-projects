/* -*- Mode: indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim: set sts=2 sw=2 et tw=80: */
"use strict";

const {classes: Cc, interfaces: Ci, utils: Cu} = Components;

XPCOMUtils.defineLazyModuleGetter(this, "Preferences",
                                  "resource://gre/modules/Preferences.jsm");

Cu.import("resource://gre/modules/ExtensionPreferencesManager.jsm");
Cu.import("resource://gre/modules/ExtensionUtils.jsm");
const {
  ExtensionError,
} = ExtensionUtils;

function checkScope(scope) {
  if (scope && scope !== "regular") {
    throw new ExtensionError(
      `Firefox does not support the ${scope} settings scope.`);
  }
}

function getAPI(extension, context, name, callback) {
  let anythingSet = false;
  return {
    async get(details) {
      return {
        levelOfControl: details.incognito ?
          "not_controllable" :
          await ExtensionPreferencesManager.getLevelOfControl(
            extension, name),
        value: await callback(),
      };
    },
    async set(details) {
      checkScope(details.scope);
      if (!anythingSet) {
        anythingSet = true;
        context.callOnClose({
          close: async () => {
            if (["ADDON_DISABLE", "ADDON_UNINSTALL"].includes(extension.shutdownReason)) {
              await ExtensionPreferencesManager.unsetAll(extension);
              anythingSet = false;
            }
          },
        });
      }
      return await ExtensionPreferencesManager.setSetting(
        extension, name, details.value);
    },
    async clear(details) {
      checkScope(details.scope);
      return await ExtensionPreferencesManager.unsetSetting(
        extension, name);
    },
  };
}

// Add settings objects for supported APIs to the preferences manager.
ExtensionPreferencesManager.addSetting("network.networkPredictionEnabled", {
  prefNames: [
    "network.predictor.enabled",
    "network.prefetch-next",
    "network.http.speculative-parallel-limit",
    "network.dns.disablePrefetch",
  ],

  setCallback(value) {
    return {
      "network.http.speculative-parallel-limit": value ? undefined : 0,
      "network.dns.disablePrefetch": !value,
      "network.predictor.enabled": value,
      "network.prefetch-next": value,
    };
  },
});

ExtensionPreferencesManager.addSetting("network.webRTCIPHandlingPolicy", {
  prefNames: [
    "media.peerconnection.ice.default_address_only",
    "media.peerconnection.ice.no_host",
    "media.peerconnection.ice.proxy_only",
  ],

  setCallback(value) {
    let prefs = {};
    // Start with all prefs being reset.
    for (let pref of this.prefNames) {
      prefs[pref] = undefined;
    }
    switch (value) {
      case "default":
        // All prefs are already set to be reset.
        break;

      case "default_public_and_private_interfaces":
        prefs["media.peerconnection.ice.default_address_only"] = true;
        break;

      case "default_public_interface_only":
        prefs["media.peerconnection.ice.default_address_only"] = true;
        prefs["media.peerconnection.ice.no_host"] = true;
        break;

      case "disable_non_proxied_udp":
        prefs["media.peerconnection.ice.proxy_only"] = true;
        break;
    }
    return prefs;
  },
});

ExtensionPreferencesManager.addSetting("websites.hyperlinkAuditingEnabled", {
  prefNames: [
    "browser.send_pings",
  ],

  setCallback(value) {
    return {[this.prefNames[0]]: value};
  },
});

extensions.registerSchemaAPI("privacy.network", "addon_parent", context => {
  let {extension} = context;
  return {
    privacy: {
      network: {
        networkPredictionEnabled: getAPI(extension, context,
          "network.networkPredictionEnabled",
          () => {
            return Preferences.get("network.predictor.enabled") &&
              Preferences.get("network.prefetch-next") &&
              Preferences.get("network.http.speculative-parallel-limit") > 0 &&
              !Preferences.get("network.dns.disablePrefetch");
          }),
        webRTCIPHandlingPolicy: getAPI(extension, context,
          "network.webRTCIPHandlingPolicy",
          () => {
            if (Preferences.get("media.peerconnection.ice.proxy_only")) {
              return "disable_non_proxied_udp";
            }

            let default_address_only =
              Preferences.get("media.peerconnection.ice.default_address_only");
            if (default_address_only) {
              if (Preferences.get("media.peerconnection.ice.no_host")) {
                return "default_public_interface_only";
              }
              return "default_public_and_private_interfaces";
            }

            return "default";
          }),
      },
      websites: {
        hyperlinkAuditingEnabled: getAPI(extension, context,
          "websites.hyperlinkAuditingEnabled",
          () => {
            return Preferences.get("browser.send_pings");
          }),
      },
    },
  };
});
