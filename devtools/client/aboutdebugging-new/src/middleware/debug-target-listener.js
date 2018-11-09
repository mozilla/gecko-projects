/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { AddonManager } = require("resource://gre/modules/AddonManager.jsm");

const {
  DEBUG_TARGETS,
  UNWATCH_RUNTIME_START,
  WATCH_RUNTIME_SUCCESS,
} = require("../constants");
const Actions = require("../actions/index");
const { isSupportedDebugTarget } = require("../modules/debug-target-support");

function debugTargetListenerMiddleware(store) {
  const onExtensionsUpdated = () => {
    store.dispatch(Actions.requestExtensions());
  };

  const onTabsUpdated = () => {
    store.dispatch(Actions.requestTabs());
  };

  const extensionsListener = {
    onDisabled() {
      onExtensionsUpdated();
    },

    onEnabled() {
      onExtensionsUpdated();
    },

    onInstalled() {
      onExtensionsUpdated();
    },

    onOperationCancelled() {
      onExtensionsUpdated();
    },

    onUninstalled() {
      onExtensionsUpdated();
    },

    onUninstalling() {
      onExtensionsUpdated();
    },
  };

  const onWorkersUpdated = () => {
    store.dispatch(Actions.requestWorkers());
  };

  return next => action => {
    switch (action.type) {
      case WATCH_RUNTIME_SUCCESS: {
        const { runtime } = action;
        const { client } = runtime.runtimeDetails;

        if (isSupportedDebugTarget(runtime.type, DEBUG_TARGETS.TAB)) {
          client.mainRoot.on("tabListChanged", onTabsUpdated);
        }

        if (isSupportedDebugTarget(runtime.type, DEBUG_TARGETS.EXTENSION)) {
          AddonManager.addAddonListener(extensionsListener);
        }

        if (isSupportedDebugTarget(runtime.type, DEBUG_TARGETS.WORKER)) {
          client.mainRoot.on("workerListChanged", onWorkersUpdated);
          client.mainRoot.on("serviceWorkerRegistrationListChanged", onWorkersUpdated);
          client.mainRoot.on("processListChanged", onWorkersUpdated);
          client.addListener("registration-changed", onWorkersUpdated);
          client.addListener("push-subscription-modified", onWorkersUpdated);
        }
        break;
      }
      case UNWATCH_RUNTIME_START: {
        const { runtime } = action;
        const { client } = runtime.runtimeDetails;

        if (isSupportedDebugTarget(runtime.type, DEBUG_TARGETS.TAB)) {
          client.mainRoot.off("tabListChanged", onTabsUpdated);
        }

        if (isSupportedDebugTarget(runtime.type, DEBUG_TARGETS.EXTENSION)) {
          AddonManager.removeAddonListener(extensionsListener);
        }

        if (isSupportedDebugTarget(runtime.type, DEBUG_TARGETS.WORKER)) {
          client.mainRoot.off("workerListChanged", onWorkersUpdated);
          client.mainRoot.off("serviceWorkerRegistrationListChanged", onWorkersUpdated);
          client.mainRoot.off("processListChanged", onWorkersUpdated);
          client.removeListener("registration-changed", onWorkersUpdated);
          client.removeListener("push-subscription-modified", onWorkersUpdated);
        }
        break;
      }
    }

    return next(action);
  };
}

module.exports = debugTargetListenerMiddleware;
