/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {classes: Cc, interfaces: Ci, utils: Cu} = Components;

Cu.import("resource://gre/modules/AppConstants.jsm", this);
Cu.import("resource://gre/modules/AsyncShutdown.jsm", this);
Cu.import("resource://gre/modules/KeyValueParser.jsm");
Cu.import("resource://gre/modules/osfile.jsm", this);
Cu.import("resource://gre/modules/PromiseUtils.jsm", this);
Cu.import("resource://gre/modules/Services.jsm", this);
Cu.import("resource://gre/modules/XPCOMUtils.jsm", this);

// Set to true if the application is quitting
var gQuitting = false;

// Tracks all the running instances of the minidump-analyzer
var gRunningProcesses = new Set();

/**
 * Run the minidump analyzer tool to gather stack traces from the minidump. The
 * stack traces will be stored in the .extra file under the StackTraces= entry.
 *
 * @param minidumpPath {string} The path to the minidump file
 *
 * @returns {Promise} A promise that gets resolved once minidump analysis has
 *          finished.
 */
function runMinidumpAnalyzer(minidumpPath) {
  return new Promise((resolve, reject) => {
    try {
      const binSuffix = AppConstants.platform === "win" ? ".exe" : "";
      const exeName = "minidump-analyzer" + binSuffix;

      let exe = Services.dirsvc.get("GreBinD", Ci.nsIFile);

      if (AppConstants.platform === "macosx") {
        exe.append("crashreporter.app");
        exe.append("Contents");
        exe.append("MacOS");
      }

      exe.append(exeName);

      let args = [ minidumpPath ];
      let process = Cc["@mozilla.org/process/util;1"]
                      .createInstance(Ci.nsIProcess);
      process.init(exe);
      process.startHidden = true;
      process.runAsync(args, args.length, (subject, topic, data) => {
        switch (topic) {
          case "process-finished":
            gRunningProcesses.delete(process);
            resolve();
            break;
          default:
            reject(new Error("Unexpected topic received " + topic));
            break;
        }
      });

      gRunningProcesses.add(process);
    } catch (e) {
      Cu.reportError(e);
    }
  });
}

/**
 * Computes the SHA256 hash of a minidump file
 *
 * @param minidumpPath {string} The path to the minidump file
 *
 * @returns {Promise} A promise that resolves to the hash value of the
 *          minidump.
 */
function computeMinidumpHash(minidumpPath) {
  return (async function() {
    try {
      let minidumpData = await OS.File.read(minidumpPath);
      let hasher = Cc["@mozilla.org/security/hash;1"]
                     .createInstance(Ci.nsICryptoHash);
      hasher.init(hasher.SHA256);
      hasher.update(minidumpData, minidumpData.length);

      let hashBin = hasher.finish(false);
      let hash = "";

      for (let i = 0; i < hashBin.length; i++) {
        // Every character in the hash string contains a byte of the hash data
        hash += ("0" + hashBin.charCodeAt(i).toString(16)).slice(-2);
      }

      return hash;
    } catch (e) {
      Cu.reportError(e);
      return null;
    }
  })();
}

/**
 * Process the given .extra file and return the annotations it contains in an
 * object.
 *
 * @param extraPath {string} The path to the .extra file
 *
 * @return {Promise} A promise that resolves to an object holding the crash
 *         annotations.
 */
function processExtraFile(extraPath) {
  return (async function() {
    try {
      let decoder = new TextDecoder();
      let extraData = await OS.File.read(extraPath);

      return parseKeyValuePairs(decoder.decode(extraData));
    } catch (e) {
      Cu.reportError(e);
      return {};
    }
  })();
}

/**
 * This component makes crash data available throughout the application.
 *
 * It is a service because some background activity will eventually occur.
 */
this.CrashService = function() {
  Services.obs.addObserver(this, "quit-application");
};

CrashService.prototype = Object.freeze({
  classID: Components.ID("{92668367-1b17-4190-86b2-1061b2179744}"),
  QueryInterface: XPCOMUtils.generateQI([
    Ci.nsICrashService,
    Ci.nsIObserver,
  ]),

  async addCrash(processType, crashType, id) {
    switch (processType) {
    case Ci.nsICrashService.PROCESS_TYPE_MAIN:
      processType = Services.crashmanager.PROCESS_TYPE_MAIN;
      break;
    case Ci.nsICrashService.PROCESS_TYPE_CONTENT:
      processType = Services.crashmanager.PROCESS_TYPE_CONTENT;
      break;
    case Ci.nsICrashService.PROCESS_TYPE_PLUGIN:
      processType = Services.crashmanager.PROCESS_TYPE_PLUGIN;
      break;
    case Ci.nsICrashService.PROCESS_TYPE_GMPLUGIN:
      processType = Services.crashmanager.PROCESS_TYPE_GMPLUGIN;
      break;
    case Ci.nsICrashService.PROCESS_TYPE_GPU:
      processType = Services.crashmanager.PROCESS_TYPE_GPU;
      break;
    default:
      throw new Error("Unrecognized PROCESS_TYPE: " + processType);
    }

    switch (crashType) {
    case Ci.nsICrashService.CRASH_TYPE_CRASH:
      crashType = Services.crashmanager.CRASH_TYPE_CRASH;
      break;
    case Ci.nsICrashService.CRASH_TYPE_HANG:
      crashType = Services.crashmanager.CRASH_TYPE_HANG;
      break;
    default:
      throw new Error("Unrecognized CRASH_TYPE: " + crashType);
    }

    let cr = Cc["@mozilla.org/toolkit/crash-reporter;1"]
               .getService(Components.interfaces.nsICrashReporter);
    let minidumpPath = cr.getMinidumpForID(id).path;
    let extraPath = cr.getExtraFileForID(id).path;
    let metadata = {};
    let hash = null;

    if (!gQuitting) {
      // Minidump analysis can take a long time, don't start it if the browser
      // is already quitting.
      await runMinidumpAnalyzer(minidumpPath);
    }

    metadata = await processExtraFile(extraPath);
    hash = await computeMinidumpHash(minidumpPath);

    if (hash) {
      metadata.MinidumpSha256Hash = hash;
    }

    let blocker = Services.crashmanager.addCrash(processType, crashType, id,
                                                 new Date(), metadata);

    AsyncShutdown.profileBeforeChange.addBlocker(
      "CrashService waiting for content crash ping to be sent", blocker
    );

    blocker.then(AsyncShutdown.profileBeforeChange.removeBlocker(blocker));

    await blocker;
  },

  observe(subject, topic, data) {
    switch (topic) {
      case "profile-after-change":
        // Side-effect is the singleton is instantiated.
        Services.crashmanager;
        break;
      case "quit-application":
        gQuitting = true;
        gRunningProcesses.forEach((process) => {
          process.kill();
          Services.obs.notifyObservers(null, "test-minidump-analyzer-killed");
        });
        break;
    }
  },
});

this.NSGetFactory = XPCOMUtils.generateNSGetFactory([CrashService]);
