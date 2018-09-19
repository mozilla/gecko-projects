/* -*- mode: js; indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

var EXPORTED_SYMBOLS = [ "ContentClick" ];

ChromeUtils.import("resource://gre/modules/Services.jsm");

ChromeUtils.defineModuleGetter(this, "PlacesUIUtils",
                               "resource:///modules/PlacesUIUtils.jsm");
ChromeUtils.defineModuleGetter(this, "PrivateBrowsingUtils",
                               "resource://gre/modules/PrivateBrowsingUtils.jsm");

var ContentClick = {
  // Listeners are added in nsBrowserGlue.js
  receiveMessage(message) {
    switch (message.name) {
      case "Content:Click":
        this.contentAreaClick(message.json, message.target);
        break;
    }
  },

  /**
   * Handles clicks in the content area.
   *
   * @param json {Object} JSON object that looks like an Event
   * @param browser {Element<browser>}
   */
  contentAreaClick(json, browser) {
    // This is heavily based on contentAreaClick from browser.js (Bug 903016)
    // The json is set up in a way to look like an Event.
    let window = browser.ownerGlobal;

    if (!json.href) {
      // Might be middle mouse navigation.
      if (Services.prefs.getBoolPref("middlemouse.contentLoadURL") &&
          !Services.prefs.getBoolPref("general.autoScroll")) {
        window.middleMousePaste(json);
      }
      return;
    }

    // If the browser is not in a place where we can open links, bail out.
    // This can happen in osx sheets, dialogs, etc. that are not browser
    // windows.  Specifically the payments UI is in an osx sheet.
    if (window.openLinkIn === undefined) {
      return;
    }

    // Mark the page as a user followed link.  This is done so that history can
    // distinguish automatic embed visits from user activated ones.  For example
    // pages loaded in frames are embed visits and lost with the session, while
    // visits across frames should be preserved.
    try {
      if (!PrivateBrowsingUtils.isWindowPrivate(window))
        PlacesUIUtils.markPageAsFollowedLink(json.href);
    } catch (ex) { /* Skip invalid URIs. */ }

    // This part is based on handleLinkClick.
    var where = window.whereToOpenLink(json);
    if (where == "current")
      return;

    // Todo(903022): code for where == save

    let params = {
      charset: browser.characterSet,
      referrerURI: browser.documentURI,
      referrerPolicy: json.referrerPolicy,
      noReferrer: json.noReferrer,
      allowMixedContent: json.allowMixedContent,
      isContentWindowPrivate: json.isContentWindowPrivate,
      originPrincipal: json.originPrincipal,
      triggeringPrincipal: json.triggeringPrincipal,
      frameOuterWindowID: json.frameOuterWindowID,
    };

    // The new tab/window must use the same userContextId.
    if (json.originAttributes.userContextId) {
      params.userContextId = json.originAttributes.userContextId;
    }

    params.allowInheritPrincipal = true;

    window.openLinkIn(json.href, where, params);
  },
};
