/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const PropTypes = require("devtools/client/shared/vendor/react-prop-types");
const { ClientWrapper } = require("../modules/client-wrapper");

const runtimeInfo = {
  // device name which is running the runtime,
  // unavailable on this-firefox runtime
  deviceName: PropTypes.string,

  // icon which represents the kind of runtime
  icon: PropTypes.string.isRequired,

  // name of runtime such as "Firefox Nightly"
  name: PropTypes.string.isRequired,

  // version of runtime
  version: PropTypes.string.isRequired,
};

const runtimeDetails = {
  // ClientWrapper built using a DebuggerClient for the runtime
  clientWrapper: PropTypes.instanceOf(ClientWrapper).isRequired,

  // reflect devtools.debugger.prompt-connection preference of this runtime
  connectionPromptEnabled: PropTypes.bool.isRequired,

  // runtime information
  info: PropTypes.shape(runtimeInfo).isRequired,

  // True if this runtime supports multiple content processes
  // This might be undefined when connecting to runtimes older than Fx 66
  isMultiE10s: PropTypes.bool,

  // True if service workers should be available in the target runtime. Service workers
  // can be disabled via preferences or if the runtime runs in fully private browsing
  // mode.
  serviceWorkersAvailable: PropTypes.bool.isRequired,
};
exports.runtimeDetails = PropTypes.shape(runtimeDetails);

const networkRuntimeConnectionParameter = {
  // host name of debugger server to connect
  host: PropTypes.string.isRequired,

  // port number of debugger server to connect
  port: PropTypes.number.isRequired,
};

const usbRuntimeConnectionParameter = {
  // device id
  deviceId: PropTypes.string.isRequired,
  // socket path to connect debugger server
  socketPath: PropTypes.string.isRequired,
};

const runtimeExtra = {
  // parameter to connect to debugger server
  // unavailable on unknown runtimes
  connectionParameters: PropTypes.oneOfType([
    PropTypes.shape(networkRuntimeConnectionParameter),
    PropTypes.shape(usbRuntimeConnectionParameter),
  ]),

  // device name
  // unavailable on this-firefox and network-location runtimes
  deviceName: PropTypes.string,
};

const runtime = {
  // unique id for the runtime
  id: PropTypes.string.isRequired,

  // object containing non standard properties that depend on the runtime type,
  // unavailable on this-firefox runtime
  extra: PropTypes.shape(runtimeExtra),

  // unknown runtimes are placeholders for devices where the runtime has not been started
  // yet. For instance an ADB device connected without a compatible runtime running.
  isUnknown: PropTypes.bool.isRequired,

  // display name of the runtime
  name: PropTypes.string.isRequired,

  // available after the connection to the runtime is established
  // unavailable on disconnected runtimes
  runtimeDetails: PropTypes.shape(runtimeDetails),

  // runtime type, for instance "network", "usb" ...
  type: PropTypes.string.isRequired,
};

/**
 * Export type of runtime
 */
exports.runtime = PropTypes.shape(runtime);
