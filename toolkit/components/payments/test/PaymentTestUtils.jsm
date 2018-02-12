"use strict";

this.EXPORTED_SYMBOLS = ["PaymentTestUtils"];

this.PaymentTestUtils = {
  /**
   * Common content tasks functions to be used with ContentTask.spawn.
   */
  ContentTasks: {
    /* eslint-env mozilla/frame-script */
    /**
     * Add a completion handler to the existing `showPromise` to call .complete().
     * @returns {Object} representing the PaymentResponse
     */
    addCompletionHandler: async () => {
      let response = await content.showPromise;
      response.complete();
      return {
        response: response.toJSON(),
        // XXX: Bug NNN: workaround for `details` not being included in `toJSON`.
        methodDetails: response.details,
      };
    },

    /**
     * Create a new payment request and cache it as `rq`.
     *
     * @param {Object} args
     * @param {PaymentMethodData[]} methodData
     * @param {PaymentDetailsInit} details
     * @param {PaymentOptions} options
     */
    createRequest: ({methodData, details, options}) => {
      const rq = new content.PaymentRequest(methodData, details, options);
      content.rq = rq; // assign it so we can retrieve it later
    },

    /**
     * Create a new payment request cached as `rq` and then show it.
     *
     * @param {Object} args
     * @param {PaymentMethodData[]} methodData
     * @param {PaymentDetailsInit} details
     * @param {PaymentOptions} options
     */
    createAndShowRequest: ({methodData, details, options}) => {
      const rq = new content.PaymentRequest(methodData, details, options);
      content.rq = rq; // assign it so we can retrieve it later
      content.showPromise = rq.show();
    },
  },

  DialogContentTasks: {
    /**
     * Click the cancel button
     *
     * Don't await on this task since the cancel can close the dialog before
     * ContentTask can resolve the promise.
     *
     * @returns {undefined}
     */
    manuallyClickCancel: () => {
      content.document.getElementById("cancel").click();
    },

    /**
     * Do the minimum possible to complete the payment succesfully.
     * @returns {undefined}
     */
    completePayment: () => {
      content.document.getElementById("pay").click();
    },

    setSecurityCode: ({securityCode}) => {
      // Waive the xray to access the untrusted `securityCodeInput` property
      let picker = Cu.waiveXrays(content.document.querySelector("payment-method-picker"));
      // Unwaive to access the ChromeOnly `setUserInput` API.
      // setUserInput dispatches changes events.
      Cu.unwaiveXrays(picker.securityCodeInput).setUserInput(securityCode);
    },
  },

  /**
   * Common PaymentMethodData for testing
   */
  MethodData: {
    basicCard: {
      supportedMethods: "basic-card",
    },
    bobPay: {
      supportedMethods: "https://www.example.com/bobpay",
    },
  },

  /**
   * Common PaymentDetailsInit for testing
   */
  Details: {
    total60USD: {
      total: {
        label: "Total due",
        amount: { currency: "USD", value: "60.00" },
      },
    },
    twoDisplayItems: {
      total: {
        label: "Total due",
        amount: { currency: "USD", value: "32.00" },
      },
      displayItems: [
        {
          label: "First",
          amount: { currency: "USD", value: "1" },
        },
        {
          label: "Second",
          amount: { currency: "USD", value: "2" },
        },
      ],
    },
    twoShippingOptions: {
      total: {
        label: "Total due",
        amount: { currency: "USD", value: "2.00" },
      },
      shippingOptions: [
        {
          id: "1",
          label: "Meh Unreliable Shipping",
          amount: { currency: "USD", value: "1" },
        },
        {
          id: "2",
          label: "Premium Slow Shipping",
          amount: { currency: "USD", value: "2" },
          selected: true,
        },
      ],
    },
    bobPayPaymentModifier: {
      total: {
        label: "Total due",
        amount: { currency: "USD", value: "2.00" },
      },
      displayItems: [
        {
          label: "First",
          amount: { currency: "USD", value: "1" },
        },
      ],
      modifiers: [
        {
          additionalDisplayItems: [
            {
              label: "Credit card fee",
              amount: { currency: "USD", value: "0.50" },
            },
          ],
          supportedMethods: "basic-card",
          total: {
            label: "Total due",
            amount: { currency: "USD", value: "2.50" },
          },
          data: {
            supportedTypes: "credit",
          },
        },
        {
          additionalDisplayItems: [
            {
              label: "Bob-pay fee",
              amount: { currency: "USD", value: "1.50" },
            },
          ],
          supportedMethods: "https://www.example.com/bobpay",
          total: {
            label: "Total due",
            amount: { currency: "USD", value: "3.50" },
          },
        },
      ],
    },
  },
};
