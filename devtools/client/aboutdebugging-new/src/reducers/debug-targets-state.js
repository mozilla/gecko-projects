/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {
  REQUEST_EXTENSIONS_SUCCESS,
  REQUEST_TABS_SUCCESS,
  REQUEST_WORKERS_SUCCESS,
  UNWATCH_RUNTIME_SUCCESS,
} = require("../constants");

function DebugTargetsState() {
  return {
    installedExtensions: [],
    otherWorkers: [],
    serviceWorkers: [],
    sharedWorkers: [],
    tabs: [],
    temporaryExtensions: [],
  };
}

function debugTargetsReducer(state = DebugTargetsState(), action) {
  switch (action.type) {
    case UNWATCH_RUNTIME_SUCCESS: {
      return DebugTargetsState();
    }
    case REQUEST_EXTENSIONS_SUCCESS: {
      const { installedExtensions, temporaryExtensions } = action;
      return Object.assign({}, state, { installedExtensions, temporaryExtensions });
    }
    case REQUEST_TABS_SUCCESS: {
      const { tabs } = action;
      return Object.assign({}, state, { tabs });
    }
    case REQUEST_WORKERS_SUCCESS: {
      const { otherWorkers, serviceWorkers, sharedWorkers } = action;
      return Object.assign({}, state, { otherWorkers, serviceWorkers, sharedWorkers });
    }

    default:
      return state;
  }
}

module.exports = {
  DebugTargetsState,
  debugTargetsReducer,
};
