// -*- indent-tabs-mode: nil; js-indent-level: 2 -*-
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

var EXPORTED_SYMBOLS = ["AboutReaderParent"];

const { Services } = ChromeUtils.import("resource://gre/modules/Services.jsm");

ChromeUtils.defineModuleGetter(
  this,
  "PlacesUtils",
  "resource://gre/modules/PlacesUtils.jsm"
);
ChromeUtils.defineModuleGetter(
  this,
  "ReaderMode",
  "resource://gre/modules/ReaderMode.jsm"
);

const gStringBundle = Services.strings.createBundle(
  "chrome://global/locale/aboutReader.properties"
);

// A set of all of the AboutReaderParent actors that exist.
// See bug 1631146 for a request for a less manual way of doing this.
let gAllActors = new Set();

// A map of message names to listeners that listen to messages
// received by the AboutReaderParent actors.
let gListeners = new Map();

class AboutReaderParent extends JSWindowActorParent {
  didDestroy() {
    gAllActors.delete(this);
  }

  isReaderMode(browser) {
    return browser.currentURI.spec.startsWith("about:reader");
  }

  static addMessageListener(name, listener) {
    if (!gListeners.has(name)) {
      gListeners.set(name, new Set([listener]));
    } else {
      gListeners.get(name).add(listener);
    }
  }

  static removeMessageListener(name, listener) {
    if (!gListeners.has(name)) {
      return;
    }

    gListeners.get(name).delete(listener);
  }

  static broadcastAsyncMessage(name, data) {
    for (let actor of gAllActors) {
      // Ignore errors for actors that might not be valid yet or anymore.
      try {
        actor.sendAsyncMessage(name, data);
      } catch (ex) {}
    }
  }

  callListeners(message) {
    let listeners = gListeners.get(message.name);
    if (!listeners) {
      return;
    }

    message.target = this.browsingContext.embedderElement;
    for (let listener of listeners.values()) {
      try {
        listener.receiveMessage(message);
      } catch (e) {
        Cu.reportError(e);
      }
    }
  }

  async receiveMessage(message) {
    switch (message.name) {
      case "Reader:FaviconRequest": {
        try {
          let preferredWidth = message.data.preferredWidth || 0;
          let uri = Services.io.newURI(message.data.url);

          let result = await new Promise(resolve => {
            PlacesUtils.favicons.getFaviconURLForPage(
              uri,
              iconUri => {
                if (iconUri) {
                  iconUri = PlacesUtils.favicons.getFaviconLinkForIcon(iconUri);
                  resolve({
                    url: message.data.url,
                    faviconUrl: iconUri.pathQueryRef.replace(/^favicon:/, ""),
                  });
                } else {
                  resolve(null);
                }
              },
              preferredWidth
            );
          });

          this.callListeners(message);
          return result;
        } catch (ex) {
          Cu.reportError(
            "Error requesting favicon URL for about:reader content: " + ex
          );
        }

        break;
      }

      case "Reader:UpdateReaderButton": {
        let browser = this.browsingContext.embedderElement;
        if (!browser) {
          return undefined;
        }

        if (browser.outerBrowser) {
          browser = browser.outerBrowser; // handle RDM mode
        }

        if (message.data && message.data.isArticle !== undefined) {
          browser.isArticle = message.data.isArticle;
        }
        this.updateReaderButton(browser);
        this.callListeners(message);
        break;
      }

      default:
        this.callListeners(message);
        break;
    }

    return undefined;
  }

  static updateReaderButton(browser) {
    let windowGlobal = browser.browsingContext.currentWindowGlobal;
    let actor = windowGlobal.getActor("AboutReader");
    actor.updateReaderButton(browser);
  }

  updateReaderButton(browser) {
    let tabBrowser = browser.getTabBrowser();
    if (!tabBrowser || browser != tabBrowser.selectedBrowser) {
      return;
    }

    let win = browser.ownerGlobal;

    let button = win.document.getElementById("reader-mode-button");
    let menuitem = win.document.getElementById("menu_readerModeItem");
    let key = win.document.getElementById("key_toggleReaderMode");
    if (this.isReaderMode(browser)) {
      gAllActors.add(this);

      let closeText = gStringBundle.GetStringFromName("readerView.close");

      button.setAttribute("readeractive", true);
      button.hidden = false;
      button.setAttribute("aria-label", closeText);

      menuitem.setAttribute("label", closeText);
      menuitem.setAttribute("hidden", false);
      menuitem.setAttribute(
        "accesskey",
        gStringBundle.GetStringFromName("readerView.close.accesskey")
      );

      key.setAttribute("disabled", false);

      Services.obs.notifyObservers(null, "reader-mode-available");
    } else {
      let enterText = gStringBundle.GetStringFromName("readerView.enter");

      button.removeAttribute("readeractive");
      button.hidden = !browser.isArticle;
      button.setAttribute("aria-label", enterText);

      menuitem.setAttribute("label", enterText);
      menuitem.setAttribute("hidden", !browser.isArticle);
      menuitem.setAttribute(
        "accesskey",
        gStringBundle.GetStringFromName("readerView.enter.accesskey")
      );

      key.setAttribute("disabled", !browser.isArticle);

      if (browser.isArticle) {
        Services.obs.notifyObservers(null, "reader-mode-available");
      }
    }
  }

  static forceShowReaderIcon(browser) {
    browser.isArticle = true;
    AboutReaderParent.updateReaderButton(browser);
  }

  static buttonClick(event) {
    if (event.button != 0) {
      return;
    }
    AboutReaderParent.toggleReaderMode(event);
  }

  static toggleReaderMode(event) {
    let win = event.target.ownerGlobal;
    if (win.gBrowser) {
      let browser = win.gBrowser.selectedBrowser;

      let windowGlobal = browser.browsingContext.currentWindowGlobal;
      let actor = windowGlobal.getActor("AboutReader");
      if (actor) {
        if (actor.isReaderMode(browser)) {
          gAllActors.delete(this);
        }
        actor.sendAsyncMessage("Reader:ToggleReaderMode", {});
      }
    }
  }

  /**
   * Gets an article for a given URL. This method will download and parse a document.
   *
   * @param url The article URL.
   * @param browser The browser where the article is currently loaded.
   * @return {Promise}
   * @resolves JS object representing the article, or null if no article is found.
   */
  async _getArticle(url, browser) {
    return ReaderMode.downloadAndParseDocument(url).catch(e => {
      if (e && e.newURL) {
        // Pass up the error so we can navigate the browser in question to the new URL:
        throw e;
      }
      Cu.reportError("Error downloading and parsing document: " + e);
      return null;
    });
  }
}
