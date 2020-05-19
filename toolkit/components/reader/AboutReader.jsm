/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

var EXPORTED_SYMBOLS = ["AboutReader"];

const { AppConstants } = ChromeUtils.import(
  "resource://gre/modules/AppConstants.jsm"
);
const { ReaderMode } = ChromeUtils.import(
  "resource://gre/modules/ReaderMode.jsm"
);
const { Services } = ChromeUtils.import("resource://gre/modules/Services.jsm");

ChromeUtils.defineModuleGetter(
  this,
  "AsyncPrefs",
  "resource://gre/modules/AsyncPrefs.jsm"
);
ChromeUtils.defineModuleGetter(
  this,
  "NarrateControls",
  "resource://gre/modules/narrate/NarrateControls.jsm"
);
ChromeUtils.defineModuleGetter(
  this,
  "UITelemetry",
  "resource://gre/modules/UITelemetry.jsm"
);
ChromeUtils.defineModuleGetter(
  this,
  "PluralForm",
  "resource://gre/modules/PluralForm.jsm"
);

var gStrings = Services.strings.createBundle(
  "chrome://global/locale/aboutReader.properties"
);

const zoomOnCtrl =
  Services.prefs.getIntPref("mousewheel.with_control.action", 3) == 3;
const zoomOnMeta =
  Services.prefs.getIntPref("mousewheel.with_meta.action", 1) == 3;

const gIsFirefoxDesktop =
  Services.appinfo.ID == "{ec8030f7-c20a-464f-9b0e-13a3a9e97384}";

var AboutReader = function(actor, articlePromise) {
  let win = actor.contentWindow;
  let url = this._getOriginalUrl(win);
  if (!(url.startsWith("http://") || url.startsWith("https://"))) {
    let errorMsg =
      "Only http:// and https:// URLs can be loaded in about:reader.";
    if (Services.prefs.getBoolPref("reader.errors.includeURLs")) {
      errorMsg += " Tried to load: " + url + ".";
    }
    Cu.reportError(errorMsg);
    win.location.href = "about:blank";
    return;
  }

  let doc = win.document;

  this._actor = actor;

  this._docRef = Cu.getWeakReference(doc);
  this._winRef = Cu.getWeakReference(win);
  this._innerWindowId = win.windowUtils.currentInnerWindowID;

  this._article = null;
  this._languagePromise = new Promise(resolve => {
    this._foundLanguage = resolve;
  });

  if (articlePromise) {
    this._articlePromise = articlePromise;
  }

  this._headerElementRef = Cu.getWeakReference(
    doc.querySelector(".reader-header")
  );
  this._domainElementRef = Cu.getWeakReference(
    doc.querySelector(".reader-domain")
  );
  this._titleElementRef = Cu.getWeakReference(
    doc.querySelector(".reader-title")
  );
  this._readTimeElementRef = Cu.getWeakReference(
    doc.querySelector(".reader-estimated-time")
  );
  this._creditsElementRef = Cu.getWeakReference(
    doc.querySelector(".reader-credits")
  );
  this._contentElementRef = Cu.getWeakReference(
    doc.querySelector(".moz-reader-content")
  );
  this._toolbarContainerElementRef = Cu.getWeakReference(
    doc.querySelector(".toolbar-container")
  );
  this._toolbarElementRef = Cu.getWeakReference(
    doc.querySelector(".reader-controls")
  );
  this._messageElementRef = Cu.getWeakReference(
    doc.querySelector(".reader-message")
  );
  this._containerElementRef = Cu.getWeakReference(
    doc.querySelector(".container")
  );

  this._scrollOffset = win.pageYOffset;

  doc.addEventListener("mousedown", this);
  doc.addEventListener("click", this);
  doc.addEventListener("touchstart", this);

  win.addEventListener("pagehide", this);
  win.addEventListener("mozvisualscroll", this, { mozSystemGroup: true });
  win.addEventListener("resize", this);
  win.addEventListener("wheel", this, { passive: false });

  Services.obs.addObserver(this, "inner-window-destroyed");

  doc.addEventListener("visibilitychange", this);

  this._setupStyleDropdown();
  this._setupButton(
    "close-button",
    this._onReaderClose.bind(this),
    "aboutReader.toolbar.close"
  );

  if (gIsFirefoxDesktop) {
    // we're ready for any external setup, send a signal for that.
    this._actor.sendAsyncMessage("Reader:OnSetup");
  }

  let colorSchemeValues = JSON.parse(
    Services.prefs.getCharPref("reader.color_scheme.values")
  );
  let colorSchemeOptions = colorSchemeValues.map(value => {
    return {
      name: gStrings.GetStringFromName("aboutReader.colorScheme." + value),
      value,
      itemClass: value + "-button",
    };
  });

  let colorScheme = Services.prefs.getCharPref("reader.color_scheme");
  this._setupSegmentedButton(
    "color-scheme-buttons",
    colorSchemeOptions,
    colorScheme,
    this._setColorSchemePref.bind(this)
  );
  this._setColorSchemePref(colorScheme);

  let styleButton = this._doc.querySelector(".style-button");
  styleButton.textContent = gStrings.GetStringFromName(
    "aboutReader.toolbar.typeControls"
  );

  let closeButton = this._doc.querySelector(".close-button");
  closeButton.textContent = gStrings.GetStringFromName("readerView.done.label");

  let fontTypeSample = gStrings.GetStringFromName("aboutReader.fontTypeSample");
  let fontTypeOptions = [
    {
      name: fontTypeSample,
      description: gStrings.GetStringFromName(
        "aboutReader.fontType.sans-serif"
      ),
      value: "sans-serif",
      itemClass: "sans-serif-button",
    },
    {
      name: fontTypeSample,
      description: gStrings.GetStringFromName("aboutReader.fontType.serif"),
      value: "serif",
      itemClass: "serif-button",
    },
  ];

  let fontType = Services.prefs.getCharPref("reader.font_type");
  this._setupSegmentedButton(
    "font-type-buttons",
    fontTypeOptions,
    fontType,
    this._setFontType.bind(this)
  );
  this._setFontType(fontType);

  this._setupFontSizeButtons();

  this._setupContentWidthButtons();

  this._setupLineHeightButtons();

  if (win.speechSynthesis && Services.prefs.getBoolPref("narrate.enabled")) {
    new NarrateControls(win, this._languagePromise);
  }

  this._loadArticle();

  let dropdown = this._toolbarElement;

  let elemL10nMap = {
    ".minus-button": "minus",
    ".plus-button": "plus",
    ".content-width-minus-button": "contentwidthminus",
    ".content-width-plus-button": "contentwidthplus",
    ".line-height-minus-button": "lineheightminus",
    ".line-height-plus-button": "lineheightplus",
    ".light-button": "colorschemelight",
    ".dark-button": "colorschemedark",
    ".sepia-button": "colorschemesepia",
  };

  let tbc = this._toolbarContainerElement;
  if (Services.locale.isAppLocaleRTL) {
    tbc.dir = "rtl";
  }

  for (let [selector, stringID] of Object.entries(elemL10nMap)) {
    dropdown
      .querySelector(selector)
      .setAttribute(
        "title",
        gStrings.GetStringFromName("aboutReader.toolbar." + stringID)
      );
  }
};

