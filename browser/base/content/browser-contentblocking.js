/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

var FastBlock = {
  PREF_ENABLED: "browser.fastblock.enabled",

  get categoryItem() {
    delete this.categoryItem;
    return this.categoryItem = document.getElementById("identity-popup-content-blocking-category-fastblock");
  },

  init() {
    XPCOMUtils.defineLazyPreferenceGetter(this, "enabled", this.PREF_ENABLED, false);
  },
};

var TrackingProtection = {
  PREF_ENABLED_GLOBALLY: "privacy.trackingprotection.enabled",
  PREF_ENABLED_IN_PRIVATE_WINDOWS: "privacy.trackingprotection.pbmode.enabled",
  enabledGlobally: false,
  enabledInPrivateWindows: false,

  get categoryItem() {
    delete this.categoryItem;
    return this.categoryItem =
      document.getElementById("identity-popup-content-blocking-category-tracking-protection");
  },

  strings: {
    get enableTooltip() {
      delete this.enableTooltip;
      return this.enableTooltip =
        gNavigatorBundle.getString("trackingProtection.toggle.enable.tooltip");
    },

    get disableTooltip() {
      delete this.disableTooltip;
      return this.disableTooltip =
        gNavigatorBundle.getString("trackingProtection.toggle.disable.tooltip");
    },

    get disableTooltipPB() {
      delete this.disableTooltipPB;
      return this.disableTooltipPB =
        gNavigatorBundle.getString("trackingProtection.toggle.disable.pbmode.tooltip");
    },

    get enableTooltipPB() {
      delete this.enableTooltipPB;
      return this.enableTooltipPB =
        gNavigatorBundle.getString("trackingProtection.toggle.enable.pbmode.tooltip");
    },
  },

  init() {
    this.updateEnabled();

    this.enabledHistogramAdd(this.enabledGlobally);
    this.disabledPBMHistogramAdd(!this.enabledInPrivateWindows);

    Services.prefs.addObserver(this.PREF_ENABLED_GLOBALLY, this);
    Services.prefs.addObserver(this.PREF_ENABLED_IN_PRIVATE_WINDOWS, this);
  },

  uninit() {
    Services.prefs.removeObserver(this.PREF_ENABLED_GLOBALLY, this);
    Services.prefs.removeObserver(this.PREF_ENABLED_IN_PRIVATE_WINDOWS, this);
  },

  observe() {
    this.updateEnabled();
  },

  get enabled() {
    return this.enabledGlobally ||
           (this.enabledInPrivateWindows &&
            PrivateBrowsingUtils.isWindowPrivate(window));
  },

  enabledHistogramAdd(value) {
    if (PrivateBrowsingUtils.isWindowPrivate(window)) {
      return;
    }
    Services.telemetry.getHistogramById("TRACKING_PROTECTION_ENABLED").add(value);
  },

  disabledPBMHistogramAdd(value) {
    if (PrivateBrowsingUtils.isWindowPrivate(window)) {
      return;
    }
    Services.telemetry.getHistogramById("TRACKING_PROTECTION_PBM_DISABLED").add(value);
  },

  onGlobalToggleCommand() {
    if (PrivateBrowsingUtils.isWindowPrivate(window)) {
      Services.prefs.setBoolPref(this.PREF_ENABLED_IN_PRIVATE_WINDOWS, !this.enabledInPrivateWindows);
    } else {
      Services.prefs.setBoolPref(this.PREF_ENABLED_GLOBALLY, !this.enabledGlobally);
    }
  },

  updateEnabled() {
    this.enabledGlobally =
      Services.prefs.getBoolPref(this.PREF_ENABLED_GLOBALLY);
    this.enabledInPrivateWindows =
      Services.prefs.getBoolPref(this.PREF_ENABLED_IN_PRIVATE_WINDOWS);

    if (!ContentBlocking.contentBlockingUIEnabled) {
      ContentBlocking.updateEnabled();
      let appMenuButton = ContentBlocking.appMenuButton;

      if (PrivateBrowsingUtils.isWindowPrivate(window)) {
        appMenuButton.setAttribute("tooltiptext", this.enabledInPrivateWindows ?
          this.strings.disableTooltipPB : this.strings.enableTooltipPB);
        appMenuButton.setAttribute("enabled", this.enabledInPrivateWindows);
        appMenuButton.setAttribute("aria-pressed", this.enabledInPrivateWindows);
      } else {
        appMenuButton.setAttribute("tooltiptext", this.enabledGlobally ?
          this.strings.disableTooltip : this.strings.enableTooltip);
        appMenuButton.setAttribute("enabled", this.enabledGlobally);
        appMenuButton.setAttribute("aria-pressed", this.enabledGlobally);
      }
    }
  },
};


