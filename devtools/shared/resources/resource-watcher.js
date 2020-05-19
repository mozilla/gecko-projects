/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const EventEmitter = require("devtools/shared/event-emitter");
const Services = require("Services");

class ResourceWatcher {
  /**
   * This class helps retrieving existing and listening to resources.
   * A resource is something that:
   *  - the target you are debugging exposes
   *  - can be created as early as the process/worker/page starts loading
   *  - can already exist, or will be created later on
   *  - doesn't require any user data to be fetched, only a type/category
   *
   * @param {TargetList} targetList
   *        A TargetList instance, which helps communicating to the backend
   *        in order to iterate and listen over the requested resources.
   */

  constructor(targetList) {
    this.targetList = targetList;

    this._onTargetAvailable = this._onTargetAvailable.bind(this);
    this._onTargetDestroyed = this._onTargetDestroyed.bind(this);

    this._onResourceAvailable = this._onResourceAvailable.bind(this);

    this._availableListeners = new EventEmitter();
    this._destroyedListeners = new EventEmitter();

    this._listenerCount = new Map();

    // This set is only used to know which resources have been watched and then
    // unwatched, since the ResourceWatcher doesn't support calling
    // watch, unwatch and watch again.
    this._previouslyListenedTypes = new Set();
  }

  get contentToolboxFissionPrefValue() {
    if (!this._contentToolboxFissionPrefValue) {
      this._contentToolboxFissionPrefValue = Services.prefs.getBoolPref(
        "devtools.contenttoolbox.fission",
        false
      );
    }
    return this._contentToolboxFissionPrefValue;
  }

  /**
   * Request to start retrieving all already existing instances of given
   * type of resources and also start watching for the one to be created after.
   *
   * @param {Array:string} resources
   *        List of all resources which should be fetched and observed.
   * @param {Object} options
   *        - {Function} onAvailable: This attribute is mandatory.
   *                                  Function which will be called once per existing
   *                                  resource and each time a resource is created.
   *        - {Function} onDestroyed: This attribute is optional.
   *                                  Function which will be called each time a resource in
   *                                  the remote target is destroyed.
   */
  async watch(resources, options) {
    const { onAvailable, onDestroyed } = options;

    // First ensuring enabling listening to targets.
    // This will call onTargetAvailable for all already existing targets,
    // as well as for the one created later.
    // Do this *before* calling _startListening in order to register
    // "resource-available" listener before requesting for the resources in _startListening.
    await this._watchAllTargets();

    for (const resource of resources) {
      this._availableListeners.on(resource, onAvailable);
      if (onDestroyed) {
        this._destroyedListeners.on(resource, onDestroyed);
      }
      await this._startListening(resource);
    }
  }

  /**
   * Stop watching for given type of resources.
   * See `watch` for the arguments as both methods receive the same.
   */
  unwatch(resources, options) {
    const { onAvailable, onDestroyed } = options;

    for (const resource of resources) {
      this._availableListeners.off(resource, onAvailable);
      if (onDestroyed) {
        this._destroyedListeners.off(resource, onDestroyed);
      }
      this._stopListening(resource);
    }

    // Stop watching for targets if we removed the last listener.
    let listeners = 0;
    for (const count of this._listenerCount) {
      listeners += count;
    }
    if (listeners <= 0) {
      this._unwatchAllTargets();
    }
  }

  /**
   * Start watching for all already existing and future targets.
   *
   * We are using ALL_TYPES, but this won't force listening to all types.
   * It will only listen for types which are defined by `TargetList.startListening`.
   */
  async _watchAllTargets() {
    if (this._isWatchingTargets) {
      return;
    }
    this._isWatchingTargets = true;
    await this.targetList.watchTargets(
      this.targetList.ALL_TYPES,
      this._onTargetAvailable,
      this._onTargetDestroyed
    );
  }

  async _unwatchAllTargets() {
    if (!this._isWatchingTargets) {
      return;
    }
    this._isWatchingTargets = false;
    await this.targetList.unwatchTargets(
      this.targetList.ALL_TYPES,
      this._onTargetAvailable,
      this._onTargetDestroyed
    );
  }

