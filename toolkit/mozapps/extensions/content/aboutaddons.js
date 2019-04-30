/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
/* eslint max-len: ["error", 80] */
/* exported initialize, hide, show */
/* import-globals-from aboutaddonsCommon.js */
/* global windowRoot */

"use strict";

XPCOMUtils.defineLazyModuleGetters(this, {
  AddonManager: "resource://gre/modules/AddonManager.jsm",
  ExtensionPermissions: "resource://gre/modules/ExtensionPermissions.jsm",
});

XPCOMUtils.defineLazyPreferenceGetter(
  this, "allowPrivateBrowsingByDefault",
  "extensions.allowPrivateBrowsingByDefault", true);
XPCOMUtils.defineLazyPreferenceGetter(
  this, "SUPPORT_URL", "app.support.baseURL",
  "", null, val => Services.urlFormatter.formatURL(val));

const PLUGIN_ICON_URL = "chrome://global/skin/plugins/pluginGeneric.svg";
const PERMISSION_MASKS = {
  "ask-to-activate": AddonManager.PERM_CAN_ASK_TO_ACTIVATE,
  enable: AddonManager.PERM_CAN_ENABLE,
  "always-activate": AddonManager.PERM_CAN_ENABLE,
  disable: AddonManager.PERM_CAN_DISABLE,
  "never-activate": AddonManager.PERM_CAN_DISABLE,
  uninstall: AddonManager.PERM_CAN_UNINSTALL,
  upgrade: AddonManager.PERM_CAN_UPGRADE,
};

const PRIVATE_BROWSING_PERM_NAME = "internal:privateBrowsingAllowed";
const PRIVATE_BROWSING_PERMS =
  {permissions: [PRIVATE_BROWSING_PERM_NAME], origins: []};

const AddonCardListenerHandler = {
  ADDON_EVENTS: new Set([
    "onDisabled", "onEnabled", "onInstalled", "onPropertyChanged",
    "onUninstalled",
  ]),
  MANAGER_EVENTS: new Set(["onUpdateModeChanged"]),
  INSTALL_EVENTS: new Set(["onNewInstall", "onInstallEnded"]),

  delegateAddonEvent(name, args) {
    this.delegateEvent(name, args[0], args);
  },

  delegateInstallEvent(name, args) {
    let addon = args[0].addon || args[0].existingAddon;
    if (!addon) {
      return;
    }
    this.delegateEvent(name, addon, args);
  },

  delegateEvent(name, addon, args) {
    let cards;
    if (this.MANAGER_EVENTS.has(name)) {
      cards = document.querySelectorAll("addon-card");
    } else {
      let card = document.querySelector(`addon-card[addon-id="${addon.id}"]`);
      cards = card ? [card] : [];
    }
    for (let card of cards) {
      try {
        if (name in card) {
          card[name](...args);
        }
      } catch (e) {
        Cu.reportError(e);
      }
    }
  },

  startup() {
    for (let name of this.ADDON_EVENTS) {
      this[name] = (...args) => this.delegateAddonEvent(name, args);
    }
    for (let name of this.INSTALL_EVENTS) {
      this[name] = (...args) => this.delegateInstallEvent(name, args);
    }
    for (let name of this.MANAGER_EVENTS) {
      this[name] = (...args) => this.delegateEvent(name, null, args);
    }
    AddonManager.addAddonListener(this);
    AddonManager.addInstallListener(this);
    AddonManager.addManagerListener(this);
  },

  shutdown() {
    AddonManager.removeAddonListener(this);
    AddonManager.removeInstallListener(this);
    AddonManager.removeManagerListener(this);
  },
};

async function isAllowedInPrivateBrowsing(addon) {
  // Use the Promise directly so this function stays sync for the other case.
  let perms = await ExtensionPermissions.get(addon.id);
  return perms.permissions.includes(PRIVATE_BROWSING_PERM_NAME);
}

function hasPermission(addon, permission) {
  return !!(addon.permissions & PERMISSION_MASKS[permission]);
}

/**
 * This function is set in initialize() by the parent about:addons window. It
 * is a helper for gViewController.loadView().
 *
 * @param {string} type The view type to load.
 * @param {string} param The (optional) param for the view.
 */
let loadViewFn;

let _templates = {};

/**
 * Import a template from the main document.
 */
function importTemplate(name) {
  if (!_templates.hasOwnProperty(name)) {
    _templates[name] = document.querySelector(`template[name="${name}"]`);
  }
  let template = _templates[name];
  if (template) {
    return document.importNode(template.content, true);
  }
  throw new Error(`Unknown template: ${name}`);
}

