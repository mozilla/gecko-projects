/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* global PaymentStateSubscriberMixin */

"use strict";

/**
 * <payment-method-picker></payment-method-picker>
 * Container around <rich-select> (eventually providing add/edit links) with
 * <basic-card-option> listening to savedBasicCards.
 */

class PaymentMethodPicker extends PaymentStateSubscriberMixin(HTMLElement) {
  constructor() {
    super();
    this.dropdown = document.createElement("rich-select");
    this.dropdown.addEventListener("change", this);
    this.spacerText = document.createTextNode(" ");
    this.securityCodeInput = document.createElement("input");
    this.securityCodeInput.autocomplete = "off";
    this.securityCodeInput.size = 3;
    this.securityCodeInput.addEventListener("change", this);
  }

  connectedCallback() {
    this.appendChild(this.dropdown);
    this.appendChild(this.spacerText);
    this.appendChild(this.securityCodeInput);
    super.connectedCallback();
  }

  render(state) {
    let {savedBasicCards} = state;
    let desiredOptions = [];
    for (let [guid, basicCard] of Object.entries(savedBasicCards)) {
      let optionEl = this.dropdown.getOptionByValue(guid);
      if (!optionEl) {
        optionEl = document.createElement("basic-card-option");
        optionEl.value = guid;
      }
      for (let [key, val] of Object.entries(basicCard)) {
        optionEl.setAttribute(key, val);
      }
      desiredOptions.push(optionEl);
    }
    let el = null;
    while ((el = this.dropdown.popupBox.querySelector(":scope > basic-card-option"))) {
      el.remove();
    }
    for (let option of desiredOptions) {
      this.dropdown.popupBox.appendChild(option);
    }

    // Update selectedness after the options are updated
    let selectedPaymentCardGUID = state[this.selectedStateKey];
    let optionWithGUID = this.dropdown.getOptionByValue(selectedPaymentCardGUID);
    this.dropdown.selectedOption = optionWithGUID;

    if (selectedPaymentCardGUID && !optionWithGUID) {
      throw new Error(`${this.selectedStateKey} option ${selectedPaymentCardGUID}` +
                      `does not exist in options`);
    }
  }

  get selectedStateKey() {
    return this.getAttribute("selected-state-key");
  }

  handleEvent(event) {
    switch (event.type) {
      case "change": {
        this.onChange(event);
        break;
      }
    }
  }

  onChange({target}) {
    let selectedKey = this.selectedStateKey;
    let stateChange = {};

    if (!selectedKey) {
      return;
    }

    switch (target) {
      case this.dropdown: {
        stateChange[selectedKey] = target.selectedOption && target.selectedOption.guid;
        // Select the security code text since the user is likely to edit it next.
        // We don't want to do this if the user simply blurs the dropdown.
        this.securityCodeInput.select();
        break;
      }
      case this.securityCodeInput: {
        stateChange[selectedKey + "SecurityCode"] = this.securityCodeInput.value;
        break;
      }
      default: {
        return;
      }
    }

    this.requestStore.setState(stateChange);
  }
}

customElements.define("payment-method-picker", PaymentMethodPicker);