  /**
   * Method called by the TargetList for each already existing or target which has just been created.
   *
   * @param {Front} targetFront
   *        The Front of the target that is available.
   *        This Front inherits from TargetMixin and is typically
   *        composed of a BrowsingContextTargetFront or ContentProcessTargetFront.
   */
  async _onTargetAvailable({ targetFront }) {
    // For each resource type...
    for (const resourceType of Object.values(ResourceWatcher.TYPES)) {
      // ...which has at least one listener...
      if (!this._listenerCount.get(resourceType)) {
        continue;
      }
      // ...request existing resource and new one to come from this one target
      await this._watchResourcesForTarget(targetFront, resourceType);
    }
  }

  /**
   * Method called by the TargetList when a target has just been destroyed
   * See _onTargetAvailable for arguments, they are the same.
   */
  _onTargetDestroyed({ targetFront }) {
    //TODO: Is there a point in doing anything?
    //
    // We could remove the available/destroyed event, but as the target is destroyed
    // its listeners will be destroyed anyway.
  }

  /**
   * Method called either by:
   * - the backward compatibility code (LegacyListeners)
   * - target actors RDP events
   * whenever an already existing resource is being listed or when a new one
   * has been created.
   *
   * @param {Front} targetFront
   *        The Target Front from which this resource comes from.
   * @param {String} resourceType
   *        One string of ResourceWatcher.TYPES, which designes the types of resources
   *        being reported
   * @param {json/Front} resource
   *        Depending on the resource Type, it can be a JSON object or a Front
   *        which describes the resource.
   */
  _onResourceAvailable(targetFront, resourceType, resource) {
    this._availableListeners.emit(resourceType, {
      resourceType,
      targetFront,
      resource,
    });
  }

  /**
   * Called everytime a resource is destroyed in the remote target.
   * See _onResourceAvailable for the argument description.
   *
   * XXX: No usage of this yet. May be useful for the inspector? sources?
   */
  _onResourceDestroyed(targetFront, resourceType, resource) {
    this._destroyedListeners.emit(resourceType, {
      resourceType,
      targetFront,
      resource,
    });
  }

  /**
   * Start listening for a given type of resource.
   * For backward compatibility code, we register the legacy listeners on
   * each individual target
   *
   * @param {String} resourceType
   *        One string of ResourceWatcher.TYPES, which designates the types of resources
   *        to be listened.
   */
  async _startListening(resourceType) {
    const isDocumentEvent =
      resourceType === ResourceWatcher.TYPES.DOCUMENT_EVENTS;

    let listeners = this._listenerCount.get(resourceType) || 0;
    listeners++;
    if (listeners > 1) {
      // If there are several calls to watch, only the first caller receives
      // "existing" resources. Throw to avoid inconsistent behaviors
      if (isDocumentEvent) {
        // For DOCUMENT_EVENTS, return without throwing because this is already
        // used by several callsites in the netmonitor.
        // This should be reviewed in Bug 1625909.
        this._listenerCount.set(resourceType, listeners);
        return;
      }

      throw new Error(
        `The ResourceWatcher is already listening to "${resourceType}", ` +
          "the client should call `watch` only once per resource type."
      );
    }

    const wasListening = this._previouslyListenedTypes.has(resourceType);
    if (wasListening && !isDocumentEvent) {
      // We already called watch/unwatch for this resource.
      // This can lead to the onAvailable callback being called twice because we
      // don't perform any cleanup in _unwatchResourcesForTarget.
      throw new Error(
        `The ResourceWatcher previously watched "${resourceType}" ` +
          "and doesn't support watching again on a previous resource."
      );
    }

    this._listenerCount.set(resourceType, listeners);
    this._previouslyListenedTypes.add(resourceType);

    // If this is the first listener for this type of resource,
    // we should go through all the existing targets as onTargetAvailable
    // has already been called for these existing targets.
    const promises = [];
    for (const targetType of this.targetList.ALL_TYPES) {
      // XXX: May be expose a getReallyAllTarget() on TargetList?
      for (const target of this.targetList.getAllTargets(targetType)) {
        promises.push(this._watchResourcesForTarget(target, resourceType));
      }
    }
    await Promise.all(promises);
  }

