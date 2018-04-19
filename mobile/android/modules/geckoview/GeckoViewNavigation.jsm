/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

var EXPORTED_SYMBOLS = ["GeckoViewNavigation"];

ChromeUtils.import("resource://gre/modules/GeckoViewModule.jsm");
ChromeUtils.import("resource://gre/modules/XPCOMUtils.jsm");

XPCOMUtils.defineLazyModuleGetters(this, {
  EventDispatcher: "resource://gre/modules/Messaging.jsm",
  LoadURIDelegate: "resource://gre/modules/LoadURIDelegate.jsm",
  Services: "resource://gre/modules/Services.jsm",
});

XPCOMUtils.defineLazyGetter(this, "dump", () =>
    ChromeUtils.import("resource://gre/modules/AndroidLog.jsm",
                       {}).AndroidLog.d.bind(null, "ViewNavigation"));

function debug(aMsg) {
  // dump(aMsg);
}

// Handles navigation requests between Gecko and a GeckoView.
// Handles GeckoView:GoBack and :GoForward requests dispatched by
// GeckoView.goBack and .goForward.
// Dispatches GeckoView:LocationChange to the GeckoView on location change when
// active.
// Implements nsIBrowserDOMWindow.
// Implements nsILoadURIDelegate.
class GeckoViewNavigation extends GeckoViewModule {
  init() {
    this._frameScriptLoaded = false;
    this.window.QueryInterface(Ci.nsIDOMChromeWindow).browserDOMWindow = this;

    this.eventDispatcher.registerListener(this, [
      "GeckoView:GoBack",
      "GeckoView:GoForward",
      "GeckoView:LoadUri",
      "GeckoView:Reload",
      "GeckoView:Stop"
    ]);
  }

  // Bundle event handler.
  onEvent(aEvent, aData, aCallback) {
    debug("onEvent: aEvent=" + aEvent + ", aData=" + JSON.stringify(aData));

    switch (aEvent) {
      case "GeckoView:GoBack":
        this.browser.goBack();
        break;
      case "GeckoView:GoForward":
        this.browser.goForward();
        break;
      case "GeckoView:LoadUri":
        this.browser.loadURI(aData.uri, null, null, null);
        break;
      case "GeckoView:Reload":
        this.browser.reload();
        break;
      case "GeckoView:Stop":
        this.browser.stop();
        break;
    }
  }

  // Message manager event handler.
  receiveMessage(aMsg) {
    debug("receiveMessage " + aMsg.name);
  }

  waitAndSetOpener(aSessionId, aOpener) {
    if (!aSessionId) {
      return Promise.resolve(null);
    }

    return new Promise(resolve => {
      const handler = {
        observe(aSubject, aTopic, aData) {
          if (aTopic === "geckoview-window-created" && aSubject.name === aSessionId) {
            if (aOpener) {
              aSubject.browser.presetOpenerWindow(aOpener);
            }
            Services.obs.removeObserver(handler, "geckoview-window-created");
            resolve(aSubject);
          }
        }
      };

      // This event is emitted from createBrowser() in geckoview.js
      Services.obs.addObserver(handler, "geckoview-window-created");
    });
  }

  handleNewSession(aUri, aOpener, aWhere, aFlags, aTriggeringPrincipal) {
    debug("handleNewSession: aUri=" + (aUri && aUri.spec) +
          " aWhere=" + aWhere +
          " aFlags=" + aFlags);

    if (!this.isRegistered) {
      return null;
    }

    const message = {
      type: "GeckoView:OnNewSession",
      uri: aUri ? aUri.displaySpec : ""
    };

    let browser = undefined;
    this.eventDispatcher.sendRequestForResult(message).then(sessionId => {
      return this.waitAndSetOpener(sessionId,
        (aFlags & Ci.nsIBrowserDOMWindow.OPEN_NO_OPENER) ? null : aOpener);
    }).then(window => {
      browser = (window && window.browser);
    }, () => {
      browser = null;
    });

    // Wait indefinitely for app to respond with a browser or null
    Services.tm.spinEventLoopUntil(() => browser !== undefined);
    return browser;
  }

  // nsIBrowserDOMWindow.
  createContentWindow(aUri, aOpener, aWhere, aFlags, aTriggeringPrincipal) {
    debug("createContentWindow: aUri=" + (aUri && aUri.spec) +
          " aWhere=" + aWhere +
          " aFlags=" + aFlags);

    if (LoadURIDelegate.load(this.eventDispatcher, aUri, aWhere, aFlags,
                             aTriggeringPrincipal)) {
      // The app has handled the load, abort open-window handling.
      Components.returnCode = Cr.NS_ERROR_ABORT;
      return null;
    }

    const browser = this.handleNewSession(aUri, aOpener, aWhere, aFlags,
                                          aTriggeringPrincipal);
    if (!browser) {
      Components.returnCode = Cr.NS_ERROR_ABORT;
      return null;
    }

    return browser.contentWindow;
  }

