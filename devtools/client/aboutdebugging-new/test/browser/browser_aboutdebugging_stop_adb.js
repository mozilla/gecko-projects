/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/* import-globals-from helper-adb.js */
Services.scriptloader.loadSubScript(CHROME_URL_ROOT + "helper-adb.js", this);

const { adbAddon } = require("devtools/shared/adb/adb-addon");
const { check } = require("devtools/shared/adb/adb-running-checker");

/**
 * Check that ADB is stopped:
 * - when the adb extension is uninstalled
 * - when no consumer is registered
 */
add_task(async function() {
  await pushPref("devtools.remote.adb.extensionURL",
                 CHROME_URL_ROOT + "resources/test-adb-extension/adb-extension-#OS#.xpi");
  await checkAdbNotRunning();

  const { tab } = await openAboutDebugging();

  info("Install the adb extension and wait for ADB to start");
  // Use "internal" as the install source to avoid triggering telemetry.
  adbAddon.install("internal");
  await waitForAdbStart();

  info("Open a second about:debugging");
  const { tab: secondTab } = await openAboutDebugging();

  info("Close the second about:debugging and check that ADB is still running");
  await removeTab(secondTab);
  ok(await check(), "ADB is still running");

  await removeTab(tab);

  info("Check that the adb process stops after closing about:debugging");
  await waitForAdbStop();

  info("Open a third about:debugging, wait for the ADB to start again");
  const { tab: thirdTab } = await openAboutDebugging();
  await waitForAdbStart();

  info("Uninstall the addon, this should stop ADB as well");
  adbAddon.uninstall();
  await waitForAdbStop();

  info("Reinstall the addon, this should start ADB again");
  adbAddon.install("internal");
  await waitForAdbStart();

  info("Close the last tab, this should stop ADB");
  await removeTab(thirdTab);
  await waitForAdbStop();
});
