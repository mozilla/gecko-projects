/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";
/* global browser, runHeuristics */

let DEBUG;

async function log() {
  // eslint-disable-next-line no-constant-condition
  if (DEBUG) {
    // eslint-disable-next-line no-console
    console.log(...arguments);
  }
}

// Gate-keeping pref to run the add-on
const DOH_ENABLED_PREF = "doh-rollout.enabled";

// Pref that sets DoH to on/off. It has multiple modes:
// 0: Off (default)
// 1: null (No setting)
// 2: Enabled, but will fall back to 0 on DNS lookup failure
// 3: Always on.
// 4: null (No setting)
// 5: Never on.
const TRR_MODE_PREF = "network.trr.mode";

// This preference is set to TRUE when DoH has been enabled via the add-on. It will
// allow the add-on to continue to function without the aid of the Normandy-triggered pref
// of "doh-rollout.enabled". Note that instead of setting it to false, it is cleared.
const DOH_SELF_ENABLED_PREF = "doh-rollout.self-enabled";

// This pref is part of a cache mechanism to see if the heuristics dictated a change in the DoH settings
const DOH_PREVIOUS_TRR_MODE_PREF = "doh-rollout.previous.trr.mode";

// Set after doorhanger has been interacted with by the user
const DOH_DOORHANGER_SHOWN_PREF = "doh-rollout.doorhanger-shown";

// Records if the user opted in/out of DoH study by clicking on doorhanger
const DOH_DOORHANGER_USER_DECISION_PREF = "doh-rollout.doorhanger-decision";

// Records if user has decided to opt out of study, either by disabling via the doorhanger,
// unchecking "DNS-over-HTTPS" with about:preferences, or manually setting network.trr.mode
const DOH_DISABLED_PREF = "doh-rollout.disable-heuristics";

// Set to true when a user has ANY enterprise policy set, making sure to not run
// heuristics, overwritting the policy.
const DOH_SKIP_HEURISTICS_PREF = "doh-rollout.skipHeuristicsCheck";

// Records when the add-on has been run once. This is in place to only check
// network.trr.mode for prefHasUserValue on first run.
const DOH_DONE_FIRST_RUN_PREF = "doh-rollout.doneFirstRun";

// This pref is set once a migration function has ran, updating local storage items to the
// new doh-rollot.X namespace. This applies to both `doneFirstRun` and `skipHeuristicsCheck`.
const DOH_BALROG_MIGRATION_PREF = "doh-rollout.balrog-migration-done";

// If set to true, debug logging will be enabled.
const DOH_DEBUG_PREF = "doh-rollout.debug";

