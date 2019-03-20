/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * This action handles the life cycle of add-on based studies. Currently that
 * means installing the add-on the first time the recipe applies to this client,
 * and uninstalling them when the recipe no longer applies.
 */

"use strict";

const {XPCOMUtils} = ChromeUtils.import("resource://gre/modules/XPCOMUtils.jsm");
const {BaseAction} = ChromeUtils.import("resource://normandy/actions/BaseAction.jsm");

XPCOMUtils.defineLazyModuleGetters(this, {
  Services: "resource://gre/modules/Services.jsm",
  PromiseUtils: "resource://gre/modules/PromiseUtils.jsm",
  AddonManager: "resource://gre/modules/AddonManager.jsm",
  ActionSchemas: "resource://normandy/actions/schemas/index.js",
  AddonStudies: "resource://normandy/lib/AddonStudies.jsm",
  NormandyApi: "resource://normandy/lib/NormandyApi.jsm",
  TelemetryEvents: "resource://normandy/lib/TelemetryEvents.jsm",
});

var EXPORTED_SYMBOLS = ["AddonStudyAction"];

const OPT_OUT_STUDIES_ENABLED_PREF = "app.shield.optoutstudies.enabled";

class AddonStudyEnrollError extends Error {
  /**
   * @param {string} studyName
   * @param {object} extra Extra details to include when reporting the error to telemetry.
   * @param {string} extra.reason The specific reason for the failure.
   */
  constructor(studyName, extra) {
    let message;
    let { reason } = extra;
    switch (reason) {
      case "conflicting-addon-id": {
        message = "an add-on with this ID is already installed";
        break;
      }
      case "download-failure": {
        message = "the add-on failed to download";
        break;
      }
      case "metadata-mismatch": {
        message = "the server metadata does not match the downloaded add-on";
        break;
      }
      case "install-failure": {
        message = "the add-on failed to install";
        break;
      }
      default: {
        throw new Error(`Unexpected AddonStudyEnrollError reason: ${reason}`);
      }
    }
    super(`Cannot install study add-on for ${studyName}: ${message}.`);
    this.studyName = studyName;
    this.extra = extra;
  }
}

class AddonStudyUpdateError extends Error {
  /**
   * @param {string} studyName
   * @param {object} extra Extra details to include when reporting the error to telemetry.
   * @param {string} extra.reason The specific reason for the failure.
   */
  constructor(studyName, extra) {
    let message;
    let { reason } = extra;
    switch (reason) {
      case "addon-id-mismatch": {
        message = "new add-on ID does not match old add-on ID";
        break;
      }
      case "addon-does-not-exist": {
        message = "an add-on with this ID does not exist";
        break;
      }
      case "no-downgrade": {
        message = "the add-on was an older version than is installed";
        break;
      }
      case "metadata-mismatch": {
        message = "the server metadata does not match the downloaded add-on";
        break;
      }
      case "download-failure": {
        message = "the add-on failed to download";
        break;
      }
      case "install-failure": {
        message = "the add-on failed to install";
        break;
      }
      default: {
        throw new Error(`Unexpected AddonStudyUpdateError reason: ${reason}`);
      }
    }
    super(`Cannot update study add-on for ${studyName}: ${message}.`);
    this.studyName = studyName;
    this.extra = extra;
  }
}

class AddonStudyAction extends BaseAction {
  get schema() {
    return ActionSchemas["addon-study"];
  }

  constructor() {
    super();
    this.seenRecipeIds = new Set();
  }

  /**
   * This hook is executed once before any recipes have been processed, it is
   * responsible for:
   *
   *   - Checking if the user has opted out of studies, and if so, it disables the action.
   *   - Setting up tracking of seen recipes, for use in _finalize.
   */
  _preExecution() {
    // Check opt-out preference
    if (!Services.prefs.getBoolPref(OPT_OUT_STUDIES_ENABLED_PREF, true)) {
      this.log.info("User has opted-out of opt-out experiments, disabling action.");
      this.disable();
    }
  }

