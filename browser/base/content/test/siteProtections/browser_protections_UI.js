/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

/* Basic UI tests for the protections panel */

ChromeUtils.defineModuleGetter(
  this,
  "ContentBlockingAllowList",
  "resource://gre/modules/ContentBlockingAllowList.jsm"
);

function checkClickTelemetry(objectName, value) {
  let events = Services.telemetry.snapshotEvents(
    Ci.nsITelemetry.DATASET_PRERELEASE_CHANNELS
  ).parent;
  let buttonEvents = events.filter(
    e =>
      e[1] == "security.ui.protectionspopup" &&
      e[2] == "click" &&
      e[3] == objectName &&
      (!value || e[4] == value)
  );
  is(buttonEvents.length, 1, `recorded ${objectName} telemetry event`);
}

add_task(async function setup() {
  await SpecialPowers.pushPrefEnv({
    set: [
      // Set the auto hide timing to 100ms for blocking the test less.
      ["browser.protections_panel.toast.timeout", 100],
      // Hide protections cards so as not to trigger more async messaging
      // when landing on the page.
      ["browser.contentblocking.report.monitor.enabled", false],
      ["browser.contentblocking.report.lockwise.enabled", false],
      ["browser.contentblocking.report.proxy.enabled", false],
    ],
  });
  let oldCanRecord = Services.telemetry.canRecordExtended;
  Services.telemetry.canRecordExtended = true;

  registerCleanupFunction(() => {
    Services.telemetry.canRecordExtended = oldCanRecord;
  });
});

add_task(async function testToggleSwitch() {
  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "https://example.com"
  );
  await openProtectionsPanel();
  let events = Services.telemetry.snapshotEvents(
    Ci.nsITelemetry.DATASET_PRERELEASE_CHANNELS
  ).parent;
  let buttonEvents = events.filter(
    e =>
      e[1] == "security.ui.protectionspopup" &&
      e[2] == "open" &&
      e[3] == "protections_popup"
  );
  is(buttonEvents.length, 1, "recorded telemetry for opening the popup");

  // Check the visibility of the "Site not working?" link.
  ok(
    BrowserTestUtils.is_visible(
      gProtectionsHandler._protectionsPopupTPSwitchBreakageLink
    ),
    "The 'Site not working?' link should be visible."
  );

  ok(
    gProtectionsHandler._protectionsPopupTPSwitch.hasAttribute("enabled"),
    "TP Switch should be enabled"
  );
  let popuphiddenPromise = BrowserTestUtils.waitForEvent(
    protectionsPopup,
    "popuphidden"
  );
  let browserLoadedPromise = BrowserTestUtils.browserLoaded(tab.linkedBrowser);
  gProtectionsHandler._protectionsPopupTPSwitch.click();

  // The 'Site not working?' link should be hidden after clicking the TP switch.
  ok(
    BrowserTestUtils.is_hidden(
      gProtectionsHandler._protectionsPopupTPSwitchBreakageLink
    ),
    "The 'Site not working?' link should be hidden after TP switch turns to off."
  );

  await popuphiddenPromise;
  checkClickTelemetry("etp_toggle_off");

  // We need to wait toast's popup shown and popup hidden events. It won't fire
  // the popup shown event if we open the protections panel while the toast is
  // opening.
  let popupShownPromise = BrowserTestUtils.waitForEvent(
    protectionsPopup,
    "popupshown"
  );
  popuphiddenPromise = BrowserTestUtils.waitForEvent(
    protectionsPopup,
    "popuphidden"
  );

  await browserLoadedPromise;

  // Wait until the toast is shown and hidden.
  await popupShownPromise;
  await popuphiddenPromise;

  await openProtectionsPanel();
  ok(
    !gProtectionsHandler._protectionsPopupTPSwitch.hasAttribute("enabled"),
    "TP Switch should be disabled"
  );

  // The 'Site not working?' link should be hidden if the TP is off.
  ok(
    BrowserTestUtils.is_hidden(
      gProtectionsHandler._protectionsPopupTPSwitchBreakageLink
    ),
    "The 'Site not working?' link should be hidden if TP is off."
  );

  // Click the TP switch again and check the visibility of the 'Site not
  // Working?'. It should be hidden after toggling the TP switch.
  browserLoadedPromise = BrowserTestUtils.browserLoaded(tab.linkedBrowser);
  gProtectionsHandler._protectionsPopupTPSwitch.click();

  ok(
    BrowserTestUtils.is_hidden(
      gProtectionsHandler._protectionsPopupTPSwitchBreakageLink
    ),
    `The 'Site not working?' link should be still hidden after toggling TP
     switch to on from off.`
  );

  await browserLoadedPromise;
  checkClickTelemetry("etp_toggle_on");

  ContentBlockingAllowList.remove(tab.linkedBrowser);
  BrowserTestUtils.removeTab(tab);
});

