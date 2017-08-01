/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * Defines a handler object to represent forms that autofill can handle.
 */

"use strict";

this.EXPORTED_SYMBOLS = ["FormAutofillHandler"];

const {classes: Cc, interfaces: Ci, utils: Cu, results: Cr} = Components;

Cu.import("resource://gre/modules/XPCOMUtils.jsm");

Cu.import("resource://formautofill/FormAutofillUtils.jsm");

XPCOMUtils.defineLazyModuleGetter(this, "FormAutofillHeuristics",
                                  "resource://formautofill/FormAutofillHeuristics.jsm");

this.log = null;
FormAutofillUtils.defineLazyLogGetter(this, this.EXPORTED_SYMBOLS[0]);

/**
 * Handles profile autofill for a DOM Form element.
 * @param {FormLike} form Form that need to be auto filled
 */
function FormAutofillHandler(form) {
  this.form = form;
  this.fieldDetails = [];
  this.winUtils = this.form.rootElement.ownerGlobal.QueryInterface(Ci.nsIInterfaceRequestor)
    .getInterface(Ci.nsIDOMWindowUtils);

  this.address = {
    /**
     * Similar to the `fieldDetails` above but contains address fields only.
     */
    fieldDetails: [],
    /**
     * String of the filled address' guid.
     */
    filledRecordGUID: null,
  };

  this.creditCard = {
    /**
     * Similar to the `fieldDetails` above but contains credit card fields only.
     */
    fieldDetails: [],
    /**
     * String of the filled creditCard's guid.
     */
    filledRecordGUID: null,
  };
}