  /**
   * This hook is executed once for each recipe that currently applies to this
   * client. It is responsible for:
   *
   *   - Enrolling studies the first time they are seen.
   *   - Updating studies that have upgraded addons.
   *   - Marking studies as having been seen in this session.
   *
   * If the recipe fails to enroll or update, it should throw to properly report its status.
   */
  async _run(recipe) {
    this.seenRecipeIds.add(recipe.id);

    const hasStudy = await AddonStudies.has(recipe.id);
    const { extensionApiId } = recipe.arguments;
    const extensionDetails = await NormandyApi.fetchExtensionDetails(extensionApiId);

    if (hasStudy) {
      await this.update(recipe, extensionDetails);
    } else {
      await this.enroll(recipe, extensionDetails);
    }
  }

  /**
   * This hook is executed once after all recipes that apply to this client
   * have been processed. It is responsible for unenrolling the client from any
   * studies that no longer apply, based on this.seenRecipeIds.
   */
  async _finalize() {
    const activeStudies = (await AddonStudies.getAll()).filter(study => study.active);

    for (const study of activeStudies) {
      if (!this.seenRecipeIds.has(study.recipeId)) {
        this.log.debug(`Stopping study for recipe ${study.recipeId}`);
        try {
          await this.unenroll(study.recipeId, "recipe-not-seen");
        } catch (err) {
          Cu.reportError(err);
        }
      }
    }
  }

  /**
   * Download and install the addon for a given recipe
   *
   * @param recipe Object describing the study to enroll in.
   * @param extensionDetails Object describing the addon to be installed.
   * @param onInstallStarted A function that returns a callback for the install listener.
   * @param onComplete A callback function that is run on completion of the download.
   * @param onFailedInstall A callback function that is run if the installation fails.
   * @param errorClass The class of error to be thrown when exceptions occur.
   * @param reportError A function that reports errors to Telemetry.
   */
  async downloadAndInstall(recipe, extensionDetails, onInstallStarted, onComplete, onFailedInstall, errorClass, reportError) {
    const { name } = recipe.arguments;
    const { hash, hash_algorithm } = extensionDetails;

    const downloadDeferred = PromiseUtils.defer();
    const installDeferred = PromiseUtils.defer();

    const install = await AddonManager.getInstallForURL(extensionDetails.xpi, {
      hash: `${hash_algorithm}:${hash}`,
      telemetryInfo: {source: "internal"},
    });

    const listener = {
      onDownloadFailed() {
        downloadDeferred.reject(new errorClass(name, {
          reason: "download-failure",
          detail: AddonManager.errorToString(install.error),
        }));
      },

      onDownloadEnded() {
        downloadDeferred.resolve();
        return false; // temporarily pause installation for Normandy bookkeeping
      },

      onInstallFailed() {
        installDeferred.reject(new errorClass(name, {
          reason: "install-failure",
          detail: AddonManager.errorToString(install.error),
        }));
      },

      onInstallEnded() {
        installDeferred.resolve();
      },
    };

    listener.onInstallStarted = onInstallStarted(installDeferred);

    install.addListener(listener);

    // Download the add-on
    try {
      install.install();
      await downloadDeferred.promise;
    } catch (err) {
      reportError(err);
      install.removeListener(listener);
      throw err;
    }

    await onComplete(install, listener);

    // Finish paused installation
    try {
      install.install();
      await installDeferred.promise;
    } catch (err) {
      reportError(err);
      install.removeListener(listener);
      await onFailedInstall();
      throw err;
    }

    install.removeListener(listener);

    return [install.addon.id, install.addon.version];
  }