/**
 * A test for the protection settings button.
 */
add_task(async function testSettingsButton() {
  // Open a tab and its protection panel.
  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "https://example.com"
  );
  await openProtectionsPanel();

  let popuphiddenPromise = BrowserTestUtils.waitForEvent(
    protectionsPopup,
    "popuphidden"
  );
  let newTabPromise = BrowserTestUtils.waitForNewTab(
    gBrowser,
    "about:preferences#privacy"
  );
  gProtectionsHandler._protectionsPopupSettingsButton.click();

  // The protection popup should be hidden after clicking settings button.
  await popuphiddenPromise;
  // Wait until the about:preferences has been opened correctly.
  let newTab = await newTabPromise;

  ok(true, "about:preferences has been opened successfully");
  checkClickTelemetry("settings");

  BrowserTestUtils.removeTab(newTab);
  BrowserTestUtils.removeTab(tab);
});

/**
 * A test for the 'Show Full Report' button in the footer section.
 */
add_task(async function testShowFullReportButton() {
  // Open a tab and its protection panel.
  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "https://example.com"
  );
  await openProtectionsPanel();

  let popuphiddenPromise = BrowserTestUtils.waitForEvent(
    protectionsPopup,
    "popuphidden"
  );
  let newTabPromise = BrowserTestUtils.waitForNewTab(
    gBrowser,
    "about:protections"
  );
  let showFullReportButton = document.getElementById(
    "protections-popup-show-report-button"
  );

  showFullReportButton.click();

  // The protection popup should be hidden after clicking the link.
  await popuphiddenPromise;
  // Wait until the 'about:protections' has been opened correctly.
  let newTab = await newTabPromise;

  ok(true, "about:protections has been opened successfully");

  // When the graph is built it means the messaging has finished,
  // we can close the tab.
  await ContentTask.spawn(newTab.linkedBrowser, {}, async function() {
    await ContentTaskUtils.waitForCondition(() => {
      let bars = content.document.querySelectorAll(".graph-bar");
      return bars.length;
    }, "The graph has been built");
  });

  checkClickTelemetry("full_report");

  BrowserTestUtils.removeTab(newTab);
  BrowserTestUtils.removeTab(tab);
});

/**
 * A test for ensuring the mini panel is working correctly
 */
add_task(async function testMiniPanel() {
  // Open a tab.
  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "https://example.com"
  );

  // Open the mini panel.
  await openProtectionsPanel(true);
  let popuphiddenPromise = BrowserTestUtils.waitForEvent(
    protectionsPopup,
    "popuphidden"
  );

  // Check that only the header is displayed.
  for (let item of protectionsPopupMainView.childNodes) {
    if (item.id !== "protections-popup-mainView-panel-header") {
      ok(
        !BrowserTestUtils.is_visible(item),
        `The section '${item.id}' is hidden in the toast.`
      );
    } else {
      ok(
        BrowserTestUtils.is_visible(item),
        "The panel header is displayed as the content of the toast."
      );
    }
  }

  // Wait until the auto hide is happening.
  await popuphiddenPromise;

  ok(true, "The mini panel hides automatically.");

  BrowserTestUtils.removeTab(tab);
});

/**
 * A test for the toggle switch flow
 */