function nl2br(text) {
  let frag = document.createDocumentFragment();
  let hasAppended = false;
  for (let part of text.split("\n")) {
    if (hasAppended) {
      frag.appendChild(document.createElement("br"));
    }
    frag.appendChild(new Text(part));
    hasAppended = true;
  }
  return frag;
}

class PanelList extends HTMLElement {
  static get observedAttributes() {
    return ["open"];
  }

  constructor() {
    super();
    this.attachShadow({mode: "open"});
    this.shadowRoot.appendChild(importTemplate("panel-list"));
  }

  attributeChangedCallback(name, oldVal, newVal) {
    if (name == "open" && newVal != oldVal) {
      if (this.open) {
        this.onShow();
      } else {
        this.onHide();
      }
    }
  }

  get open() {
    return this.hasAttribute("open");
  }

  set open(val) {
    if (val) {
      this.setAttribute("open", "true");
    } else {
      this.removeAttribute("open");
    }
  }

  show(triggeringEvent) {
    this.triggeringEvent = triggeringEvent;
    this.open = true;
  }

  hide(triggeringEvent) {
    this.triggeringEvent = triggeringEvent;
    this.open = false;
  }

  toggle(triggeringEvent) {
    if (this.open) {
      this.hide(triggeringEvent);
    } else {
      this.show(triggeringEvent);
    }
  }

  async setAlign() {
    // Set the showing attribute to hide the panel until its alignment is set.
    this.setAttribute("showing", "true");
    // Tell the parent node to hide any overflow in case the panel extends off
    // the page before the alignment is set.
    this.parentNode.style.overflow = "hidden";

    // Wait for a layout flush, then find the bounds.
    let {height, width, y, left, right, winHeight, winWidth} =
      await new Promise(resolve => {
        requestAnimationFrame(() => setTimeout(() => {
          // Use y since top is reserved.
          let {y, left, right} =
            window.windowUtils.getBoundsWithoutFlushing(this.parentNode);
          let {height, width} =
            window.windowUtils.getBoundsWithoutFlushing(this);
          resolve({
            height, width, y, left, right,
            winHeight: innerHeight, winWidth: innerWidth,
          });
        }, 0));
      });

    // Calculate the left/right alignment.
    let align;
    if (Services.locale.isAppLocaleRTL) {
      // Prefer aligning on the right.
      align = right - width + 14 < 0 ? "left" : "right";
    } else {
      // Prefer aligning on the left.
      align = left + width - 14 > winWidth ? "right" : "left";
    }

    // "bottom" style will move the panel down 30px from the top of the parent.
    let valign = y + height + 30 > winHeight ? "top" : "bottom";

    // Set the alignments and show the panel.
    this.setAttribute("align", align);
    this.setAttribute("valign", valign);
    this.parentNode.style.overflow = "";
    this.removeAttribute("showing");

    // Send the shown event after the next paint.
    requestAnimationFrame(() => this.sendEvent("shown"));
  }

  addHideListeners() {
    // Hide when a panel-item is clicked in the list.
    this.addEventListener("click", this);
    // Hide when a click is initiated outside the panel.
    document.addEventListener("mousedown", this);
    // Hide if focus changes and the panel isn't in focus.
    document.addEventListener("focusin", this);
    // Reset or focus tracking, we treat the first focusin differently.
    this.focusHasChanged = false;
    // Hide on resize, scroll or losing window focus.
    window.addEventListener("resize", this);
    window.addEventListener("scroll", this);
    window.addEventListener("blur", this);
  }

  removeHideListeners() {
    this.removeEventListener("click", this);
    document.removeEventListener("mousedown", this);
    document.removeEventListener("focusin", this);
    window.removeEventListener("resize", this);
    window.removeEventListener("scroll", this);
    window.removeEventListener("blur", this);
  }

  handleEvent(e) {
    // Ignore the event if it caused the panel to open.
    if (e == this.triggeringEvent) {
      return;
    }

    switch (e.type) {
      case "resize":
      case "scroll":
      case "blur":
        this.hide();
        break;
      case "click":
        if (e.target.tagName == "PANEL-ITEM") {
          this.hide();
        }
        break;
      case "mousedown":
      case "focusin":
        // There will be a focusin after the mousedown that opens the panel
        // using the mouse. Ignore the first focusin event if it's on the
        // triggering target.
        if (this.triggeringEvent &&
            e.target == this.triggeringEvent.target &&
            !this.focusHasChanged) {
          this.focusHasChanged = true;
        // If the target isn't in the panel, hide. This will close when focus
        // moves out of the panel, or there's a click started outside the panel.
        } else if (!e.target || e.target.closest("panel-list") != this) {
          this.hide();
        // Just record that there was a focusin event.
        } else {
          this.focusHasChanged = true;
        }
        break;
    }
  }

