/* -*- indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* This content script should work in any browser or iframe and should not
 * depend on the frame being contained in tabbrowser. */

/* eslint-env mozilla/frame-script */
/* eslint no-unused-vars: ["error", {args: "none"}] */

ChromeUtils.import("resource://gre/modules/XPCOMUtils.jsm");
ChromeUtils.import("resource://gre/modules/Services.jsm");

// TabChildGlobal
var global = this;

XPCOMUtils.defineLazyModuleGetters(this, {
  BlockedSiteContent: "resource:///modules/BlockedSiteContent.jsm",
  ContentLinkHandler: "resource:///modules/ContentLinkHandler.jsm",
  ContentMetaHandler: "resource:///modules/ContentMetaHandler.jsm",
  ContentWebRTC: "resource:///modules/ContentWebRTC.jsm",
  LoginFormFactory: "resource://gre/modules/LoginManagerContent.jsm",
  InsecurePasswordUtils: "resource://gre/modules/InsecurePasswordUtils.jsm",
  PluginContent: "resource:///modules/PluginContent.jsm",
  FormSubmitObserver: "resource:///modules/FormSubmitObserver.jsm",
  NetErrorContent: "resource:///modules/NetErrorContent.jsm",
  PageMetadata: "resource://gre/modules/PageMetadata.jsm",
  WebNavigationFrames: "resource://gre/modules/WebNavigationFrames.jsm",
  ContextMenu: "resource:///modules/ContextMenu.jsm",
});

XPCOMUtils.defineLazyProxy(this, "contextMenu", () => {
  return new ContextMenu(global);
});

XPCOMUtils.defineLazyProxy(this, "ClickEventHandler", () => {
  let tmp = {};
  ChromeUtils.import("resource:///modules/ClickEventHandler.jsm", tmp);
  return new tmp.ClickEventHandler(global);
});

XPCOMUtils.defineLazyGetter(this, "LoginManagerContent", () => {
  let tmp = {};
  ChromeUtils.import("resource://gre/modules/LoginManagerContent.jsm", tmp);
  tmp.LoginManagerContent.setupEventListeners(global);
  return tmp.LoginManagerContent;
});

XPCOMUtils.defineLazyProxy(this, "formSubmitObserver", () => {
  return new FormSubmitObserver(content, this);
}, {
  // stub QI
  QueryInterface: ChromeUtils.generateQI([Ci.nsIFormSubmitObserver, Ci.nsISupportsWeakReference])
});

XPCOMUtils.defineLazyProxy(this, "PageInfoListener",
                           "resource:///modules/PageInfoListener.jsm");

XPCOMUtils.defineLazyProxy(this, "LightWeightThemeWebInstallListener",
                           "resource:///modules/LightWeightThemeWebInstallListener.jsm");

Services.els.addSystemEventListener(global, "contextmenu", contextMenu, false);

Services.obs.addObserver(formSubmitObserver, "invalidformsubmit", true);

addMessageListener("PageInfo:getData", PageInfoListener);

// NOTE: Much of this logic is duplicated in BrowserCLH.js for Android.
addMessageListener("RemoteLogins:fillForm", function(message) {
  // intercept if ContextMenu.jsm had sent a plain object for remote targets
  message.objects.inputElement = contextMenu.getTarget(message, "inputElement");
  LoginManagerContent.receiveMessage(message, content);
});
addEventListener("DOMFormHasPassword", function(event) {
  LoginManagerContent.onDOMFormHasPassword(event, content);
  let formLike = LoginFormFactory.createFromForm(event.originalTarget);
  InsecurePasswordUtils.reportInsecurePasswords(formLike);
});
addEventListener("DOMInputPasswordAdded", function(event) {
  LoginManagerContent.onDOMInputPasswordAdded(event, content);
  let formLike = LoginFormFactory.createFromField(event.originalTarget);
  InsecurePasswordUtils.reportInsecurePasswords(formLike);
});
addEventListener("DOMAutoComplete", function(event) {
  LoginManagerContent.onUsernameInput(event);
});

var AboutBlockedSiteListener = {
  init(chromeGlobal) {
    addMessageListener("DeceptiveBlockedDetails", this);
    chromeGlobal.addEventListener("AboutBlockedLoaded", this, false, true);
    this.init = null;
  },

  get isBlockedSite() {
    return content.document.documentURI.startsWith("about:blocked");
  },

  receiveMessage(msg) {
    if (!this.isBlockedSite) {
      return;
    }

    BlockedSiteContent.receiveMessage(global, msg);
  },

  handleEvent(aEvent) {
    if (!this.isBlockedSite) {
      return;
    }

    if (aEvent.type != "AboutBlockedLoaded") {
      return;
    }

    BlockedSiteContent.handleEvent(global, aEvent);
  },
};
AboutBlockedSiteListener.init(this);

