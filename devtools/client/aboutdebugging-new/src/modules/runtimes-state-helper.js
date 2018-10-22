/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

function getCurrentRuntime(runtimesState) {
  const selectedRuntimeId = runtimesState.selectedRuntimeId;
  return findRuntimeById(selectedRuntimeId, runtimesState);
}
exports.getCurrentRuntime = getCurrentRuntime;

function getCurrentClient(runtimesState) {
  const connection = getCurrentConnection(runtimesState);
  return connection ? connection.client : null;
}
exports.getCurrentClient = getCurrentClient;

function getCurrentRuntimeInfo(runtimesState) {
  const connection = getCurrentConnection(runtimesState);
  return connection ? connection.info : null;
}
exports.getCurrentRuntimeInfo = getCurrentRuntimeInfo;

function findRuntimeById(id, runtimesState) {
  const allRuntimes = [
    ...runtimesState.networkRuntimes,
    ...runtimesState.thisFirefoxRuntimes,
    ...runtimesState.usbRuntimes,
  ];
  return allRuntimes.find(r => r.id === id);
}
exports.findRuntimeById = findRuntimeById;

function getCurrentConnection(runtimesState) {
  const runtime = getCurrentRuntime(runtimesState);
  return runtime ? runtime.connection : null;
}
