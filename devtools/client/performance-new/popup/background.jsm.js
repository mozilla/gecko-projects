/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
// @ts-check
"use strict";

/**
 * This file contains all of the background logic for controlling the state and
 * configuration of the profiler. It is in a JSM so that the logic can be shared
 * with both the popup client, and the keyboard shortcuts. The shortcuts don't need
 * access to any UI, and need to be loaded independent of the popup.
 */

// The following are not lazily loaded as they are needed during initialization.

/** @type {import("resource://gre/modules/Services.jsm")} */
const { Services } = ChromeUtils.import("resource://gre/modules/Services.jsm");
/** @type {import("resource://gre/modules/AppConstants.jsm")} */
const { AppConstants } = ChromeUtils.import(
  "resource://gre/modules/AppConstants.jsm"
);

/**
 * @typedef {import("../@types/perf").RecordingStateFromPreferences} RecordingStateFromPreferences
 * @typedef {import("../@types/perf").PopupBackgroundFeatures} PopupBackgroundFeatures
 * @typedef {import("../@types/perf").SymbolTableAsTuple} SymbolTableAsTuple
 * @typedef {import("../@types/perf").PerformancePref} PerformancePref
 */

/** @type {PerformancePref["Entries"]} */
const ENTRIES_PREF = "devtools.performance.recording.entries";
/** @type {PerformancePref["Interval"]} */
const INTERVAL_PREF = "devtools.performance.recording.interval";
/** @type {PerformancePref["Features"]} */
const FEATURES_PREF = "devtools.performance.recording.features";
/** @type {PerformancePref["Threads"]} */
const THREADS_PREF = "devtools.performance.recording.threads";
/** @type {PerformancePref["ObjDirs"]} */
const OBJDIRS_PREF = "devtools.performance.recording.objdirs";
/** @type {PerformancePref["Duration"]} */
const DURATION_PREF = "devtools.performance.recording.duration";

// The following utilities are lazily loaded as they are not needed when controlling the
// global state of the profiler, and only are used during specific funcationality like
// symbolication or capturing a profile.

/**
 * TS-TODO
 *
 * This function replaces lazyRequireGetter, and TypeScript can understand it. It's
 * currently duplicated until we have consensus that TypeScript is a good idea.
 *
 * @template T
 * @type {(callback: () => T) => () => T}
 */
function requireLazy(callback) {
  /** @type {T | undefined} */
  let cache;
  return () => {
    if (cache === undefined) {
      cache = callback();
    }
    return cache;
  };
}

const lazyOS = requireLazy(() =>
  /** @type {import("resource://gre/modules/osfile.jsm")} */
  (ChromeUtils.import("resource://gre/modules/osfile.jsm"))
);

const lazyProfilerGetSymbols = requireLazy(() =>
  /** @type {import("resource://gre/modules/ProfilerGetSymbols.jsm")} */
  (ChromeUtils.import("resource://gre/modules/ProfilerGetSymbols.jsm"))
);

const lazyReceiveProfile = requireLazy(() => {
  const { require } = ChromeUtils.import(
    "resource://devtools/shared/Loader.jsm"
  );
  /** @type {import("devtools/client/performance-new/browser")} */
  const browserModule = require("devtools/client/performance-new/browser");
  return browserModule.receiveProfile;
});

/**
 * @type {() => Promise<void>}
 */
async function captureProfile() {
  if (!Services.profiler.IsActive()) {
    // The profiler is not active, ignore this shortcut.
    return;
  }
  // Pause profiler before we collect the profile, so that we don't capture
  // more samples while the parent process waits for subprocess profiles.
  Services.profiler.PauseSampling();

  const profile = await Services.profiler
    .getProfileDataAsGzippedArrayBuffer()
    .catch(
      /** @type {(e: any) => {}} */ e => {
        console.error(e);
        return {};
      }
    );

  // This Map caches the symbols from the shared libraries.
  const _symbolCache = new Map();

  const receiveProfile = lazyReceiveProfile();

  receiveProfile(profile, async function getSymbols(debugName, breakpadId) {
    if (_symbolCache.size === 0) {
      // Prime the symbols cache.
      for (const lib of Services.profiler.sharedLibraries) {
        _symbolCache.set(`${lib.debugName}/${lib.breakpadId}`, {
          path: lib.path,
          debugPath: lib.debugPath,
        });
      }
    }

    const cachedLibInfo = _symbolCache.get(`${debugName}/${breakpadId}`);
    if (!cachedLibInfo) {
      throw new Error(
        `The library ${debugName} ${breakpadId} is not in the ` +
          "Services.profiler.sharedLibraries list, so the local path for it is not known " +
          "and symbols for it can not be obtained. This usually happens if a content " +
          "process uses a library that's not used in the parent process - " +
          "Services.profiler.sharedLibraries only knows about libraries in the " +
          "parent process."
      );
    }

    const { path, debugPath } = cachedLibInfo;
    const { OS } = lazyOS();
    if (!OS.Path.split(path).absolute) {
      throw new Error(
        "Services.profiler.sharedLibraries did not contain an absolute path for " +
          `the library ${debugName} ${breakpadId}, so symbols for this library can not ` +
          "be obtained."
      );
    }

    const { ProfilerGetSymbols } = lazyProfilerGetSymbols();

    return ProfilerGetSymbols.getSymbolTable(path, debugPath, breakpadId);
  });

  Services.profiler.StopProfiler();
}

