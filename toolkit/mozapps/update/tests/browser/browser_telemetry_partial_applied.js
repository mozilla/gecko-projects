/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Telemetry test for Application Update phases.
// Telemetry update.startup
// Partial and complete patches
// Partial patch download
// Partial patch applied
add_task(async function telemetry_partial_applied() {
  let updateParams = "";
  await runTelemetryUpdateTest(updateParams, "update-downloaded");

  writeStatusFile(STATE_SUCCEEDED);
  testPostUpdateProcessing();

  let expected = getTelemetryUpdatePhaseValues({
    noInternalComplete: true,
    noBitsComplete: true,
  });
  checkTelemetryUpdatePhases(expected);

  // Verify that update phase session telemetry is empty.
  checkTelemetryUpdatePhaseEmpty(false);
});
