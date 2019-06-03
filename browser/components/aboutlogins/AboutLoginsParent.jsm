/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

var EXPORTED_SYMBOLS = ["AboutLoginsParent"];

const {XPCOMUtils} = ChromeUtils.import("resource://gre/modules/XPCOMUtils.jsm");
ChromeUtils.defineModuleGetter(this, "E10SUtils",
                               "resource://gre/modules/E10SUtils.jsm");
ChromeUtils.defineModuleGetter(this, "Localization",
                               "resource://gre/modules/Localization.jsm");
ChromeUtils.defineModuleGetter(this, "LoginHelper",
                               "resource://gre/modules/LoginHelper.jsm");
ChromeUtils.defineModuleGetter(this, "Services",
                               "resource://gre/modules/Services.jsm");

XPCOMUtils.defineLazyGetter(this, "log", () => {
  return LoginHelper.createLogger("AboutLoginsParent");
});

const ABOUT_LOGINS_ORIGIN = "about:logins";
const MASTER_PASSWORD_NOTIFICATION_ID = "master-password-login-required";

const PRIVILEGEDABOUT_PROCESS_PREF =
  "browser.tabs.remote.separatePrivilegedContentProcess";
const PRIVILEGEDABOUT_PROCESS_ENABLED =
  Services.prefs.getBoolPref(PRIVILEGEDABOUT_PROCESS_PREF, false);

// When the privileged content process is enabled, we expect about:logins
// to load in it. Otherwise, it's in a normal web content process.
const EXPECTED_ABOUTLOGINS_REMOTE_TYPE =
  PRIVILEGEDABOUT_PROCESS_ENABLED ? E10SUtils.PRIVILEGEDABOUT_REMOTE_TYPE
                                  : E10SUtils.DEFAULT_REMOTE_TYPE;

const isValidLogin = login => {
  return !(login.hostname || "").startsWith("chrome://");
};

const convertSubjectToLogin = subject => {
  subject.QueryInterface(Ci.nsILoginMetaInfo).QueryInterface(Ci.nsILoginInfo);
  const login = LoginHelper.loginToVanillaObject(subject);
  if (!isValidLogin(login)) {
    return null;
  }
  return augmentVanillaLoginObject(login);
};