var ContentBlocking = {
  // If the user ignores the doorhanger, we stop showing it after some time.
  MAX_INTROS: 20,
  PREF_ENABLED: "browser.contentblocking.enabled",
  PREF_UI_ENABLED: "browser.contentblocking.ui.enabled",
  PREF_ANIMATIONS_ENABLED: "toolkit.cosmeticAnimations.enabled",
  content: null,
  icon: null,
  activeTooltipText: null,
  disabledTooltipText: null,

  get appMenuLabel() {
    delete this.appMenuLabel;
    return this.appMenuLabel = document.getElementById("appMenu-tp-label");
  },

  get appMenuButton() {
    delete this.appMenuButton;
    return this.appMenuButton = document.getElementById("appMenu-tp-toggle");
  },

  strings: {
    get enableTooltip() {
      delete this.enableTooltip;
      return this.enableTooltip =
        gNavigatorBundle.getString("contentBlocking.toggle.enable.tooltip");
    },

    get disableTooltip() {
      delete this.disableTooltip;
      return this.disableTooltip =
        gNavigatorBundle.getString("contentBlocking.toggle.disable.tooltip");
    },

    get appMenuTitle() {
      delete this.appMenuTitle;
      return this.appMenuTitle =
        gNavigatorBundle.getString("contentBlocking.title");
    },

    get appMenuTooltip() {
      delete this.appMenuTooltip;
      return this.appMenuTooltip =
        gNavigatorBundle.getString("contentBlocking.tooltip");
    },
  },

  // A list of blockers that will be displayed in the categories list
  // when blockable content is detected. A blocker must be an object
  // with at least the following two properties:
  //  - enabled: Whether the blocker is currently turned on.
  //  - categoryItem: The DOM item that represents the entry in the category list.
  //
  // It may also contain an init() and uninit() function, which will be called
  // on ContentBlocking.init() and ContentBlocking.uninit().
  blockers: [FastBlock, TrackingProtection],

  get _baseURIForChannelClassifier() {
    // Convert document URI into the format used by
    // nsChannelClassifier::ShouldEnableTrackingProtection.
    // Any scheme turned into https is correct.
    try {
      return Services.io.newURI("https://" + gBrowser.selectedBrowser.currentURI.hostPort);
    } catch (e) {
      // Getting the hostPort for about: and file: URIs fails, but TP doesn't work with
      // these URIs anyway, so just return null here.
      return null;
    }
  },

  init() {
    let $ = selector => document.querySelector(selector);
    this.content = $("#identity-popup-content-blocking-content");
    this.icon = $("#tracking-protection-icon");
    this.iconBox = $("#tracking-protection-icon-box");
    this.animatedIcon = $("#tracking-protection-icon-animatable-image");
    this.animatedIcon.addEventListener("animationend", () => this.iconBox.removeAttribute("animate"));

    this.updateAnimationsEnabled = () => {
      this.iconBox.toggleAttribute("animationsenabled",
        Services.prefs.getBoolPref(this.PREF_ANIMATIONS_ENABLED, false));
    };

    for (let blocker of this.blockers) {
      if (blocker.init) {
        blocker.init();
      }
    }

    this.updateAnimationsEnabled();

    Services.prefs.addObserver(this.PREF_ANIMATIONS_ENABLED, this.updateAnimationsEnabled);

    XPCOMUtils.defineLazyPreferenceGetter(this, "contentBlockingEnabled", this.PREF_ENABLED, false,
      this.updateEnabled.bind(this));
    XPCOMUtils.defineLazyPreferenceGetter(this, "contentBlockingUIEnabled", this.PREF_UI_ENABLED, false,
      this.updateUIEnabled.bind(this));

    this.updateEnabled();
    this.updateUIEnabled();

    this.activeTooltipText =
      gNavigatorBundle.getString("trackingProtection.icon.activeTooltip");
    this.disabledTooltipText =
      gNavigatorBundle.getString("trackingProtection.icon.disabledTooltip");
  },

  uninit() {
    for (let blocker of this.blockers) {
      if (blocker.uninit) {
        blocker.uninit();
      }
    }

    Services.prefs.removeObserver(this.PREF_ANIMATIONS_ENABLED, this.updateAnimationsEnabled);
  },

  get enabled() {
    return this.contentBlockingUIEnabled ? this.contentBlockingEnabled : TrackingProtection.enabled;
  },

  updateEnabled() {
    this.content.toggleAttribute("enabled", this.enabled);

    if (this.contentBlockingUIEnabled) {
      this.appMenuButton.setAttribute("tooltiptext", this.enabled ?
        this.strings.disableTooltip : this.strings.enableTooltip);
      this.appMenuButton.setAttribute("enabled", this.enabled);
      this.appMenuButton.setAttribute("aria-pressed", this.enabled);
    }
  },

  updateUIEnabled() {
    this.content.toggleAttribute("contentBlockingUI", this.contentBlockingUIEnabled);

    if (this.contentBlockingUIEnabled) {
      this.appMenuLabel.setAttribute("label", this.strings.appMenuTitle);
      this.appMenuLabel.setAttribute("tooltiptext", this.strings.appMenuTooltip);
    }

    this.updateEnabled();
  },

  onGlobalToggleCommand() {
    if (this.contentBlockingUIEnabled) {
      Services.prefs.setBoolPref(this.PREF_ENABLED, !this.enabled);
    } else {
      TrackingProtection.onGlobalToggleCommand();
    }
  },

  hideIdentityPopupAndReload() {
    document.getElementById("identity-popup").hidePopup();
    BrowserReload();
  },

  openPreferences(origin) {
    openPreferences("privacy-trackingprotection", { origin });
  },

  eventsHistogramAdd(value) {
    if (PrivateBrowsingUtils.isWindowPrivate(window)) {
      return;
    }
    Services.telemetry.getHistogramById("TRACKING_PROTECTION_EVENTS").add(value);
  },

  shieldHistogramAdd(value) {
    if (PrivateBrowsingUtils.isWindowPrivate(window)) {
      return;
    }
    Services.telemetry.getHistogramById("TRACKING_PROTECTION_SHIELD").add(value);
  },

  cancelAnimation() {
    let iconAnimation = this.animatedIcon.getAnimations()[0];
    if (iconAnimation && iconAnimation.currentTime) {
      iconAnimation.cancel();
    }
    this.iconBox.removeAttribute("animate");
  },

  onSecurityChange(state, webProgress, isSimulated) {
    let baseURI = this._baseURIForChannelClassifier;

    // Don't deal with about:, file: etc.
    if (!baseURI) {
      this.cancelAnimation();
      this.iconBox.removeAttribute("active");
      this.iconBox.removeAttribute("hasException");
      return;
    }

    // The user might have navigated before the shield animation
    // finished. In this case, reset the animation to be able to
    // play it in full again and avoid choppiness.
    if (webProgress.isTopLevel) {
      this.cancelAnimation();
    }

    let isBlocking = state & Ci.nsIWebProgressListener.STATE_BLOCKED_TRACKING_CONTENT;
    let isAllowing = state & Ci.nsIWebProgressListener.STATE_LOADED_TRACKING_CONTENT;
    let detected = isBlocking || isAllowing;

    // We consider the shield state as "active" when any kind of blocking-related
    // activity occurs on the page (blocking or allowing). Since we have several
    // content blockers, we need to go through them individually to figure out which
    // ones are actually turned on or off.
    // This state will be overriden later if there's an exception set for this site.
    let active = this.enabled && detected;

    for (let blocker of this.blockers) {
      blocker.categoryItem.classList.toggle("blocked", this.enabled && blocker.enabled);
    }

    // Check whether the user has added an exception for this site.
    let hasException = false;
    if (PrivateBrowsingUtils.isBrowserPrivate(gBrowser.selectedBrowser)) {
      hasException = PrivateBrowsingUtils.existsInTrackingAllowlist(baseURI);
    } else {
      hasException = Services.perms.testExactPermission(baseURI,
        "trackingprotection") == Services.perms.ALLOW_ACTION;
    }

    this.content.toggleAttribute("detected", detected);
    this.content.toggleAttribute("hasException", hasException);

    this.iconBox.toggleAttribute("active", active);
    this.iconBox.toggleAttribute("hasException", this.enabled && hasException);

    if (isSimulated) {
      this.cancelAnimation();
    } else if (active && webProgress.isTopLevel) {
      this.iconBox.setAttribute("animate", "true");

      // Open the tracking protection introduction panel, if applicable.
      if (TrackingProtection.enabledGlobally) {
        let introCount = Services.prefs.getIntPref("privacy.trackingprotection.introCount");
        if (introCount < this.MAX_INTROS) {
          Services.prefs.setIntPref("privacy.trackingprotection.introCount", ++introCount);
          Services.prefs.savePrefFile(null);
          this.showIntroPanel();
        }
      }
    }

    if (hasException) {
      this.iconBox.setAttribute("tooltiptext", this.disabledTooltipText);
      this.shieldHistogramAdd(1);
    } else if (active) {
      this.iconBox.setAttribute("tooltiptext", this.activeTooltipText);
      this.shieldHistogramAdd(2);
    } else {
      this.iconBox.removeAttribute("tooltiptext");
      this.shieldHistogramAdd(0);
    }

    // Telemetry for state change.
    this.eventsHistogramAdd(0);
  },

  disableForCurrentPage() {
    let baseURI = this._baseURIForChannelClassifier;

    // Add the current host in the 'trackingprotection' consumer of
    // the permission manager using a normalized URI. This effectively
    // places this host on the tracking protection allowlist.
    if (PrivateBrowsingUtils.isBrowserPrivate(gBrowser.selectedBrowser)) {
      PrivateBrowsingUtils.addToTrackingAllowlist(baseURI);
    } else {
      Services.perms.add(baseURI,
        "trackingprotection", Services.perms.ALLOW_ACTION);
    }

    // Telemetry for disable protection.
    this.eventsHistogramAdd(1);

    this.hideIdentityPopupAndReload();
  },

  enableForCurrentPage() {
    // Remove the current host from the 'trackingprotection' consumer
    // of the permission manager. This effectively removes this host
    // from the tracking protection allowlist.
    let baseURI = this._baseURIForChannelClassifier;

    if (PrivateBrowsingUtils.isBrowserPrivate(gBrowser.selectedBrowser)) {
      PrivateBrowsingUtils.removeFromTrackingAllowlist(baseURI);
    } else {
      Services.perms.remove(baseURI, "trackingprotection");
    }

    // Telemetry for enable protection.
    this.eventsHistogramAdd(2);

    this.hideIdentityPopupAndReload();
  },

  dontShowIntroPanelAgain() {
    // This function may be called in private windows, but it does not change
    // any preference unless Tracking Protection is enabled globally.
    if (TrackingProtection.enabledGlobally) {
      Services.prefs.setIntPref("privacy.trackingprotection.introCount",
                                this.MAX_INTROS);
      Services.prefs.savePrefFile(null);
    }
  },

  async showIntroPanel() {
    let brandBundle = document.getElementById("bundle_brand");
    let brandShortName = brandBundle.getString("brandShortName");

    let openStep2 = () => {
      // When the user proceeds in the tour, adjust the counter to indicate that
      // the user doesn't need to see the intro anymore.
      this.dontShowIntroPanelAgain();

      let nextURL = Services.urlFormatter.formatURLPref("privacy.trackingprotection.introURL") +
                    "?step=2&newtab=true";
      switchToTabHavingURI(nextURL, true, {
        // Ignore the fragment in case the intro is shown on the tour page
        // (e.g. if the user manually visited the tour or clicked the link from
        // about:privatebrowsing) so we can avoid a reload.
        ignoreFragment: "whenComparingAndReplace",
        triggeringPrincipal: Services.scriptSecurityManager.getSystemPrincipal(),
      });
    };

    let buttons = [
      {
        label: gNavigatorBundle.getString("trackingProtection.intro.step1of3"),
        style: "text",
      },
      {
        callback: openStep2,
        label: gNavigatorBundle.getString("trackingProtection.intro.nextButton.label"),
        style: "primary",
      },
    ];

    let panelTarget = await UITour.getTarget(window, "trackingProtection");
    UITour.initForBrowser(gBrowser.selectedBrowser, window);
    UITour.showInfo(window, panelTarget,
                    gNavigatorBundle.getString("trackingProtection.intro.title"),
                    gNavigatorBundle.getFormattedString("trackingProtection.intro.description2",
                                                        [brandShortName]),
                    undefined, buttons,
                    { closeButtonCallback: () => this.dontShowIntroPanelAgain() });
  },
};
