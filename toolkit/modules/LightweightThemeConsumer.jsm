/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

var EXPORTED_SYMBOLS = ["LightweightThemeConsumer"];

ChromeUtils.import("resource://gre/modules/XPCOMUtils.jsm");
ChromeUtils.import("resource://gre/modules/Services.jsm");
ChromeUtils.import("resource://gre/modules/AppConstants.jsm");

const toolkitVariableMap = [
  ["--lwt-accent-color", {
    lwtProperty: "accentcolor",
    processColor(rgbaChannels, element) {
      if (!rgbaChannels) {
        return "white";
      }
      // Remove the alpha channel
      const {r, g, b} = rgbaChannels;
      return `rgb(${r}, ${g}, ${b})`;
    }
  }],
  ["--lwt-text-color", {
    lwtProperty: "textcolor",
    processColor(rgbaChannels, element) {
      if (!rgbaChannels) {
        element.removeAttribute("lwthemetextcolor");
        element.removeAttribute("lwtheme");
        return null;
      }
      const {r, g, b, a} = rgbaChannels;
      const luminance = 0.2125 * r + 0.7154 * g + 0.0721 * b;
      element.setAttribute("lwthemetextcolor", luminance <= 110 ? "dark" : "bright");
      element.setAttribute("lwtheme", "true");
      return `rgba(${r}, ${g}, ${b}, ${a})` || "black";
    }
  }],
  ["--arrowpanel-background", {
    lwtProperty: "popup"
  }],
  ["--arrowpanel-color", {
    lwtProperty: "popup_text"
  }],
  ["--arrowpanel-border-color", {
    lwtProperty: "popup_border"
  }],
];

// Get the theme variables from the app resource directory.
// This allows per-app variables.
ChromeUtils.import("resource:///modules/ThemeVariableMap.jsm");

ChromeUtils.defineModuleGetter(this, "LightweightThemeImageOptimizer",
  "resource://gre/modules/addons/LightweightThemeImageOptimizer.jsm");

function LightweightThemeConsumer(aDocument) {
  this._doc = aDocument;
  this._win = aDocument.defaultView;

  Services.obs.addObserver(this, "lightweight-theme-styling-update");

  var temp = {};
  ChromeUtils.import("resource://gre/modules/LightweightThemeManager.jsm", temp);
  this._update(temp.LightweightThemeManager.currentThemeForDisplay);
  this._win.addEventListener("unload", this, { once: true });
}

LightweightThemeConsumer.prototype = {
  _lastData: null,
  // Whether the active lightweight theme should be shown on the window.
  _enabled: true,
  // Whether a lightweight theme is enabled.
  _active: false,

  enable() {
    this._enabled = true;
    this._update(this._lastData);
  },

  disable() {
    // Dance to keep the data, but reset the applied styles:
    let lastData = this._lastData;
    this._update(null);
    this._enabled = false;
    this._lastData = lastData;
  },

  getData() {
    return this._enabled ? Cu.cloneInto(this._lastData, this._win) : null;
  },

  observe(aSubject, aTopic, aData) {
    if (aTopic != "lightweight-theme-styling-update")
      return;

    const { outerWindowID } = this._win
      .QueryInterface(Ci.nsIInterfaceRequestor)
      .getInterface(Ci.nsIDOMWindowUtils);

    const parsedData = JSON.parse(aData);
    if (parsedData && parsedData.window && parsedData.window !== outerWindowID) {
      return;
    }

    this._update(parsedData);
  },

  handleEvent(aEvent) {
    switch (aEvent.type) {
      case "unload":
        Services.obs.removeObserver(this, "lightweight-theme-styling-update");
        this._win = this._doc = null;
        break;
    }
  },

  _update(aData) {
    if (!aData) {
      aData = { headerURL: "", footerURL: "", textcolor: "", accentcolor: "" };
      this._lastData = aData;
    } else {
      this._lastData = aData;
      aData = LightweightThemeImageOptimizer.optimize(aData, this._win.screen);
    }
    if (!this._enabled)
      return;

    let root = this._doc.documentElement;

    if (aData.headerURL) {
      root.setAttribute("lwtheme-image", "true");
    } else {
      root.removeAttribute("lwtheme-image");
    }

    let active = !!aData.accentcolor;
    this._active = active;

    if (aData.icons) {
      let activeIcons = active ? Object.keys(aData.icons).join(" ") : "";
      root.setAttribute("lwthemeicons", activeIcons);
      for (let [name, value] of Object.entries(aData.icons)) {
        _setImage(root, active, name, value);
      }
    } else {
      root.removeAttribute("lwthemeicons");
    }

    _setImage(root, active, "--lwt-header-image", aData.headerURL);
    _setImage(root, active, "--lwt-footer-image", aData.footerURL);
    _setImage(root, active, "--lwt-additional-images", aData.additionalBackgrounds);
    _setProperties(root, active, aData);

    if (active && aData.footerURL)
      root.setAttribute("lwthemefooter", "true");
    else
      root.removeAttribute("lwthemefooter");

    Services.obs.notifyObservers(this._win, "lightweight-theme-window-updated",
                                 JSON.stringify(aData));
  }
};

function _setImage(aRoot, aActive, aVariableName, aURLs) {
  if (aURLs && !Array.isArray(aURLs)) {
    aURLs = [aURLs];
  }
  _setProperty(aRoot, aActive, aVariableName, aURLs && aURLs.map(v => `url("${v.replace(/"/g, '\\"')}")`).join(","));
}

function _setProperty(elem, active, variableName, value) {
  if (active && value) {
    elem.style.setProperty(variableName, value);
  } else {
    elem.style.removeProperty(variableName);
  }
}

function _setProperties(root, active, themeData) {
  for (let map of [toolkitVariableMap, ThemeVariableMap]) {
    for (let [cssVarName, definition] of map) {
      const {
        lwtProperty,
        optionalElementID,
        processColor,
        isColor = true
      } = definition;
      let elem = optionalElementID ? root.ownerDocument.getElementById(optionalElementID)
                                   : root;

      let val = themeData[lwtProperty];
      if (isColor) {
        val = _sanitizeCSSColor(root.ownerDocument, val);
        if (processColor) {
          val = processColor(_parseRGBA(val), elem);
        }
      }
      _setProperty(elem, active, cssVarName, val);
    }
  }
}

function _sanitizeCSSColor(doc, cssColor) {
  if (!cssColor) {
    return null;
  }
  // style.color normalizes color values and rejects invalid ones, so a
  // simple round trip gets us a sanitized color value.
  let span = doc.createElementNS("http://www.w3.org/1999/xhtml", "span");
  span.style.color = cssColor;
  cssColor = doc.defaultView.getComputedStyle(span).color;
  if (cssColor == "rgba(0, 0, 0, 0)") {
    return null;
  }
  return cssColor;
}

function _parseRGBA(aColorString) {
  if (!aColorString) {
    return null;
  }
  var rgba = aColorString.replace(/(rgba?\()|(\)$)/g, "").split(",");
  rgba = rgba.map(x => parseFloat(x));
  return {
    r: rgba[0],
    g: rgba[1],
    b: rgba[2],
    a: rgba[3] || 1,
  };
}
