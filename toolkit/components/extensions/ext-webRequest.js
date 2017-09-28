"use strict";

// The ext-* files are imported into the same scopes.
/* import-globals-from ext-toolkit.js */

// This file expectes tabTracker to be defined in the global scope (e.g.
// by ext-utils.js).
/* global tabTracker */

XPCOMUtils.defineLazyModuleGetter(this, "WebRequest",
                                  "resource://gre/modules/WebRequest.jsm");

// EventManager-like class specifically for WebRequest. Inherits from
// EventManager. Takes care of converting |details| parameter
// when invoking listeners.
function WebRequestEventManager(context, eventName) {
  let name = `webRequest.${eventName}`;
  let register = (fire, filter, info) => {
    let listener = data => {
      // data.isProxy is only set from the WebRequest AuthRequestor handler,
      // in which case we know that this is onAuthRequired.  If this is proxy
      // authorization, we allow without any additional matching/filtering.
      let isProxyAuth = data.isProxy && context.extension.hasPermission("proxy");

      // Prevent listening in on requests originating from system principal to
      // prevent tinkering with OCSP, app and addon updates, etc.  However,
      // proxy addons need to be able to provide auth for any request so we
      // allow those through.  The exception is for proxy extensions handling
      // proxy authentication.
      if (data.isSystemPrincipal && !isProxyAuth) {
        return;
      }

      // Check hosts permissions for both the resource being requested,
      const hosts = context.extension.whiteListedHosts;
      if (!hosts.matches(data.URI)) {
        return;
      }
      // and the origin that is loading the resource.
      const origin = data.documentUrl;
      const own = origin && origin.startsWith(context.extension.getURL());
      if (origin && !own && !isProxyAuth && !hosts.matches(data.documentURI)) {
        return;
      }

      let browserData = {tabId: -1, windowId: -1};
      if (data.browser) {
        browserData = tabTracker.getBrowserData(data.browser);
      }
      if (filter.tabId != null && browserData.tabId != filter.tabId) {
        return;
      }
      if (filter.windowId != null && browserData.windowId != filter.windowId) {
        return;
      }

      let event = data.serialize(eventName);
      event.tabId = browserData.tabId;

      return fire.sync(event);
    };

    let filter2 = {};
    if (filter.urls) {
      let perms = new MatchPatternSet([...context.extension.whiteListedHosts.patterns,
                                       ...context.extension.optionalOrigins.patterns]);

      filter2.urls = new MatchPatternSet(filter.urls);

      if (!perms.overlapsAll(filter2.urls)) {
        Cu.reportError("The webRequest.addListener filter doesn't overlap with host permissions.");
      }
    }
    if (filter.types) {
      filter2.types = filter.types;
    }
    if (filter.tabId) {
      filter2.tabId = filter.tabId;
    }
    if (filter.windowId) {
      filter2.windowId = filter.windowId;
    }

    let blockingAllowed = context.extension.hasPermission("webRequestBlocking");

    let info2 = [];
    if (info) {
      for (let desc of info) {
        if (desc == "blocking" && !blockingAllowed) {
          Cu.reportError("Using webRequest.addListener with the blocking option " +
                         "requires the 'webRequestBlocking' permission.");
        } else {
          info2.push(desc);
        }
      }
    }

    let listenerDetails = {
      addonId: context.extension.id,
      blockingAllowed,
      tabParent: context.xulBrowser.frameLoader.tabParent,
    };

    WebRequest[eventName].addListener(
      listener, filter2, info2,
      listenerDetails);
    return () => {
      WebRequest[eventName].removeListener(listener);
    };
  };

  return EventManager.call(this, context, name, register);
}

WebRequestEventManager.prototype = Object.create(EventManager.prototype);

this.webRequest = class extends ExtensionAPI {
  getAPI(context) {
    return {
      webRequest: {
        onBeforeRequest: new WebRequestEventManager(context, "onBeforeRequest").api(),
        onBeforeSendHeaders: new WebRequestEventManager(context, "onBeforeSendHeaders").api(),
        onSendHeaders: new WebRequestEventManager(context, "onSendHeaders").api(),
        onHeadersReceived: new WebRequestEventManager(context, "onHeadersReceived").api(),
        onAuthRequired: new WebRequestEventManager(context, "onAuthRequired").api(),
        onBeforeRedirect: new WebRequestEventManager(context, "onBeforeRedirect").api(),
        onResponseStarted: new WebRequestEventManager(context, "onResponseStarted").api(),
        onErrorOccurred: new WebRequestEventManager(context, "onErrorOccurred").api(),
        onCompleted: new WebRequestEventManager(context, "onCompleted").api(),
        handlerBehaviorChanged: function() {
          // TODO: Flush all caches.
        },
      },
    };
  }
};