add_task(async function testToggleSwitchFlow() {
  // Open a tab.
  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "https://example.com"
  );
  await openProtectionsPanel();

  let popuphiddenPromise = BrowserTestUtils.waitForEvent(
    protectionsPopup,
    "popuphidden"
  );
  let popupShownPromise = BrowserTestUtils.waitForEvent(
    protectionsPopup,
    "popupshown"
  );
  let browserLoadedPromise = BrowserTestUtils.browserLoaded(tab.linkedBrowser);

  // Click the TP switch, from On -> Off.
  gProtectionsHandler._protectionsPopupTPSwitch.click();

  // Check that the icon state has been changed.
  ok(
    gProtectionsHandler.iconBox.hasAttribute("hasException"),
    "The tracking protection icon state has been changed to disabled."
  );

  // The panel should be closed and the mini panel will show up after refresh.
  await popuphiddenPromise;
  await browserLoadedPromise;
  await popupShownPromise;

  ok(
    gProtectionsHandler._protectionsPopup.hasAttribute("toast"),
    "The protections popup should have the 'toast' attribute."
  );

  // Click on the mini panel and making sure the protection popup shows up.
  popupShownPromise = BrowserTestUtils.waitForEvent(
    protectionsPopup,
    "popupshown"
  );
  popuphiddenPromise = BrowserTestUtils.waitForEvent(
    protectionsPopup,
    "popuphidden"
  );
  protectionsPopupHeader.click();
  await popuphiddenPromise;
  await popupShownPromise;

  ok(
    !gProtectionsHandler._protectionsPopup.hasAttribute("toast"),
    "The 'toast' attribute should be cleared on the protections popup."
  );

  // Click the TP switch again, from Off -> On.
  popuphiddenPromise = BrowserTestUtils.waitForEvent(
    protectionsPopup,
    "popuphidden"
  );
  popupShownPromise = BrowserTestUtils.waitForEvent(
    protectionsPopup,
    "popupshown"
  );
  browserLoadedPromise = BrowserTestUtils.browserLoaded(tab.linkedBrowser);
  gProtectionsHandler._protectionsPopupTPSwitch.click();

  // Check that the icon state has been changed.
  ok(
    !gProtectionsHandler.iconBox.hasAttribute("hasException"),
    "The tracking protection icon state has been changed to enabled."
  );

  // Protections popup hidden -> Page refresh -> Mini panel shows up.
  await popuphiddenPromise;
  popuphiddenPromise = BrowserTestUtils.waitForEvent(
    protectionsPopup,
    "popuphidden"
  );
  await browserLoadedPromise;
  await popupShownPromise;

  ok(
    gProtectionsHandler._protectionsPopup.hasAttribute("toast"),
    "The protections popup should have the 'toast' attribute."
  );

  // Wait until the auto hide is happening.
  await popuphiddenPromise;

  // Clean up the TP state.
  ContentBlockingAllowList.remove(tab.linkedBrowser);
  BrowserTestUtils.removeTab(tab);
});

/**
 * A test for ensuring the tracking protection icon will show a correct
 * icon according to the TP enabling state.
 */
add_task(async function testTrackingProtectionIcon() {
  // Open a tab and its protection panel.
  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "https://example.com"
  );

  let TPIcon = document.getElementById("tracking-protection-icon");
  // Check the icon url. It will show a shield icon if TP is enabled.
  is(
    gBrowser.ownerGlobal
      .getComputedStyle(TPIcon)
      .getPropertyValue("list-style-image"),
    `url("chrome://browser/skin/tracking-protection.svg")`,
    "The tracking protection icon shows a shield icon."
  );

  // Disable the tracking protection.
  let browserLoadedPromise = BrowserTestUtils.browserLoaded(
    tab.linkedBrowser,
    false,
    "https://example.com/"
  );
  gProtectionsHandler.disableForCurrentPage();
  await browserLoadedPromise;

  // Check that the tracking protection icon should show a strike-through shield
  // icon after page is reloaded.
  is(
    gBrowser.ownerGlobal
      .getComputedStyle(TPIcon)
      .getPropertyValue("list-style-image"),
    `url("chrome://browser/skin/tracking-protection-disabled.svg")`,
    "The tracking protection icon shows a strike through shield icon."
  );

  // Clean up the TP state.
  ContentBlockingAllowList.remove(tab.linkedBrowser);
  BrowserTestUtils.removeTab(tab);
});

add_task(async function testSubViewTelemetry() {
  let items = [
    ["protections-popup-category-tracking-protection", "trackers"],
    ["protections-popup-category-socialblock", "social"],
    ["protections-popup-category-cookies", "cookies"],
    ["protections-popup-category-cryptominers", "cryptominers"],
    ["protections-popup-category-fingerprinters", "fingerprinters"],
  ].map(item => [document.getElementById(item[0]), item[1]]);

  for (let [item, telemetryId] of items) {
    await BrowserTestUtils.withNewTab("http://www.example.com", async () => {
      await openProtectionsPanel();
      item.classList.remove("notFound"); // Force visible for test
      let viewShownEvent = BrowserTestUtils.waitForEvent(
        gProtectionsHandler._protectionsPopupMultiView,
        "ViewShown"
      );
      item.click();
      let panelView = (await viewShownEvent).originalTarget;
      checkClickTelemetry(telemetryId);
      let prefsTabPromise = BrowserTestUtils.waitForNewTab(
        gBrowser,
        "about:preferences#privacy"
      );
      panelView.querySelector(".panel-footer > button").click();
      let prefsTab = await prefsTabPromise;
      BrowserTestUtils.removeTab(prefsTab);
      checkClickTelemetry("subview_settings", telemetryId);
    });
  }
});
