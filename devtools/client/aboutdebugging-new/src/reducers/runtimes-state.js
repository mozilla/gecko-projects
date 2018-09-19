/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {
  CONNECT_RUNTIME_SUCCESS,
  DISCONNECT_RUNTIME_SUCCESS,
  NETWORK_LOCATIONS_UPDATED,
  RUNTIMES,
  UNWATCH_RUNTIME_SUCCESS,
  WATCH_RUNTIME_SUCCESS,
} = require("../constants");

const {
  findRuntimeById,
} = require("devtools/client/aboutdebugging-new/src/modules/runtimes-state-helper");

// Map between known runtime types and nodes in the runtimes state.
const TYPE_TO_RUNTIMES_KEY = {
  [RUNTIMES.THIS_FIREFOX]: "thisFirefoxRuntimes",
  [RUNTIMES.NETWORK]: "networkRuntimes",
};

function RuntimesState(networkRuntimes = []) {
  return {
    networkRuntimes,
    selectedRuntimeId: null,
    thisFirefoxRuntimes: [{
      id: RUNTIMES.THIS_FIREFOX,
      type: RUNTIMES.THIS_FIREFOX,
    }]
  };
}

function runtimesReducer(state = RuntimesState(), action) {
  switch (action.type) {
    case CONNECT_RUNTIME_SUCCESS: {
      const { id, client } = action.runtime;

      // Find the array of runtimes that contains the updated runtime.
      const runtime = findRuntimeById(id, state);
      const key = TYPE_TO_RUNTIMES_KEY[runtime.type];
      const runtimesToUpdate = state[key];

      // Add the new client to the runtime.
      const updatedRuntimes = runtimesToUpdate.map(r => {
        if (r.id === id) {
          return Object.assign({}, r, { client });
        }
        return r;
      });
      return Object.assign({}, state, { [key]: updatedRuntimes });
    }

    case DISCONNECT_RUNTIME_SUCCESS: {
      const { id } = action.runtime;

      // Find the array of runtimes that contains the updated runtime.
      const runtime = findRuntimeById(id, state);
      const key = TYPE_TO_RUNTIMES_KEY[runtime.type];
      const runtimesToUpdate = state[key];

      // Remove the client from the updated runtime.
      const updatedRuntimes = runtimesToUpdate.map(r => {
        if (r.id === id) {
          return Object.assign({}, r, { client: null });
        }
        return r;
      });
      return Object.assign({}, state, { [key]: updatedRuntimes });
    }

    case NETWORK_LOCATIONS_UPDATED: {
      const { locations } = action;
      const networkRuntimes = locations.map(location => {
        return {
          id: location,
          type: RUNTIMES.NETWORK,
        };
      });
      return Object.assign({}, state, { networkRuntimes });
    }

    case UNWATCH_RUNTIME_SUCCESS: {
      return Object.assign({}, state, { selectedRuntimeId: null });
    }

    case WATCH_RUNTIME_SUCCESS: {
      const { id } = action.runtime;
      return Object.assign({}, state, { selectedRuntimeId: id });
    }

    default:
      return state;
  }
}

module.exports = {
  RuntimesState,
  runtimesReducer,
};