AboutReader.prototype = {
  _BLOCK_IMAGES_SELECTOR:
    ".content p > img:only-child, " +
    ".content p > a:only-child > img:only-child, " +
    ".content .wp-caption img, " +
    ".content figure img",

  PLATFORM_HAS_CACHE: AppConstants.platform == "android",

  FONT_SIZE_MIN: 1,

  FONT_SIZE_LEGACY_MAX: 9,

  FONT_SIZE_MAX: 15,

  FONT_SIZE_EXTENDED_VALUES: [32, 40, 56, 72, 96, 128],

  get _doc() {
    return this._docRef.get();
  },

  get _win() {
    return this._winRef.get();
  },

  get _headerElement() {
    return this._headerElementRef.get();
  },

  get _domainElement() {
    return this._domainElementRef.get();
  },

  get _titleElement() {
    return this._titleElementRef.get();
  },

  get _readTimeElement() {
    return this._readTimeElementRef.get();
  },

  get _creditsElement() {
    return this._creditsElementRef.get();
  },

  get _contentElement() {
    return this._contentElementRef.get();
  },

  get _toolbarElement() {
    return this._toolbarElementRef.get();
  },

  get _toolbarContainerElement() {
    return this._toolbarContainerElementRef.get();
  },

  get _messageElement() {
    return this._messageElementRef.get();
  },

  get _containerElement() {
    return this._containerElementRef.get();
  },

  get _isToolbarVertical() {
    if (this._toolbarVertical !== undefined) {
      return this._toolbarVertical;
    }
    return (this._toolbarVertical = Services.prefs.getBoolPref(
      "reader.toolbar.vertical"
    ));
  },

  receiveMessage(message) {
    switch (message.name) {
      case "Reader:AddButton": {
        if (
          message.data.id &&
          message.data.image &&
          !this._doc.getElementsByClassName(message.data.id)[0]
        ) {
          let btn = this._doc.createElement("button");
          btn.dataset.buttonid = message.data.id;
          btn.className = "button " + message.data.id;
          btn.textContent = message.data.label;
          btn.style.backgroundImage = "url('" + message.data.image + "')";
          if (message.data.title) {
            btn.title = message.data.title;
          }
          if (message.data.text) {
            btn.textContent = message.data.text;
          }
          if (message.data.width && message.data.height) {
            btn.style.backgroundSize = `${message.data.width}px ${message.data.height}px`;
          }
          let tb = this._toolbarElement;
          tb.appendChild(btn);
          this._setupButton(message.data.id, button => {
            this._actor.sendAsyncMessage(
              "Reader:Clicked-" + button.dataset.buttonid,
              { article: this._article }
            );
          });
        }
        break;
      }
      case "Reader:RemoveButton": {
        if (message.data.id) {
          let btn = this._doc.getElementsByClassName(message.data.id)[0];
          if (btn) {
            btn.remove();
          }
        }
        break;
      }
      case "Reader:ZoomIn": {
        this._changeFontSize(+1);
        break;
      }
      case "Reader:ZoomOut": {
        this._changeFontSize(-1);
        break;
      }
      case "Reader:ResetZoom": {
        this._resetFontSize();
        break;
      }
    }
  },

  handleEvent(aEvent) {
    if (!aEvent.isTrusted) {
      return;
    }

    let target = aEvent.target;
    switch (aEvent.type) {
      case "touchstart":
      /* fall through */
      case "mousedown":
        if (!target.closest(".dropdown-popup")) {
          this._closeDropdowns();
        }
        break;
      case "click":
        if (target.classList.contains("dropdown-toggle")) {
          this._toggleDropdownClicked(aEvent);
        }
        break;
      case "mozvisualscroll":
        const vv = aEvent.originalTarget; // VisualViewport
        let tbc = this._toolbarContainerElement;

        if (gIsFirefoxDesktop) {
          this._closeDropdowns(true);
          tbc.classList.toggle("scrolled", vv.pageTop > 0);
        } else if (this._scrollOffset != vv.pageTop) {
          // hide the system UI and the "reader-toolbar" only if the dropdown is not opened
          let selector = ".dropdown.open";
          let openDropdowns = this._doc.querySelectorAll(selector);
          if (openDropdowns.length) {
            break;
          }

          let isScrollingUp = this._scrollOffset > vv.pageTop;
          this._setToolbarVisibility(isScrollingUp);
        }

        this._scrollOffset = vv.pageTop;
        break;
      case "resize":
        this._updateImageMargins();
        break;

      case "wheel":
        let doZoom =
          (aEvent.ctrlKey && zoomOnCtrl) || (aEvent.metaKey && zoomOnMeta);
        if (!doZoom) {
          return;
        }
        aEvent.preventDefault();

        // Throttle events to once per 150ms. This avoids excessively fast zooming.
        if (aEvent.timeStamp <= this._zoomBackoffTime) {
          return;
        }
        this._zoomBackoffTime = aEvent.timeStamp + 150;

        // Determine the direction of the delta (we don't care about its size);
        // This code is adapted from normalizeWheelEventDelta in
        // browser/extensions/pdfjs/content/web/viewer.js
        let delta = Math.abs(aEvent.deltaX) + Math.abs(aEvent.deltaY);
        let angle = Math.atan2(aEvent.deltaY, aEvent.deltaX);
        if (-0.25 * Math.PI < angle && angle < 0.75 * Math.PI) {
          delta = -delta;
        }

        if (delta > 0) {
          this._changeFontSize(+1);
        } else if (delta < 0) {
          this._changeFontSize(-1);
        }
        break;

      case "devicelight":
        this._handleDeviceLight(aEvent.value);
        break;

      case "visibilitychange":
        this._handleVisibilityChange();
        break;

      case "pagehide":
        // Close the Banners Font-dropdown, cleanup Android BackPressListener.
        this._closeDropdowns();

        this._actor.readerModeHidden();
        this.clearActor();
        break;
    }
  },

  clearActor() {
    this._actor = null;
  },

  _onReaderClose() {
    ReaderMode.leaveReaderMode(this._actor.docShell, this._win);
  },

  async _resetFontSize() {
    await AsyncPrefs.reset("reader.font_size");
    let currentSize = Services.prefs.getIntPref("reader.font_size");
    this._setFontSize(currentSize);
  },

  _setFontSize(newFontSize) {
    this._fontSize = Math.min(
      this.FONT_SIZE_MAX,
      Math.max(this.FONT_SIZE_MIN, newFontSize)
    );
    let size;
    if (this._fontSize > this.FONT_SIZE_LEGACY_MAX) {
      // -1 because we're indexing into a 0-indexed array, so the first value
      // over the legacy max should be 0, the next 1, etc.
      let index = this._fontSize - this.FONT_SIZE_LEGACY_MAX - 1;
      size = this.FONT_SIZE_EXTENDED_VALUES[index];
    } else {
      size = 10 + 2 * this._fontSize;
    }

    let readerBody = this._doc.querySelector("body");
    readerBody.style.setProperty("--font-size", size + "px");
    return AsyncPrefs.set("reader.font_size", this._fontSize);
  },

  _setupFontSizeButtons() {
    // Sample text shown in Android UI.
    let sampleText = this._doc.querySelector(".font-size-sample");
    sampleText.textContent = gStrings.GetStringFromName(
      "aboutReader.fontTypeSample"
    );

    let plusButton = this._doc.querySelector(".plus-button");
    let minusButton = this._doc.querySelector(".minus-button");

    let currentSize = Services.prefs.getIntPref("reader.font_size");
    this._setFontSize(currentSize);
    this._updateFontSizeButtonControls();

    plusButton.addEventListener(
      "click",
      event => {
        if (!event.isTrusted) {
          return;
        }
        event.stopPropagation();
        this._changeFontSize(+1);
      },
      true
    );

    minusButton.addEventListener(
      "click",
      event => {
        if (!event.isTrusted) {
          return;
        }
        event.stopPropagation();
        this._changeFontSize(-1);
      },
      true
    );
  },

  _updateFontSizeButtonControls() {
    let plusButton = this._doc.querySelector(".plus-button");
    let minusButton = this._doc.querySelector(".minus-button");

    let currentSize = this._fontSize;
    let fontValue = this._doc.querySelector(".font-size-value");
    fontValue.textContent = currentSize;

    if (currentSize === this.FONT_SIZE_MIN) {
      minusButton.setAttribute("disabled", true);
    } else {
      minusButton.removeAttribute("disabled");
    }
    if (currentSize === this.FONT_SIZE_MAX) {
      plusButton.setAttribute("disabled", true);
    } else {
      plusButton.removeAttribute("disabled");
    }
  },

  _changeFontSize(changeAmount) {
    let currentSize =
      Services.prefs.getIntPref("reader.font_size") + changeAmount;
    this._setFontSize(currentSize);
    this._updateFontSizeButtonControls();
  },

  _setContentWidth(newContentWidth) {
    let containerClasses = this._containerElement.classList;

    if (this._contentWidth > 0) {
      containerClasses.remove("content-width" + this._contentWidth);
    }

    this._contentWidth = newContentWidth;
    this._displayContentWidth(newContentWidth);
    let width = 20 + 5 * (this._contentWidth - 1) + "em";
    this._containerElement.style.setProperty("--content-width", width);
    return AsyncPrefs.set("reader.content_width", this._contentWidth);
  },

  _displayContentWidth(currentContentWidth) {
    let contentWidthValue = this._doc.querySelector(".content-width-value");
    contentWidthValue.textContent = currentContentWidth;
  },

  _setupContentWidthButtons() {
    const CONTENT_WIDTH_MIN = 1;
    const CONTENT_WIDTH_MAX = 9;

    let currentContentWidth = Services.prefs.getIntPref("reader.content_width");
    currentContentWidth = Math.max(
      CONTENT_WIDTH_MIN,
      Math.min(CONTENT_WIDTH_MAX, currentContentWidth)
    );

    this._displayContentWidth(currentContentWidth);

    let plusButton = this._doc.querySelector(".content-width-plus-button");
    let minusButton = this._doc.querySelector(".content-width-minus-button");

    function updateControls() {
      if (currentContentWidth === CONTENT_WIDTH_MIN) {
        minusButton.setAttribute("disabled", true);
      } else {
        minusButton.removeAttribute("disabled");
      }
      if (currentContentWidth === CONTENT_WIDTH_MAX) {
        plusButton.setAttribute("disabled", true);
      } else {
        plusButton.removeAttribute("disabled");
      }
    }

    updateControls();
    this._setContentWidth(currentContentWidth);

    plusButton.addEventListener(
      "click",
      event => {
        if (!event.isTrusted) {
          return;
        }
        event.stopPropagation();

        if (currentContentWidth >= CONTENT_WIDTH_MAX) {
          return;
        }

        currentContentWidth++;
        updateControls();
        this._setContentWidth(currentContentWidth);
      },
      true
    );

    minusButton.addEventListener(
      "click",
      event => {
        if (!event.isTrusted) {
          return;
        }
        event.stopPropagation();

        if (currentContentWidth <= CONTENT_WIDTH_MIN) {
          return;
        }

        currentContentWidth--;
        updateControls();
        this._setContentWidth(currentContentWidth);
      },
      true
    );
  },

  _setLineHeight(newLineHeight) {
    this._displayLineHeight(newLineHeight);
    let height = 1 + 0.2 * (newLineHeight - 1) + "em";
    this._containerElement.style.setProperty("--line-height", height);
    return AsyncPrefs.set("reader.line_height", newLineHeight);
  },

  _displayLineHeight(currentLineHeight) {
    let lineHeightValue = this._doc.querySelector(".line-height-value");
    lineHeightValue.textContent = currentLineHeight;
  },

  _setupLineHeightButtons() {
    const LINE_HEIGHT_MIN = 1;
    const LINE_HEIGHT_MAX = 9;

    let currentLineHeight = Services.prefs.getIntPref("reader.line_height");
    currentLineHeight = Math.max(
      LINE_HEIGHT_MIN,
      Math.min(LINE_HEIGHT_MAX, currentLineHeight)
    );

    this._displayLineHeight(currentLineHeight);

    let plusButton = this._doc.querySelector(".line-height-plus-button");
    let minusButton = this._doc.querySelector(".line-height-minus-button");

    function updateControls() {
      if (currentLineHeight === LINE_HEIGHT_MIN) {
        minusButton.setAttribute("disabled", true);
      } else {
        minusButton.removeAttribute("disabled");
      }
      if (currentLineHeight === LINE_HEIGHT_MAX) {
        plusButton.setAttribute("disabled", true);
      } else {
        plusButton.removeAttribute("disabled");
      }
    }

    updateControls();
    this._setLineHeight(currentLineHeight);

    plusButton.addEventListener(
      "click",
      event => {
        if (!event.isTrusted) {
          return;
        }
        event.stopPropagation();

        if (currentLineHeight >= LINE_HEIGHT_MAX) {
          return;
        }

        currentLineHeight++;
        updateControls();
        this._setLineHeight(currentLineHeight);
      },
      true
    );

    minusButton.addEventListener(
      "click",
      event => {
        if (!event.isTrusted) {
          return;
        }
        event.stopPropagation();

        if (currentLineHeight <= LINE_HEIGHT_MIN) {
          return;
        }

        currentLineHeight--;
        updateControls();
        this._setLineHeight(currentLineHeight);
      },
      true
    );
  },

  _handleDeviceLight(newLux) {
    // Desired size of the this._luxValues array.
    let luxValuesSize = 10;
    // Add new lux value at the front of the array.
    this._luxValues.unshift(newLux);
    // Add new lux value to this._totalLux for averaging later.
    this._totalLux += newLux;

    // Don't update when length of array is less than luxValuesSize except when it is 1.
    if (this._luxValues.length < luxValuesSize) {
      // Use the first lux value to set the color scheme until our array equals luxValuesSize.
      if (this._luxValues.length == 1) {
        this._updateColorScheme(newLux);
      }
      return;
    }
    // Holds the average of the lux values collected in this._luxValues.
    let averageLuxValue = this._totalLux / luxValuesSize;

    this._updateColorScheme(averageLuxValue);
    // Pop the oldest value off the array.
    let oldLux = this._luxValues.pop();
    // Subtract oldLux since it has been discarded from the array.
    this._totalLux -= oldLux;
  },

  _handleVisibilityChange() {
    let colorScheme = Services.prefs.getCharPref("reader.color_scheme");
    if (colorScheme != "auto") {
      return;
    }

    // Turn off the ambient light sensor if the page is hidden
    this._enableAmbientLighting(!this._doc.hidden);
  },

  // Setup or teardown the ambient light tracking system.
  _enableAmbientLighting(enable) {
    if (enable) {
      this._win.addEventListener("devicelight", this);
      this._luxValues = [];
      this._totalLux = 0;
    } else {
      this._win.removeEventListener("devicelight", this);
      delete this._luxValues;
      delete this._totalLux;
    }
  },

  _updateColorScheme(luxValue) {
    // Upper bound value for "dark" color scheme beyond which it changes to "light".
    let upperBoundDark = 50;
    // Lower bound value for "light" color scheme beyond which it changes to "dark".
    let lowerBoundLight = 10;
    // Threshold for color scheme change.
    let colorChangeThreshold = 20;

    // Ignore changes that are within a certain threshold of previous lux values.
    if (
      (this._colorScheme === "dark" && luxValue < upperBoundDark) ||
      (this._colorScheme === "light" && luxValue > lowerBoundLight)
    ) {
      return;
    }

    if (luxValue < colorChangeThreshold) {
      this._setColorScheme("dark");
    } else {
      this._setColorScheme("light");
    }
  },

  _setColorScheme(newColorScheme) {
    // "auto" is not a real color scheme
    if (this._colorScheme === newColorScheme || newColorScheme === "auto") {
      return;
    }

    let bodyClasses = this._doc.body.classList;

    if (this._colorScheme) {
      bodyClasses.remove(this._colorScheme);
    }

    this._colorScheme = newColorScheme;
    bodyClasses.add(this._colorScheme);
  },

  // Pref values include "dark", "light", and "auto", which automatically switches
  // between light and dark color schemes based on the ambient light level.
  _setColorSchemePref(colorSchemePref) {
    this._enableAmbientLighting(colorSchemePref === "auto");
    this._setColorScheme(colorSchemePref);

    AsyncPrefs.set("reader.color_scheme", colorSchemePref);
  },

  _setFontType(newFontType) {
    if (this._fontType === newFontType) {
      return;
    }

    let bodyClasses = this._doc.body.classList;

    if (this._fontType) {
      bodyClasses.remove(this._fontType);
    }

    this._fontType = newFontType;
    bodyClasses.add(this._fontType);

    AsyncPrefs.set("reader.font_type", this._fontType);
  },

  _setToolbarVisibility(visible) {
    let tb = this._toolbarElement;

    if (visible) {
      if (tb.style.opacity != "1") {
        tb.removeAttribute("hidden");
        tb.style.opacity = "1";
      }
    } else if (tb.style.opacity != "0") {
      tb.addEventListener(
        "transitionend",
        evt => {
          if (tb.style.opacity == "0") {
            tb.setAttribute("hidden", "");
          }
        },
        { once: true }
      );
      tb.style.opacity = "0";
    }
  },

  async _loadArticle() {
    let url = this._getOriginalUrl();
    this._showProgressDelayed();

    let article;
    if (this._articlePromise) {
      article = await this._articlePromise;
    } else {
      try {
        article = await ReaderMode.downloadAndParseDocument(url);
      } catch (e) {
        if (e && e.newURL) {
          let readerURL = "about:reader?url=" + encodeURIComponent(e.newURL);
          this._win.location.replace(readerURL);
          return;
        }
      }
    }

    if (!this._actor) {
      return;
    }

    // Replace the loading message with an error message if there's a failure.
    // Users are supposed to navigate away by themselves (because we cannot
    // remove ourselves from session history.)
    if (!article) {
      this._showError();
      return;
    }

    this._showContent(article);
  },

  async _requestFavicon() {
    let iconDetails = await this._actor.sendQuery("Reader:FaviconRequest", {
      url: this._article.url,
      preferredWidth: 16 * this._win.devicePixelRatio,
    });

    if (iconDetails) {
      this._loadFavicon(iconDetails.url, iconDetails.faviconUrl);
    }
  },

  _loadFavicon(url, faviconUrl) {
    if (this._article.url !== url) {
      return;
    }

    let doc = this._doc;

    let link = doc.createElement("link");
    link.rel = "shortcut icon";
    link.href = faviconUrl;

    doc.getElementsByTagName("head")[0].appendChild(link);
  },

  _updateImageMargins() {
    let windowWidth = this._win.innerWidth;
    let bodyWidth = this._doc.body.clientWidth;

    let setImageMargins = function(img) {
      // If the image is at least as wide as the window, make it fill edge-to-edge on mobile.
      if (img.naturalWidth >= windowWidth) {
        img.setAttribute("moz-reader-full-width", true);
      } else {
        img.removeAttribute("moz-reader-full-width");
      }

      // If the image is at least half as wide as the body, center it on desktop.
      if (img.naturalWidth >= bodyWidth / 2) {
        img.setAttribute("moz-reader-center", true);
      } else {
        img.removeAttribute("moz-reader-center");
      }
    };

    let imgs = this._doc.querySelectorAll(this._BLOCK_IMAGES_SELECTOR);
    for (let i = imgs.length; --i >= 0; ) {
      let img = imgs[i];

      if (img.naturalWidth > 0) {
        setImageMargins(img);
      } else {
        img.onload = function() {
          setImageMargins(img);
        };
      }
    }
  },

  _maybeSetTextDirection: function Read_maybeSetTextDirection(article) {
    if (article.dir) {
      // Set "dir" attribute on content
      this._contentElement.setAttribute("dir", article.dir);
      this._headerElement.setAttribute("dir", article.dir);

      // The native locale could be set differently than the article's text direction.
      var localeDirection = Services.locale.isAppLocaleRTL ? "rtl" : "ltr";
      this._readTimeElement.setAttribute("dir", localeDirection);
      this._readTimeElement.style.textAlign =
        article.dir == "rtl" ? "right" : "left";
    }
  },

  _formatReadTime(slowEstimate, fastEstimate) {
    let displayStringKey = "aboutReader.estimatedReadTimeRange1";

    // only show one reading estimate when they are the same value
    if (slowEstimate == fastEstimate) {
      displayStringKey = "aboutReader.estimatedReadTimeValue1";
    }

    return PluralForm.get(
      slowEstimate,
      gStrings.GetStringFromName(displayStringKey)
    )
      .replace("#1", fastEstimate)
      .replace("#2", slowEstimate);
  },

  _showError() {
    this._headerElement.classList.remove("reader-show-element");
    this._contentElement.classList.remove("reader-show-element");

    let errorMessage = gStrings.GetStringFromName("aboutReader.loadError");
    this._messageElement.textContent = errorMessage;
    this._messageElement.style.display = "block";

    this._doc.title = errorMessage;

    this._doc.documentElement.dataset.isError = true;

    this._error = true;

    this._doc.dispatchEvent(
      new this._win.CustomEvent("AboutReaderContentError", {
        bubbles: true,
        cancelable: false,
      })
    );
  },

  // This function is the JS version of Java's StringUtils.stripCommonSubdomains.
  _stripHost(host) {
    if (!host) {
      return host;
    }

    let start = 0;

    if (host.startsWith("www.")) {
      start = 4;
    } else if (host.startsWith("m.")) {
      start = 2;
    } else if (host.startsWith("mobile.")) {
      start = 7;
    }

    return host.substring(start);
  },

  _showContent(article) {
    this._messageElement.classList.remove("reader-show-element");

    this._article = article;

    this._domainElement.href = article.url;
    let articleUri = Services.io.newURI(article.url);
    this._domainElement.textContent = this._stripHost(articleUri.host);
    this._creditsElement.textContent = article.byline;

    this._titleElement.textContent = article.title;
    this._readTimeElement.textContent = this._formatReadTime(
      article.readingTimeMinsSlow,
      article.readingTimeMinsFast
    );
    this._doc.title = article.title;

    this._headerElement.classList.add("reader-show-element");

    let parserUtils = Cc["@mozilla.org/parserutils;1"].getService(
      Ci.nsIParserUtils
    );
    let contentFragment = parserUtils.parseFragment(
      article.content,
      Ci.nsIParserUtils.SanitizerDropForms |
        Ci.nsIParserUtils.SanitizerAllowStyle,
      false,
      articleUri,
      this._contentElement
    );
    this._contentElement.innerHTML = "";
    this._contentElement.appendChild(contentFragment);
    this._maybeSetTextDirection(article);
    this._foundLanguage(article.language);

    this._contentElement.classList.add("reader-show-element");
    this._updateImageMargins();

    this._requestFavicon();
    this._doc.body.classList.add("loaded");

    this._goToReference(articleUri.ref);

    Services.obs.notifyObservers(this._win, "AboutReader:Ready");

    this._doc.dispatchEvent(
      new this._win.CustomEvent("AboutReaderContentReady", {
        bubbles: true,
        cancelable: false,
      })
    );
  },

  _hideContent() {
    this._headerElement.classList.remove("reader-show-element");
    this._contentElement.classList.remove("reader-show-element");
  },

  _showProgressDelayed() {
    this._win.setTimeout(() => {
      // No need to show progress if the article has been loaded,
      // if the window has been unloaded, or if there was an error
      // trying to load the article.
      if (this._article || !this._actor || this._error) {
        return;
      }

      this._headerElement.classList.remove("reader-show-element");
      this._contentElement.classList.remove("reader-show-element");

      this._messageElement.textContent = gStrings.GetStringFromName(
        "aboutReader.loading2"
      );
      this._messageElement.classList.add("reader-show-element");
    }, 300);
  },

  /**
   * Returns the original article URL for this about:reader view.
   */
  _getOriginalUrl(win) {
    let url = win ? win.location.href : this._win.location.href;
    return ReaderMode.getOriginalUrl(url) || url;
  },

  _setupSegmentedButton(id, options, initialValue, callback) {
    let doc = this._doc;
    let segmentedButton = doc.getElementsByClassName(id)[0];

    for (let i = 0; i < options.length; i++) {
      let option = options[i];

      let item = doc.createElement("button");

      let radioButton = doc.createElement("input");
      radioButton.type = "radio";
      radioButton.classList.add("radio-button");
      radioButton.id = "radio-item-" + option.itemClass;
      item.appendChild(radioButton);

      if (option.itemClass !== undefined) {
        item.classList.add(option.itemClass);
      }

      if (option.description !== undefined) {
        let description = doc.createElement("label");
        description.textContent = option.description;
        description.classList.add("description");
        description.htmlFor = "radio-item-" + option.itemClass;
        item.appendChild(description);

        let span = doc.createElement("span");
        span.classList.add("name");
        item.appendChild(span);
      } else {
        let name = doc.createElement("label");
        name.textContent = option.name;
        name.classList.add("description");
        name.htmlFor = "radio-item-" + option.itemClass;
        item.appendChild(name);
      }

      segmentedButton.appendChild(item);

      item.addEventListener(
        "click",
        function(aEvent) {
          if (!aEvent.isTrusted) {
            return;
          }

          aEvent.stopPropagation();

          // Just pass the ID of the button as an extra and hope the ID doesn't change
          // unless the context changes
          UITelemetry.addEvent("action.1", "button", null, id);

          let items = segmentedButton.children;
          for (let j = items.length - 1; j >= 0; j--) {
            items[j].classList.remove("selected");
            let itemRadioButton = items[j].firstElementChild;
            itemRadioButton.checked = false;
          }

          item.classList.add("selected");
          radioButton.checked = true;
          callback(option.value);
        },
        true
      );

      if (option.value === initialValue) {
        radioButton.checked = true;
        item.classList.add("selected");
      }
    }
  },

  _setupButton(id, callback, titleEntity, textEntity) {
    if (titleEntity) {
      this._setButtonTip(id, titleEntity);
    }

    let button = this._doc.getElementsByClassName(id)[0];
    if (textEntity) {
      button.textContent = gStrings.GetStringFromName(textEntity);
    }
    button.removeAttribute("hidden");
    button.addEventListener(
      "click",
      function(aEvent) {
        if (!aEvent.isTrusted) {
          return;
        }

        let btn = aEvent.target;
        callback(btn);
      },
      true
    );
  },

  /**
   * Sets a toolTip for a button. Performed at initial button setup
   * and dynamically as button state changes.
   * @param   Localizable string providing UI element usage tip.
   */
  _setButtonTip(id, titleEntity) {
    let button = this._doc.getElementsByClassName(id)[0];
    button.setAttribute("title", gStrings.GetStringFromName(titleEntity));
  },

  _setupStyleDropdown() {
    let dropdownToggle = this._doc.querySelector(
      ".style-dropdown .dropdown-toggle"
    );
    dropdownToggle.setAttribute(
      "title",
      gStrings.GetStringFromName("aboutReader.toolbar.typeControls")
    );
  },

  _toggleDropdownClicked(event) {
    let dropdown = event.target.closest(".dropdown");

    if (!dropdown) {
      return;
    }

    event.stopPropagation();

    if (dropdown.classList.contains("open")) {
      this._closeDropdowns();
    } else {
      this._openDropdown(dropdown);
    }
  },

  /*
   * If the ReaderView banner font-dropdown is closed, open it.
   */
  _openDropdown(dropdown, window) {
    if (dropdown.classList.contains("open")) {
      return;
    }

    this._closeDropdowns();

    // Trigger BackPressListener initialization in Android.
    dropdown.classList.add("open");
    let { windowUtils } = this._winRef.get();
    let toggle = dropdown.querySelector(".dropdown-toggle");
    let anchorWidth = windowUtils.getBoundsWithoutFlushing(toggle).width;
    dropdown.style.setProperty("--popup-anchor-width", anchorWidth + "px");
  },

  /*
   * If the ReaderView has open dropdowns, close them. If we are closing the
   * dropdowns because the page is scrolling, allow popups to stay open with
   * the keep-open class.
   */
  _closeDropdowns(scrolling) {
    let selector = ".dropdown.open";
    if (scrolling) {
      selector += ":not(.keep-open)";
    }

    let openDropdowns = this._doc.querySelectorAll(selector);
    for (let dropdown of openDropdowns) {
      dropdown.classList.remove("open");
    }
  },

  /*
   * Scroll reader view to a reference
   */
  _goToReference(ref) {
    if (ref) {
      this._win.location.hash = ref;
    }
  },
};
