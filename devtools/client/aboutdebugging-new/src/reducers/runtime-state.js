/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {
  CONNECT_RUNTIME_SUCCESS,
  DEBUG_TARGETS,
  DISCONNECT_RUNTIME_SUCCESS,
  REQUEST_EXTENSIONS_SUCCESS,
  REQUEST_TABS_SUCCESS,
} = require("../constants");

function RuntimeState() {
  return {
    client: null,
    installedExtensions: [],
    tabs: [],
    temporaryExtensions: [],
  };
}

function runtimeReducer(state = RuntimeState(), action) {
  switch (action.type) {
    case CONNECT_RUNTIME_SUCCESS: {
      const { client } = action;
      return Object.assign({}, state, { client });
    }
    case DISCONNECT_RUNTIME_SUCCESS: {
      return RuntimeState();
    }
    case REQUEST_EXTENSIONS_SUCCESS: {
      const { installedExtensions, temporaryExtensions } = action;
      return Object.assign({}, state, {
        installedExtensions: toExtensionComponentData(installedExtensions),
        temporaryExtensions: toExtensionComponentData(temporaryExtensions),
      });
    }
    case REQUEST_TABS_SUCCESS: {
      const { tabs } = action;
      return Object.assign({}, state, { tabs: toTabComponentData(tabs) });
    }

    default:
      return state;
  }
}

function getExtensionFilePath(extension) {
  // Only show file system paths, and only for temporarily installed add-ons.
  if (!extension.temporarilyInstalled ||
      !extension.url ||
      !extension.url.startsWith("file://")) {
    return null;
  }

  // Strip a leading slash from Windows drive letter URIs.
  // file:///home/foo ~> /home/foo
  // file:///C:/foo ~> C:/foo
  const windowsRegex = /^file:\/\/\/([a-zA-Z]:\/.*)/;

  if (windowsRegex.test(extension.url)) {
    return windowsRegex.exec(extension.url)[1];
  }

  return extension.url.slice("file://".length);
}

function toExtensionComponentData(extensions) {
  return extensions.map(extension => {
    const type = DEBUG_TARGETS.EXTENSION;
    const { iconURL, id, manifestURL, name } = extension;
    const icon = iconURL || "chrome://mozapps/skin/extensions/extensionGeneric.svg";
    const location = getExtensionFilePath(extension);
    const uuid = manifestURL ? /moz-extension:\/\/([^/]*)/.exec(manifestURL)[1] : null;
    return { type, id, icon, location, manifestURL, name, uuid };
  });
}

function toTabComponentData(tabs) {
  return tabs.map(tab => {
    const type = DEBUG_TARGETS.TAB;
    const id = tab.outerWindowID;
    const icon = tab.favicon
      ? `data:image/png;base64,${ btoa(String.fromCharCode.apply(String, tab.favicon)) }`
      : "chrome://devtools/skin/images/globe.svg";
    const name = tab.title || tab.url;
    const url = tab.url;
    return { type, id, icon, name, url };
  });
}

module.exports = {
  RuntimeState,
  runtimeReducer,
};