FormAutofillHandler.prototype = {
  /**
   * DOM Form element to which this object is attached.
   */
  form: null,

  _formFieldCount: 0,

  /**
   * Array of collected data about relevant form fields.  Each item is an object
   * storing the identifying details of the field and a reference to the
   * originally associated element from the form.
   *
   * The "section", "addressType", "contactType", and "fieldName" values are
   * used to identify the exact field when the serializable data is received
   * from the backend.  There cannot be multiple fields which have
   * the same exact combination of these values.
   *
   * A direct reference to the associated element cannot be sent to the user
   * interface because processing may be done in the parent process.
   */
  fieldDetails: null,

  /**
   * Subcategory of handler that contains address related data.
   */
  address: null,

  /**
   * Subcategory of handler that contains credit card related data.
   */
  creditCard: null,

  /**
   * A WindowUtils reference of which Window the form belongs
   */
  winUtils: null,

  /**
   * Enum for form autofill MANUALLY_MANAGED_STATES values
   */
  fieldStateEnum: {
    // not themed
    NORMAL: null,
    // highlighted
    AUTO_FILLED: "-moz-autofill",
    // highlighted && grey color text
    PREVIEW: "-moz-autofill-preview",
  },

  get isFormChangedSinceLastCollection() {
    // When the number of form controls is the same with last collection, it
    // can be recognized as there is no element changed. However, we should
    // improve the function to detect the element changes. e.g. a tel field
    // is changed from type="hidden" to type="tel".
    return this._formFieldCount != this.form.elements.length;
  },

  /**
   * Set fieldDetails from the form about fields that can be autofilled.

   * @returns {Array} The valid address and credit card details.
   */
  collectFormFields() {
    this._cacheValue.allFieldNames = null;
    this._formFieldCount = this.form.elements.length;
    let fieldDetails = FormAutofillHeuristics.getFormInfo(this.form);
    this.fieldDetails = fieldDetails ? fieldDetails : [];
    log.debug("Collected details on", this.fieldDetails.length, "fields");

    this.address.fieldDetails = this.fieldDetails.filter(
      detail => FormAutofillUtils.isAddressField(detail.fieldName)
    );
    this.creditCard.fieldDetails = this.fieldDetails.filter(
      detail => FormAutofillUtils.isCreditCardField(detail.fieldName)
    );

    if (this.address.fieldDetails.length < FormAutofillUtils.AUTOFILL_FIELDS_THRESHOLD) {
      log.debug("Ignoring address related fields since it has only",
                this.address.fieldDetails.length,
                "field(s)");
      this.address.fieldDetails = [];
    }

    if (!this.creditCard.fieldDetails.some(i => i.fieldName == "cc-number")) {
      log.debug("Ignoring credit card related fields since it's without credit card number field");
      this.creditCard.fieldDetails = [];
    }

    return Array.of(...(this.address.fieldDetails),
                    ...(this.creditCard.fieldDetails));
  },

  getFieldDetailByName(fieldName) {
    return this.fieldDetails.find(detail => detail.fieldName == fieldName);
  },

  _cacheValue: {
    allFieldNames: null,
    oneLineStreetAddress: null,
    matchingSelectOption: null,
  },

  get allFieldNames() {
    if (!this._cacheValue.allFieldNames) {
      this._cacheValue.allFieldNames = this.fieldDetails.map(record => record.fieldName);
    }
    return this._cacheValue.allFieldNames;
  },

  _getOneLineStreetAddress(address) {
    if (!this._cacheValue.oneLineStreetAddress) {
      this._cacheValue.oneLineStreetAddress = {};
    }
    if (!this._cacheValue.oneLineStreetAddress[address]) {
      this._cacheValue.oneLineStreetAddress[address] = FormAutofillUtils.toOneLineAddress(address);
    }
    return this._cacheValue.oneLineStreetAddress[address];
  },

  _addressTransformer(profile) {
    if (profile["street-address"]) {
      // "-moz-street-address-one-line" is used by the labels in
      // ProfileAutoCompleteResult.
      profile["-moz-street-address-one-line"] = this._getOneLineStreetAddress(profile["street-address"]);
      let streetAddressDetail = this.getFieldDetailByName("street-address");
      if (streetAddressDetail &&
          (streetAddressDetail.elementWeakRef.get() instanceof Ci.nsIDOMHTMLInputElement)) {
        profile["street-address"] = profile["-moz-street-address-one-line"];
      }

      let waitForConcat = [];
      for (let f of ["address-line3", "address-line2", "address-line1"]) {
        waitForConcat.unshift(profile[f]);
        if (this.getFieldDetailByName(f)) {
          if (waitForConcat.length > 1) {
            profile[f] = FormAutofillUtils.toOneLineAddress(waitForConcat);
          }
          waitForConcat = [];
        }
      }
    }
  },

  _matchSelectOptions(profile) {
    if (!this._cacheValue.matchingSelectOption) {
      this._cacheValue.matchingSelectOption = new WeakMap();
    }

    for (let fieldName in profile) {
      let fieldDetail = this.getFieldDetailByName(fieldName);
      if (!fieldDetail) {
        continue;
      }

      let element = fieldDetail.elementWeakRef.get();
      if (!(element instanceof Ci.nsIDOMHTMLSelectElement)) {
        continue;
      }

      let cache = this._cacheValue.matchingSelectOption.get(element) || {};
      let value = profile[fieldName];
      if (cache[value] && cache[value].get()) {
        continue;
      }

      let option = FormAutofillUtils.findSelectOption(element, profile, fieldName);
      if (option) {
        cache[value] = Cu.getWeakReference(option);
        this._cacheValue.matchingSelectOption.set(element, cache);
      } else {
        if (cache[value]) {
          delete cache[value];
          this._cacheValue.matchingSelectOption.set(element, cache);
        }
        // Delete the field so the phishing hint won't treat it as a "also fill"
        // field.
        delete profile[fieldName];
      }
    }
  },

  getAdaptedProfiles(originalProfiles) {
    for (let profile of originalProfiles) {
      this._addressTransformer(profile);
      this._matchSelectOptions(profile);
    }
    return originalProfiles;
  },

  /**
   * Processes form fields that can be autofilled, and populates them with the
   * profile provided by backend.
   *
   * @param {Object} profile
   *        A profile to be filled in.
   * @param {Object} focusedInput
   *        A focused input element which is skipped for filling.
   */
  autofillFormFields(profile, focusedInput) {
    log.debug("profile in autofillFormFields:", profile);

    this.address.filledRecordGUID = profile.guid;
    for (let fieldDetail of this.address.fieldDetails) {
      // Avoid filling field value in the following cases:
      // 1. the focused input which is filled in FormFillController.
      // 2. a non-empty input field
      // 3. the invalid value set
      // 4. value already chosen in select element

      let element = fieldDetail.elementWeakRef.get();
      if (!element) {
        continue;
      }

      let value = profile[fieldDetail.fieldName];
      if (element instanceof Ci.nsIDOMHTMLInputElement && !element.value && value) {
        if (element !== focusedInput) {
          element.setUserInput(value);
        }
        this.changeFieldState(fieldDetail, "AUTO_FILLED");
      } else if (element instanceof Ci.nsIDOMHTMLSelectElement) {
        let cache = this._cacheValue.matchingSelectOption.get(element) || {};
        let option = cache[value] && cache[value].get();
        if (!option) {
          continue;
        }
        // Do not change value or dispatch events if the option is already selected.
        // Use case for multiple select is not considered here.
        if (!option.selected) {
          option.selected = true;
          element.dispatchEvent(new element.ownerGlobal.UIEvent("input", {bubbles: true}));
          element.dispatchEvent(new element.ownerGlobal.Event("change", {bubbles: true}));
        }
        // Autofill highlight appears regardless if value is changed or not
        this.changeFieldState(fieldDetail, "AUTO_FILLED");
      }

      // Unlike using setUserInput directly, FormFillController dispatches an
      // asynchronous "DOMAutoComplete" event with an "input" event follows right
      // after. So, we need to suppress the first "input" event fired off from
      // focused input to make sure the latter change handler won't be affected
      // by auto filling.
      if (element === focusedInput) {
        const suppressFirstInputHandler = e => {
          if (e.isTrusted) {
            e.stopPropagation();
            element.removeEventListener("input", suppressFirstInputHandler);
          }
        };

        element.addEventListener("input", suppressFirstInputHandler);
      }
      element.previewValue = "";
    }

    // Handle the highlight style resetting caused by user's correction afterward.
    log.debug("register change handler for filled form:", this.form);
    const onChangeHandler = e => {
      let hasFilledFields;

      if (!e.isTrusted) {
        return;
      }

      for (let fieldDetail of this.address.fieldDetails) {
        let element = fieldDetail.elementWeakRef.get();

        if (!element) {
          return;
        }

        if (e.target == element || (e.target == element.form && e.type == "reset")) {
          this.changeFieldState(fieldDetail, "NORMAL");
        }

        hasFilledFields |= (fieldDetail.state == "AUTO_FILLED");
      }

      // Unregister listeners and clear guid once no field is in AUTO_FILLED state.
      if (!hasFilledFields) {
        this.form.rootElement.removeEventListener("input", onChangeHandler);
        this.form.rootElement.removeEventListener("reset", onChangeHandler);
        this.address.filledRecordGUID = null;
      }
    };

    this.form.rootElement.addEventListener("input", onChangeHandler);
    this.form.rootElement.addEventListener("reset", onChangeHandler);
  },

  /**
   * Populates result to the preview layers with given profile.
   *
   * @param {Object} profile
   *        A profile to be previewed with
   */
  previewFormFields(profile) {
    log.debug("preview profile in autofillFormFields:", profile);

    for (let fieldDetail of this.address.fieldDetails) {
      let element = fieldDetail.elementWeakRef.get();
      let value = profile[fieldDetail.fieldName] || "";

      // Skip the field that is null
      if (!element) {
        continue;
      }

      if (element instanceof Ci.nsIDOMHTMLSelectElement) {
        // Unlike text input, select element is always previewed even if
        // the option is already selected.
        if (value) {
          let cache = this._cacheValue.matchingSelectOption.get(element) || {};
          let option = cache[value] && cache[value].get();
          if (option) {
            value = option.text || "";
          } else {
            value = "";
          }
        }
      } else if (element.value) {
        // Skip the field if it already has text entered.
        continue;
      }
      element.previewValue = value;
      this.changeFieldState(fieldDetail, value ? "PREVIEW" : "NORMAL");
    }
  },

  /**
   * Clear preview text and background highlight of all fields.
   */
  clearPreviewedFormFields() {
    log.debug("clear previewed fields in:", this.form);

    for (let fieldDetail of this.address.fieldDetails) {
      let element = fieldDetail.elementWeakRef.get();
      if (!element) {
        log.warn(fieldDetail.fieldName, "is unreachable");
        continue;
      }

      element.previewValue = "";

      // We keep the state if this field has
      // already been auto-filled.
      if (fieldDetail.state === "AUTO_FILLED") {
        continue;
      }

      this.changeFieldState(fieldDetail, "NORMAL");
    }
  },

  /**
   * Change the state of a field to correspond with different presentations.
   *
   * @param {Object} fieldDetail
   *        A fieldDetail of which its element is about to update the state.
   * @param {string} nextState
   *        Used to determine the next state
   */
  changeFieldState(fieldDetail, nextState) {
    let element = fieldDetail.elementWeakRef.get();

    if (!element) {
      log.warn(fieldDetail.fieldName, "is unreachable while changing state");
      return;
    }
    if (!(nextState in this.fieldStateEnum)) {
      log.warn(fieldDetail.fieldName, "is trying to change to an invalid state");
      return;
    }

    for (let [state, mmStateValue] of Object.entries(this.fieldStateEnum)) {
      // The NORMAL state is simply the absence of other manually
      // managed states so we never need to add or remove it.
      if (!mmStateValue) {
        continue;
      }

      if (state == nextState) {
        this.winUtils.addManuallyManagedState(element, mmStateValue);
      } else {
        this.winUtils.removeManuallyManagedState(element, mmStateValue);
      }
    }

    fieldDetail.state = nextState;
  },

  /**
   * Return the records that is converted from address/creditCard fieldDetails and
   * only valid form records are included.
   *
   * @returns {Object} The new profile that convert from details with trimmed result.
   */
  createRecords() {
    let records = {};

    ["address", "creditCard"].forEach(type => {
      let details = this[type].fieldDetails;
      if (!details || details.length == 0) {
        return;
      }

      let recordName = `${type}Record`;
      records[recordName] = {};
      details.forEach(detail => {
        let element = detail.elementWeakRef.get();
        // Remove the unnecessary spaces
        let value = element && element.value.trim();
        if (!value) {
          return;
        }

        records[recordName][detail.fieldName] = value;
      });
    });

    if (records.addressRecord &&
        Object.keys(records.addressRecord).length < FormAutofillUtils.AUTOFILL_FIELDS_THRESHOLD) {
      log.debug("No address record saving since there are only",
                     Object.keys(records.addressRecord).length,
                     "usable fields");
      delete records.addressRecord;
    }

    if (records.creditCardRecord && !records.creditCardRecord["cc-number"]) {
      log.debug("No credit card record saving since card number is empty");
      delete records.creditCardRecord;
    }

    return records;
  },
};