  // nsIBrowserDOMWindow.
  createContentWindowInFrame(aUri, aParams, aWhere, aFlags, aNextTabParentId,
                             aName) {
    debug("createContentWindowInFrame: aUri=" + (aUri && aUri.spec) +
          " aParams=" + aParams +
          " aWhere=" + aWhere +
          " aFlags=" + aFlags +
          " aNextTabParentId=" + aNextTabParentId +
          " aName=" + aName);

    if (LoadURIDelegate.load(this.eventDispatcher, aUri, aWhere, aFlags, null)) {
      // The app has handled the load, abort open-window handling.
      Components.returnCode = Cr.NS_ERROR_ABORT;
      return null;
    }

    const browser = this.handleNewSession(aUri, null, aWhere, aFlags, null);
    if (!browser) {
      Components.returnCode = Cr.NS_ERROR_ABORT;
      return null;
    }

    browser.setAttribute("nextTabParentId", aNextTabParentId);
    return browser;
  }

  handleOpenUri(aUri, aOpener, aWhere, aFlags, aTriggeringPrincipal,
                aNextTabParentId) {
    debug("handleOpenUri: aUri=" + (aUri && aUri.spec) +
          " aWhere=" + aWhere +
          " aFlags=" + aFlags);

    if (LoadURIDelegate.load(this.eventDispatcher, aUri, aWhere, aFlags,
                             aTriggeringPrincipal)) {
      return null;
    }

    let browser = this.browser;

    if (aWhere === Ci.nsIBrowserDOMWindow.OPEN_NEWWINDOW ||
        aWhere === Ci.nsIBrowserDOMWindow.OPEN_NEWTAB ||
        aWhere === Ci.nsIBrowserDOMWindow.OPEN_SWITCHTAB) {
      browser = this.handleNewSession(aUri, aOpener, aWhere, aFlags,
                                      aTriggeringPrincipal);
    }

    if (!browser) {
      // Should we throw?
      return null;
    }
    browser.loadURI(aUri.spec, null, null, null, null, aTriggeringPrincipal);
    return browser;
  }

  // nsIBrowserDOMWindow.
  openURI(aUri, aOpener, aWhere, aFlags, aTriggeringPrincipal) {
    const browser = this.handleOpenUri(aUri, aOpener, aWhere, aFlags,
                                       aTriggeringPrincipal, null);
    return browser && browser.contentWindow;
  }

  // nsIBrowserDOMWindow.
  openURIInFrame(aUri, aParams, aWhere, aFlags, aNextTabParentId, aName) {
    const browser = this.handleOpenUri(aUri, null, aWhere, aFlags, null,
                                       aNextTabParentId);
    return browser;
  }

  // nsIBrowserDOMWindow.
  isTabContentWindow(aWindow) {
    return this.browser.contentWindow === aWindow;
  }

  // nsIBrowserDOMWindow.
  canClose() {
    debug("canClose");
    return false;
  }

  register() {
    debug("register");

    if (!this._frameScriptLoaded) {
      this.messageManager.loadFrameScript(
        "chrome://geckoview/content/GeckoViewNavigationContent.js", true);
      this._frameScriptLoaded = true;
    }

    let flags = Ci.nsIWebProgress.NOTIFY_LOCATION;
    this.progressFilter =
      Cc["@mozilla.org/appshell/component/browser-status-filter;1"]
      .createInstance(Ci.nsIWebProgress);
    this.progressFilter.addProgressListener(this, flags);
    this.browser.addProgressListener(this.progressFilter, flags);
  }

  unregister() {
    debug("unregister");

    if (!this.progressFilter) {
      return;
    }
    this.progressFilter.removeProgressListener(this);
    this.browser.removeProgressListener(this.progressFilter);
  }

  // WebProgress event handler.
  onLocationChange(aWebProgress, aRequest, aLocationURI, aFlags) {
    debug("onLocationChange");

    let fixedURI = aLocationURI;

    try {
      fixedURI = Services.uriFixup.createExposableURI(aLocationURI);
    } catch (ex) { }

    let message = {
      type: "GeckoView:LocationChange",
      uri: fixedURI.displaySpec,
      canGoBack: this.browser.canGoBack,
      canGoForward: this.browser.canGoForward,
    };

    debug("dispatch " + JSON.stringify(message));

    this.eventDispatcher.sendRequest(message);
  }
}
