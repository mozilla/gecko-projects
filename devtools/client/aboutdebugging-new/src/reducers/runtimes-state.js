/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {
  CONNECT_RUNTIME_SUCCESS,
  DISCONNECT_RUNTIME_SUCCESS,
  RUNTIMES,
  UPDATE_CONNECTION_PROMPT_SETTING_SUCCESS,
  UPDATE_EXTENSION_DEBUG_SETTING_SUCCESS,
  UPDATE_RUNTIME_MULTIE10S_SUCCESS,
  REMOTE_RUNTIMES_UPDATED,
  SELECTED_RUNTIME_ID_UPDATED,
  THIS_FIREFOX_RUNTIME_CREATED,
} = require("../constants");

const {
  findRuntimeById,
} = require("../modules/runtimes-state-helper");

const { remoteClientManager } =
  require("devtools/client/shared/remote-debugging/remote-client-manager");

// Map between known runtime types and nodes in the runtimes state.
const TYPE_TO_RUNTIMES_KEY = {
  [RUNTIMES.THIS_FIREFOX]: "thisFirefoxRuntimes",
  [RUNTIMES.NETWORK]: "networkRuntimes",
  [RUNTIMES.USB]: "usbRuntimes",
};

function RuntimesState() {
  return {
    networkRuntimes: [],
    selectedRuntimeId: null,
    // "This Firefox" runtimes is an array for consistency, but it should only contain one
    // runtime. This runtime will be added after initializing the application via
    // THIS_FIREFOX_RUNTIME_CREATED.
    thisFirefoxRuntimes: [],
    usbRuntimes: [],
  };
}

/**
 * Update the runtime matching the provided runtimeId with the content of updatedRuntime,
 * and return the new state.
 *
 * @param  {String} runtimeId
 *         The id of the runtime to update
 * @param  {Object} updatedRuntime
 *         Object used to update the runtime matching the idea using Object.assign.
 * @param  {Object} state
 *         Current runtimes state.
 * @return {Object} The updated state
 */
function _updateRuntimeById(runtimeId, updatedRuntime, state) {
  // Find the array of runtimes that contains the updated runtime.
  const runtime = findRuntimeById(runtimeId, state);
  const key = TYPE_TO_RUNTIMES_KEY[runtime.type];
  const runtimesToUpdate = state[key];

  // Update the runtime with the provided updatedRuntime.
  const updatedRuntimes = runtimesToUpdate.map(r => {
    if (r.id === runtimeId) {
      return Object.assign({}, r, updatedRuntime);
    }
    return r;
  });
  return Object.assign({}, state, { [key]: updatedRuntimes });
}

function runtimesReducer(state = RuntimesState(), action) {
  switch (action.type) {
    case CONNECT_RUNTIME_SUCCESS: {
      const { id, runtimeDetails, type } = action.runtime;
      remoteClientManager.setClient(id, type, runtimeDetails.clientWrapper.client);
      return _updateRuntimeById(id, { runtimeDetails }, state);
    }

    case DISCONNECT_RUNTIME_SUCCESS: {
      const { id, type } = action.runtime;
      remoteClientManager.removeClient(id, type);
      return _updateRuntimeById(id, { runtimeDetails: null }, state);
    }

    case SELECTED_RUNTIME_ID_UPDATED: {
      const selectedRuntimeId = action.runtimeId || null;
      return Object.assign({}, state, { selectedRuntimeId });
    }

    case UPDATE_CONNECTION_PROMPT_SETTING_SUCCESS: {
      const { connectionPromptEnabled } = action;
      const { id: runtimeId } = action.runtime;
      const runtime = findRuntimeById(runtimeId, state);
      const runtimeDetails =
        Object.assign({}, runtime.runtimeDetails, { connectionPromptEnabled });
      return _updateRuntimeById(runtimeId, { runtimeDetails }, state);
    }

    case UPDATE_EXTENSION_DEBUG_SETTING_SUCCESS: {
      const { extensionDebugEnabled } = action;
      const { id: runtimeId } = action.runtime;
      const runtime = findRuntimeById(runtimeId, state);
      const runtimeDetails =
        Object.assign({}, runtime.runtimeDetails, { extensionDebugEnabled });
      return _updateRuntimeById(runtimeId, { runtimeDetails }, state);
    }

    case UPDATE_RUNTIME_MULTIE10S_SUCCESS: {
      const { isMultiE10s } = action;
      const { id: runtimeId } = action.runtime;
      const runtime = findRuntimeById(runtimeId, state);
      const runtimeDetails =
        Object.assign({}, runtime.runtimeDetails, { isMultiE10s });
      return _updateRuntimeById(runtimeId, { runtimeDetails }, state);
    }

    case REMOTE_RUNTIMES_UPDATED: {
      const { runtimes, runtimeType } = action;
      const key = TYPE_TO_RUNTIMES_KEY[runtimeType];
      return Object.assign({}, state, {
        [key]: runtimes,
      });
    }

    case THIS_FIREFOX_RUNTIME_CREATED: {
      const { runtime } = action;
      return Object.assign({}, state, {
        thisFirefoxRuntimes: [runtime],
      });
    }

    default:
      return state;
  }
}

module.exports = {
  RuntimesState,
  runtimesReducer,
};