this.AboutNetAndCertErrorListener = {
  init(chromeGlobal) {
    addMessageListener("CertErrorDetails", this);
    addMessageListener("Browser:CaptivePortalFreed", this);
    chromeGlobal.addEventListener("AboutNetErrorLoad", this, false, true);
    chromeGlobal.addEventListener("AboutNetErrorOpenCaptivePortal", this, false, true);
    chromeGlobal.addEventListener("AboutNetErrorSetAutomatic", this, false, true);
    chromeGlobal.addEventListener("AboutNetErrorResetPreferences", this, false, true);
    this.init = null;
  },

  isAboutNetError(doc) {
    return doc.documentURI.startsWith("about:neterror");
  },

  isAboutCertError(doc) {
    return doc.documentURI.startsWith("about:certerror");
  },

  receiveMessage(msg) {
    if (msg.name == "CertErrorDetails") {
      let frameDocShell = WebNavigationFrames.findDocShell(msg.data.frameId, docShell);
      // We need nsIWebNavigation to access docShell.document.
      frameDocShell && frameDocShell.QueryInterface(Ci.nsIWebNavigation);
      if (!frameDocShell || !this.isAboutCertError(frameDocShell.document)) {
        return;
      }

      NetErrorContent.onCertErrorDetails(global, msg, frameDocShell);
    } else if (msg.name == "Browser:CaptivePortalFreed") {
      // TODO: This check is not correct for frames.
      if (!this.isAboutCertError(content.document)) {
        return;
      }

      this.onCaptivePortalFreed(msg);
    }
  },

  onCaptivePortalFreed(msg) {
    content.dispatchEvent(new content.CustomEvent("AboutNetErrorCaptivePortalFreed"));
  },

  handleEvent(aEvent) {
    // Documents have a null ownerDocument.
    let doc = aEvent.originalTarget.ownerDocument || aEvent.originalTarget;

    if (!this.isAboutNetError(doc) && !this.isAboutCertError(doc)) {
      return;
    }

    NetErrorContent.handleEvent(global, aEvent);
  },
};
AboutNetAndCertErrorListener.init(this);

Services.els.addSystemEventListener(global, "click", ClickEventHandler, true);

new ContentLinkHandler(this);
ContentMetaHandler.init(this);

var PluginContentStub = {
  EVENTS: [
    "PluginCrashed",
    "PluginOutdated",
    "PluginInstantiated",
    "PluginRemoved",
    "HiddenPlugin",
  ],

  MESSAGES: [
    "BrowserPlugins:ActivatePlugins",
    "BrowserPlugins:NotificationShown",
    "BrowserPlugins:ContextMenuCommand",
    "BrowserPlugins:NPAPIPluginProcessCrashed",
    "BrowserPlugins:CrashReportSubmitted",
    "BrowserPlugins:Test:ClearCrashData",
  ],

  _pluginContent: null,
  get pluginContent() {
    if (!this._pluginContent) {
      this._pluginContent = new PluginContent(global);
    }
    return this._pluginContent;
  },

  init() {
    addEventListener("unload", this);

    addEventListener("PluginBindingAttached", this, true, true);

    for (let event of this.EVENTS) {
      addEventListener(event, this, true);
    }
    for (let msg of this.MESSAGES) {
      addMessageListener(msg, this);
    }
    Services.obs.addObserver(this, "decoder-doctor-notification");
    this.init = null;
  },

  uninit() {
    Services.obs.removeObserver(this, "decoder-doctor-notification");
  },

  observe(subject, topic, data) {
    return this.pluginContent.observe(subject, topic, data);
  },

  handleEvent(event) {
    if (event.type === "unload") {
      return this.uninit();
    }
    return this.pluginContent.handleEvent(event);
  },

  receiveMessage(msg) {
    return this.pluginContent.receiveMessage(msg);
  },
};

PluginContentStub.init();

// This is a temporary hack to prevent regressions (bug 1471327).
void content;

addEventListener("DOMWindowFocus", function(event) {
  sendAsyncMessage("DOMWindowFocus", {});
}, false);

// We use this shim so that ContentWebRTC.jsm will not be loaded until
// it is actually needed.
var ContentWebRTCShim = message => ContentWebRTC.receiveMessage(message);

addMessageListener("rtcpeer:Allow", ContentWebRTCShim);
addMessageListener("rtcpeer:Deny", ContentWebRTCShim);
addMessageListener("webrtc:Allow", ContentWebRTCShim);
addMessageListener("webrtc:Deny", ContentWebRTCShim);
addMessageListener("webrtc:StopSharing", ContentWebRTCShim);