  onShow() {
    this.setAlign();
    this.addHideListeners();
  }

  onHide() {
    this.removeHideListeners();
  }

  sendEvent(name, detail) {
    this.dispatchEvent(new CustomEvent(name, {detail}));
  }
}
customElements.define("panel-list", PanelList);

class PanelItem extends HTMLElement {
  constructor() {
    super();
    this.attachShadow({mode: "open"});
    this.shadowRoot.appendChild(importTemplate("panel-item"));
    this.button = this.shadowRoot.querySelector("button");
  }

  get disabled() {
    return this.button.hasAttribute("disabled");
  }

  set disabled(val) {
    if (val) {
      this.button.setAttribute("disabled", "");
    } else {
      this.button.removeAttribute("disabled");
    }
  }

  get checked() {
    return this.hasAttribute("checked");
  }

  set checked(val) {
    if (val) {
      this.setAttribute("checked", "");
    } else {
      this.removeAttribute("checked");
    }
  }
}
customElements.define("panel-item", PanelItem);

class AddonOptions extends HTMLElement {
  connectedCallback() {
    if (this.children.length == 0) {
      this.render();
    }
  }

  render() {
    this.appendChild(importTemplate("addon-options"));
  }

  update(card, addon, updateInstall) {
    // Hide remove button if not allowed.
    let removeButton = this.querySelector('[action="remove"]');
    removeButton.hidden = !hasPermission(addon, "uninstall");

    // Set disable label and hide if not allowed.
    let toggleDisabledButton = this.querySelector('[action="toggle-disabled"]');
    let toggleDisabledAction = addon.userDisabled ? "enable" : "disable";
    document.l10n.setAttributes(
      toggleDisabledButton, `${toggleDisabledAction}-addon-button`);
    toggleDisabledButton.hidden = !hasPermission(addon, toggleDisabledAction);

    // Set the update button and badge the menu if there's an update.
    this.querySelector('[action="install-update"]').hidden = !updateInstall;

    // The separator isn't needed when expanded (nothing under it) or when the
    // remove and disable buttons are hidden (nothing above it).
    let separator = this.querySelector("panel-item-separator");
    separator.hidden = card.expanded ||
      removeButton.hidden && toggleDisabledButton.hidden;

    // Hide the expand button if we're expanded.
    this.querySelector('[action="expand"]').hidden = card.expanded;
  }
}
customElements.define("addon-options", AddonOptions);

class PluginOptions extends HTMLElement {
  connectedCallback() {
    if (this.children.length == 0) {
      this.render();
    }
  }

  render() {
    this.appendChild(importTemplate("plugin-options"));
  }

  update(card, addon) {
    let actions = [{
      action: "ask-to-activate",
      userDisabled: AddonManager.STATE_ASK_TO_ACTIVATE,
    }, {
      action: "always-activate",
      userDisabled: false,
    }, {
      action: "never-activate",
      userDisabled: true,
    }];
    for (let {action, userDisabled} of actions) {
      let el = this.querySelector(`[action="${action}"]`);
      el.checked = addon.userDisabled === userDisabled;
      el.disabled = !(el.checked || hasPermission(addon, action));
    }
  }
}
customElements.define("plugin-options", PluginOptions);

class AddonDetails extends HTMLElement {
  connectedCallback() {
    if (this.children.length == 0) {
      this.render();
    }
  }

  setAddon(addon) {
    this.addon = addon;
  }

  update() {
    let {addon} = this;

    // Show the update check button if necessary. The button might not exist if
    // the add-on doesn't support updates.
    let updateButton = this.querySelector('[action="update-check"]');
    if (updateButton) {
      updateButton.hidden =
        this.addon.updateInstall || AddonManager.shouldAutoUpdate(this.addon);
    }

    // Set the value for auto updates.
    let inputs = this.querySelectorAll(".addon-detail-row-updates input");
    for (let input of inputs) {
      input.checked = input.value == addon.applyBackgroundUpdates;
    }
  }

