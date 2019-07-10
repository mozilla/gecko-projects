/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Preference Experiments temporarily change a preference to one of several test
 * values for the duration of the experiment. Telemetry packets are annotated to
 * show what experiments are active, and we use this data to measure the
 * effectiveness of the preference change.
 *
 * Info on active and past experiments is stored in a JSON file in the profile
 * folder.
 *
 * Active preference experiments are stopped if they aren't active on the recipe
 * server. They also expire if Firefox isn't able to contact the recipe server
 * after a period of time, as well as if the user modifies the preference during
 * an active experiment.
 */

/**
 * Experiments store info about an active or expired preference experiment.
 * @typedef {Object} Experiment
 * @property {string} name
 *   Unique name of the experiment
 * @property {string|null} userFacingName
 *   A user-friendly name for the experiment. Null on old-style
 *   single-preference experiments, which do not have a
 *   userFacingName.
 * @property {string|null} userFacingDescription
 *   A user-friendly description of the experiment. Null on old-style
 *   single-preference experiments, which do not have a
 *   userFacingDescription.
 * @property {string} branch
 *   Experiment branch that the user was matched to
 * @property {boolean} expired
 *   If false, the experiment is active.
 * @property {string} lastSeen
 *   ISO-formatted date string of when the experiment was last seen from the
 *   recipe server.
 * @property {Object} preferences
 *   An object consisting of all the preferences that are set by this experiment.
 *   Keys are the name of each preference affected by this experiment.
 *   Values are Preference Objects, about which see below.
 * @property {string} experimentType
 *   The type to report to Telemetry's experiment marker API.
 */

/**
 * Each Preference stores information about a preference that an
 * experiment sets.
 * @property {string|integer|boolean} preferenceValue
 *   Value to change the preference to during the experiment.
 * @property {string} preferenceType
 *   Type of the preference value being set.
 * @property {string|integer|boolean|undefined} previousPreferenceValue
 *   Value of the preference prior to the experiment, or undefined if it was
 *   unset.
 * @property {PreferenceBranchType} preferenceBranchType
 *   Controls how we modify the preference to affect the client.
 *
 *   If "default", when the experiment is active, the default value for the
 *   preference is modified on startup of the add-on. If "user", the user value
 *   for the preference is modified when the experiment starts, and is reset to
 *   its original value when the experiment ends.
 */

"use strict";

ChromeUtils.defineModuleGetter(
  this,
  "Services",
  "resource://gre/modules/Services.jsm"
);
ChromeUtils.defineModuleGetter(
  this,
  "CleanupManager",
  "resource://normandy/lib/CleanupManager.jsm"
);
ChromeUtils.defineModuleGetter(
  this,
  "JSONFile",
  "resource://gre/modules/JSONFile.jsm"
);
ChromeUtils.defineModuleGetter(this, "OS", "resource://gre/modules/osfile.jsm");
ChromeUtils.defineModuleGetter(
  this,
  "LogManager",
  "resource://normandy/lib/LogManager.jsm"
);
ChromeUtils.defineModuleGetter(
  this,
  "TelemetryEnvironment",
  "resource://gre/modules/TelemetryEnvironment.jsm"
);
ChromeUtils.defineModuleGetter(
  this,
  "TelemetryEvents",
  "resource://normandy/lib/TelemetryEvents.jsm"
);

var EXPORTED_SYMBOLS = ["PreferenceExperiments", "migrateStorage"];

const EXPERIMENT_FILE = "shield-preference-experiments.json";
const STARTUP_EXPERIMENT_PREFS_BRANCH = "app.normandy.startupExperimentPrefs.";

const MAX_EXPERIMENT_TYPE_LENGTH = 20; // enforced by TelemetryEnvironment
const EXPERIMENT_TYPE_PREFIX = "normandy-";
const MAX_EXPERIMENT_SUBTYPE_LENGTH =
  MAX_EXPERIMENT_TYPE_LENGTH - EXPERIMENT_TYPE_PREFIX.length;

const PREFERENCE_TYPE_MAP = {
  boolean: Services.prefs.PREF_BOOL,
  string: Services.prefs.PREF_STRING,
  integer: Services.prefs.PREF_INT,
};

const UserPreferences = Services.prefs;
const DefaultPreferences = Services.prefs.getDefaultBranch("");