var PageMetadataMessenger = {
  init() {
    addMessageListener("PageMetadata:GetPageData", this);
    addMessageListener("PageMetadata:GetMicroformats", this);
    this.init = null;
  },
  receiveMessage(message) {
    switch (message.name) {
      case "PageMetadata:GetPageData": {
        let target = contextMenu.getTarget(message);
        let result = PageMetadata.getData(content.document, target);
        sendAsyncMessage("PageMetadata:PageDataResult", result);
        break;
      }
      case "PageMetadata:GetMicroformats": {
        let target = contextMenu.getTarget(message);
        let result = PageMetadata.getMicroformats(content.document, target);
        sendAsyncMessage("PageMetadata:MicroformatsResult", result);
        break;
      }
    }
  }
};
PageMetadataMessenger.init();

addEventListener("InstallBrowserTheme", LightWeightThemeWebInstallListener, false, true);
addEventListener("PreviewBrowserTheme", LightWeightThemeWebInstallListener, false, true);
addEventListener("ResetBrowserThemePreview", LightWeightThemeWebInstallListener, false, true);

let OfflineApps = {
  _docId: 0,
  _docIdMap: new Map(),

  _docManifestSet: new Set(),

  _observerAdded: false,
  registerWindow(aWindow) {
    if (!this._observerAdded) {
      this._observerAdded = true;
      Services.obs.addObserver(this, "offline-cache-update-completed", true);
    }
    let manifestURI = this._getManifestURI(aWindow);
    this._docManifestSet.add(manifestURI.spec);
  },

  handleEvent(event) {
    if (event.type == "MozApplicationManifest") {
      this.offlineAppRequested(event.originalTarget.defaultView);
    }
  },

  _getManifestURI(aWindow) {
    if (!aWindow.document.documentElement)
      return null;

    var attr = aWindow.document.documentElement.getAttribute("manifest");
    if (!attr)
      return null;

    try {
      return Services.io.newURI(attr, aWindow.document.characterSet,
                                Services.io.newURI(aWindow.location.href));
    } catch (e) {
      return null;
    }
  },

  offlineAppRequested(aContentWindow) {
    this.registerWindow(aContentWindow);
    if (!Services.prefs.getBoolPref("browser.offline-apps.notify")) {
      return;
    }

    let currentURI = aContentWindow.document.documentURIObject;
    // don't bother showing UI if the user has already made a decision
    if (Services.perms.testExactPermission(currentURI, "offline-app") != Services.perms.UNKNOWN_ACTION)
      return;

    try {
      if (Services.prefs.getBoolPref("offline-apps.allow_by_default")) {
        // all pages can use offline capabilities, no need to ask the user
        return;
      }
    } catch (e) {
      // this pref isn't set by default, ignore failures
    }
    let docId = ++this._docId;
    this._docIdMap.set(docId, Cu.getWeakReference(aContentWindow.document));
    sendAsyncMessage("OfflineApps:RequestPermission", {
      uri: currentURI.spec,
      docId,
    });
  },

  _startFetching(aDocument) {
    if (!aDocument.documentElement)
      return;

    let manifestURI = this._getManifestURI(aDocument.defaultView);
    if (!manifestURI)
      return;

    var updateService = Cc["@mozilla.org/offlinecacheupdate-service;1"].
                        getService(Ci.nsIOfflineCacheUpdateService);
    updateService.scheduleUpdate(manifestURI, aDocument.documentURIObject,
                                 aDocument.nodePrincipal, aDocument.defaultView);
  },

  receiveMessage(aMessage) {
    if (aMessage.name == "OfflineApps:StartFetching") {
      let doc = this._docIdMap.get(aMessage.data.docId);
      doc = doc && doc.get();
      if (doc) {
        this._startFetching(doc);
      }
      this._docIdMap.delete(aMessage.data.docId);
    }
  },

  observe(aSubject, aTopic, aState) {
    if (aTopic == "offline-cache-update-completed") {
      let cacheUpdate = aSubject.QueryInterface(Ci.nsIOfflineCacheUpdate);
      let uri = cacheUpdate.manifestURI;
      if (uri && this._docManifestSet.has(uri.spec)) {
        sendAsyncMessage("OfflineApps:CheckUsage", {uri: uri.spec});
      }
    }
  },
  QueryInterface: ChromeUtils.generateQI([Ci.nsIObserver,
                                          Ci.nsISupportsWeakReference]),
};

addEventListener("MozApplicationManifest", OfflineApps, false);
addMessageListener("OfflineApps:StartFetching", OfflineApps);
