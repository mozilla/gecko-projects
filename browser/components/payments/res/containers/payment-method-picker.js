/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import BasicCardOption from "../components/basic-card-option.js";
import RichPicker from "./rich-picker.js";
import paymentRequest from "../paymentRequest.js";

/**
 * <payment-method-picker></payment-method-picker>
 * Container around add/edit links and <rich-select> with
 * <basic-card-option> listening to savedBasicCards.
 */

export default class PaymentMethodPicker extends RichPicker {
  constructor() {
    super();
    this.dropdown.setAttribute("option-type", "basic-card-option");
    this.securityCodeInput = document.createElement("input");
    this.securityCodeInput.autocomplete = "off";
    this.securityCodeInput.placeholder = this.dataset.cvvPlaceholder;
    this.securityCodeInput.size = 3;
    this.securityCodeInput.classList.add("security-code");
    this.securityCodeInput.addEventListener("change", this);
  }

  connectedCallback() {
    super.connectedCallback();
    this.dropdown.after(this.securityCodeInput);
  }

  get fieldNames() {
    let fieldNames = [...BasicCardOption.recordAttributes];
    // Type is not a required field though it may be present.
    fieldNames.splice(fieldNames.indexOf("type"), 1);
    return fieldNames;
  }

  render(state) {
    let basicCards = paymentRequest.getBasicCards(state);
    let desiredOptions = [];
    for (let [guid, basicCard] of Object.entries(basicCards)) {
      let optionEl = this.dropdown.getOptionByValue(guid);
      if (!optionEl) {
        optionEl = document.createElement("option");
        optionEl.value = guid;
      }

      for (let key of BasicCardOption.recordAttributes) {
        let val = basicCard[key];
        if (val) {
          optionEl.setAttribute(key, val);
        } else {
          optionEl.removeAttribute(key);
        }
      }

      optionEl.textContent = BasicCardOption.formatSingleLineLabel(basicCard);
      desiredOptions.push(optionEl);
    }

    this.dropdown.popupBox.textContent = "";
    for (let option of desiredOptions) {
      this.dropdown.popupBox.appendChild(option);
    }

    // Update selectedness after the options are updated
    let selectedPaymentCardGUID = state[this.selectedStateKey];
    this.dropdown.value = selectedPaymentCardGUID;

    if (selectedPaymentCardGUID && selectedPaymentCardGUID !== this.dropdown.value) {
      throw new Error(`The option ${selectedPaymentCardGUID} ` +
                      `does not exist in the payment method picker`);
    }

    super.render(state);
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
      case "click": {
        this.onClick(event);
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
      case this.dropdown.popupBox: {
        stateChange[selectedKey] = this.dropdown.value;
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

  onClick({target}) {
    let nextState = {
      page: {
        id: "basic-card-page",
      },
      "basic-card-page": {},
    };

    switch (target) {
      case this.addLink: {
        nextState["basic-card-page"].guid = null;
        break;
      }
      case this.editLink: {
        let state = this.requestStore.getState();
        let selectedPaymentCardGUID = state[this.selectedStateKey];
        nextState["basic-card-page"].guid = selectedPaymentCardGUID;
        break;
      }
      default: {
        throw new Error("Unexpected onClick");
      }
    }

    this.requestStore.setState(nextState);
  }
}

customElements.define("payment-method-picker", PaymentMethodPicker);