const stateManager = {
  async setState(state) {
    log("setState: ", state);

    switch (state) {
      case "uninstalled":
        break;
      case "disabled":
        rollout.setSetting(TRR_MODE_PREF, 0);
        break;
      case "manuallyDisabled":
        browser.experiments.preferences.clearUserPref(DOH_SELF_ENABLED_PREF);
        break;
      case "UIOk":
        rollout.setSetting(DOH_SELF_ENABLED_PREF, true);
        break;
      case "enabled":
        rollout.setSetting(TRR_MODE_PREF, 2);
        rollout.setSetting(DOH_SELF_ENABLED_PREF, true);
        break;
      case "UIDisabled":
        rollout.setSetting(TRR_MODE_PREF, 5);
        browser.experiments.preferences.clearUserPref(DOH_SELF_ENABLED_PREF);
        break;
    }

    await browser.experiments.heuristics.sendStatePing(state);
    await stateManager.rememberTRRMode();
  },

  async rememberTRRMode() {
    let curMode = await browser.experiments.preferences.getIntPref(
      TRR_MODE_PREF,
      0
    );
    log("Saving current trr mode:", curMode);
    await rollout.setSetting(DOH_PREVIOUS_TRR_MODE_PREF, curMode, true);
  },

  async rememberDoorhangerShown() {
    // This will be shown on startup and network changes until a user clicks
    // to confirm/disable DoH or presses the esc key (confirming)
    log("Remembering that doorhanger has been shown");
    await rollout.setSetting(DOH_DOORHANGER_SHOWN_PREF, true);
  },

  async rememberDoorhangerDecision(decision) {
    log("Remember doorhanger decision:", decision);
    await rollout.setSetting(DOH_DOORHANGER_USER_DECISION_PREF, decision, true);
  },

  async rememberDisableHeuristics() {
    log("Remembering to never run heuristics again");
    await rollout.setSetting(DOH_DISABLED_PREF, true);
  },

  async shouldRunHeuristics() {
    // Check if heuristics has been disabled from rememberDisableHeuristics()
    let disableHeuristics = await rollout.getSetting(DOH_DISABLED_PREF, false);

    if (disableHeuristics) {
      // Do not modify DoH for this user.
      log("disableHeuristics has been enabled.");
      return false;
    }

    let prevMode = await rollout.getSetting(DOH_PREVIOUS_TRR_MODE_PREF, 0);

    let curMode = await browser.experiments.preferences.getIntPref(
      TRR_MODE_PREF,
      0
    );

    log("Comparing previous trr mode to current mode:", prevMode, curMode);

    // Don't run heuristics if:
    //  1) Previous doesn't mode equals current mode, i.e. user overrode our changes
    //  2) TRR mode equals 5, i.e. user clicked "No" on doorhanger
    //  3) TRR mode equals 3, i.e. user enabled "strictly on" for DoH
    //  4) They've been disabled in the past for the reasons listed above
    //
    // In other words, if the user has made their own decision for DoH,
    // then we want to respect that and never run the heuristics again

    if (prevMode === curMode) {
      return true;
    }

    // On Mismatch - run never run again (make init check a function)
    log("Mismatched, curMode: ", curMode);

    // Cache results for Telemetry send, including setting eval reason
    let results = await runHeuristics();
    results.evaluateReason = "userModified";
    if (curMode === 0 || curMode === 5) {
      // If user has manually set trr.mode to 0, and it was previously something else.
      browser.experiments.heuristics.sendHeuristicsPing("disable_doh", results);
      browser.experiments.preferences.clearUserPref(DOH_SELF_ENABLED_PREF);
      await stateManager.rememberDisableHeuristics();
    } else {
      // Check if trr.mode is not in default value.
      await rollout.trrModePrefHasUserValue(
        "shouldRunHeuristics_mismatch",
        results
      );
    }

    return false;
  },

  async shouldShowDoorhanger() {
    let doorhangerShown = await rollout.getSetting(
      DOH_DOORHANGER_SHOWN_PREF,
      false
    );
    log("Should show doorhanger:", !doorhangerShown);

    return !doorhangerShown;
  },

  async showDoorHangerAndEnableDoH() {
    browser.experiments.doorhanger.onDoorhangerAccept.addListener(
      rollout.doorhangerAcceptListener
    );

    browser.experiments.doorhanger.onDoorhangerDecline.addListener(
      rollout.doorhangerDeclineListener
    );

    await browser.experiments.doorhanger.show({
      name: browser.i18n.getMessage("doorhangerName"),
      text: "<> " + browser.i18n.getMessage("doorhangerBody"),
      okLabel: browser.i18n.getMessage("doorhangerButtonOk"),
      okAccessKey: browser.i18n.getMessage("doorhangerButtonOkAccessKey"),
      cancelLabel: browser.i18n.getMessage("doorhangerButtonCancel2"),
      cancelAccessKey: browser.i18n.getMessage(
        "doorhangerButtonCancelAccessKey"
      ),
    });

    // By default, enable DoH when showing the doorhanger,
    // if heuristics returned no reason to not run.
    await stateManager.setState("enabled");
  },
};

