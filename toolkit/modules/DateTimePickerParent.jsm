/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const DEBUG = false;
function debug(aStr) {
  if (DEBUG) {
    dump("-*- DateTimePickerParent: " + aStr + "\n");
  }
}

var EXPORTED_SYMBOLS = [
  "DateTimePickerParent",
];

const {Services} = ChromeUtils.import("resource://gre/modules/Services.jsm");
ChromeUtils.defineModuleGetter(this, "DateTimePickerPanel", "resource://gre/modules/DateTimePickerPanel.jsm");

/*
 * DateTimePickerParent receives message from content side (input box) and
 * is reposible for opening, closing and updating the picker. Similarly,
 * DateTimePickerParent listens for picker's events and notifies the content
 * side (input box) about them.
 */
var DateTimePickerParent = {
  picker: null,
  weakBrowser: null,

  MESSAGES: [
    "FormDateTime:OpenPicker",
    "FormDateTime:ClosePicker",
    "FormDateTime:UpdatePicker",
  ],

  init() {
    for (let msg of this.MESSAGES) {
      Services.mm.addMessageListener(msg, this);
    }
  },

  uninit() {
    for (let msg of this.MESSAGES) {
      Services.mm.removeMessageListener(msg, this);
    }
  },

  // MessageListener
  receiveMessage(aMessage) {
    debug("receiveMessage: " + aMessage.name);
    switch (aMessage.name) {
      case "FormDateTime:OpenPicker": {
        this.showPicker(aMessage.target, aMessage.data);
        break;
      }
      case "FormDateTime:ClosePicker": {
        if (!this.picker) {
          return;
        }
        this.picker.closePicker();
        this.close();
        break;
      }
      case "FormDateTime:UpdatePicker": {
        if (!this.picker) {
          return;
        }
        this.picker.setPopupValue(aMessage.data);
        break;
      }
      default:
        break;
    }
  },

  // nsIDOMEventListener
  handleEvent(aEvent) {
    debug("handleEvent: " + aEvent.type);
    switch (aEvent.type) {
      case "DateTimePickerValueChanged": {
        this.updateInputBoxValue(aEvent);
        break;
      }
      case "popuphidden": {
        let browser = this.weakBrowser ? this.weakBrowser.get() : null;
        if (browser) {
          browser.messageManager.sendAsyncMessage("FormDateTime:PickerClosed");
        }
        this.picker.closePicker();
        this.close();
        break;
      }
      default:
        break;
    }
  },

  // Called when picker value has changed, notify input box about it.
  updateInputBoxValue(aEvent) {
    let browser = this.weakBrowser ? this.weakBrowser.get() : null;
    if (browser) {
      browser.messageManager.sendAsyncMessage(
        "FormDateTime:PickerValueChanged", aEvent.detail);
    }
  },

  // Get picker from browser and show it anchored to the input box.
  showPicker(aBrowser, aData) {
    let rect = aData.rect;
    let type = aData.type;
    let detail = aData.detail;

    this._anchor = aBrowser.ownerGlobal.gBrowser.popupAnchor;
    this._anchor.left = rect.left;
    this._anchor.top = rect.top;
    this._anchor.width = rect.width;
    this._anchor.height = rect.height;
    this._anchor.hidden = false;

    debug("Opening picker with details: " + JSON.stringify(detail));

    let window = aBrowser.ownerGlobal;
    let tabbrowser = window.gBrowser;
    if (Services.focus.activeWindow != window ||
        tabbrowser.selectedBrowser != aBrowser) {
      // We were sent a message from a window or tab that went into the
      // background, so we'll ignore it for now.
      return;
    }

    this.weakBrowser = Cu.getWeakReference(aBrowser);
    if (!aBrowser.dateTimePicker) {
      debug("aBrowser.dateTimePicker not found, exiting now.");
      return;
    }
    this.picker = new DateTimePickerPanel(aBrowser.dateTimePicker);
    // The arrow panel needs an anchor to work. The popupAnchor (this._anchor)
    // is a transparent div that the arrow can point to.
    this.picker.openPicker(type, this._anchor, detail);

    this.addPickerListeners();
  },

  // Picker is closed, do some cleanup.
  close() {
    this.removePickerListeners();
    this.picker = null;
    this.weakBrowser = null;
    this._anchor.hidden = true;
  },

  // Listen to picker's event.
  addPickerListeners() {
    if (!this.picker) {
      return;
    }
    this.picker.element.addEventListener("popuphidden", this);
    this.picker.element.addEventListener("DateTimePickerValueChanged", this);
  },

  // Stop listening to picker's event.
  removePickerListeners() {
    if (!this.picker) {
      return;
    }
    this.picker.element.removeEventListener("popuphidden", this);
    this.picker.element.removeEventListener("DateTimePickerValueChanged", this);
  },
};
