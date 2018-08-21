/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const EXPORTED_SYMBOLS = ["LinkHandlerChild"];

ChromeUtils.import("resource://gre/modules/Services.jsm");
ChromeUtils.import("resource://gre/modules/ActorChild.jsm");

ChromeUtils.defineModuleGetter(this, "Feeds",
  "resource:///modules/Feeds.jsm");
ChromeUtils.defineModuleGetter(this, "FaviconLoader",
  "resource:///modules/FaviconLoader.jsm");

class LinkHandlerChild extends ActorChild {
  constructor(mm) {
    super(mm);

    this.seenTabIcon = false;
    this._iconLoader = null;
  }

  get iconLoader() {
    if (!this._iconLoader) {
      this._iconLoader = new FaviconLoader(this.mm);
    }
    return this._iconLoader;
  }

  addRootIcon() {
    if (!this.seenTabIcon && Services.prefs.getBoolPref("browser.chrome.guess_favicon", true) &&
        Services.prefs.getBoolPref("browser.chrome.site_icons", true)) {
      // Inject the default icon. Use documentURIObject so that we do the right
      // thing with about:-style error pages. See bug 453442
      let baseURI = this.content.document.documentURIObject;
      if (["http", "https"].includes(baseURI.scheme)) {
        this.seenTabIcon = true;
        this.iconLoader.addDefaultIcon(baseURI);
      }
    }
  }

  onHeadParsed(event) {
    if (event.target.ownerDocument != this.content.document) {
      return;
    }

    // Per spec icons are meant to be in the <head> tag so we should have seen
    // all the icons now so add the root icon if no other tab icons have been
    // seen.
    this.addRootIcon();

    // We're likely done with icon parsing so load the pending icons now.
    if (this._iconLoader) {
      this._iconLoader.onPageShow();
    }
  }

  onPageShow(event) {
    if (event.target != this.content.document) {
      return;
    }

    this.addRootIcon();

    if (this._iconLoader) {
      this._iconLoader.onPageShow();
    }
  }

  onPageHide(event) {
    if (event.target != this.content.document) {
      return;
    }

    if (this._iconLoader) {
      this._iconLoader.onPageHide();
    }

    this.seenTabIcon = false;
  }

  onLinkEvent(event) {
    let link = event.target;
    // Ignore sub-frames (bugs 305472, 479408).
    if (link.ownerGlobal != this.content) {
      return;
    }

    let rel = link.rel && link.rel.toLowerCase();
    if (!rel || !link.href)
      return;

    // Note: following booleans only work for the current link, not for the
    // whole content
    let feedAdded = false;
    let iconAdded = false;
    let searchAdded = false;
    let rels = {};
    for (let relString of rel.split(/\s+/))
      rels[relString] = true;

    for (let relVal in rels) {
      let isRichIcon = false;

      switch (relVal) {
        case "feed":
        case "alternate":
          if (!feedAdded && event.type == "DOMLinkAdded") {
            if (!rels.feed && rels.alternate && rels.stylesheet)
              break;

            if (Feeds.isValidFeed(link, link.ownerDocument.nodePrincipal, "feed" in rels)) {
              this.mm.sendAsyncMessage("Link:AddFeed", {
                type: link.type,
                href: link.href,
                title: link.title,
              });
              feedAdded = true;
            }
          }
          break;
        case "apple-touch-icon":
        case "apple-touch-icon-precomposed":
        case "fluid-icon":
          isRichIcon = true;
        case "icon":
          if (iconAdded || link.hasAttribute("mask")) { // Masked icons are not supported yet.
            break;
          }

          if (!Services.prefs.getBoolPref("browser.chrome.site_icons", true)) {
            return;
          }

          let iconInfo = FaviconLoader.makeFaviconFromLink(link, isRichIcon);
          if (iconInfo) {
            iconAdded = true;
            if (!isRichIcon) {
              this.seenTabIcon = true;
            }
            this.iconLoader.addIcon(iconInfo);
          }
          break;
        case "search":
          if (Services.policies && !Services.policies.isAllowed("installSearchEngine")) {
            break;
          }

          if (!searchAdded && event.type == "DOMLinkAdded") {
            let type = link.type && link.type.toLowerCase();
            type = type.replace(/^\s+|\s*(?:;.*)?$/g, "");

            let re = /^(?:https?|ftp):/i;
            if (type == "application/opensearchdescription+xml" && link.title &&
                re.test(link.href)) {
              let engine = { title: link.title, href: link.href };
              this.mm.sendAsyncMessage("Link:AddSearch", {
                engine,
                url: link.ownerDocument.documentURI,
              });
              searchAdded = true;
            }
          }
          break;
      }
    }
  }

  handleEvent(event) {
    switch (event.type) {
      case "pageshow":
        return this.onPageShow(event);
      case "pagehide":
        return this.onPageHide(event);
      case "DOMHeadElementParsed":
        return this.onHeadParsed(event);
      default:
        return this.onLinkEvent(event);
    }
  }
}