  async render() {
    let {addon} = this;
    if (!addon) {
      throw new Error("addon-details must be initialized by setAddon");
    }

    this.textContent = "";
    this.appendChild(importTemplate("addon-details"));

    // Full description.
    let description = this.querySelector(".addon-detail-description");
    if (addon.getFullDescription) {
      description.appendChild(addon.getFullDescription(document));
    } else if (addon.fullDescription) {
      description.appendChild(nl2br(addon.fullDescription));
    }

    // Contribute.
    if (!addon.contributionURL) {
      this.querySelector(".addon-detail-contribute").remove();
    }

    // Auto updates setting.
    if (!hasPermission(addon, "upgrade")) {
      this.querySelector(".addon-detail-row-updates").remove();
    }

    let pbRow = this.querySelector(".addon-detail-row-private-browsing");
    if (!allowPrivateBrowsingByDefault && addon.type == "extension" &&
        addon.incognito != "not_allowed") {
      let isAllowed = await isAllowedInPrivateBrowsing(addon);
      pbRow.querySelector(`[value="${isAllowed ? 1 : 0}"]`).checked = true;
      let learnMore = pbRow.nextElementSibling
        .querySelector('a[data-l10n-name="learn-more"]');
      learnMore.href = SUPPORT_URL + "extensions-pb";
    } else {
      pbRow.remove();
    }

    // Author.
    let creatorRow = this.querySelector(".addon-detail-row-author");
    if (addon.creator) {
      let creator;
      if (addon.creator.url) {
        creator = document.createElement("a");
        creator.href = addon.creator.url;
        creator.target = "_blank";
        creator.textContent = addon.creator.name;
      } else {
        creator = new Text(addon.creator.name);
      }
      creatorRow.appendChild(creator);
    } else {
      creatorRow.remove();
    }

    // Version. Don't show a version for LWTs.
    let version = this.querySelector(".addon-detail-row-version");
    if (addon.version && !/@personas\.mozilla\.org/.test(addon.id)) {
      version.appendChild(new Text(addon.version));
    } else {
      version.remove();
    }

    // Last updated.
    let updateDate = this.querySelector(".addon-detail-row-lastUpdated");
    if (addon.updateDate) {
      let lastUpdated = addon.updateDate.toLocaleDateString(undefined, {
        year: "numeric",
        month: "long",
        day: "numeric",
      });
      updateDate.appendChild(new Text(lastUpdated));
    } else {
      updateDate.remove();
    }

    // Homepage.
    let homepageRow = this.querySelector(".addon-detail-row-homepage");
    if (addon.homepageURL) {
      let homepageURL = homepageRow.querySelector("a");
      homepageURL.href = addon.homepageURL;
      homepageURL.textContent = addon.homepageURL;
    } else {
      homepageRow.remove();
    }

    // Rating.
    let ratingRow = this.querySelector(".addon-detail-row-rating");
    if (addon.averageRating) {
      let stars = ratingRow.querySelectorAll(".addon-detail-rating-star");
      for (let i = 0; i < stars.length; i++) {
        let fill = "";
        if (addon.averageRating > i) {
          fill = addon.averageRating > i + 0.5 ? "full" : "half";
        }
        stars[i].setAttribute("fill", fill);
      }
      let reviews = ratingRow.querySelector("a");
      reviews.href = addon.reviewURL;
      document.l10n.setAttributes(reviews, "addon-detail-reviews-link", {
        numberOfReviews: addon.reviewCount,
      });
    } else {
      ratingRow.remove();
    }

    this.update();
  }
}
customElements.define("addon-details", AddonDetails);

/**
 * A card component for managing an add-on. It should be initialized by setting
 * the add-on with `setAddon()` before being connected to the document.
 *
 *    let card = document.createElement("addon-card");
 *    card.setAddon(addon);
 *    document.body.appendChild(card);
 */
class AddonCard extends HTMLElement {
  connectedCallback() {
    // If we've already rendered we can just update, otherwise render.
    if (this.children.length > 0) {
      this.update();
    } else {
      this.render();
    }
    this.registerListeners();
  }

  disconnectedCallback() {
    this.removeListeners();
  }

  get expanded() {
    return this.hasAttribute("expanded");
  }

  set expanded(val) {
    if (val) {
      this.setAttribute("expanded", "true");
    } else {
      this.removeAttribute("expanded");
    }
  }

  get updateInstall() {
    return this._updateInstall;
  }

  set updateInstall(install) {
    this._updateInstall = install;
    if (this.children.length > 0) {
      this.update();
    }
  }

  get reloading() {
    return this.hasAttribute("reloading");
  }

  set reloading(val) {
    if (val) {
      this.setAttribute("reloading", "true");
    } else {
      this.removeAttribute("reloading");
    }
  }

