/* -*- indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* This content script should work in any browser or iframe and should not
 * depend on the frame being contained in tabbrowser. */

/* eslint-env mozilla/frame-script */
/* eslint no-unused-vars: ["error", {args: "none"}] */

var { XPCOMUtils } = ChromeUtils.import(
  "resource://gre/modules/XPCOMUtils.jsm"
);

XPCOMUtils.defineLazyModuleGetters(this, {
  ContentMetaHandler: "resource:///modules/ContentMetaHandler.jsm",
  LoginFormFactory: "resource://gre/modules/LoginFormFactory.jsm",
  LoginManagerContent: "resource://gre/modules/LoginManagerContent.jsm",
  InsecurePasswordUtils: "resource://gre/modules/InsecurePasswordUtils.jsm",
});

// NOTE: Much of this logic is duplicated in BrowserCLH.js for Android.
addMessageListener("PasswordManager:fillForm", function(message) {
  // intercept if ContextMenu.jsm had sent a plain object for remote targets
  LoginManagerContent.receiveMessage(message, content);
});
addMessageListener("PasswordManager:fillGeneratedPassword", function(message) {
  // forward message to LMC
  LoginManagerContent.receiveMessage(message, content);
});

function shouldIgnoreLoginManagerEvent(event) {
  let nodePrincipal = event.target.nodePrincipal;
  // If we have a system or null principal then prevent any more password manager code from running and
  // incorrectly using the document `location`. Also skip password manager for about: pages.
  return (
    nodePrincipal.isSystemPrincipal ||
    nodePrincipal.isNullPrincipal ||
    nodePrincipal.schemeIs("about")
  );
}

addEventListener("DOMFormBeforeSubmit", function(event) {
  if (shouldIgnoreLoginManagerEvent(event)) {
    return;
  }
  LoginManagerContent.onDOMFormBeforeSubmit(event);
});
addEventListener("DOMFormHasPassword", function(event) {
  if (shouldIgnoreLoginManagerEvent(event)) {
    return;
  }
  LoginManagerContent.onDOMFormHasPassword(event);
  let formLike = LoginFormFactory.createFromForm(event.originalTarget);
  InsecurePasswordUtils.reportInsecurePasswords(formLike);
});
addEventListener("DOMInputPasswordAdded", function(event) {
  if (shouldIgnoreLoginManagerEvent(event)) {
    return;
  }
  LoginManagerContent.onDOMInputPasswordAdded(event, content);
  let formLike = LoginFormFactory.createFromField(event.originalTarget);
  InsecurePasswordUtils.reportInsecurePasswords(formLike);
});

ContentMetaHandler.init(this);

// This is a temporary hack to prevent regressions (bug 1471327).
void content;
