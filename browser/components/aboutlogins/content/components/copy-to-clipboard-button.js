/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import {recordTelemetryEvent} from "../aboutLoginsUtils.js";
import ReflectedFluentElement from "./reflected-fluent-element.js";

export default class CopyToClipboardButton extends ReflectedFluentElement {
  /**
   * The number of milliseconds to display the "Copied" success message
   * before reverting to the normal "Copy" button.
   */
  static get BUTTON_RESET_TIMEOUT() {
    return 5000;
  }

  constructor() {
    super();

    this._relatedInput = null;
  }

  connectedCallback() {
    if (this.shadowRoot) {
      return;
    }

    let CopyToClipboardButtonTemplate = document.querySelector("#copy-to-clipboard-button-template");
    this.attachShadow({mode: "open"})
        .appendChild(CopyToClipboardButtonTemplate.content.cloneNode(true));

    this._copyButton = this.shadowRoot.querySelector(".copy-button");
    this._copyButton.addEventListener("click", this);

    super.connectedCallback();
  }

  static get reflectedFluentIDs() {
    return ["copy-button-text", "copied-button-text"];
  }

  static get observedAttributes() {
    return CopyToClipboardButton.reflectedFluentIDs;
  }

  handleSpecialCaseFluentString(attrName) {
    if (attrName != "copied-button-text" &&
        attrName != "copy-button-text") {
      return false;
    }

    let span = this.shadowRoot.querySelector("." + attrName);
    span.textContent = this.getAttribute(attrName);
    return true;
  }

  handleEvent(event) {
    if (event.type != "click" || event.currentTarget != this._copyButton) {
      return;
    }

    this._copyButton.disabled = true;
    navigator.clipboard.writeText(this._relatedInput.value).then(() => {
      this.dataset.copied = true;
      setTimeout(() => {
        this._copyButton.disabled = false;
        delete this.dataset.copied;
      }, CopyToClipboardButton.BUTTON_RESET_TIMEOUT);
    }, () => this._copyButton.disabled = false);

    if (this.dataset.telemetryObject) {
      recordTelemetryEvent({object: this.dataset.telemetryObject, method: "copy"});
    }
  }

  /**
   * @param {Element} val A reference to the input element whose value will
   *                      be placed on the clipboard.
   */
  set relatedInput(val) {
    this._relatedInput = val;
  }
}
customElements.define("copy-to-clipboard-button", CopyToClipboardButton);