/**
 * Enum storing Preference modules for each type of preference branch.
 * @enum {Object}
 */
const PreferenceBranchType = {
  user: UserPreferences,
  default: DefaultPreferences,
};

/**
 * Asynchronously load the JSON file that stores experiment status in the profile.
 */
let gStorePromise;
function ensureStorage() {
  if (gStorePromise === undefined) {
    const path = OS.Path.join(OS.Constants.Path.profileDir, EXPERIMENT_FILE);
    const storage = new JSONFile({ path });
    gStorePromise = storage.load().then(() => {
      migrateStorage(storage);
      return storage;
    });
  }
  return gStorePromise;
}

/**
 * Migrate storage of experiments from old format (one preference per
 * experiment) to new format.
 *
 * This function is exported for testing purposes but should not be
 * called otherwise.
 */
function migrateStorage(storage) {
  if (storage.data.__version == 3) {
    return;
  }

  // v1 doesn't have a __version; it's just experiments
  const oldVersion = storage.data.__version || 1;

  if (oldVersion == 1) {
    // Add version field
    storage.data = {
      __version: 2,
      experiments: storage.data,
    };

    // Migrate storage.data to multi-preference format
    const oldExperiments = storage.data.experiments;
    const v2Experiments = {};

    for (let [expName, experiment] of Object.entries(oldExperiments)) {
      if (expName == "__version") {
        continue;
      }

      const {
        name,
        branch,
        expired,
        lastSeen,
        preferenceName,
        preferenceValue,
        preferenceType,
        previousPreferenceValue,
        preferenceBranchType,
        experimentType,
      } = experiment;
      const newExperiment = {
        name,
        branch,
        expired,
        lastSeen,
        preferences: {
          [preferenceName]: {
            preferenceBranchType,
            preferenceType,
            preferenceValue,
            previousPreferenceValue,
          },
        },
        experimentType,
      };
      v2Experiments[expName] = newExperiment;
    }
    storage.data.experiments = v2Experiments;
  }
  if (oldVersion <= 2) {
    // Add "actionName" field for experiments that don't have it
    for (const experiment of Object.values(storage.data.experiments)) {
      if (!experiment.actionName) {
        // Assume SinglePreferenceExperimentAction because as of this
        // writing, no multi-pref experiment recipe has launched.
        experiment.actionName = "SinglePreferenceExperimentAction";
      }
    }

    // Bump version
    storage.data.__version = 3;
  }
}

const log = LogManager.getLogger("preference-experiments");

// List of active preference observers. Cleaned up on shutdown.
let experimentObservers = new Map();
CleanupManager.addCleanupHandler(() =>
  PreferenceExperiments.stopAllObservers()
);

function getPref(prefBranch, prefName, prefType) {
  if (prefBranch.getPrefType(prefName) === 0) {
    // pref doesn't exist
    return null;
  }

  switch (prefType) {
    case "boolean": {
      return prefBranch.getBoolPref(prefName);
    }

    case "string":
      return prefBranch.getStringPref(prefName);

    case "integer":
      return prefBranch.getIntPref(prefName);

    default:
      throw new TypeError(
        `Unexpected preference type (${prefType}) for ${prefName}.`
      );
  }
}

function setPref(prefBranch, prefName, prefType, prefValue) {
  switch (prefType) {
    case "boolean":
      prefBranch.setBoolPref(prefName, prefValue);
      break;

    case "string":
      prefBranch.setStringPref(prefName, prefValue);
      break;

    case "integer":
      prefBranch.setIntPref(prefName, prefValue);
      break;

    default:
      throw new TypeError(
        `Unexpected preference type (${prefType}) for ${prefName}.`
      );
  }
}