const rollout = {
  // Pretend that there was a network change at the beginning of time.
  lastNetworkChangeTime: 0,

  async doorhangerAcceptListener(tabId) {
    log("Doorhanger accepted on tab", tabId);
    await stateManager.setState("UIOk");
    await stateManager.rememberDoorhangerDecision("UIOk");
    await stateManager.rememberDoorhangerShown();
  },

  async doorhangerDeclineListener(tabId) {
    log("Doorhanger declined on tab", tabId);
    await stateManager.setState("UIDisabled");
    await stateManager.rememberDoorhangerDecision("UIDisabled");
    let results = await runHeuristics();
    results.evaluateReason = "doorhangerDecline";
    browser.experiments.heuristics.sendHeuristicsPing("disable_doh", results);
    await stateManager.rememberDisableHeuristics();
    await stateManager.rememberDoorhangerShown();
  },

  async heuristics(evaluateReason) {
    // Run heuristics defined in heuristics.js and experiments/heuristics/api.js
    let results = await runHeuristics();

    // Check if DoH should be disabled
    let disableDoh = Object.values(results).includes("disable_doh")
      ? "disable_doh"
      : "enable_doh";

    log("Heuristics decision on " + evaluateReason + ": " + disableDoh);

    // Send Telemetry on results of heuristics
    results.evaluateReason = evaluateReason;
    browser.experiments.heuristics.sendHeuristicsPing(disableDoh, results);

    return disableDoh;
  },

  async getSetting(name, defaultValue) {
    let value;

    switch (typeof defaultValue) {
      case "boolean":
        value = await browser.experiments.preferences.getBoolPref(
          name,
          defaultValue
        );
        break;
      case "number":
        value = await browser.experiments.preferences.getIntPref(
          name,
          defaultValue
        );
        break;
      case "string":
        value = await browser.experiments.preferences.getCharPref(
          name,
          defaultValue
        );
        break;
      default:
        throw new Error(
          `Invalid defaultValue argument when trying to fetch pref: ${JSON.stringify(
            name
          )}`
        );
    }

    log({
      context: "getSetting",
      type: typeof defaultValue,
      name,
      value,
    });

    return value;
  },

  /**
   * Exposed
   *
   * @param  {type} name  description
   * @param  {type} value description
   * @return {type}       description
   */
  async setSetting(name, value) {
    // Based on type of pref, set pref accordingly
    switch (typeof value) {
      case "boolean":
        await browser.experiments.preferences.setBoolPref(name, value);
        break;
      case "number":
        await browser.experiments.preferences.setIntPref(name, value);
        break;
      case "string":
        await browser.experiments.preferences.setCharPref(name, value);
        break;
      default:
        throw new Error("setSetting typeof value unknown!");
    }

    log({
      context: "setSetting",
      type: typeof value,
      name,
      value,
    });
  },

  async trrModePrefHasUserValue(event, results) {
    results.evaluateReason = event;

    // Reset skipHeuristicsCheck
    await this.setSetting(DOH_SKIP_HEURISTICS_PREF, false);

    // This confirms if a user has modified DoH (via the TRR_MODE_PREF) outside of the addon
    // This runs only on the FIRST time that add-on is enabled and if the stored pref
    // mismatches the current pref (Meaning something outside of the add-on has changed it)

    if (await browser.experiments.preferences.prefHasUserValue(TRR_MODE_PREF)) {
      // Send ping that user had specific trr.mode pref set before add-on study was ran.
      // Note that this does not include the trr.mode - just that the addon cannot be ran.
      browser.experiments.heuristics.sendHeuristicsPing(
        "prefHasUserValue",
        results
      );

      browser.experiments.preferences.clearUserPref(DOH_SELF_ENABLED_PREF);
      await stateManager.rememberDisableHeuristics();
    }
  },

  async enterprisePolicyCheck(event, results) {
    results.evaluateReason = event;

    // Reset skipHeuristicsCheck
    await this.setSetting(DOH_SKIP_HEURISTICS_PREF, false);

    // Check for Policies before running the rest of the heuristics
    let policyEnableDoH = await browser.experiments.heuristics.checkEnterprisePolicies();

    log("Enterprise Policy Check:", policyEnableDoH);

    // Determine to skip additional heuristics (by presence of an enterprise policy)

    if (policyEnableDoH === "no_policy_set") {
      // Resetting skipHeuristicsCheck in case a user had a policy and then removed it!
      await this.setSetting(DOH_SKIP_HEURISTICS_PREF, false);
      return;
    }

    if (policyEnableDoH === "policy_without_doh") {
      await stateManager.setState("disabled");
    }

    // Don't check for prefHasUserValue if policy is set to disable DoH
    await this.setSetting(DOH_SKIP_HEURISTICS_PREF, true);

    browser.experiments.heuristics.sendHeuristicsPing(policyEnableDoH, results);
  },

  async migrateLocalStoragePrefs() {
    // Migrate updated local storage item names. If this has already been done once, skip the migration
    const isMigrated = await browser.experiments.preferences.getBoolPref(
      DOH_BALROG_MIGRATION_PREF,
      false
    );

    if (isMigrated) {
      log("User has already been migrated.");
      return;
    }

    // Check all local storage keys from v1.0.4 users and migrate them to prefs.
    // This only applies to keys that have a value.
    const legacyLocalStorageKeys = [
      "doneFirstRun",
      "skipHeuristicsCheck",
      DOH_PREVIOUS_TRR_MODE_PREF,
      DOH_DOORHANGER_SHOWN_PREF,
      DOH_DOORHANGER_USER_DECISION_PREF,
      DOH_DISABLED_PREF,
    ];

    for (let item of legacyLocalStorageKeys) {
      let data = await browser.storage.local.get(item);
      let value = data[item];

      log({ context: "migration", item, value });

      if (data.hasOwnProperty(item)) {
        let migratedName = item;

        if (!item.startsWith("doh-rollout.")) {
          migratedName = "doh-rollout." + item;
        }

        await this.setSetting(migratedName, value);
      }
    }

    // Set pref to skip this function in the future.
    browser.experiments.preferences.setBoolPref(
      DOH_BALROG_MIGRATION_PREF,
      true
    );

    log("User successfully migrated.");
  },

  async init() {
    log("calling init");

    // Check if the add-on has run before
    let doneFirstRun = await this.getSetting(DOH_DONE_FIRST_RUN_PREF, false);

    // Register the events for sending pings
    browser.experiments.heuristics.setupTelemetry();

    // Cache runHeuristics results for first run/start up checks
    let results = await runHeuristics();

    if (!doneFirstRun) {
      log("first run!");
      await this.setSetting(DOH_DONE_FIRST_RUN_PREF, true);
      // Check if user has a set a custom pref only on first run, not on each startup
      await this.trrModePrefHasUserValue("first_run", results);
      await this.enterprisePolicyCheck("first_run", results);
    } else {
      log("not first run!");
      await this.enterprisePolicyCheck("startup", results);
    }

    // Only run the heuristics if user hasn't explicitly enabled/disabled DoH
    let skipHeuristicsCheck = await this.getSetting(
      DOH_SKIP_HEURISTICS_PREF,
      false
    );

    log("skipHeuristicsCheck: ", skipHeuristicsCheck);

    if (!skipHeuristicsCheck && (await stateManager.shouldRunHeuristics())) {
      await this.runStartupHeuristics();
    }

    // Listen for network change events to run heuristics again
    browser.networkStatus.onConnectionChanged.addListener(async () => {
      log("onConnectionChanged");

      let linkInfo = await browser.networkStatus.getLinkInfo();
      if (linkInfo.status !== "up") {
        log("Link down.");
        if (rollout.networkSettledTimeout) {
          log("Canceling queued heuristics run.");
          clearTimeout(rollout.networkSettledTimeout);
          rollout.networkSettledTimeout = null;
        }
        return;
      }

      log("Queing a heuristics run in 60s, will cancel if network fluctuates.");
      rollout.networkSettledTimeout = setTimeout(async () => {
        log("No network fluctuation for 60 seconds, running heuristics.");
        // Only run the heuristics if user hasn't explicitly enabled/disabled DoH
        let shouldRunHeuristics = await stateManager.shouldRunHeuristics();
        let shouldShowDoorhanger = await stateManager.shouldShowDoorhanger();

        if (!shouldRunHeuristics) {
          return;
        }

        const netChangeDecision = await rollout.heuristics("netChange");

        if (netChangeDecision === "disable_doh") {
          await stateManager.setState("disabled");
        } else if (shouldShowDoorhanger) {
          await stateManager.showDoorHangerAndEnableDoH();
        } else {
          await stateManager.setState("enabled");
        }
      }, 60000);
    });

    // Listen to the captive portal when it unlocks
    browser.captivePortal.onConnectivityAvailable.addListener(async () => {
      log("Captive portal onConnectivityAvailable, running heuristics.");
      if (rollout.networkSettledTimeout) {
        log("Canceling queued heuristics run.");
        clearTimeout(rollout.networkSettledTimeout);
        rollout.networkSettledTimeout = null;
      }
      await this.runStartupHeuristics();
    });
  },

  async runStartupHeuristics() {
    let decision = await this.heuristics("startup");
    let shouldShowDoorhanger = await stateManager.shouldShowDoorhanger();
    if (decision === "disable_doh") {
      await stateManager.setState("disabled");

      // If the heuristics say to enable DoH, determine if the doorhanger
      // should be shown
    } else if (shouldShowDoorhanger) {
      await stateManager.showDoorHangerAndEnableDoH();
    } else {
      // Doorhanger has been shown before and did not opt-out
      await stateManager.setState("enabled");
    }
  },
};

