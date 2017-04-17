"use strict";

XPCOMUtils.defineLazyModuleGetter(this, "ExtensionManagement",
                                  "resource://gre/modules/ExtensionManagement.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "MatchPattern",
                                  "resource://gre/modules/MatchPattern.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "WebRequest",
                                  "resource://gre/modules/WebRequest.jsm");

// EventManager-like class specifically for WebRequest. Inherits from
// SingletonEventManager. Takes care of converting |details| parameter
// when invoking listeners.
function WebRequestEventManager(context, eventName) {
  let name = `webRequest.${eventName}`;
  let register = (fire, filter, info) => {
    let listener = data => {
      // Prevent listening in on requests originating from system principal to
      // prevent tinkering with OCSP, app and addon updates, etc.
      if (data.isSystemPrincipal) {
        return;
      }

      // Check hosts permissions for both the resource being requested,
      const hosts = context.extension.whiteListedHosts;
      if (!hosts.matchesIgnoringPath(Services.io.newURI(data.url))) {
        return;
      }
      // and the origin that is loading the resource.
      const origin = data.documentUrl;
      const own = origin && origin.startsWith(context.extension.getURL());
      if (origin && !own && !hosts.matchesIgnoringPath(Services.io.newURI(origin))) {
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

      let data2 = {
        requestId: data.requestId,
        url: data.url,
        originUrl: data.originUrl,
        documentUrl: data.documentUrl,
        method: data.method,
        tabId: browserData.tabId,
        type: data.type,
        timeStamp: Date.now(),
        frameId: data.type == "main_frame" ? 0 : data.windowId,
        parentFrameId: data.type == "main_frame" ? -1 : data.parentWindowId,
      };

      const maybeCached = ["onResponseStarted", "onBeforeRedirect", "onCompleted", "onErrorOccurred"];
      if (maybeCached.includes(eventName)) {
        data2.fromCache = !!data.fromCache;
      }

      if ("ip" in data) {
        data2.ip = data.ip;
      }

      let optional = ["requestHeaders", "responseHeaders", "statusCode", "statusLine", "error", "redirectUrl",
                      "requestBody", "scheme", "realm", "isProxy", "challenger"];
      for (let opt of optional) {
        if (opt in data) {
          data2[opt] = data[opt];
        }
      }

      return fire.sync(data2);
    };

    let filter2 = {};
    if (filter.urls) {
      filter2.urls = new MatchPattern(filter.urls);
      if (!filter2.urls.overlapsPermissions(context.extension.whiteListedHosts, context.extension.optionalOrigins)) {
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

    let info2 = [];
    if (info) {
      for (let desc of info) {
        if (desc == "blocking" && !context.extension.hasPermission("webRequestBlocking")) {
          Cu.reportError("Using webRequest.addListener with the blocking option " +
                         "requires the 'webRequestBlocking' permission.");
        } else {
          info2.push(desc);
        }
      }
    }

    WebRequest[eventName].addListener(listener, filter2, info2);
    return () => {
      WebRequest[eventName].removeListener(listener);
    };
  };

  return SingletonEventManager.call(this, context, name, register);
}

WebRequestEventManager.prototype = Object.create(SingletonEventManager.prototype);

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