var PreferenceExperiments = {
  /**
   * Update the the experiment storage with changes that happened during early startup.
   * @param {object} studyPrefsChanged Map from pref name to previous pref value
   */
  async recordOriginalValues(studyPrefsChanged) {
    const store = await ensureStorage();

    for (const experiment of Object.values(store.data.experiments)) {
      for (const [prefName, prefInfo] of Object.entries(
        experiment.preferences
      )) {
        if (studyPrefsChanged.hasOwnProperty(prefName)) {
          if (experiment.expired) {
            log.warn(
              "Expired preference experiment changed value during startup"
            );
          }
          if (prefInfo.preferenceBranch !== "default") {
            log.warn(
              "Non-default branch preference experiment changed value during startup"
            );
          }
          prefInfo.previousPreferenceValue = studyPrefsChanged[prefName];
        }
      }
    }

    // not calling store.saveSoon() because if the data doesn't get
    // written, it will get updated with fresher data next time the
    // browser starts.
  },

  /**
   * Set the default preference value for active experiments that use the
   * default preference branch.
   */
  async init() {
    CleanupManager.addCleanupHandler(this.saveStartupPrefs.bind(this));

    for (const experiment of await this.getAllActive()) {
      // Check that the current value of the preference is still what we set it to
      let stopped = false;
      for (const [prefName, prefInfo] of Object.entries(
        experiment.preferences
      )) {
        if (
          getPref(UserPreferences, prefName, prefInfo.preferenceType) !==
          prefInfo.preferenceValue
        ) {
          // if not, stop the experiment, and skip the remaining steps
          log.info(
            `Stopping experiment "${experiment.name}" because its value changed`
          );
          await this.stop(experiment.name, {
            resetValue: false,
            reason: "user-preference-changed-sideload",
          });
          stopped = true;
          break;
        }
      }
      if (stopped) {
        continue;
      }

      // Notify Telemetry of experiments we're running, since they don't persist between restarts
      TelemetryEnvironment.setExperimentActive(
        experiment.name,
        experiment.branch,
        { type: EXPERIMENT_TYPE_PREFIX + experiment.experimentType }
      );

      // Watch for changes to the experiment's preference
      this.startObserver(experiment.name, experiment.preferences);
    }
  },

  /**
   * Save in-progress, default-branch preference experiments in a sub-branch of
   * the normandy preferences. On startup, we read these to set the
   * experimental values.
   *
   * This is needed because the default branch does not persist between Firefox
   * restarts. To compensate for that, Normandy sets the default branch to the
   * experiment values again every startup. The values to set the preferences
   * to are stored in user-branch preferences because preferences have minimal
   * impact on the performance of startup.
   */
  async saveStartupPrefs() {
    const prefBranch = Services.prefs.getBranch(
      STARTUP_EXPERIMENT_PREFS_BRANCH
    );
    for (const pref of prefBranch.getChildList("")) {
      prefBranch.clearUserPref(pref);
    }

    // Only store prefs to set on the default branch.
    // Be careful not to store user branch prefs here, because this
    // would cause the default branch to match the user branch,
    // causing the user branch pref to get cleared.
    const allExperiments = await this.getAllActive();
    const defaultBranchPrefs = allExperiments
      .flatMap(exp => Object.entries(exp.preferences))
      .filter(
        ([preferenceName, preferenceInfo]) =>
          preferenceInfo.preferenceBranchType === "default"
      );
    for (const [preferenceName, { preferenceValue }] of defaultBranchPrefs) {
      switch (typeof preferenceValue) {
        case "string":
          prefBranch.setCharPref(preferenceName, preferenceValue);
          break;

        case "number":
          prefBranch.setIntPref(preferenceName, preferenceValue);
          break;

        case "boolean":
          prefBranch.setBoolPref(preferenceName, preferenceValue);
          break;

        default:
          throw new Error(`Invalid preference type ${typeof preferenceValue}`);
      }
    }
  },

  /**
   * Test wrapper that temporarily replaces the stored experiment data with fake
   * data for testing.
   */
  withMockExperiments(mockExperiments = []) {
    return function wrapper(testFunction) {
      return async function wrappedTestFunction(...args) {
        const experiments = {};

        for (const exp of mockExperiments) {
          experiments[exp.name] = exp;
        }
        const data = {
          __version: 2,
          experiments,
        };

        const oldPromise = gStorePromise;
        gStorePromise = Promise.resolve({
          data,
          saveSoon() {},
        });
        const oldObservers = experimentObservers;
        experimentObservers = new Map();
        try {
          await testFunction(...args, mockExperiments);
        } finally {
          gStorePromise = oldPromise;
          PreferenceExperiments.stopAllObservers();
          experimentObservers = oldObservers;
        }
      };
    };
  },

  /**
   * Clear all stored data about active and past experiments.
   */
  async clearAllExperimentStorage() {
    const store = await ensureStorage();
    store.data = {
      __version: 2,
      experiments: {},
    };
    store.saveSoon();
  },

  /**
   * Start a new preference experiment.
   * @param {Object} experiment
   * @param {string} experiment.name
   * @param {string} experiment.actionName  The action who knows about this
   *   experiment and is responsible for cleaning it up. This should
   *   correspond to the name of some BaseAction subclass.
   * @param {string} experiment.branch
   * @param {string} experiment.preferenceName
   * @param {string|integer|boolean} experiment.preferenceValue
   * @param {PreferenceBranchType} experiment.preferenceBranchType
   * @rejects {Error}
   *   - If an experiment with the given name already exists
   *   - if an experiment for the given preference is active
   *   - If the given preferenceType does not match the existing stored preference
   */
  async start({
    name,
    actionName,
    branch,
    preferences,
    experimentType = "exp",
    userFacingName = null,
    userFacingDescription = null,
  }) {
    log.debug(`PreferenceExperiments.start(${name}, ${branch})`);

    const store = await ensureStorage();
    if (name in store.data.experiments) {
      TelemetryEvents.sendEvent("enrollFailed", "preference_study", name, {
        reason: "name-conflict",
      });
      throw new Error(
        `A preference experiment named "${name}" already exists.`
      );
    }

    const activeExperiments = Object.values(store.data.experiments).filter(
      e => !e.expired
    );
    const preferencesWithConflicts = Object.keys(preferences).filter(
      preferenceName => {
        return activeExperiments.some(e =>
          e.preferences.hasOwnProperty(preferenceName)
        );
      }
    );

    if (preferencesWithConflicts.length > 0) {
      TelemetryEvents.sendEvent("enrollFailed", "preference_study", name, {
        reason: "pref-conflict",
      });
      throw new Error(
        `Another preference experiment for the pref "${
          preferencesWithConflicts[0]
        }" is currently active.`
      );
    }

    if (experimentType.length > MAX_EXPERIMENT_SUBTYPE_LENGTH) {
      TelemetryEvents.sendEvent("enrollFailed", "preference_study", name, {
        reason: "experiment-type-too-long",
      });
      throw new Error(
        `experimentType must be less than ${MAX_EXPERIMENT_SUBTYPE_LENGTH} characters. ` +
          `"${experimentType}" is ${experimentType.length} long.`
      );
    }

    // Sanity check each preference
    for (const [preferenceName, preferenceInfo] of Object.entries(
      preferences
    )) {
      const { preferenceBranchType, preferenceType } = preferenceInfo;
      const preferenceBranch = PreferenceBranchType[preferenceBranchType];
      if (!preferenceBranch) {
        TelemetryEvents.sendEvent("enrollFailed", "preference_study", name, {
          reason: "invalid-branch",
        });
        throw new Error(
          `Invalid value for preferenceBranchType: ${preferenceBranchType}`
        );
      }

      const prevPrefType = Services.prefs.getPrefType(preferenceName);
      const givenPrefType = PREFERENCE_TYPE_MAP[preferenceType];

      if (!preferenceType || !givenPrefType) {
        TelemetryEvents.sendEvent("enrollFailed", "preference_study", name, {
          reason: "invalid-type",
        });
        throw new Error(
          `Invalid preferenceType provided (given "${preferenceType}")`
        );
      }

      if (
        prevPrefType !== Services.prefs.PREF_INVALID &&
        prevPrefType !== givenPrefType
      ) {
        TelemetryEvents.sendEvent("enrollFailed", "preference_study", name, {
          reason: "invalid-type",
        });
        throw new Error(
          `Previous preference value is of type "${prevPrefType}", but was given ` +
            `"${givenPrefType}" (${preferenceType})`
        );
      }

      preferenceInfo.previousPreferenceValue = getPref(
        preferenceBranch,
        preferenceName,
        preferenceType
      );
    }

    for (const [preferenceName, preferenceInfo] of Object.entries(
      preferences
    )) {
      const {
        preferenceType,
        preferenceValue,
        preferenceBranchType,
      } = preferenceInfo;
      const preferenceBranch = PreferenceBranchType[preferenceBranchType];
      setPref(
        preferenceBranch,
        preferenceName,
        preferenceType,
        preferenceValue
      );
    }
    PreferenceExperiments.startObserver(name, preferences);

    /** @type {Experiment} */
    const experiment = {
      name,
      actionName,
      branch,
      expired: false,
      lastSeen: new Date().toJSON(),
      preferences,
      experimentType,
      userFacingName,
      userFacingDescription,
    };

    store.data.experiments[name] = experiment;
    store.saveSoon();

    TelemetryEnvironment.setExperimentActive(name, branch, {
      type: EXPERIMENT_TYPE_PREFIX + experimentType,
    });
    TelemetryEvents.sendEvent("enroll", "preference_study", name, {
      experimentType,
      branch,
    });
    await this.saveStartupPrefs();
  },

  /**
   * Register a preference observer that stops an experiment when the user
   * modifies the preference.
   * @param {string} experimentName
   * @param {string} preferenceName
   * @param {string|integer|boolean} preferenceValue
   * @throws {Error}
   *   If an observer for the named experiment is already active.
   */
  startObserver(experimentName, preferences) {
    log.debug(`PreferenceExperiments.startObserver(${experimentName})`);

    if (experimentObservers.has(experimentName)) {
      throw new Error(
        `An observer for the preference experiment ${experimentName} is already active.`
      );
    }

    const observerInfo = {
      preferences,
      observe(aSubject, aTopic, preferenceName) {
        const { preferenceValue, preferenceType } = preferences[preferenceName];
        const newValue = getPref(
          UserPreferences,
          preferenceName,
          preferenceType
        );
        if (newValue !== preferenceValue) {
          PreferenceExperiments.stop(experimentName, {
            resetValue: false,
            reason: "user-preference-changed",
          }).catch(Cu.reportError);
        }
      },
    };
    experimentObservers.set(experimentName, observerInfo);
    for (const preferenceName of Object.keys(preferences)) {
      Services.prefs.addObserver(preferenceName, observerInfo);
    }
  },

  /**
   * Check if a preference observer is active for an experiment.
   * @param {string} experimentName
   * @return {Boolean}
   */
  hasObserver(experimentName) {
    log.debug(`PreferenceExperiments.hasObserver(${experimentName})`);
    return experimentObservers.has(experimentName);
  },

  /**
   * Disable a preference observer for the named experiment.
   * @param {string} experimentName
   * @throws {Error}
   *   If there is no active observer for the named experiment.
   */
  stopObserver(experimentName) {
    log.debug(`PreferenceExperiments.stopObserver(${experimentName})`);

    if (!experimentObservers.has(experimentName)) {
      throw new Error(
        `No observer for the preference experiment ${experimentName} found.`
      );
    }

    const observer = experimentObservers.get(experimentName);
    for (const preferenceName of Object.keys(observer.preferences)) {
      Services.prefs.removeObserver(preferenceName, observer);
    }
    experimentObservers.delete(experimentName);
  },

  /**
   * Disable all currently-active preference observers for experiments.
   */
  stopAllObservers() {
    log.debug("PreferenceExperiments.stopAllObservers()");
    for (const observer of experimentObservers.values()) {
      for (const preferenceName of Object.keys(observer.preferences)) {
        Services.prefs.removeObserver(preferenceName, observer);
      }
    }
    experimentObservers.clear();
  },

  /**
   * Update the timestamp storing when Normandy last sent a recipe for the named
   * experiment.
   * @param {string} experimentName
   * @rejects {Error}
   *   If there is no stored experiment with the given name.
   */
  async markLastSeen(experimentName) {
    log.debug(`PreferenceExperiments.markLastSeen(${experimentName})`);

    const store = await ensureStorage();
    if (!(experimentName in store.data.experiments)) {
      throw new Error(
        `Could not find a preference experiment named "${experimentName}"`
      );
    }

    store.data.experiments[experimentName].lastSeen = new Date().toJSON();
    store.saveSoon();
  },

  /**
   * Stop an active experiment, deactivate preference watchers, and optionally
   * reset the associated preference to its previous value.
   * @param {string} experimentName
   * @param {Object} options
   * @param {boolean} [options.resetValue = true]
   *   If true, reset the preference to its original value prior to
   *   the experiment. Optional, defaults to true.
   * @param {String} [options.reason = "unknown"]
   *   Reason that the experiment is ending. Optional, defaults to
   *   "unknown".
   * @rejects {Error}
   *   If there is no stored experiment with the given name, or if the
   *   experiment has already expired.
   */
  async stop(experimentName, { resetValue = true, reason = "unknown" } = {}) {
    log.debug(
      `PreferenceExperiments.stop(${experimentName}, {resetValue: ${resetValue}, reason: ${reason}})`
    );
    if (reason === "unknown") {
      log.warn(`experiment ${experimentName} ending for unknown reason`);
    }

    const store = await ensureStorage();
    if (!(experimentName in store.data.experiments)) {
      TelemetryEvents.sendEvent(
        "unenrollFailed",
        "preference_study",
        experimentName,
        { reason: "does-not-exist" }
      );
      throw new Error(
        `Could not find a preference experiment named "${experimentName}"`
      );
    }

    const experiment = store.data.experiments[experimentName];
    if (experiment.expired) {
      TelemetryEvents.sendEvent(
        "unenrollFailed",
        "preference_study",
        experimentName,
        { reason: "already-unenrolled" }
      );
      throw new Error(
        `Cannot stop preference experiment "${experimentName}" because it is already expired`
      );
    }

    if (PreferenceExperiments.hasObserver(experimentName)) {
      PreferenceExperiments.stopObserver(experimentName);
    }

    if (resetValue) {
      for (const [preferenceName, prefInfo] of Object.entries(
        experiment.preferences
      )) {
        const {
          preferenceType,
          previousPreferenceValue,
          preferenceBranchType,
        } = prefInfo;
        const preferences = PreferenceBranchType[preferenceBranchType];

        if (previousPreferenceValue !== null) {
          setPref(
            preferences,
            preferenceName,
            preferenceType,
            previousPreferenceValue
          );
        } else if (preferenceBranchType === "user") {
          // Remove the "user set" value (which Shield set), but leave the default intact.
          preferences.clearUserPref(preferenceName);
        } else {
          log.warn(
            `Can't revert pref ${preferenceName} for experiment ${experimentName} ` +
              `because it had no default value. ` +
              `Preference will be reset at the next restart.`
          );
          // It would seem that Services.prefs.deleteBranch() could be used for
          // this, but in Normandy's case it does not work. See bug 1502410.
        }
      }
    }

    experiment.expired = true;
    store.saveSoon();

    TelemetryEnvironment.setExperimentInactive(experimentName);
    TelemetryEvents.sendEvent("unenroll", "preference_study", experimentName, {
      didResetValue: resetValue ? "true" : "false",
      branch: experiment.branch,
      reason,
    });
    await this.saveStartupPrefs();
  },

  /**
   * Clone an experiment using knowledge of its structure to avoid
   * having to serialize/deserialize it.
   *
   * We do this in places where return experiments so clients can't
   * accidentally mutate our data underneath us.
   */
  _cloneExperiment(experiment) {
    return {
      ...experiment,
      preferences: {
        ...experiment.preferences,
      },
    };
  },

  /**
   * Get the experiment object for the named experiment.
   * @param {string} experimentName
   * @resolves {Experiment}
   * @rejects {Error}
   *   If no preference experiment exists with the given name.
   */
  async get(experimentName) {
    log.debug(`PreferenceExperiments.get(${experimentName})`);
    const store = await ensureStorage();
    if (!(experimentName in store.data.experiments)) {
      throw new Error(
        `Could not find a preference experiment named "${experimentName}"`
      );
    }

    return this._cloneExperiment(store.data.experiments[experimentName]);
  },

  /**
   * Get a list of all stored experiment objects.
   * @resolves {Experiment[]}
   */
  async getAll() {
    const store = await ensureStorage();

    return Object.values(store.data.experiments).map(experiment =>
      this._cloneExperiment(experiment)
    );
  },

  /**
   * Get a list of experiment objects for all active experiments.
   * @resolves {Experiment[]}
   */
  async getAllActive() {
    const store = await ensureStorage();
    return Object.values(store.data.experiments)
      .filter(e => !e.expired)
      .map(e => this._cloneExperiment(e));
  },

  /**
   * Check if an experiment exists with the given name.
   * @param {string} experimentName
   * @resolves {boolean} True if the experiment exists, false if it doesn't.
   */
  async has(experimentName) {
    log.debug(`PreferenceExperiments.has(${experimentName})`);
    const store = await ensureStorage();
    return experimentName in store.data.experiments;
  },
};
