/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import {recordTelemetryEvent} from "../aboutLoginsUtils.js";
import ReflectedFluentElement from "./reflected-fluent-element.js";

export default class LoginItem extends ReflectedFluentElement {
  constructor() {
    super();
    this._login = {};
  }

  connectedCallback() {
    if (this.shadowRoot) {
      this.render();
      return;
    }

    let loginItemTemplate = document.querySelector("#login-item-template");
    this.attachShadow({mode: "open"})
        .appendChild(loginItemTemplate.content.cloneNode(true));

    for (let selector of [
      ".delete-button",
      ".edit-button",
      ".open-site-button",
      ".save-changes-button",
      ".cancel-button",
    ]) {
      let button = this.shadowRoot.querySelector(selector);
      button.addEventListener("click", this);
    }

    this._copyPasswordButton = this.shadowRoot.querySelector(".copy-password-button");
    this._copyUsernameButton = this.shadowRoot.querySelector(".copy-username-button");
    this._deleteButton = this.shadowRoot.querySelector(".delete-button");
    this._editButton = this.shadowRoot.querySelector(".edit-button");
    this._form = this.shadowRoot.querySelector("form");
    this._originInput = this.shadowRoot.querySelector("input[name='origin']");
    this._usernameInput = this.shadowRoot.querySelector("input[name='username']");
    this._passwordInput = this.shadowRoot.querySelector("input[name='password']");
    this._revealCheckbox = this.shadowRoot.querySelector(".reveal-password-checkbox");
    this._title = this.shadowRoot.querySelector(".title");

    this._copyUsernameButton.relatedInput = this._usernameInput;
    this._copyPasswordButton.relatedInput = this._passwordInput;

    this.render();

    this._originInput.addEventListener("blur", this);
    this._revealCheckbox.addEventListener("click", this);
    window.addEventListener("AboutLoginsLoginSelected", this);

    super.connectedCallback();
  }

  static get reflectedFluentIDs() {
    return [
      "cancel-button",
      "copied-password-button",
      "copied-username-button",
      "copy-password-button",
      "copy-username-button",
      "delete-button",
      "edit-button",
      "new-login-title",
      "open-site-button",
      "origin-label",
      "origin-placeholder",
      "password-hide-title",
      "password-label",
      "password-show-title",
      "save-changes-button",
      "time-created",
      "time-changed",
      "time-used",
      "username-label",
      "username-placeholder",
    ];
  }

  static get observedAttributes() {
    return this.reflectedFluentIDs;
  }

  handleSpecialCaseFluentString(attrName) {
    switch (attrName) {
      case "copied-password-button":
      case "copy-password-button": {
        let newAttrName = attrName.substr(0, attrName.indexOf("-")) + "-button-text";
        this._copyPasswordButton.setAttribute(newAttrName, this.getAttribute(attrName));
        break;
      }
      case "copied-username-button":
      case "copy-username-button": {
        let newAttrName = attrName.substr(0, attrName.indexOf("-")) + "-button-text";
        this._copyUsernameButton.setAttribute(newAttrName, this.getAttribute(attrName));
        break;
      }
      case "new-login-title": {
        this._title.setAttribute(attrName, this.getAttribute(attrName));
        if (!this._login.title) {
          this._title.textContent = this.getAttribute(attrName);
        }
        break;
      }
      case "origin-placeholder": {
        this._originInput.setAttribute("placeholder", this.getAttribute(attrName));
        break;
      }
      case "password-hide-title":
      case "password-show-title": {
        this._updatePasswordRevealState();
        break;
      }
      case "username-placeholder": {
        this._usernameInput.setAttribute("placeholder", this.getAttribute(attrName));
        break;
      }
      default:
        return false;
    }
    return true;
  }

  render() {
    let l10nArgs = {
      timeCreated: this._login.timeCreated || "",
      timeChanged: this._login.timePasswordChanged || "",
      timeUsed: this._login.timeLastUsed || "",
    };
    document.l10n.setAttributes(this, "login-item", l10nArgs);

    this._title.textContent = this._login.title || this._title.getAttribute("new-login-title");
    this._originInput.defaultValue = this._login.origin || "";
    this._usernameInput.defaultValue = this._login.username || "";
    this._passwordInput.defaultValue = this._login.password || "";
    this._updatePasswordRevealState();
  }