  /**
   * Set the add-on for this card. The card will be populated based on the
   * add-on when it is connected to the DOM.
   *
   * @param {AddonWrapper} addon The add-on to use.
   */
  setAddon(addon) {
    this.addon = addon;
    let install = addon.updateInstall;
    if (install && install.state == AddonManager.STATE_AVAILABLE) {
      this.updateInstall = install;
    }
    if (this.children.length > 0) {
      this.render();
    }
  }

  /**
   * Determine which screenshot fits best into the given img element. The img
   * should have a width and height set on it.
   *
   * @param {HTMLImageElement} img The <img> the screenshot is being set on.
   */
  screenshotForImg(img) {
    let {addon} = this;
    if (addon.screenshots && addon.screenshots[0]) {
      let {width, height} = getComputedStyle(img);
      let sectionWidth = parseInt(width, 10);
      let sectionHeight = parseInt(height, 10);
      let screenshots = addon.screenshots
        // Only check screenshots with a width and height.
        .filter(s => s.width && s.height)
        // Sort the screenshots based how close their dimensions are to the
        // requested size.
        .sort((a, b) => {
          let aCloseness =
            Math.abs((a.width - sectionWidth) * (a.height - sectionHeight));
          let bCloseness =
            Math.abs((b.width - sectionWidth) * (b.height - sectionHeight));
          if (aCloseness == bCloseness) {
            return 0;
          }
          return aCloseness < bCloseness ? -1 : 1;
        });
      return screenshots[0];
    }
    return null;
  }

  async handleEvent(e) {
    let {addon} = this;
    let action = e.target.getAttribute("action");

    if (e.type == "click") {
      switch (action) {
        case "toggle-disabled":
          if (addon.userDisabled) {
            await addon.enable();
          } else {
            await addon.disable();
          }
          if (e.mozInputSource == MouseEvent.MOZ_SOURCE_KEYBOARD) {
            // Refocus the open menu button so it's clear where the focus is.
            this.querySelector('[action="more-options"]').focus();
          }
          break;
        case "ask-to-activate":
          if (hasPermission(addon, "ask-to-activate")) {
            addon.userDisabled = AddonManager.STATE_ASK_TO_ACTIVATE;
          }
          break;
        case "always-activate":
          addon.userDisabled = false;
          break;
        case "never-activate":
          addon.userDisabled = true;
          break;
        case "update-check":
          let listener = {
            onUpdateAvailable(addon, install) {
              attachUpdateHandler(install);
            },
            onNoUpdateAvailable: () => {
              this.sendEvent("no-update");
            },
          };
          addon.findUpdates(listener, AddonManager.UPDATE_WHEN_USER_REQUESTED);
          break;
        case "install-update":
          this.updateInstall.install().then(() => {
            // The card will update with the new add-on when it gets installed.
            this.sendEvent("update-installed");
          }, () => {
            // Update our state if the install is cancelled.
            this.update();
            this.sendEvent("update-cancelled");
          });
          // Clear the install since it will be removed from the global list of
          // available updates (whether it succeeds or fails).
          this.updateInstall = null;
          break;
        case "contribute":
          windowRoot.ownerGlobal.openUILinkIn(addon.contributionURL, "tab", {
            triggeringPrincipal:
              Services.scriptSecurityManager.createNullPrincipal({}),
          });
          break;
        case "remove":
          {
            this.panel.hide();
            let response = windowRoot.ownerGlobal.promptRemoveExtension(addon);
            if (response == 0) {
              await addon.uninstall();
              this.sendEvent("remove");
            } else {
              this.sendEvent("remove-cancelled");
            }
          }
          break;
        case "expand":
          loadViewFn("detail", this.addon.id);
          break;
        case "more-options":
          // Open panel on click from the keyboard.
          if (e.mozInputSource == MouseEvent.MOZ_SOURCE_KEYBOARD) {
            this.panel.toggle(e);
          }
          break;
      }
    } else if (e.type == "change") {
      let {name} = e.target;
      if (name == "autoupdate") {
        addon.applyBackgroundUpdates = e.target.value;
      } else if (name == "private-browsing") {
        let policy = WebExtensionPolicy.getByID(addon.id);
        let extension = policy && policy.extension;

        if (e.target.value == "1") {
          await ExtensionPermissions.add(
            addon.id, PRIVATE_BROWSING_PERMS, extension);
        } else {
          await ExtensionPermissions.remove(
            addon.id, PRIVATE_BROWSING_PERMS, extension);
        }
        // Reload the extension if it is already enabled. This ensures any
        // change on the private browsing permission is properly handled.
        if (addon.isActive) {
          this.reloading = true;
          // Reloading will trigger an enable and update the card.
          addon.reload();
        } else {
          // Update the card if the add-on isn't active.
          this.update();
        }
      }
    } else if (e.type == "mousedown") {
      // Open panel on mousedown when the mouse is used.
      if (action == "more-options") {
        this.panel.toggle(e);
      }
    } else if (e.type == "dblclick") {
      // Don't expand if expanded or a button is double clicked.
      if (!this.expanded && e.target.tagName != "BUTTON") {
        loadViewFn("detail", this.addon.id);
      }
    }
  }

