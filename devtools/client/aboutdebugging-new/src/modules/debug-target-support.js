/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { DEBUG_TARGET_PANE, RUNTIMES } = require("../constants");

const ALL_DEBUG_TARGET_PANES = [
  DEBUG_TARGET_PANE.INSTALLED_EXTENSION,
  DEBUG_TARGET_PANE.OTHER_WORKER,
  DEBUG_TARGET_PANE.SERVICE_WORKER,
  DEBUG_TARGET_PANE.SHARED_WORKER,
  DEBUG_TARGET_PANE.TAB,
  DEBUG_TARGET_PANE.TEMPORARY_EXTENSION,
];

// All debug target panes except temporary extensions
const REMOTE_DEBUG_TARGET_PANES = ALL_DEBUG_TARGET_PANES.filter(p =>
  p !== DEBUG_TARGET_PANE.TEMPORARY_EXTENSION);

const SUPPORTED_TARGET_PANE_BY_RUNTIME = {
  [RUNTIMES.THIS_FIREFOX]: ALL_DEBUG_TARGET_PANES,
  [RUNTIMES.USB]: REMOTE_DEBUG_TARGET_PANES,
  [RUNTIMES.NETWORK]: REMOTE_DEBUG_TARGET_PANES,
};

/**
 * If extension debug setting is needed for given runtime type, return true.
 *
 * @param {String} runtimeType
 * @return {bool} true: needed
 */
function isExtensionDebugSettingNeeded(runtimeType) {
  // Debugging local addons for This Firefox reuses the Browser Toolbox, which requires
  // some preferences to be enabled.
  return runtimeType === RUNTIMES.THIS_FIREFOX;
}
exports.isExtensionDebugSettingNeeded = isExtensionDebugSettingNeeded;

/**
 * A debug target pane is more specialized than a debug target. For instance EXTENSION is
 * a DEBUG_TARGET but INSTALLED_EXTENSION and TEMPORARY_EXTENSION are DEBUG_TARGET_PANES.
 */
function isSupportedDebugTargetPane(runtimeType, debugTargetPaneKey) {
  return SUPPORTED_TARGET_PANE_BY_RUNTIME[runtimeType].includes(debugTargetPaneKey);
}
exports.isSupportedDebugTargetPane = isSupportedDebugTargetPane;