  /**
   * Call backward compatibility code from `LegacyListeners` in order to listen for a given
   * type of resource from a given target.
   */
  _watchResourcesForTarget(targetFront, resourceType) {
    const onAvailable = this._onResourceAvailable.bind(
      this,
      targetFront,
      resourceType
    );
    return LegacyListeners[resourceType]({
      targetList: this.targetList,
      targetFront,
      isFissionEnabledOnContentToolbox: this.contentToolboxFissionPrefValue,
      onAvailable,
    });
  }

  /**
   * Reverse of _startListening. Stop listening for a given type of resource.
   * For backward compatibility, we unregister from each individual target.
   */
  _stopListening(resourceType) {
    let listeners = this._listenerCount.get(resourceType);
    if (!listeners || listeners <= 0) {
      throw new Error(
        `Stopped listening for resource '${resourceType}' that isn't being listened to`
      );
    }
    listeners--;
    this._listenerCount.set(resourceType, listeners);
    if (listeners > 0) {
      return;
    }

    // If this was the last listener, we should stop watching these events from the actors
    // and the actors should stop watching things from the platform
    for (const targetType of this.targetList.ALL_TYPES) {
      // XXX: May be expose a getReallyAllTarget() on TargetList?
      for (const target of this.targetList.getAllTargets(targetType)) {
        this._unwatchResourcesForTarget(targetType, target, resourceType);
      }
    }
  }

  /**
   * Backward compatibility code, reverse of _watchResourcesForTarget.
   */
  _unwatchResourcesForTarget(targetType, targetFront, resourceType) {
    // Is there really a point in:
    // - unregistering `onAvailable` RDP event callbacks from target-scoped actors?
    // - calling `stopListeners()` as we are most likely closing the toolbox and destroying everything?
    //
    // It is important to keep this method synchronous and do as less as possible
    // in the case of toolbox destroy.
    //
    // We are aware of one case where that might be useful.
    // When a panel is disabled via the options panel, after it has been opened.
    // Would that justify doing this? Is there another usecase?
  }
}

ResourceWatcher.TYPES = ResourceWatcher.prototype.TYPES = {
  CONSOLE_MESSAGES: "console-messages",
  ERROR_MESSAGES: "error-messages",
  PLATFORM_MESSAGES: "platform-messages",
  DOCUMENT_EVENTS: "document-events",
  ROOT_NODE: "root-node",
};
module.exports = { ResourceWatcher };

// Backward compat code for each type of resource.
// Each section added here should eventually be removed once the equivalent server
// code is implement in Firefox, in its release channel.
const LegacyListeners = {
  [ResourceWatcher.TYPES
    .CONSOLE_MESSAGES]: require("devtools/shared/resources/legacy-listeners/console-messages"),
  [ResourceWatcher.TYPES
    .ERROR_MESSAGES]: require("devtools/shared/resources/legacy-listeners/error-messages"),
  [ResourceWatcher.TYPES
    .PLATFORM_MESSAGES]: require("devtools/shared/resources/legacy-listeners/platform-messages"),
  async [ResourceWatcher.TYPES.DOCUMENT_EVENTS]({
    targetList,
    targetFront,
    onAvailable,
  }) {
    // DocumentEventsListener of webconsole handles only top level document.
    if (!targetFront.isTopLevel) {
      return;
    }

    const webConsoleFront = await targetFront.getFront("console");
    webConsoleFront.on("documentEvent", onAvailable);
    await webConsoleFront.startListeners(["DocumentEvents"]);
  },
  [ResourceWatcher.TYPES
    .ROOT_NODE]: require("devtools/shared/resources/legacy-listeners/root-node"),
};
