/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

ChromeUtils.import("resource://gre/modules/XPCOMUtils.jsm");
ChromeUtils.import("resource://gre/modules/Services.jsm");
ChromeUtils.import("resource://gre/modules/AppConstants.jsm");

XPCOMUtils.defineLazyModuleGetters(this, {
  NetUtil: "resource://gre/modules/NetUtil.jsm",
  Policies: "resource:///modules/policies/Policies.jsm",
  PoliciesValidator: "resource:///modules/policies/PoliciesValidator.jsm",
});

// This is the file that will be searched for in the
// ${InstallDir}/distribution folder.
const POLICIES_FILENAME = "policies.json";

// For easy testing, modify the helpers/sample.json file,
// and set PREF_ALTERNATE_PATH in firefox.js as:
// /your/repo/browser/components/enterprisepolicies/helpers/sample.json
const PREF_ALTERNATE_PATH     = "browser.policies.alternatePath";

// This pref is meant to be temporary: it will only be used while we're
// testing this feature without rolling it out officially. When the
// policy engine is released, this pref should be removed.
const PREF_ENABLED            = "browser.policies.enabled";
const PREF_LOGLEVEL           = "browser.policies.loglevel";

XPCOMUtils.defineLazyGetter(this, "log", () => {
  let { ConsoleAPI } = ChromeUtils.import("resource://gre/modules/Console.jsm", {});
  return new ConsoleAPI({
    prefix: "Enterprise Policies",
    // tip: set maxLogLevel to "debug" and use log.debug() to create detailed
    // messages during development. See LOG_LEVELS in Console.jsm for details.
    maxLogLevel: "error",
    maxLogLevelPref: PREF_LOGLEVEL,
  });
});

// ==== Start XPCOM Boilerplate ==== \\

// Factory object
const EnterprisePoliciesFactory = {
  _instance: null,
  createInstance: function BGSF_createInstance(outer, iid) {
    if (outer != null)
      throw Components.results.NS_ERROR_NO_AGGREGATION;
    return this._instance == null ?
      this._instance = new EnterprisePoliciesManager() : this._instance;
  }
};

// ==== End XPCOM Boilerplate ==== //

// Constructor
function EnterprisePoliciesManager() {
  Services.obs.addObserver(this, "profile-after-change", true);
  Services.obs.addObserver(this, "final-ui-startup", true);
  Services.obs.addObserver(this, "sessionstore-windows-restored", true);
  Services.obs.addObserver(this, "EnterprisePolicies:Restart", true);
}