  /**
   * Enroll in the study represented by the given recipe.
   * @param recipe Object describing the study to enroll in.
   * @param extensionDetails Object describing the addon to be installed.
   */
  async enroll(recipe, extensionDetails) {
    // This function first downloads the add-on to get its metadata. Then it
    // uses that metadata to record a study in `AddonStudies`. Then, it finishes
    // installing the add-on, and finally sends telemetry. If any of these steps
    // fails, the previous ones are undone, as needed.
    //
    // This ordering is important because the only intermediate states we can be
    // in are:
    //   1. The add-on is only downloaded, in which case AddonManager will clean it up.
    //   2. The study has been recorded, in which case we will unenroll on next
    //      start up, assuming that the add-on was uninstalled while the browser was
    //      shutdown.
    //   3. After installation is complete, but before telemetry, in which case we
    //      lose an enroll event. This is acceptable.
    //
    // This way we a shutdown, crash or unexpected error can't leave Normandy in
    // a long term inconsistent state. The main thing avoided is having a study
    // add-on installed but no record of it, which would leave it permanently
    // installed.

    if (recipe.arguments.isEnrollmentPaused) {
      // Recipe does not need anything done
      return;
    }

    const { extensionApiId, name, description } = recipe.arguments;

    const onInstallStarted = installDeferred => {
      return cbInstall => {
        const versionMatches = cbInstall.addon.version === extensionDetails.version;
        const idMatches = cbInstall.addon.id === extensionDetails.extension_id;

        if (cbInstall.existingAddon) {
          installDeferred.reject(new AddonStudyEnrollError(name, {reason: "conflicting-addon-id"}));
          return false; // cancel the installation, no upgrades allowed
        } else if (!versionMatches || !idMatches) {
          installDeferred.reject(new AddonStudyEnrollError(name, {
            reason: "metadata-mismatch",
          }));
          return false; // cancel the installation, server metadata do not match downloaded add-on
        }
        return true;
      };
    };

    const onComplete = async (install, listener) => {
      const study = {
        recipeId: recipe.id,
        name,
        description,
        addonId: install.addon.id,
        addonVersion: install.addon.version,
        addonUrl: extensionDetails.xpi,
        extensionApiId,
        extensionHash: extensionDetails.hash,
        extensionHashAlgorithm: extensionDetails.hash_algorithm,
        active: true,
        studyStartDate: new Date(),
      };

      try {
        await AddonStudies.add(study);
      } catch (err) {
        this.reportEnrollError(err);
        install.removeListener(listener);
        install.cancel();
        throw err;
      }
    };

    const onFailedInstall = async () => {
      await AddonStudies.delete(recipe.id);
    };

    const [installedId, installedVersion] = await this.downloadAndInstall(
      recipe,
      extensionDetails,
      onInstallStarted,
      onComplete,
      onFailedInstall,
      AddonStudyEnrollError,
      this.reportEnrollError,
    );

    // All done, report success to Telemetry
    TelemetryEvents.sendEvent("enroll", "addon_study", name, {
      addonId: installedId,
      addonVersion: installedVersion,
    });
  }

