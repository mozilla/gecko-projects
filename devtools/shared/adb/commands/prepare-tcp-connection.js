/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { dumpn } = require("devtools/shared/DevToolsUtils");
const {
  ConnectionManager,
} = require("devtools/shared/client/connection-manager");
const { runCommand } = require("devtools/shared/adb/commands/run-command");

// sends adb forward deviceId, localPort and devicePort
const forwardPort = function(deviceId, localPort, devicePort) {
  dumpn("forwardPort " + localPort + " -- " + devicePort);
  // Send "host-serial:<serial-number>:<request>",
  // with <request> set to "forward:<local>;<remote>"
  // See https://android.googlesource.com/platform/system/core/+/jb-dev/adb/SERVICES.TXT
  return runCommand(
    `host-serial:${deviceId}:forward:${localPort};${devicePort}`
  ).then(function onSuccess(data) {
    return data;
  });
};

// Prepare TCP connection for provided device id and socket path.
// The returned value is a port number of localhost for the connection.
const prepareTCPConnection = async function(deviceId, socketPath) {
  const port = ConnectionManager.getFreeTCPPort();
  const local = `tcp:${port}`;
  const remote = socketPath.startsWith("@")
    ? `localabstract:${socketPath.substring(1)}`
    : `localfilesystem:${socketPath}`;
  await forwardPort(deviceId, local, remote);
  return port;
};
exports.prepareTCPConnection = prepareTCPConnection;
