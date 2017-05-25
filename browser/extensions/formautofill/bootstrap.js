/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

/* exported startup, shutdown, install, uninstall */

const {classes: Cc, interfaces: Ci, results: Cr, utils: Cu} = Components;
const STYLESHEET_URI = "chrome://formautofill/content/formautofill.css";
const CACHED_STYLESHEETS = new WeakMap();

Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource://gre/modules/XPCOMUtils.jsm");

XPCOMUtils.defineLazyModuleGetter(this, "FormAutofillParent",
                                  "resource://formautofill/FormAutofillParent.jsm");

function insertStyleSheet(domWindow, url) {
  let doc = domWindow.document;
  let styleSheetAttr = `href="${url}" type="text/css"`;
  let styleSheet = doc.createProcessingInstruction("xml-stylesheet", styleSheetAttr);

  doc.insertBefore(styleSheet, doc.documentElement);

  if (CACHED_STYLESHEETS.has(domWindow)) {
    CACHED_STYLESHEETS.get(domWindow).push(styleSheet);
  } else {
    CACHED_STYLESHEETS.set(domWindow, [styleSheet]);
  }
}

function onMaybeOpenPopup(evt) {
  let domWindow = evt.target.ownerGlobal;
  if (CACHED_STYLESHEETS.has(domWindow)) {
    // This window already has autofill stylesheets.
    return;
  }

  insertStyleSheet(domWindow, STYLESHEET_URI);
}

function startup() {
  if (!Services.prefs.getBoolPref("extensions.formautofill.experimental")) {
    return;
  }

  // Listen for the autocomplete popup message to lazily append our stylesheet related to the popup.
  Services.mm.addMessageListener("FormAutoComplete:MaybeOpenPopup", onMaybeOpenPopup);

  let parent = new FormAutofillParent();
  parent.init().catch(Cu.reportError);
  Services.ppmm.loadProcessScript("data:,new " + function() {
    Components.utils.import("resource://formautofill/FormAutofillContent.jsm");
  }, true);
  Services.mm.loadFrameScript("chrome://formautofill/content/FormAutofillFrameScript.js", true);
}

function shutdown() {
  Services.mm.removeMessageListener("FormAutoComplete:MaybeOpenPopup", onMaybeOpenPopup);

  let enumerator = Services.wm.getEnumerator("navigator:browser");
  while (enumerator.hasMoreElements()) {
    let win = enumerator.getNext();
    let domWindow = win.QueryInterface(Ci.nsIInterfaceRequestor).getInterface(Ci.nsIDOMWindow);
    let cachedStyleSheets = CACHED_STYLESHEETS.get(domWindow);

    if (!cachedStyleSheets) {
      continue;
    }

    while (cachedStyleSheets.length !== 0) {
      cachedStyleSheets.pop().remove();
    }
  }
}

function install() {}
function uninstall() {}