  handleEvent(event) {
    switch (event.type) {
      case "AboutLoginsLoginSelected": {
        this.setLogin(event.detail);
        break;
      }
      case "blur": {
        // Add https:// prefix if one was not provided.
        let originValue = this._originInput.value.trim();
        if (!originValue) {
          return;
        }
        if (!originValue.match(/:\/\//)) {
          this._originInput.value = "https://" + originValue;
        }
        break;
      }
      case "click": {
        let classList = event.target.classList;
        if (classList.contains("reveal-password-checkbox")) {
          this._updatePasswordRevealState();

          let method = this._revealCheckbox.checked ? "show" : "hide";
          recordTelemetryEvent({object: "password", method});
          return;
        }

        // Prevent form submit behavior on the following buttons.
        event.preventDefault();
        if (classList.contains("cancel-button")) {
          if (this._login.guid) {
            this.setLogin(this._login);
          } else {
            // TODO, should select the first login if it exists
            // or show the no-logins view otherwise
            this._toggleEditing();
            this.render();
          }

          recordTelemetryEvent({
            object: this._login.guid ? "existing_login" : "new_login",
            method: "cancel",
          });
          return;
        }
        if (classList.contains("delete-button")) {
          document.dispatchEvent(new CustomEvent("AboutLoginsDeleteLogin", {
            bubbles: true,
            detail: this._login,
          }));

          recordTelemetryEvent({object: "existing_login", method: "delete"});
          return;
        }
        if (classList.contains("edit-button")) {
          this._toggleEditing();

          recordTelemetryEvent({object: "existing_login", method: "edit"});
          return;
        }
        if (classList.contains("open-site-button")) {
          document.dispatchEvent(new CustomEvent("AboutLoginsOpenSite", {
            bubbles: true,
            detail: this._login,
          }));

          recordTelemetryEvent({object: "existing_login", method: "open_site"});
          return;
        }
        if (classList.contains("save-changes-button")) {
          if (!this._isFormValid({reportErrors: true})) {
            return;
          }
          let loginUpdates = this._loginFromForm();
          if (this._login.guid) {
            loginUpdates.guid = this._login.guid;
            document.dispatchEvent(new CustomEvent("AboutLoginsUpdateLogin", {
              bubbles: true,
              detail: loginUpdates,
            }));

            recordTelemetryEvent({object: "existing_login", method: "save"});
          } else {
            document.dispatchEvent(new CustomEvent("AboutLoginsCreateLogin", {
              bubbles: true,
              detail: loginUpdates,
            }));

            recordTelemetryEvent({object: "new_login", method: "save"});
          }
        }
        break;
      }
    }
  }

  /**
   * @param {login} login The login that should be displayed. The login object is
   *                      a plain JS object representation of nsILoginInfo/nsILoginMetaInfo.
   */
  setLogin(login) {
    this._login = login;

    this._form.reset();

    if (login.guid) {
      delete this.dataset.isNewLogin;
    } else {
      this.dataset.isNewLogin = true;
    }
    this._toggleEditing(!login.guid);

    this._revealCheckbox.checked = false;

    this.render();
  }

  /**
   * Updates the view if the login argument matches the login currently
   * displayed.
   *
   * @param {login} login The login that was added to storage. The login object is
   *                      a plain JS object representation of nsILoginInfo/nsILoginMetaInfo.
   */
  loginAdded(login) {
    if (this._login.guid ||
        !window.AboutLoginsUtils.doLoginsMatch(login, this._loginFromForm())) {
      return;
    }

    this._toggleEditing(false);
    this._login = login;
    this.render();
  }

  /**
   * Updates the view if the login argument matches the login currently
   * displayed.
   *
   * @param {login} login The login that was modified in storage. The login object is
   *                      a plain JS object representation of nsILoginInfo/nsILoginMetaInfo.
   */
  loginModified(login) {
    if (this._login.guid != login.guid) {
      return;
    }

    this._toggleEditing(false);
    this._login = login;
    this.render();
  }

  /**
   * Clears the displayed login if the argument matches the currently
   * displayed login.
   *
   * @param {login} login The login that was removed from storage. The login object is
   *                      a plain JS object representation of nsILoginInfo/nsILoginMetaInfo.
   */
  loginRemoved(login) {
    if (login.guid != this._login.guid) {
      return;
    }

    this._toggleEditing(false);
    this._login = {};
    this.render();
  }

  /**
   * Checks that the edit/new-login form has valid values present for their
   * respective required fields.
   *
   * @param {boolean} reportErrors If true, validation errors will be reported
   *                               to the user.
   */
  _isFormValid({reportErrors} = {}) {
    let fields = [this._passwordInput];
    if (this.dataset.isNewLogin) {
      fields.push(this._originInput);
    }
    let valid = true;
    // Check validity on all required fields so each field will get :invalid styling
    // if applicable.
    for (let field of fields) {
      if (reportErrors) {
        valid &= field.reportValidity();
      } else {
        valid &= field.checkValidity();
      }
    }
    return valid;
  }

  _loginFromForm() {
    return {
      username: this._usernameInput.value.trim(),
      password: this._passwordInput.value.trim(),
      origin: this._originInput.value.trim(),
    };
  }

  /**
   * Toggles the login-item view from editing to non-editing mode.
   *
   * @param {boolean} force When true puts the form in 'edit' mode, otherwise
   *                        puts the form in read-only mode.
   */
  _toggleEditing(force) {
    let shouldEdit = force !== undefined ? force : !this.dataset.editing;

    if (!shouldEdit) {
      delete this.dataset.isNewLogin;
    }

    if (shouldEdit) {
      this._passwordInput.style.removeProperty("width");
    } else {
      // Need to set a shorter width than -moz-available so the reveal checkbox
      // will still appear next to the password.
      this._passwordInput.style.width = (this._login.password || "").length + "ch";
    }

    this._deleteButton.disabled = this.dataset.isNewLogin;
    this._editButton.disabled = shouldEdit;
    this._originInput.readOnly = !this.dataset.isNewLogin;
    this._usernameInput.readOnly = !shouldEdit;
    this._passwordInput.readOnly = !shouldEdit;
    if (shouldEdit) {
      this.dataset.editing = true;
    } else {
      delete this.dataset.editing;
    }
  }

  _updatePasswordRevealState() {
    let labelAttr = this._revealCheckbox.checked ? "password-show-title"
                                                 : "password-hide-title";
    this._revealCheckbox.setAttribute("aria-label", this.getAttribute(labelAttr));
    this._revealCheckbox.setAttribute("title", this.getAttribute(labelAttr));

    let {checked} = this._revealCheckbox;
    let inputType = checked ? "type" : "password";
    this._passwordInput.setAttribute("type", inputType);
  }
}
customElements.define("login-item", LoginItem);
