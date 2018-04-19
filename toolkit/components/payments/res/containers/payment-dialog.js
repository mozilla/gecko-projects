/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* global PaymentStateSubscriberMixin, paymentRequest */

"use strict";

/**
 * <payment-dialog></payment-dialog>
 */

class PaymentDialog extends PaymentStateSubscriberMixin(HTMLElement) {
  constructor() {
    super();
    this._template = document.getElementById("payment-dialog-template");
    this._cachedState = {};
  }

  connectedCallback() {
    let contents = document.importNode(this._template.content, true);
    this._hostNameEl = contents.querySelector("#host-name");

    this._cancelButton = contents.querySelector("#cancel");
    this._cancelButton.addEventListener("click", this.cancelRequest);

    this._payButton = contents.querySelector("#pay");
    this._payButton.addEventListener("click", this);

    this._viewAllButton = contents.querySelector("#view-all");
    this._viewAllButton.addEventListener("click", this);

    this._orderDetailsOverlay = contents.querySelector("#order-details-overlay");
    this._shippingTypeLabel = contents.querySelector("#shipping-type-label");
    this._shippingRelatedEls = contents.querySelectorAll(".shipping-related");
    this._payerRelatedEls = contents.querySelectorAll(".payer-related");
    this._payerAddressPicker = contents.querySelector("address-picker.payer-related");

    this._errorText = contents.querySelector("#error-text");

    this._disabledOverlay = contents.getElementById("disabled-overlay");

    this.appendChild(contents);

    super.connectedCallback();
  }

  disconnectedCallback() {
    this._cancelButton.removeEventListener("click", this.cancelRequest);
    this._payButton.removeEventListener("click", this.pay);
    this._viewAllButton.removeEventListener("click", this);
    super.disconnectedCallback();
  }

  handleEvent(event) {
    if (event.type == "click") {
      switch (event.target) {
        case this._viewAllButton:
          let orderDetailsShowing = !this.requestStore.getState().orderDetailsShowing;
          this.requestStore.setState({ orderDetailsShowing });
          break;
        case this._payButton:
          this.pay();
          break;
      }
    }
  }

  cancelRequest() {
    paymentRequest.cancel();
  }

  pay() {
    let {
      selectedPayerAddress,
      selectedPaymentCard,
      selectedPaymentCardSecurityCode,
    } = this.requestStore.getState();

    paymentRequest.pay({
      selectedPayerAddressGUID: selectedPayerAddress,
      selectedPaymentCardGUID: selectedPaymentCard,
      selectedPaymentCardSecurityCode,
    });
  }

  changeShippingAddress(shippingAddressGUID) {
    paymentRequest.changeShippingAddress({
      shippingAddressGUID,
    });
  }

  changeShippingOption(optionID) {
    paymentRequest.changeShippingOption({
      optionID,
    });
  }

  /**
   * Set some state from the privileged parent process.
   * Other elements that need to set state should use their own `this.requestStore.setState`
   * method provided by the `PaymentStateSubscriberMixin`.
   *
   * @param {object} state - See `PaymentsStore.setState`
   */
  setStateFromParent(state) {
    this.requestStore.setState(state);

    // Check if any foreign-key constraints were invalidated.
    state = this.requestStore.getState();
    let {
      savedAddresses,
      savedBasicCards,
      selectedPayerAddress,
      selectedPaymentCard,
      selectedShippingAddress,
      selectedShippingOption,
    } = state;
    let shippingOptions = state.request.paymentDetails.shippingOptions;

    // Ensure `selectedShippingAddress` never refers to a deleted address and refers
    // to an address if one exists.
    if (!savedAddresses[selectedShippingAddress]) {
      this.requestStore.setState({
        selectedShippingAddress: Object.keys(savedAddresses)[0] || null,
      });
    }

    // Ensure `selectedPaymentCard` never refers to a deleted payment card and refers
    // to a payment card if one exists.
    if (!savedBasicCards[selectedPaymentCard]) {
      this.requestStore.setState({
        selectedPaymentCard: Object.keys(savedBasicCards)[0] || null,
        selectedPaymentCardSecurityCode: null,
      });
    }

    // Ensure `selectedShippingOption` never refers to a deleted shipping option and
    // refers to a shipping option if one exists.
    if (shippingOptions &&
        !shippingOptions.find(option => option.id == selectedShippingOption)) {
      // The shippingOption spec says to use the last specified selected: true item.
      for (let i = shippingOptions.length - 1; i >= 0; i--) {
        if (shippingOptions[i].selected) {
          selectedShippingOption = shippingOptions[i].id;
          break;
        }
      }
      if (!selectedShippingOption && shippingOptions.length) {
        selectedShippingOption = shippingOptions[0].id;
      }
      this._cachedState.selectedShippingOption = selectedShippingOption;
      this.requestStore.setState({
        selectedShippingOption,
      });
    }


    // Ensure `selectedPayerAddress` never refers to a deleted address and refers
    // to an address if one exists.
    if (!savedAddresses[selectedPayerAddress]) {
      this.requestStore.setState({
        selectedPayerAddress: Object.keys(savedAddresses)[0] || null,
      });
    }
  }