const augmentVanillaLoginObject = login => {
  let title;
  try {
    title = (new URL(login.hostname)).host;
  } catch (ex) {
    title = login.hostname;
  }
  title = title.replace(/^http(s)?:\/\//, "").
                replace(/^www\d*\./, "");
  return Object.assign({}, login, {
    title,
  });
};

var AboutLoginsParent = {
  _l10n: null,
  _subscribers: new WeakSet(),

  // Listeners are added in BrowserGlue.jsm
  receiveMessage(message) {
    // Only respond to messages sent from about:logins.
    if (message.target.remoteType != EXPECTED_ABOUTLOGINS_REMOTE_TYPE ||
        message.target.contentPrincipal.originNoSuffix != ABOUT_LOGINS_ORIGIN) {
      return;
    }

    switch (message.name) {
      case "AboutLogins:CreateLogin": {
        let newLogin = message.data.login;
        Object.assign(newLogin, {
          formSubmitURL: "",
          usernameField: "",
          passwordField: "",
        });
        Services.logins.addLogin(LoginHelper.vanillaObjectToLogin(newLogin));
        break;
      }
      case "AboutLogins:DeleteLogin": {
        let login = LoginHelper.vanillaObjectToLogin(message.data.login);
        Services.logins.removeLogin(login);
        break;
      }
      case "AboutLogins:OpenSite": {
        let guid = message.data.login.guid;
        let logins = LoginHelper.searchLoginsWithObject({guid});
        if (logins.length != 1) {
          log.warn(`AboutLogins:OpenSite: expected to find a login for guid: ${guid} but found ${logins.length}`);
          return;
        }

        message.target.ownerGlobal.openWebLinkIn(logins[0].hostname, "tab", {relatedToCurrent: true});
        break;
      }
      case "AboutLogins:Subscribe": {
        if (!ChromeUtils.nondeterministicGetWeakSetKeys(this._subscribers).length) {
          Services.obs.addObserver(this, "passwordmgr-crypto-login");
          Services.obs.addObserver(this, "passwordmgr-crypto-loginCanceled");
          Services.obs.addObserver(this, "passwordmgr-storage-changed");
        }
        this._subscribers.add(message.target);

        let messageManager = message.target.messageManager;
        messageManager.sendAsyncMessage("AboutLogins:AllLogins", this.getAllLogins());
        break;
      }
      case "AboutLogins:UpdateLogin": {
        let loginUpdates = message.data.login;
        let logins = LoginHelper.searchLoginsWithObject({guid: loginUpdates.guid});
        if (logins.length != 1) {
          log.warn(`AboutLogins:UpdateLogin: expected to find a login for guid: ${loginUpdates.guid} but found ${logins.length}`);
          return;
        }

        let modifiedLogin = logins[0].clone();
        if (loginUpdates.hasOwnProperty("username")) {
          modifiedLogin.username = loginUpdates.username;
        }
        if (loginUpdates.hasOwnProperty("password")) {
          modifiedLogin.password = loginUpdates.password;
        }

        Services.logins.modifyLogin(logins[0], modifiedLogin);
        break;
      }
    }
  },

  observe(subject, topic, type) {
    if (!ChromeUtils.nondeterministicGetWeakSetKeys(this._subscribers).length) {
      Services.obs.removeObserver(this, "passwordmgr-crypto-login");
      Services.obs.removeObserver(this, "passwordmgr-crypto-loginCanceled");
      Services.obs.removeObserver(this, "passwordmgr-storage-changed");
      return;
    }

    if (topic == "passwordmgr-crypto-login") {
      this.removeMasterPasswordLoginNotifications();
      this.messageSubscribers("AboutLogins:AllLogins", this.getAllLogins());
      return;
    }

    if (topic == "passwordmgr-crypto-loginCanceled") {
      this.showMasterPasswordLoginNotifications();
      return;
    }

    switch (type) {
      case "addLogin": {
        const login = convertSubjectToLogin(subject);
        if (!login) {
          return;
        }
        this.messageSubscribers("AboutLogins:LoginAdded", login);
        break;
      }
      case "modifyLogin": {
        subject.QueryInterface(Ci.nsIArrayExtensions);
        const login = convertSubjectToLogin(subject.GetElementAt(1));
        if (!login) {
          return;
        }
        this.messageSubscribers("AboutLogins:LoginModified", login);
        break;
      }
      case "removeLogin": {
        const login = convertSubjectToLogin(subject);
        if (!login) {
          return;
        }
        this.messageSubscribers("AboutLogins:LoginRemoved", login);
      }
      default: {
        break;
      }
    }
  },

  async showMasterPasswordLoginNotifications() {
    if (!this._l10n) {
      this._l10n = new Localization(["browser/aboutLogins.ftl"]);
    }

    let messageString = await this._l10n.formatValue("master-password-notification-message");
    for (let subscriber of this._subscriberIterator()) {
      // If there's already an existing notification bar, don't do anything.
      let {gBrowser} = subscriber.ownerGlobal;
      let browser = subscriber;
      let notificationBox = gBrowser.getNotificationBox(browser);
      let notification = notificationBox.getNotificationWithValue(MASTER_PASSWORD_NOTIFICATION_ID);
      if (notification) {
        continue;
      }

      // Configure the notification bar
      let priority = notificationBox.PRIORITY_WARNING_MEDIUM;
      let iconURL = "chrome://browser/skin/login.svg";
      let reloadLabel = await this._l10n.formatValue("master-password-reload-button-label");
      let reloadKey = await this._l10n.formatValue("master-password-reload-button-accesskey");

      let buttons = [{
        label: reloadLabel,
        accessKey: reloadKey,
        popup: null,
        callback() { browser.reload(); },
      }];

      notification = notificationBox.appendNotification(messageString, MASTER_PASSWORD_NOTIFICATION_ID,
                                                        iconURL, priority, buttons);
    }
  },

  removeMasterPasswordLoginNotifications() {
    for (let subscriber of this._subscriberIterator()) {
      let {gBrowser} = subscriber.ownerGlobal;
      let browser = subscriber;
      let notificationBox = gBrowser.getNotificationBox(browser);
      let notification = notificationBox.getNotificationWithValue(MASTER_PASSWORD_NOTIFICATION_ID);
      if (!notification) {
        continue;
      }
      notificationBox.removeNotification(notification);
    }
  },

  * _subscriberIterator() {
    let subscribers = ChromeUtils.nondeterministicGetWeakSetKeys(this._subscribers);
    for (let subscriber of subscribers) {
      if (subscriber.remoteType != EXPECTED_ABOUTLOGINS_REMOTE_TYPE ||
          !subscriber.contentPrincipal ||
          subscriber.contentPrincipal.originNoSuffix != ABOUT_LOGINS_ORIGIN) {
        this._subscribers.delete(subscriber);
        continue;
      }
      yield subscriber;
    }
  },

  messageSubscribers(name, details) {
    for (let subscriber of this._subscriberIterator()) {
      try {
        subscriber.messageManager.sendAsyncMessage(name, details);
      } catch (ex) {}
    }
  },

  getAllLogins() {
    return Services.logins
                   .getAllLogins()
                   .filter(isValidLogin)
                   .map(LoginHelper.loginToVanillaObject)
                   .map(augmentVanillaLoginObject);
  },
};