  get panel() {
    return this.card.querySelector("panel-list");
  }

  registerListeners() {
    this.addEventListener("change", this);
    this.addEventListener("click", this);
    this.addEventListener("dblclick", this);
    this.addEventListener("mousedown", this);
  }

  removeListeners() {
    this.removeEventListener("change", this);
    this.removeEventListener("click", this);
    this.removeEventListener("dblclick", this);
    this.removeEventListener("mousedown", this);
  }

  onNewInstall(install) {
    this.updateInstall = install;
    this.sendEvent("update-found");
  }

  onInstallEnded(install) {
    this.setAddon(install.addon);
  }

  onDisabled(addon) {
    if (!this.reloading) {
      this.update();
    }
  }

  onEnabled(addon) {
    this.reloading = false;
    this.update();
  }

  onUpdateModeChanged() {
    this.update();
  }

  onPropertyChanged(addon, changed) {
    if (this.details && changed.includes("applyBackgroundUpdates")) {
      this.details.update();
    } else if (addon.type == "plugin" && changed.includes("userDisabled")) {
      this.update();
    }
  }

  /**
   * Update the card's contents based on the previously set add-on. This should
   * be called if there has been a change to the add-on.
   */
  update() {
    let {addon, card} = this;

    // Update the icon.
    let icon;
    if (addon.type == "plugin") {
      icon = PLUGIN_ICON_URL;
    } else {
      icon = AddonManager.getPreferredIconURL(addon, 32, window);
    }
    card.querySelector(".addon-icon").src = icon;

    // Update the theme preview.
    let preview = card.querySelector(".card-heading-image");
    preview.hidden = true;
    if (addon.type == "theme") {
      let screenshot = this.screenshotForImg(preview);
      if (screenshot) {
        preview.src = screenshot.url;
        preview.hidden = false;
      }
    }

    // Update the name.
    let name = card.querySelector(".addon-name");
    if (addon.isActive) {
      name.textContent = addon.name;
      name.removeAttribute("data-l10n-id");
    } else {
      document.l10n.setAttributes(name, "addon-name-disabled", {
        name: addon.name,
      });
    }
    name.title = `${addon.name} ${addon.version}`;

    // Set the items in the more options menu.
    this.options.update(this, addon, this.updateInstall);

    // Badge the more options menu if there's an update.
    card.querySelector(".more-options-button")
      .classList.toggle("more-options-button-badged", !!this.updateInstall);

    // Set the private browsing badge visibility.
    if (!allowPrivateBrowsingByDefault && addon.type == "extension" &&
        addon.incognito != "not_allowed") {
      // Keep update synchronous, the badge can appear later.
      isAllowedInPrivateBrowsing(addon).then(isAllowed => {
        card.querySelector(".addon-badge-private-browsing-allowed")
          .hidden = !isAllowed;
      });
    }

    // Update description.
    card.querySelector(".addon-description").textContent = addon.description;

    // Update the details if they're shown.
    if (this.details) {
      this.details.update();
    }

    this.sendEvent("update");
  }

  expand() {
    if (this.children.length == 0) {
      this.expanded = true;
    } else {
      throw new Error("expand() is only supported before render()");
    }
  }

  render() {
    this.textContent = "";

    let {addon} = this;
    if (!addon) {
      throw new Error("addon-card must be initialized with setAddon()");
    }

    this.card = importTemplate("card").firstElementChild;
    this.setAttribute("addon-id", addon.id);

    let panelType = addon.type == "plugin" ? "plugin-options" : "addon-options";
    this.options = document.createElement(panelType);
    this.options.render();
    this.card.querySelector(".more-options-menu").appendChild(this.options);

    // Set the contents.
    this.update();

    let doneRenderPromise = Promise.resolve();
    if (this.expanded) {
      if (!this.details) {
        this.details = document.createElement("addon-details");
      }
      this.details.setAddon(this.addon);
      doneRenderPromise = this.details.render();

      // If we're re-rendering we still need to append the details since the
      // entire card was emptied at the beginning of the render.
      this.card.appendChild(this.details);
    }

    this.appendChild(this.card);

    // Return the promise of details rendering to wait on in DetailView.
    return doneRenderPromise;
  }

