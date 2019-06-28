/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import LoginListItem from "./login-list-item.js";
import ReflectedFluentElement from "./reflected-fluent-element.js";

const collator = new Intl.Collator();
const sortFnOptions = {
  name: (a, b) => collator.compare(a.title, b.title),
  "last-used": (a, b) => (a.timeLastUsed < b.timeLastUsed),
  "last-changed": (a, b) => (a.timePasswordChanged < b.timePasswordChanged),
};

export default class LoginList extends ReflectedFluentElement {
  constructor() {
    super();
    this._logins = [];
    this._filter = "";
    this._selectedGuid = null;
    this._blankLoginListItem = new LoginListItem({});
  }

  connectedCallback() {
    if (this.shadowRoot) {
      return;
    }
    let loginListTemplate = document.querySelector("#login-list-template");
    this.attachShadow({mode: "open"})
        .appendChild(loginListTemplate.content.cloneNode(true));

    this._list = this.shadowRoot.querySelector("ol");

    this.render();

    this.shadowRoot.getElementById("login-sort")
                   .addEventListener("change", this);
    window.addEventListener("AboutLoginsLoginSelected", this);
    window.addEventListener("AboutLoginsFilterLogins", this);

    super.connectedCallback();
  }

  render() {
    this._list.textContent = "";

    if (!this._logins.length) {
      document.l10n.setAttributes(this, "login-list", {count: 0});
      return;
    }

    if (!this._selectedGuid) {
      this._blankLoginListItem.classList.add("selected");
      this._list.append(this._blankLoginListItem);
    }

    for (let login of this._logins) {
      let listItem = new LoginListItem(login);
      listItem.setAttribute("missing-username", this.getAttribute("missing-username"));
      if (login.guid == this._selectedGuid) {
        listItem.classList.add("selected");
      }
      this._list.append(listItem);
    }

    let visibleLoginCount = this._applyFilter();
    document.l10n.setAttributes(this, "login-list", {count: visibleLoginCount});
  }

  handleEvent(event) {
    switch (event.type) {
      case "change": {
        const sort = event.target.value;
        this._logins = this._logins.sort((a, b) => sortFnOptions[sort](a, b));
        this.render();
        break;
      }
      case "AboutLoginsFilterLogins": {
        this._filter = event.detail.toLocaleLowerCase();
        this.render();
        break;
      }
      case "AboutLoginsLoginSelected": {
        if (this._selectedGuid == event.detail.guid) {
          return;
        }

        this._selectedGuid = event.detail.guid || null;
        this.render();
        break;
      }
    }
  }

  static get reflectedFluentIDs() {
    return ["count",
            "last-used-option",
            "last-changed-option",
            "missing-username",
            "name-option",
            "new-login-subtitle",
            "new-login-title",
            "sort-label-text"];
  }

  static get observedAttributes() {
    return this.reflectedFluentIDs;
  }

  handleSpecialCaseFluentString(attrName) {
    switch (attrName) {
      case "missing-username": {
        break;
      }
      case "new-login-subtitle":
      case "new-login-title": {
        this._blankLoginListItem.setAttribute(attrName, this.getAttribute(attrName));
        break;
      }
      default:
        return false;
    }
    return true;
  }

  /**
   * @param {login[]} logins An array of logins used for displaying in the list.
   */
  setLogins(logins) {
    this._logins = logins;
    this.render();
  }

  /**
   * @param {login} login A login that was added to storage.
   */
  loginAdded(login) {
    this._logins.push(login);
    this.render();
  }

  /**
   * @param {login} login A login that was modified in storage. The related login-list-item
   *                       will get updated.
   */
  loginModified(login) {
    for (let i = 0; i < this._logins.length; i++) {
      if (this._logins[i].guid == login.guid) {
        this._logins[i] = login;
        break;
      }
    }
    this.render();
  }

  /**
   * @param {login} login A login that was removed from storage. The related login-list-item
   *                      will get removed. The login object is a plain JS object
   *                      representation of nsILoginInfo/nsILoginMetaInfo.
   */
  loginRemoved(login) {
    this._logins = this._logins.filter(l => l.guid != login.guid);
    this.render();
  }

  /**
   * Filters the displayed logins in the list to only those matching the
   * cached filter value.
   */
  _applyFilter() {
    let matchingLoginGuids;
    if (this._filter) {
      matchingLoginGuids = this._logins.filter(login => {
        return login.origin.toLocaleLowerCase().includes(this._filter) ||
               login.username.toLocaleLowerCase().includes(this._filter);
      }).map(login => login.guid);
    } else {
      matchingLoginGuids = this._logins.map(login => login.guid);
    }

    for (let listItem of this._list.querySelectorAll("login-list-item")) {
      if (!listItem.dataset.guid) {
        // Don't hide the 'New Login' item if it is present.
        continue;
      }
      if (matchingLoginGuids.includes(listItem.dataset.guid)) {
        if (listItem.hidden) {
          listItem.hidden = false;
        }
      } else if (!listItem.hidden) {
        listItem.hidden = true;
      }
    }

    return matchingLoginGuids.length;
  }
}
customElements.define("login-list", LoginList);