function startProfiler() {
  const {
    entries,
    interval,
    features,
    threads,
    duration,
  } = getRecordingPreferencesFromBrowser();

  Services.profiler.StartProfiler(
    entries,
    interval,
    features,
    threads,
    duration
  );
}

/**
 * @type {() => void}
 */
function stopProfiler() {
  Services.profiler.StopProfiler();
}

/**
 * @type {() => void}
 */
function toggleProfiler() {
  if (Services.profiler.IsActive()) {
    stopProfiler();
  } else {
    startProfiler();
  }
}

/**
 * @type {() => void}
 */
function restartProfiler() {
  stopProfiler();
  startProfiler();
}

/**
 * @param {string} prefName
 * @param {string[]} defaultValue
 * @return {string[]}
 */
function _getArrayOfStringsPref(prefName, defaultValue) {
  let array;
  try {
    const text = Services.prefs.getCharPref(prefName);
    array = JSON.parse(text);
  } catch (error) {
    return defaultValue;
  }

  if (
    Array.isArray(array) &&
    array.every(feature => typeof feature === "string")
  ) {
    return array;
  }

  return defaultValue;
}

/**
 * @param {string} prefName
 * @param {string[]} defaultValue
 * @return {string[]}
 */
function _getArrayOfStringsHostPref(prefName, defaultValue) {
  let array;
  try {
    const text = Services.prefs.getStringPref(
      prefName,
      JSON.stringify(defaultValue)
    );
    array = JSON.parse(text);
  } catch (error) {
    return defaultValue;
  }

  if (
    Array.isArray(array) &&
    array.every(feature => typeof feature === "string")
  ) {
    return array;
  }

  return defaultValue;
}

/**
 * A simple cache for the recording settings.
 * @type {RecordingStateFromPreferences}
 */
let _defaultSettings;

/**
 * This function contains the canonical defaults for both the popup and panel's
 * recording settings.
 */
function getDefaultRecordingSettings() {
  if (!_defaultSettings) {
    _defaultSettings = {
      entries: 10000000, // ~80mb,
      // Do not expire markers, let them roll off naturally from the circular buffer.
      duration: 0,
      interval: 1, // milliseconds
      features: ["js", "leaf", "responsiveness", "stackwalk"],
      threads: ["GeckoMain", "Compositor"],
      objdirs: [],
    };

    if (AppConstants.platform === "android") {
      // Java profiling is only meaningful on android.
      _defaultSettings.features.push("java");
    }
  }

  return _defaultSettings;
}

/**
 * @returns {RecordingStateFromPreferences}
 */
function getRecordingPreferencesFromBrowser() {
  const defaultSettings = getDefaultRecordingSettings();

  const entries = Services.prefs.getIntPref(
    ENTRIES_PREF,
    defaultSettings.entries
  );
  const interval = Services.prefs.getIntPref(
    INTERVAL_PREF,
    defaultSettings.interval
  );
  const features = _getArrayOfStringsPref(
    FEATURES_PREF,
    defaultSettings.features
  );
  const threads = _getArrayOfStringsPref(THREADS_PREF, defaultSettings.threads);
  const objdirs = _getArrayOfStringsHostPref(
    OBJDIRS_PREF,
    defaultSettings.objdirs
  );
  const duration = Services.prefs.getIntPref(
    DURATION_PREF,
    defaultSettings.duration
  );

  const supportedFeatures = new Set(Services.profiler.GetFeatures());

  return {
    entries,
    // The pref stores the value in usec.
    interval: interval / 1000,
    // Validate the features before passing them to the profiler.
    features: features.filter(feature => supportedFeatures.has(feature)),
    threads,
    objdirs,
    duration,
  };
}

/**
 * @param {RecordingStateFromPreferences} settings
 */
function setRecordingPreferencesOnBrowser(settings) {
  Services.prefs.setIntPref(ENTRIES_PREF, settings.entries);
  // The interval pref stores the value in microseconds for extra precision.
  Services.prefs.setIntPref(INTERVAL_PREF, settings.interval * 1000);
  Services.prefs.setCharPref(FEATURES_PREF, JSON.stringify(settings.features));
  Services.prefs.setCharPref(THREADS_PREF, JSON.stringify(settings.threads));
  Services.prefs.setCharPref(OBJDIRS_PREF, JSON.stringify(settings.objdirs));
}

const platform = AppConstants.platform;

/**
 * @type {() => void}
 */
function revertRecordingPreferences() {
  setRecordingPreferencesOnBrowser(getDefaultRecordingSettings());
}

var EXPORTED_SYMBOLS = [
  "captureProfile",
  "startProfiler",
  "stopProfiler",
  "restartProfiler",
  "toggleProfiler",
  "platform",
  "getDefaultRecordingSettings",
  "getRecordingPreferencesFromBrowser",
  "setRecordingPreferencesOnBrowser",
  "revertRecordingPreferences",
];