  sendEvent(name, detail) {
    this.dispatchEvent(new CustomEvent(name, {detail}));
  }
}
customElements.define("addon-card", AddonCard);

/**
 * A list view for add-ons of a certain type. It should be initialized with the
 * type of add-on to render and have section data set before being connected to
 * the document.
 *
 *    let list = document.createElement("addon-list");
 *    list.type = "plugin";
 *    list.setSections([{
 *      headingId: "plugin-section-heading",
 *      filterFn: addon => !addon.isSystem,
 *    }]);
 *    document.body.appendChild(list);
 */
class AddonList extends HTMLElement {
  constructor() {
    super();
    this.sections = [];
  }

  async connectedCallback() {
    // Register the listener and get the add-ons, these operations should
    // happpen as close to each other as possible.
    this.registerListener();
    // Don't render again if we were rendered prior to being inserted.
    if (this.children.length == 0) {
      // Render the initial view.
      this.render();
    }
  }

  disconnectedCallback() {
    // Remove content and stop listening until this is connected again.
    this.textContent = "";
    this.removeListener();
  }

  /**
   * Configure the sections in the list.
   *
   * @param {object[]} sections
   *        The options for the section. Each entry in the array should have:
   *          headingId: The fluent id for the section's heading.
   *          filterFn: A function that determines if an add-on belongs in
   *                    the section.
   */
  setSections(sections) {
    this.sections = sections.map(section => Object.assign({}, section));
  }

  /**
   * Set the add-on type for this list. This will be used to filter the add-ons
   * that are displayed.
   *
   * @param {string} val The type to filter on.
   */
  set type(val) {
    this.setAttribute("type", val);
  }

  get type() {
    return this.getAttribute("type");
  }

  getSection(index) {
    return this.sections[index].node;
  }

  getCards(section) {
    return section.querySelectorAll("addon-card");
  }

  getCard(addon) {
    return this.querySelector(`addon-card[addon-id="${addon.id}"]`);
  }

  sortByFn(aAddon, bAddon) {
    return aAddon.name.localeCompare(bAddon.name);
  }

  async getAddons() {
    if (!this.type) {
      throw new Error(`type must be set to find add-ons`);
    }

    // Find everything matching our type.
    let addons = await AddonManager.getAddonsByTypes([this.type]);

    // Sort by name.
    addons.sort(this.sortByFn);

    // Put the add-ons into the sections, an add-on goes in the first section
    // that it matches the filterFn for. It might not go in any section.
    let sectionedAddons = this.sections.map(() => []);
    for (let addon of addons) {
      let index = this.sections.findIndex(({filterFn}) => filterFn(addon));
      if (index != -1) {
        sectionedAddons[index].push(addon);
      }
    }

    return sectionedAddons;
  }

  createSectionHeading(headingIndex) {
    let {headingId} = this.sections[headingIndex];
    let heading = document.createElement("h2");
    heading.classList.add("list-section-heading");
    document.l10n.setAttributes(heading, headingId);
    return heading;
  }

  updateSectionIfEmpty(section) {
    // The header is added before any add-on cards, so if there's only one
    // child then it's the header. In that case we should empty out the section.
    if (section.children.length == 1) {
      section.textContent = "";
    }
  }

  insertCardInto(card, sectionIndex) {
    let section = this.getSection(sectionIndex);
    let sectionCards = this.getCards(section);

    // If this is the first card in the section, create the heading.
    if (sectionCards.length == 0) {
      section.appendChild(this.createSectionHeading(sectionIndex));
    }

    // Find where to insert the card.
    let insertBefore = Array.from(sectionCards).find(
      otherCard => this.sortByFn(card.addon, otherCard.addon) < 0);
    // This will append if insertBefore is null.
    section.insertBefore(card, insertBefore || null);
  }

  addAddon(addon) {
    // Only insert add-ons of the right type.
    if (addon.type != this.type) {
      this.sendEvent("skip-add", "type-mismatch");
      return;
    }

    let insertSection = this.sections.findIndex(
      ({filterFn}) => filterFn(addon));

    // Don't add the add-on if it doesn't go in a section.
    if (insertSection == -1) {
      return;
    }

    // Create and insert the card.
    let card = document.createElement("addon-card");
    card.setAddon(addon);
    this.insertCardInto(card, insertSection);
    this.sendEvent("add", {id: addon.id});
  }