  _renderPayButton(state) {
    this._payButton.disabled = state.changesPrevented;
    switch (state.completionState) {
      case "initial":
      case "processing":
      case "success":
      case "fail":
        break;
      default:
        throw new Error("Invalid completionState");
    }

    this._payButton.textContent = this._payButton.dataset[state.completionState + "Label"];
  }

  stateChangeCallback(state) {
    super.stateChangeCallback(state);

    // Don't dispatch change events for initial selectedShipping* changes at initialization
    // if requestShipping is false.
    if (state.request.paymentOptions.requestShipping) {
      if (state.selectedShippingAddress != this._cachedState.selectedShippingAddress) {
        this.changeShippingAddress(state.selectedShippingAddress);
      }

      if (state.selectedShippingOption != this._cachedState.selectedShippingOption) {
        this.changeShippingOption(state.selectedShippingOption);
      }
    }

    this._cachedState.selectedShippingAddress = state.selectedShippingAddress;
    this._cachedState.selectedShippingOption = state.selectedShippingOption;
  }

  render(state) {
    let request = state.request;
    let paymentDetails = request.paymentDetails;
    this._hostNameEl.textContent = request.topLevelPrincipal.URI.displayHost;

    let totalItem = paymentDetails.totalItem;
    let totalAmountEl = this.querySelector("#total > currency-amount");
    totalAmountEl.value = totalItem.amount.value;
    totalAmountEl.currency = totalItem.amount.currency;

    this._orderDetailsOverlay.hidden = !state.orderDetailsShowing;
    this._errorText.textContent = paymentDetails.error;

    let paymentOptions = request.paymentOptions;
    for (let element of this._shippingRelatedEls) {
      element.hidden = !paymentOptions.requestShipping;
    }
    let payerRequested = paymentOptions.requestPayerName ||
                         paymentOptions.requestPayerEmail ||
                         paymentOptions.requestPayerPhone;
    for (let element of this._payerRelatedEls) {
      element.hidden = !payerRequested;
    }

    if (payerRequested) {
      let fieldNames = new Set(); // default: ["name", "tel", "email"]
      if (paymentOptions.requestPayerName) {
        fieldNames.add("name");
      }
      if (paymentOptions.requestPayerEmail) {
        fieldNames.add("email");
      }
      if (paymentOptions.requestPayerPhone) {
        fieldNames.add("tel");
      }
      this._payerAddressPicker.setAttribute("address-fields", [...fieldNames].join(" "));
    } else {
      this._payerAddressPicker.removeAttribute("address-fields");
    }

    let shippingType = paymentOptions.shippingType || "shipping";
    this._shippingTypeLabel.querySelector("label").textContent =
      this._shippingTypeLabel.dataset[shippingType + "AddressLabel"];

    this._renderPayButton(state);

    let {
      changesPrevented,
      completionState,
    } = state;
    if (changesPrevented) {
      this.setAttribute("changes-prevented", "");
    } else {
      this.removeAttribute("changes-prevented");
    }
    this.setAttribute("completion-state", completionState);
    this._disabledOverlay.hidden = !changesPrevented;
  }
}

customElements.define("payment-dialog", PaymentDialog);
