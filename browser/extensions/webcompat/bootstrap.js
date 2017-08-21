/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const {utils: Cu} = Components;

Cu.import("resource://gre/modules/XPCOMUtils.jsm");
Cu.import("resource://gre/modules/Services.jsm");

const PREF_BRANCH = "extensions.webcompat.";
const PREF_DEFAULTS = {perform_ua_overrides: true};

const UA_OVERRIDES_INIT_TOPIC = "useragentoverrides-initialized";
const UA_ENABLE_PREF_NAME = "extensions.webcompat.perform_ua_overrides";

XPCOMUtils.defineLazyModuleGetter(this, "UAOverrider", "chrome://webcompat/content/lib/ua_overrider.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "UAOverrides", "chrome://webcompat/content/data/ua_overrides.jsm");

let overrider;

function UAEnablePrefObserver() {
  let isEnabled = Services.prefs.getBoolPref(UA_ENABLE_PREF_NAME);
  if (isEnabled && !overrider) {
    overrider = new UAOverrider(UAOverrides);
    overrider.init();
  } else if (!isEnabled && overrider) {
    overrider = null;
  }
}

function setDefaultPrefs() {
  const branch = Services.prefs.getDefaultBranch(PREF_BRANCH);
  for (const [key, val] of Object.entries(PREF_DEFAULTS)) {
    // If someone beat us to setting a default, don't overwrite it.
    if (branch.getPrefType(key) !== branch.PREF_INVALID) {
      continue;
    }

    switch (typeof val) {
      case "boolean":
        branch.setBoolPref(key, val);
        break;
      case "number":
        branch.setIntPref(key, val);
        break;
      case "string":
        branch.setCharPref(key, val);
        break;
    }
  }
}

this.install = function() {};
this.uninstall = function() {};

this.startup = function({webExtension}) {
  setDefaultPrefs();

  // Intentionally reset the preference on every browser restart to avoid site
  // breakage by accidentally toggled preferences or by leaving it off after
  // debugging a site.
  Services.prefs.clearUserPref(UA_ENABLE_PREF_NAME);
  Services.prefs.addObserver(UA_ENABLE_PREF_NAME, UAEnablePrefObserver);

  // Listen to the useragentoverrides-initialized notification we get and
  // initialize our overrider there. This is done to avoid slowing down the
  // apparent startup proces, since we avoid loading anything before the first
  // window is visible to the user. See bug 1371442 for details.
  let startupWatcher = {
    observe(aSubject, aTopic, aData) {
      if (aTopic !== UA_OVERRIDES_INIT_TOPIC) {
        return;
      }

      Services.obs.removeObserver(this, UA_OVERRIDES_INIT_TOPIC);
      overrider = new UAOverrider(UAOverrides);
      overrider.init();
    }
  };
  Services.obs.addObserver(startupWatcher, UA_OVERRIDES_INIT_TOPIC);
};

this.shutdown = function() {
  Services.prefs.removeObserver(UA_ENABLE_PREF_NAME, UAEnablePrefObserver);
};
