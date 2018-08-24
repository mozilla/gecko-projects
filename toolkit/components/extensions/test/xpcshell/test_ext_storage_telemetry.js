/* -*- Mode: indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim: set sts=2 sw=2 et tw=80: */
"use strict";

ChromeUtils.import("resource://gre/modules/ExtensionStorageIDB.jsm");

const HISTOGRAM_JSON_IDS = [
  "WEBEXT_STORAGE_LOCAL_SET_MS", "WEBEXT_STORAGE_LOCAL_GET_MS",
];

const HISTOGRAM_IDB_IDS = [
  "WEBEXT_STORAGE_LOCAL_IDB_SET_MS", "WEBEXT_STORAGE_LOCAL_IDB_GET_MS",
];

const HISTOGRAM_IDS = [].concat(HISTOGRAM_JSON_IDS, HISTOGRAM_IDB_IDS);

const EXTENSION_ID1 = "@test-extension1";
const EXTENSION_ID2 = "@test-extension2";

async function test_telemetry_background() {
  const expectedEmptyHistograms = ExtensionStorageIDB.isBackendEnabled ?
          HISTOGRAM_JSON_IDS : HISTOGRAM_IDB_IDS;

  const expectedNonEmptyHistograms = ExtensionStorageIDB.isBackendEnabled ?
          HISTOGRAM_IDB_IDS : HISTOGRAM_JSON_IDS;

  const server = createHttpServer();
  server.registerDirectory("/data/", do_get_file("data"));

  const BASE_URL = `http://localhost:${server.identity.primaryPort}/data`;

  async function contentScript() {
    await browser.storage.local.set({a: "b"});
    await browser.storage.local.get("a");
    browser.test.sendMessage("contentDone");
  }

  let baseManifest = {
    permissions: ["storage"],
    content_scripts: [
      {
        "matches": ["http://*/*/file_sample.html"],
        "js": ["content_script.js"],
      },
    ],
  };

  let baseExtInfo = {
    async background() {
      await browser.storage.local.set({a: "b"});
      await browser.storage.local.get("a");
      browser.test.sendMessage("backgroundDone");
    },
    files: {
      "content_script.js": contentScript,
    },
  };

  let extInfo1 = {
    ...baseExtInfo,
    manifest: {
      ...baseManifest,
      applications: {
        gecko: {id: EXTENSION_ID1},
      },
    },
  };
  let extInfo2 = {
    ...baseExtInfo,
    manifest: {
      ...baseManifest,
      applications: {
        gecko: {id: EXTENSION_ID2},
      },
    },
  };

  let extension1 = ExtensionTestUtils.loadExtension(extInfo1);
  let extension2 = ExtensionTestUtils.loadExtension(extInfo2);

  clearHistograms();

  let process = IS_OOP ? "extension" : "parent";
  let snapshots = getSnapshots(process);

  for (let id of HISTOGRAM_IDS) {
    ok(!(id in snapshots), `No data recorded for histogram: ${id}.`);
  }

  await extension1.startup();
  await extension1.awaitMessage("backgroundDone");
  for (let id of expectedNonEmptyHistograms) {
    await promiseTelemetryRecorded(id, process, 1);
  }

  // Telemetry from extension1's background page should be recorded.
  snapshots = getSnapshots(process);
  for (let id of expectedNonEmptyHistograms) {
    equal(arraySum(snapshots[id].counts), 1,
          `Data recorded for histogram: ${id}.`);
  }

  await extension2.startup();
  await extension2.awaitMessage("backgroundDone");
  for (let id of expectedNonEmptyHistograms) {
    await promiseTelemetryRecorded(id, process, 2);
  }

  // Telemetry from extension2's background page should be recorded.
  snapshots = getSnapshots(process);
  for (let id of expectedNonEmptyHistograms) {
    equal(arraySum(snapshots[id].counts), 2,
          `Additional data recorded for histogram: ${id}.`);
  }

  await extension2.unload();

  // Run a content script.
  process = IS_OOP ? "content" : "parent";
  let expectedCount = IS_OOP ? 1 : 3;

  let contentPage = await ExtensionTestUtils.loadContentPage(`${BASE_URL}/file_sample.html`);
  await extension1.awaitMessage("contentDone");

  for (let id of expectedNonEmptyHistograms) {
    await promiseTelemetryRecorded(id, process, expectedCount);
  }

  // Telemetry from extension1's content script should be recorded.
  snapshots = getSnapshots(process);
  for (let id of expectedNonEmptyHistograms) {
    equal(arraySum(snapshots[id].counts), expectedCount,
          `Data recorded in content script for histogram: ${id}.`);
  }

  await extension1.unload();

  // Telemetry for histograms that we expect to be empty.
  for (let id of expectedEmptyHistograms) {
    ok(!(id in snapshots), `No data recorded for histogram: ${id}.`);
  }

  await contentPage.close();
}

add_task(function test_telemetry_background_file_backend() {
  return runWithPrefs([[ExtensionStorageIDB.BACKEND_ENABLED_PREF, false]],
                      test_telemetry_background);
});

add_task(function test_telemetry_background_idb_backend() {
  return runWithPrefs([
    [ExtensionStorageIDB.BACKEND_ENABLED_PREF, true],
    // Set the migrated preference for the two test extension, because the
    // first storage.local call fallbacks to run in the parent process when we
    // don't know which is the selected backend during the extension startup
    // and so we can't choose the telemetry histogram to use.
    [`${ExtensionStorageIDB.IDB_MIGRATED_PREF_BRANCH}.${EXTENSION_ID1}`, true],
    [`${ExtensionStorageIDB.IDB_MIGRATED_PREF_BRANCH}.${EXTENSION_ID2}`, true],
  ], test_telemetry_background);
});