  /**
   * Update the study represented by the given recipe.
   * @param recipe Object describing the study to be updated.
   * @param extensionDetails Object describing the addon to be installed.
   */
  async update(recipe, extensionDetails) {
    const study = await AddonStudies.get(recipe.id);
    const { extensionApiId, name } = recipe.arguments;

    let error;

    if (study.addonId !== extensionDetails.extension_id) {
      error = new AddonStudyUpdateError(name, {
        reason: "addon-id-mismatch",
      });
    }

    const versionCompare = Services.vc.compare(study.addonVersion, extensionDetails.version);
    if (versionCompare > 0) {
      error = new AddonStudyUpdateError(name, {
        reason: "no-downgrade",
      });
    } else if (versionCompare === 0) {
      return; // Unchanged, do nothing
    }

    if (error) {
      this.reportUpdateError(error);
      throw error;
    }

    const onInstallStarted = installDeferred => {
      return cbInstall => {
        const versionMatches = cbInstall.addon.version === extensionDetails.version;
        const idMatches = cbInstall.addon.id === extensionDetails.extension_id;

        if (!cbInstall.existingAddon) {
          installDeferred.reject(new AddonStudyUpdateError(name, {
            reason: "addon-does-not-exist",
          }));
          return false; // cancel the installation, must upgrade an existing add-on
        } else if (!versionMatches || !idMatches) {
          installDeferred.reject(new AddonStudyUpdateError(name, {
            reason: "metadata-mismatch",
          }));
          return false; // cancel the installation, server metadata do not match downloaded add-on
        }

        return true;
      };
    };

    const onComplete = async (install, listener) => {
      try {
        await AddonStudies.update({
          ...study,
          addonVersion: install.addon.version,
          addonUrl: extensionDetails.xpi,
          extensionHash: extensionDetails.hash,
          extensionHashAlgorithm: extensionDetails.hash_algorithm,
          extensionApiId,
        });
      } catch (err) {
        this.reportUpdateError(err);
        install.removeListener(listener);
        install.cancel();
        throw err;
      }
    };

    const onFailedInstall = () => {
      AddonStudies.update(study);
    };

    const [installedId, installedVersion] = await this.downloadAndInstall(
      recipe,
      extensionDetails,
      onInstallStarted,
      onComplete,
      onFailedInstall,
      AddonStudyUpdateError,
      this.reportUpdateError,
    );

    // All done, report success to Telemetry
    TelemetryEvents.sendEvent("update", "addon_study", name, {
      addonId: installedId,
      addonVersion: installedVersion,
    });
  }

  reportEnrollError(error) {
    if (error instanceof AddonStudyEnrollError) {
      // One of our known errors. Report it nicely to telemetry
      TelemetryEvents.sendEvent("enrollFailed", "addon_study", error.studyName, error.extra);
    } else {
      /*
        * Some unknown error. Add some helpful details, and report it to
        * telemetry. The actual stack trace and error message could possibly
        * contain PII, so we don't include them here. Instead include some
        * information that should still be helpful, and is less likely to be
        * unsafe.
        */
      const safeErrorMessage = `${error.fileName}:${error.lineNumber}:${error.columnNumber} ${error.name}`;
      TelemetryEvents.sendEvent("enrollFailed", "addon_study", error.studyName, {
        reason: safeErrorMessage.slice(0, 80),  // max length is 80 chars
      });
    }
  }

  reportUpdateError(error) {
    if (error instanceof AddonStudyUpdateError) {
      // One of our known errors. Report it nicely to telemetry
      TelemetryEvents.sendEvent("updateFailed", "addon_study", error.studyName, error.extra);
    } else {
      /*
        * Some unknown error. Add some helpful details, and report it to
        * telemetry. The actual stack trace and error message could possibly
        * contain PII, so we don't include them here. Instead include some
        * information that should still be helpful, and is less likely to be
        * unsafe.
        */
      const safeErrorMessage = `${error.fileName}:${error.lineNumber}:${error.columnNumber} ${error.name}`;
      TelemetryEvents.sendEvent("updateFailed", "addon_study", error.studyName, {
        reason: safeErrorMessage.slice(0, 80),  // max length is 80 chars
      });
    }
  }

  /**
   * Unenrolls the client from the study with a given recipe ID.
   * @param recipeId The recipe ID of an enrolled study
   * @param reason The reason for this unenrollment, to be used in Telemetry
   * @throws If the specified study does not exist, or if it is already inactive.
   */
  async unenroll(recipeId, reason = "unknown") {
    const study = await AddonStudies.get(recipeId);
    if (!study) {
      throw new Error(`No study found for recipe ${recipeId}.`);
    }
    if (!study.active) {
      throw new Error(`Cannot stop study for recipe ${recipeId}; it is already inactive.`);
    }

    await AddonStudies.markAsEnded(study, reason);

    const addon = await AddonManager.getAddonByID(study.addonId);
    if (addon) {
      await addon.uninstall();
    } else {
      this.log.warn(`Could not uninstall addon ${study.addonId} for recipe ${study.recipeId}: it is not installed.`);
    }
  }
}