const setup = {
  async start() {
    DEBUG = await browser.experiments.preferences.getBoolPref(
      DOH_DEBUG_PREF,
      false
    );

    // Run Migration First, to continue to run rest of start up logic
    await rollout.migrateLocalStoragePrefs();

    const isAddonDisabled = await rollout.getSetting(DOH_DISABLED_PREF, false);
    const runAddonPref = await rollout.getSetting(DOH_ENABLED_PREF, false);
    const runAddonBypassPref = await rollout.getSetting(
      DOH_SELF_ENABLED_PREF,
      false
    );
    const runAddonDoorhangerDecision = await rollout.getSetting(
      DOH_DOORHANGER_USER_DECISION_PREF,
      ""
    );
    const runAddonPreviousTRRMode = await rollout.getSetting(
      DOH_PREVIOUS_TRR_MODE_PREF,
      -1
    );

    if (isAddonDisabled) {
      // Regardless of pref, the user has chosen/heuristics dictated that this add-on should be disabled.
      // DoH status will not be modified from whatever the current setting is at runtime
      log(
        "Addon has been disabled. DoH status will not be modified from current setting"
      );
      await stateManager.rememberDisableHeuristics();
      return;
    }

    if (
      runAddonPref ||
      runAddonBypassPref ||
      runAddonDoorhangerDecision === "UIOk" ||
      runAddonDoorhangerDecision === "enabled" ||
      runAddonPreviousTRRMode === 2 ||
      runAddonPreviousTRRMode === 0
    ) {
      rollout.init();
    } else {
      log(
        "Disabled, aborting! Watching `doh-rollout.enabled` pref for change event"
      );
      // Listen for changes to the enabled pref. TODO: Also listen after init
      // and properly handle the value of enabled changing to false.
      browser.experiments.preferences.onPrefChanged.addListener(
        function listener() {
          browser.experiments.preferences.onPrefChanged.removeListener(
            listener
          );
          setup.start();
        }
      );
    }
  },
};

setup.start();
