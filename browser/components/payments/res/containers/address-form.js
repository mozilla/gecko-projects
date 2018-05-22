/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* import-globals-from ../../../../../browser/extensions/formautofill/content/autofillEditForms.js*/
import LabelledCheckbox from "../components/labelled-checkbox.js";
import PaymentStateSubscriberMixin from "../mixins/PaymentStateSubscriberMixin.js";
import paymentRequest from "../paymentRequest.js";
/* import-globals-from ../unprivileged-fallbacks.js */

/**
 * <address-form></address-form>
 *
 * XXX: Bug 1446164 - This form isn't localized when used via this custom element
 * as it will be much easier to share the logic once we switch to Fluent.
 */

export default class AddressForm extends PaymentStateSubscriberMixin(HTMLElement) {
  constructor() {
    super();

    this.pageTitle = document.createElement("h1");
    this.genericErrorText = document.createElement("div");

    this.cancelButton = document.createElement("button");
    this.cancelButton.className = "cancel-button";
    this.cancelButton.addEventListener("click", this);

    this.backButton = document.createElement("button");
    this.backButton.className = "back-button";
    this.backButton.addEventListener("click", this);

    this.saveButton = document.createElement("button");
    this.saveButton.className = "save-button";
    this.saveButton.addEventListener("click", this);

    this.persistCheckbox = new LabelledCheckbox();

    // The markup is shared with form autofill preferences.
    let url = "formautofill/editAddress.xhtml";
    this.promiseReady = this._fetchMarkup(url).then(doc => {
      this.form = doc.getElementById("form");
      return this.form;
    });
  }

  _fetchMarkup(url) {
    return new Promise((resolve, reject) => {
      let xhr = new XMLHttpRequest();
      xhr.responseType = "document";
      xhr.addEventListener("error", reject);
      xhr.addEventListener("load", evt => {
        resolve(xhr.response);
      });
      xhr.open("GET", url);
      xhr.send();
    });
  }

  connectedCallback() {
    this.promiseReady.then(form => {
      this.appendChild(this.pageTitle);
      this.appendChild(form);

      let record = {};
      this.formHandler = new EditAddress({
        form,
      }, record, {
        DEFAULT_REGION: PaymentDialogUtils.DEFAULT_REGION,
        getFormFormat: PaymentDialogUtils.getFormFormat,
        supportedCountries: PaymentDialogUtils.supportedCountries,
      });

      this.appendChild(this.persistCheckbox);
      this.appendChild(this.genericErrorText);
      this.appendChild(this.cancelButton);
      this.appendChild(this.backButton);
      this.appendChild(this.saveButton);
      // Only call the connected super callback(s) once our markup is fully
      // connected, including the shared form fetched asynchronously.
      super.connectedCallback();
    });
  }

  render(state) {
    let record = {};
    let {
      page,
    } = state;

    if (this.id && page && page.id !== this.id) {
      log.debug(`AddressForm: no need to further render inactive page: ${page.id}`);
      return;
    }

    this.cancelButton.textContent = this.dataset.cancelButtonLabel;
    this.backButton.textContent = this.dataset.backButtonLabel;
    this.saveButton.textContent = this.dataset.saveButtonLabel;
    this.persistCheckbox.label = this.dataset.persistCheckboxLabel;

    this.backButton.hidden = page.onboardingWizard;
    this.cancelButton.hidden = !page.onboardingWizard;

    if (page.addressFields) {
      this.setAttribute("address-fields", page.addressFields);
    } else {
      this.removeAttribute("address-fields");
    }

    this.pageTitle.textContent = page.title;
    this.genericErrorText.textContent = page.error;

    let editing = !!page.guid;
    let addresses = paymentRequest.getAddresses(state);

    // If an address is selected we want to edit it.
    if (editing) {
      record = addresses[page.guid];
      if (!record) {
        throw new Error("Trying to edit a non-existing address: " + page.guid);
      }
      // When editing an existing record, prevent changes to persistence
      this.persistCheckbox.hidden = true;
    } else {
      // Adding a new record: default persistence to checked when in a not-private session
      this.persistCheckbox.hidden = false;
      this.persistCheckbox.checked = !state.isPrivate;
    }

    this.formHandler.loadRecord(record);
  }

  handleEvent(event) {
    switch (event.type) {
      case "click": {
        this.onClick(event);
        break;
      }
    }
  }

  onClick(evt) {
    switch (evt.target) {
      case this.cancelButton: {
        paymentRequest.cancel();
        break;
      }
      case this.backButton: {
        this.requestStore.setState({
          page: {
            id: "payment-summary",
          },
        });
        break;
      }
      case this.saveButton: {
        this.saveRecord();
        break;
      }
      default: {
        throw new Error("Unexpected click target");
      }
    }
  }

  saveRecord() {
    let record = this.formHandler.buildFormObject();
    let {
      page,
      tempAddresses,
      savedBasicCards,
    } = this.requestStore.getState();
    let editing = !!page.guid;

    if (editing ? (page.guid in tempAddresses) : !this.persistCheckbox.checked) {
      record.isTemporary = true;
    }

    let state = {
      errorStateChange: {
        page: {
          id: "address-page",
          onboardingWizard: page.onboardingWizard,
          error: this.dataset.errorGenericSave,
        },
      },
      preserveOldProperties: true,
      selectedStateKey: page.selectedStateKey,
    };

    if (page.onboardingWizard && !Object.keys(savedBasicCards).length) {
      state.successStateChange = {
        page: {
          id: "basic-card-page",
          onboardingWizard: true,
          guid: null,
        },
      };
    } else {
      state.successStateChange = {
        page: {
          id: "payment-summary",
        },
      };
    }

    paymentRequest.updateAutofillRecord("addresses", record, page.guid, state);
  }
}

customElements.define("address-form", AddressForm);