EnterprisePoliciesManager.prototype = {
  // for XPCOM
  classID:          Components.ID("{ea4e1414-779b-458b-9d1f-d18e8efbc145}"),
  QueryInterface: XPCOMUtils.generateQI([Ci.nsIObserver,
                                         Ci.nsISupportsWeakReference,
                                         Ci.nsIEnterprisePolicies]),

  // redefine the default factory for XPCOMUtils
  _xpcom_factory: EnterprisePoliciesFactory,

  _initialize() {
    if (!Services.prefs.getBoolPref(PREF_ENABLED, false)) {
      this.status = Ci.nsIEnterprisePolicies.INACTIVE;
      return;
    }

    let provider = this._chooseProvider();

    if (!provider) {
      this.status = Ci.nsIEnterprisePolicies.INACTIVE;
      return;
    }

    if (provider.failed) {
      this.status = Ci.nsIEnterprisePolicies.FAILED;
      return;
    }

    this.status = Ci.nsIEnterprisePolicies.ACTIVE;
    this._activatePolicies(provider.policies);
  },

  _chooseProvider() {
    // TODO: Bug 1433136 - Add GPO provider with higher precendence here

    let jsonProvider = new JSONPoliciesProvider();
    if (jsonProvider.hasPolicies) {
      return jsonProvider;
    }

    return null;
  },

  _activatePolicies(unparsedPolicies) {
    let { schema } = ChromeUtils.import("resource:///modules/policies/schema.jsm", {});

    for (let policyName of Object.keys(unparsedPolicies)) {
      let policySchema = schema.properties[policyName];
      let policyParameters = unparsedPolicies[policyName];

      if (!policySchema) {
        log.error(`Unknown policy: ${policyName}`);
        continue;
      }

      let [parametersAreValid, parsedParameters] =
        PoliciesValidator.validateAndParseParameters(policyParameters,
                                                     policySchema);

      if (!parametersAreValid) {
        log.error(`Invalid parameters specified for ${policyName}.`);
        continue;
      }

      let policyImpl = Policies[policyName];

      for (let timing of Object.keys(this._callbacks)) {
        let policyCallback = policyImpl["on" + timing];
        if (policyCallback) {
          this._schedulePolicyCallback(
            timing,
            policyCallback.bind(null,
                                this, /* the EnterprisePoliciesManager */
                                parsedParameters));
        }
      }
    }
  },

  _callbacks: {
    BeforeAddons: [],
    ProfileAfterChange: [],
    BeforeUIStartup: [],
    AllWindowsRestored: [],
  },

  _schedulePolicyCallback(timing, callback) {
    this._callbacks[timing].push(callback);
  },

  _runPoliciesCallbacks(timing) {
    let callbacks = this._callbacks[timing];
    while (callbacks.length > 0) {
      let callback = callbacks.shift();
      try {
        callback();
      } catch (ex) {
        log.error("Error running ", callback, `for ${timing}:`, ex);
      }
    }
  },

  async _restart() {
    if (!Cu.isInAutomation) {
      return;
    }

    DisallowedFeatures = {};

    this._status = Ci.nsIEnterprisePolicies.UNINITIALIZED;
    for (let timing of Object.keys(this._callbacks)) {
      this._callbacks[timing] = [];
    }
    delete Services.ppmm.initialProcessData.policies;
    Services.ppmm.broadcastAsyncMessage("EnterprisePolicies:Restart", null);

    let { PromiseUtils } = ChromeUtils.import("resource://gre/modules/PromiseUtils.jsm",
                                              {});

    // Simulate the startup process. This step-by-step is a bit ugly but it
    // tries to emulate the same behavior as of a normal startup.

    await PromiseUtils.idleDispatch(() => {
      this.observe(null, "policies-startup", null);
    });

    await PromiseUtils.idleDispatch(() => {
      this.observe(null, "profile-after-change", null);
    });

    await PromiseUtils.idleDispatch(() => {
      this.observe(null, "final-ui-startup", null);
    });

    await PromiseUtils.idleDispatch(() => {
      this.observe(null, "sessionstore-windows-restored", null);
    });
  },

  // nsIObserver implementation
  observe: function BG_observe(subject, topic, data) {
    switch (topic) {
      case "policies-startup":
        this._initialize();
        this._runPoliciesCallbacks("BeforeAddons");
        break;

      case "profile-after-change":
        // Before the first set of policy callbacks runs, we must
        // initialize the service.
        this._runPoliciesCallbacks("ProfileAfterChange");
        break;

      case "final-ui-startup":
        this._runPoliciesCallbacks("BeforeUIStartup");
        break;

      case "sessionstore-windows-restored":
        this._runPoliciesCallbacks("AllWindowsRestored");

        // After the last set of policy callbacks ran, notify the test observer.
        Services.obs.notifyObservers(null,
                                     "EnterprisePolicies:AllPoliciesApplied");
        break;

      case "EnterprisePolicies:Restart":
        this._restart().then(null, Cu.reportError);
        break;
    }
  },

  disallowFeature(feature, neededOnContentProcess = false) {
    DisallowedFeatures[feature] = true;

    // NOTE: For optimization purposes, only features marked as needed
    // on content process will be passed onto the child processes.
    if (neededOnContentProcess) {
      Services.ppmm.initialProcessData.policies
                                      .disallowedFeatures.push(feature);

      if (Services.ppmm.childCount > 1) {
        // If there has been a content process already initialized, let's
        // broadcast the newly disallowed feature.
        Services.ppmm.broadcastAsyncMessage(
          "EnterprisePolicies:DisallowFeature", {feature}
        );
      }
    }
  },

  // ------------------------------
  // public nsIEnterprisePolicies members
  // ------------------------------

  _status: Ci.nsIEnterprisePolicies.UNINITIALIZED,

  set status(val) {
    this._status = val;
    if (val != Ci.nsIEnterprisePolicies.INACTIVE) {
      Services.ppmm.initialProcessData.policies = {
        status: val,
        disallowedFeatures: [],
      };
    }
    return val;
  },

  get status() {
    return this._status;
  },

  isAllowed: function BG_sanitize(feature) {
    return !(feature in DisallowedFeatures);
  },
};

let DisallowedFeatures = {};


/*
 * JSON PROVIDER OF POLICIES
 *
 * This is a platform-agnostic provider which looks for
 * policies specified through a policies.json file stored
 * in the installation's distribution folder.
 */

class JSONPoliciesProvider {
  constructor() {
    this._policies = null;
    this._failed = false;
    this._readData();
  }

  get hasPolicies() {
    return this._policies !== null || this._failed;
  }

  get policies() {
    return this._policies;
  }

  get failed() {
    return this._failed;
  }

  _getConfigurationFile() {
    let configFile = Services.dirsvc.get("XREAppDist", Ci.nsIFile);
    configFile.append(POLICIES_FILENAME);

    let alternatePath = Services.prefs.getStringPref(PREF_ALTERNATE_PATH, "");

    if (alternatePath && !configFile.exists()) {
      // We only want to use the alternate file path if the file on the install
      // folder doesn't exist. Otherwise it'd be possible for a user to override
      // the admin-provided policies by changing the user-controlled prefs.
      // This pref is only meant for tests, so it's fine to use this extra
      // synchronous configFile.exists() above.
      configFile = Cc["@mozilla.org/file/local;1"]
                     .createInstance(Ci.nsIFile);
      configFile.initWithPath(alternatePath);
    }

    return configFile;
  }

  _readData() {
    try {
      let data = Cu.readUTF8File(this._getConfigurationFile());
      if (data) {
        this._policies = JSON.parse(data).policies;
      }
    } catch (ex) {
      if (ex instanceof Components.Exception &&
          ex.result == Cr.NS_ERROR_FILE_NOT_FOUND) {
        // Do nothing, _policies will remain null
      } else if (ex instanceof SyntaxError) {
        log.error("Error parsing JSON file");
        this._failed = true;
      } else {
        log.error("Error reading file");
        this._failed = true;
      }
    }
  }
}


var components = [EnterprisePoliciesManager];
this.NSGetFactory = XPCOMUtils.generateNSGetFactory(components);
