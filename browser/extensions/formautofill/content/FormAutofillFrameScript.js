/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Form Autofill frame script.
 */

"use strict";

/* eslint-env mozilla/frame-script */

var { Services } = ChromeUtils.import("resource://gre/modules/Services.jsm");
ChromeUtils.defineModuleGetter(
  this,
  "setTimeout",
  "resource://gre/modules/Timer.jsm"
);
ChromeUtils.defineModuleGetter(
  this,
  "FormAutofill",
  "resource://formautofill/FormAutofill.jsm"
);
ChromeUtils.defineModuleGetter(
  this,
  "FormAutofillContent",
  "resource://formautofill/FormAutofillContent.jsm"
);
ChromeUtils.defineModuleGetter(
  this,
  "FormAutofillUtils",
  "resource://formautofill/FormAutofillUtils.jsm"
);

/**
 * Handles content's interactions for the frame.
 */
var FormAutofillFrameScript = {
  _nextHandleElement: null,
  _alreadyDOMContentLoaded: false,
  _hasDOMContentLoadedHandler: false,
  _hasPendingTask: false,

  _doIdentifyAutofillFields() {
    if (this._hasPendingTask) {
      return;
    }
    this._hasPendingTask = true;

    setTimeout(() => {
      FormAutofillContent.identifyAutofillFields(this._nextHandleElement);
      this._hasPendingTask = false;
      this._nextHandleElement = null;
      // This is for testing purpose only which sends a message to indicate that the
      // form has been identified, and ready to open popup.
      sendAsyncMessage("FormAutofill:FieldsIdentified");
      FormAutofillContent.updateActiveInput();
    });
  },

  init() {
    addEventListener("focusin", this);
    addEventListener("DOMFormBeforeSubmit", this);
    addMessageListener("FormAutofill:PreviewProfile", this);
    addMessageListener("FormAutofill:ClearForm", this);
    addMessageListener("FormAutoComplete:PopupClosed", this);
    addMessageListener("FormAutoComplete:PopupOpened", this);
  },

  handleEvent(evt) {
    if (!evt.isTrusted || !FormAutofill.isAutofillEnabled) {
      return;
    }

    switch (evt.type) {
      case "focusin": {
        this.onFocusIn(evt);
        break;
      }
      case "DOMFormBeforeSubmit": {
        this.onDOMFormBeforeSubmit(evt);
        break;
      }
      default: {
        throw new Error("Unexpected event type");
      }
    }
  },

  onFocusIn(evt) {
    FormAutofillContent.updateActiveInput();

    let element = evt.target;
    if (!FormAutofillUtils.isFieldEligibleForAutofill(element)) {
      return;
    }
    this._nextHandleElement = element;

    if (!this._alreadyDOMContentLoaded) {
      let doc = element.ownerDocument;
      if (doc.readyState === "loading") {
        if (!this._hasDOMContentLoadedHandler) {
          this._hasDOMContentLoadedHandler = true;
          doc.addEventListener(
            "DOMContentLoaded",
            () => this._doIdentifyAutofillFields(),
            { once: true }
          );
        }
        return;
      }
      this._alreadyDOMContentLoaded = true;
    }

    this._doIdentifyAutofillFields();
  },

  /**
   * Handle the DOMFormBeforeSubmit event.
   * @param {Event} evt
   */
  onDOMFormBeforeSubmit(evt) {
    let formElement = evt.target;

    if (!FormAutofill.isAutofillEnabled) {
      return;
    }

    FormAutofillContent.formSubmitted(formElement);
  },

  receiveMessage(message) {
    if (!FormAutofill.isAutofillEnabled) {
      return;
    }

    const doc = content.document;
    const { chromeEventHandler } = doc.ownerGlobal.docShell;

    switch (message.name) {
      case "FormAutofill:PreviewProfile": {
        FormAutofillContent.previewProfile(doc);
        break;
      }
      case "FormAutofill:ClearForm": {
        FormAutofillContent.clearForm();
        break;
      }
      case "FormAutoComplete:PopupClosed": {
        FormAutofillContent.onPopupClosed(message.data.selectedRowStyle);
        Services.tm.dispatchToMainThread(() => {
          chromeEventHandler.removeEventListener(
            "keydown",
            FormAutofillContent._onKeyDown,
            true
          );
        });

        break;
      }
      case "FormAutoComplete:PopupOpened": {
        chromeEventHandler.addEventListener(
          "keydown",
          FormAutofillContent._onKeyDown,
          true
        );
        break;
      }
    }
  },
};

FormAutofillFrameScript.init();