  sendEvent(name, detail) {
    this.dispatchEvent(new CustomEvent(name, {detail}));
  }

  removeAddon(addon) {
    let card = this.getCard(addon);
    if (card) {
      let section = card.parentNode;
      card.remove();
      this.updateSectionIfEmpty(section);
      this.sendEvent("remove", {id: addon.id});
    }
  }

  updateAddon(addon) {
    let card = this.getCard(addon);
    if (card) {
      let sectionIndex = this.sections.findIndex(s => s.filterFn(addon));
      if (sectionIndex != -1) {
        // Move the card, if needed. This will allow an animation between
        // page sections and provides clearer events for testing.
        if (card.parentNode.getAttribute("section") != sectionIndex) {
          let oldSection = card.parentNode;
          this.insertCardInto(card, sectionIndex);
          this.updateSectionIfEmpty(oldSection);
          this.sendEvent("move", {id: addon.id});
        }
      } else {
        this.removeAddon(addon);
      }
    } else {
      // Add the add-on, this will do nothing if it shouldn't be in the list.
      this.addAddon(addon);
    }
  }

  renderSection(addons, index) {
    let section = document.createElement("section");
    section.setAttribute("section", index);

    // Render the heading and add-ons if there are any.
    if (addons.length > 0) {
      section.appendChild(this.createSectionHeading(index));

      for (let addon of addons) {
        let card = document.createElement("addon-card");
        card.setAddon(addon);
        card.render();
        section.appendChild(card);
      }
    }

    return section;
  }

  async render() {
    this.textContent = "";

    let sectionedAddons = await this.getAddons();

    // Render the sections.
    let frag = document.createDocumentFragment();

    for (let i = 0; i < sectionedAddons.length; i++) {
      this.sections[i].node = this.renderSection(sectionedAddons[i], i);
      frag.appendChild(this.sections[i].node);
    }

    // Make sure fluent has set all the strings before we render. This will
    // avoid the height changing as strings go from 0 height to having text.
    await document.l10n.translateFragment(frag);
    this.appendChild(frag);
  }

  registerListener() {
    AddonManager.addAddonListener(this);
  }

  removeListener() {
    AddonManager.removeAddonListener(this);
  }

  onEnabled(addon) {
    this.updateAddon(addon);
  }

  onDisabled(addon) {
    this.updateAddon(addon);
  }

  onInstalled(addon) {
    if (this.querySelector(`addon-card[addon-id="${addon.id}"]`)) {
      return;
    }
    this.addAddon(addon);
  }

  onUninstalled(addon) {
    this.removeAddon(addon);
  }
}
customElements.define("addon-list", AddonList);

class ListView {
  constructor({param, root}) {
    this.type = param;
    this.root = root;
  }

  async render() {
    let list = document.createElement("addon-list");
    list.type = this.type;
    list.setSections([{
      headingId: "addons-enabled-heading",
      filterFn: addon => !addon.hidden && addon.isActive,
    }, {
      headingId: "addons-disabled-heading",
      filterFn: addon => !addon.hidden && !addon.isActive,
    }]);

    await list.render();
    this.root.textContent = "";
    this.root.appendChild(list);
  }
}

class DetailView {
  constructor({param, root}) {
    this.id = param;
    this.root = root;
  }

  async render() {
    let addon = await AddonManager.getAddonByID(this.id);
    let card = document.createElement("addon-card");

    // Go back to the list view when the add-on is removed.
    card.addEventListener("remove", () => loadViewFn("list", addon.type));

    card.setAddon(addon);
    card.expand();
    await card.render();

    this.root.textContent = "";
    this.root.appendChild(card);
  }
}

// Generic view management.
let root = null;

/**
 * Called from extensions.js once, when about:addons is loading.
 */
function initialize(opts) {
  root = document.getElementById("main");
  loadViewFn = opts.loadViewFn;
  AddonCardListenerHandler.startup();
  window.addEventListener("unload", () => {
    // Clear out the root node so the disconnectedCallback will trigger
    // properly and all of the custom elements can cleanup.
    root.textContent = "";
    AddonCardListenerHandler.shutdown();
  }, {once: true});
}

/**
 * Called from extensions.js to load a view. The view's render method should
 * resolve once the view has been updated to conform with other about:addons
 * views.
 */
async function show(type, param) {
  if (type == "list") {
    await new ListView({param, root}).render();
  } else if (type == "detail") {
    await new DetailView({param, root}).render();
  }
}

function hide() {
  root.textContent = "";
}
