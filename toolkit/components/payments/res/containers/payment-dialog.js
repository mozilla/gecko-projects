/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* global PaymentStateSubscriberMixin, PaymentRequest */

"use strict";

/**
 * <payment-dialog></payment-dialog>
 */

class PaymentDialog extends PaymentStateSubscriberMixin(HTMLElement) {
  constructor() {
    super();
    this._template = document.getElementById("payment-dialog-template");
  }

  connectedCallback() {
    let contents = document.importNode(this._template.content, true);
    this._hostNameEl = contents.querySelector("#host-name");

    this._cancelButton = contents.querySelector("#cancel");
    this._cancelButton.addEventListener("click", this.cancelRequest);

    this._payButton = contents.querySelector("#pay");
    this._payButton.addEventListener("click", this.pay);

    this._viewAllButton = contents.querySelector("#view-all");
    this._viewAllButton.addEventListener("click", this);

    this._orderDetailsOverlay = contents.querySelector("#order-details-overlay");

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
      }
    }
  }

  cancelRequest() {
    PaymentRequest.cancel();
  }

  pay() {
    PaymentRequest.pay({
      methodName: "basic-card",
      methodData: {
        cardholderName: "John Doe",
        cardNumber: "9999999999",
        expiryMonth: "01",
        expiryYear: "9999",
        cardSecurityCode: "999",
      },
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
  }

  render(state) {
    let request = state.request;
    this._hostNameEl.textContent = request.topLevelPrincipal.URI.displayHost;

    let totalItem = request.paymentDetails.totalItem;
    let totalAmountEl = this.querySelector("#total > currency-amount");
    totalAmountEl.value = totalItem.amount.value;
    totalAmountEl.currency = totalItem.amount.currency;

    this._orderDetailsOverlay.hidden = !state.orderDetailsShowing;
  }
}

customElements.define("payment-dialog", PaymentDialog);
