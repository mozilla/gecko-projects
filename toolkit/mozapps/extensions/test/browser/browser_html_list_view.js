/* eslint max-len: ["error", 80] */

let promptService;

const SECTION_INDEXES = {
  enabled: 0,
  disabled: 1,
};
function getSection(doc, type) {
  return doc.querySelector(`section[section="${SECTION_INDEXES[type]}"]`);
}

function getTestCards(root) {
  return root.querySelectorAll('addon-card[addon-id$="@mochi.test"]');
}

function getCardByAddonId(root, id) {
  return root.querySelector(`addon-card[addon-id="${id}"]`);
}

function isEmpty(el) {
  return el.children.length == 0;
}

function waitForThemeChange(list) {
  // Wait for two move events. One theme will be enabled and another disabled.
  let moveCount = 0;
  return BrowserTestUtils.waitForEvent(list, "move", () => ++moveCount == 2);
}

add_task(async function enableHtmlViews() {
  await SpecialPowers.pushPrefEnv({
    set: [["extensions.htmlaboutaddons.enabled", true]],
  });
  promptService = mockPromptService();
});

let extensionsCreated = 0;

function createExtensions(manifestExtras) {
  return manifestExtras.map(extra => ExtensionTestUtils.loadExtension({
    manifest: {
      name: "Test extension",
      applications: {gecko: {id: `test-${extensionsCreated++}@mochi.test`}},
      icons: {
        32: "test-icon.png",
      },
      ...extra,
    },
    useAddonManager: "temporary",
  }));
}

add_task(async function testExtensionList() {
  let extension = ExtensionTestUtils.loadExtension({
    manifest: {
      name: "Test extension",
      applications: {gecko: {id: "test@mochi.test"}},
      icons: {
        32: "test-icon.png",
      },
    },
    useAddonManager: "temporary",
  });
  await extension.startup();

  let addon = await AddonManager.getAddonByID("test@mochi.test");
  ok(addon, "The add-on can be found");

  let win = await loadInitialView("extension");
  let doc = win.document;

  // Find the addon-list to listen for events.
  let list = doc.querySelector("addon-list");

  // There shouldn't be any disabled extensions.
  let disabledSection = getSection(doc, "disabled");
  ok(isEmpty(disabledSection), "The disabled section is empty");

  // The loaded extension should be in the enabled list.
  let enabledSection = getSection(doc, "enabled");
  ok(!isEmpty(enabledSection), "The enabled section isn't empty");
  let card = getCardByAddonId(enabledSection, "test@mochi.test");
  ok(card, "The card is in the enabled section");

  // Check the properties of the card.
  is(card.querySelector(".addon-name").textContent, "Test extension",
     "The name is set");
  let icon = card.querySelector(".addon-icon");
  ok(icon.src.endsWith("/test-icon.png"), "The icon is set");

  // Disable the extension.
  let disableButton = card.querySelector('[action="toggle-disabled"]');
  is(doc.l10n.getAttributes(disableButton).id, "disable-addon-button",
     "The button has the disable label");

  let disabled = BrowserTestUtils.waitForEvent(list, "move");
  disableButton.click();
  await disabled;
  is(card.parentNode, disabledSection,
    "The card is now in the disabled section");

  // The disable button is now enable.
  is(doc.l10n.getAttributes(disableButton).id, "enable-addon-button",
     "The button has the enable label");

  // Remove the add-on.
  let removeButton = card.querySelector('[action="remove"]');
  is(doc.l10n.getAttributes(removeButton).id, "remove-addon-button",
     "The button has the remove label");

  // Remove but cancel.
  let cancelled = BrowserTestUtils.waitForEvent(card, "remove-cancelled");
  removeButton.click();
  await cancelled;

  let removed = BrowserTestUtils.waitForEvent(list, "remove");
  // Tell the mock prompt service that the prompt was accepted.
  promptService._response = 0;
  removeButton.click();
  await removed;

  addon = await AddonManager.getAddonByID("test@mochi.test");
  ok(!addon, "The addon is not longer found");

  await extension.unload();
  await closeView(win);
});

add_task(async function testMouseSupport() {
  let extension = ExtensionTestUtils.loadExtension({
    manifest: {
      name: "Test extension",
      applications: {gecko: {id: "test@mochi.test"}},
    },
    useAddonManager: "temporary",
  });
  await extension.startup();

  let win = await loadInitialView("extension");
  let doc = win.document;

  let [card] = getTestCards(doc);
  is(card.addon.id, "test@mochi.test", "The right card is found");

  let menuButton = card.querySelector('[action="more-options"]');
  let panel = card.querySelector("panel-list");

  ok(!panel.open, "The panel is initially closed");
  await BrowserTestUtils.synthesizeMouseAtCenter(
    menuButton, {type: "mousedown"}, gBrowser.selectedBrowser);
  ok(panel.open, "The panel is now open");

  await closeView(win);
  await extension.unload();
});

add_task(async function testKeyboardSupport() {
  let extension = ExtensionTestUtils.loadExtension({
    manifest: {
      name: "Test extension",
      applications: {gecko: {id: "test@mochi.test"}},
    },
    useAddonManager: "temporary",
  });
  await extension.startup();

  let win = await loadInitialView("extension");
  let doc = win.document;

  // Some helpers.
  let tab = event => EventUtils.synthesizeKey("VK_TAB", event);
  let space = () => EventUtils.synthesizeKey(" ", {});
  let isFocused = (el, msg) => is(doc.activeElement, el, msg);

  // Find the addon-list to listen for events.
  let list = doc.querySelector("addon-list");
  let enabledSection = getSection(doc, "enabled");
  let disabledSection = getSection(doc, "disabled");

  // Find the card.
  let [card] = getTestCards(list);
  is(card.addon.id, "test@mochi.test", "The right card is found");

  // Focus the more options menu button.
  let moreOptionsButton = card.querySelector('[action="more-options"]');
  moreOptionsButton.focus();
  isFocused(moreOptionsButton, "The more options button is focused");

  // Test opening and closing the menu.
  let moreOptionsMenu = card.querySelector("panel-list");
  is(moreOptionsMenu.open, false, "The menu is closed");
  space();
  is(moreOptionsMenu.open, true, "The menu is open");
  space();
  is(moreOptionsMenu.open, false, "The menu is closed");

  // Test tabbing out of the menu.
  space();
  is(moreOptionsMenu.open, true, "The menu is open");
  tab({shiftKey: true});
  is(moreOptionsMenu.open, false, "Tabbing away from the menu closes it");
  tab();
  isFocused(moreOptionsButton, "The button is focused again");
  space();
  is(moreOptionsMenu.open, true, "The menu is open");
  tab();
  tab();
  tab();
  isFocused(moreOptionsButton, "The last item is focused");
  tab();
  is(moreOptionsMenu.open, false, "Tabbing out of the menu closes it");

  // Focus the button again, focus may have moved out of the browser.
  moreOptionsButton.focus();
  isFocused(moreOptionsButton, "The button is focused again");

  // Open the menu to test contents.
  let shown = BrowserTestUtils.waitForEvent(moreOptionsMenu, "shown");
  space();
  is(moreOptionsMenu.open, true, "The menu is open");
  // Wait for the panel to be shown.
  await shown;

  // Disable the add-on.
  let toggleDisableButton = card.querySelector('[action="toggle-disabled"]');
  tab();
  isFocused(toggleDisableButton, "The disable button is focused");
  is(card.parentNode, enabledSection, "The card is in the enabled section");
  let disabled = BrowserTestUtils.waitForEvent(list, "move");
  space();
  await disabled;
  is(moreOptionsMenu.open, false, "The menu is closed");
  is(card.parentNode, disabledSection,
     "The card is now in the disabled section");

  // Open the menu again.
  shown = BrowserTestUtils.waitForEvent(moreOptionsMenu, "shown");
  isFocused(moreOptionsButton, "The more options button is focused");
  space();
  await shown;

  // Remove the add-on.
  tab();
  tab();
  let removeButton = card.querySelector('[action="remove"]');
  isFocused(removeButton, "The remove button is focused");
  let removed = BrowserTestUtils.waitForEvent(list, "remove");
  space();
  await removed;
  is(card.parentNode, null, "The card is no longer on the page");

  await extension.unload();
  await closeView(win);
});

add_task(async function testExtensionReordering() {
  let extensions = createExtensions([
    {name: "Extension One"},
    {name: "This is last"},
    {name: "An extension, is first"},
  ]);

  await Promise.all(extensions.map(extension => extension.startup()));

  let win = await loadInitialView("extension");
  let doc = win.document;

  // Get a reference to the addon-list for events.
  let list = doc.querySelector("addon-list");

  // Find the related cards, they should all have @mochi.test ids.
  let enabledSection = getSection(doc, "enabled");
  let cards = getTestCards(enabledSection);

  is(cards.length, 3, "Each extension has an addon-card");

  let order = Array.from(cards).map(card => card.addon.name);
  Assert.deepEqual(order, [
    "An extension, is first",
    "Extension One",
    "This is last",
  ], "The add-ons are sorted by name");

  // Disable the second extension.
  let disabledSection = getSection(doc, "disabled");
  ok(isEmpty(disabledSection), "The disabled section is initially empty");

  // Disable the add-ons in a different order.
  let reorderedCards = [cards[1], cards[0], cards[2]];
  for (let {addon} of reorderedCards) {
    let moved = BrowserTestUtils.waitForEvent(list, "move");
    await addon.disable();
    await moved;
  }

  order = Array.from(getTestCards(disabledSection))
    .map(card => card.addon.name);
  Assert.deepEqual(order, [
    "An extension, is first",
    "Extension One",
    "This is last",
  ], "The add-ons are sorted by name");

  // All of our installed add-ons are disabled, install a new one.
  let [newExtension] = createExtensions([{name: "Extension New"}]);
  let added = BrowserTestUtils.waitForEvent(list, "add");
  await newExtension.startup();
  await added;

  let [newCard] = getTestCards(enabledSection);
  is(newCard.addon.name, "Extension New",
     "The new add-on is in the enabled list");

  // Enable everything again.
  for (let {addon} of cards) {
    let moved = BrowserTestUtils.waitForEvent(list, "move");
    await addon.enable();
    await moved;
  }

  order = Array.from(getTestCards(enabledSection))
    .map(card => card.addon.name);
  Assert.deepEqual(order, [
    "An extension, is first",
    "Extension New",
    "Extension One",
    "This is last",
  ], "The add-ons are sorted by name");

  // Remove the new extension.
  let removed = BrowserTestUtils.waitForEvent(list, "remove");
  await newExtension.unload();
  await removed;
  is(newCard.parentNode, null, "The new card has been removed");

  await Promise.all(extensions.map(extension => extension.unload()));
  await closeView(win);
});

add_task(async function testThemeList() {
  let theme = ExtensionTestUtils.loadExtension({
    manifest: {
      applications: {gecko: {id: "theme@mochi.test"}},
      name: "My theme",
      theme: {},
    },
    useAddonManager: "temporary",
  });

  let win = await loadInitialView("theme");
  let doc = win.document;

  let list = doc.querySelector("addon-list");

  let cards = getTestCards(list);
  is(cards.length, 0, "There are no test themes to start");

  let added = BrowserTestUtils.waitForEvent(list, "add");
  await theme.startup();
  await added;

  cards = getTestCards(list);
  is(cards.length, 1, "There is now one custom theme");

  let [card] = cards;
  is(card.addon.name, "My theme", "The card is for the test theme");

  let enabledSection = getSection(doc, "enabled");
  let disabledSection = getSection(doc, "disabled");

  await TestUtils.waitForCondition(() =>
      enabledSection.querySelectorAll("addon-card").length == 1);

  is(card.parentNode, enabledSection,
     "The new theme card is in the enabled section");
  is(enabledSection.querySelectorAll("addon-card").length,
     1, "There is one enabled theme");

  let themesChanged = waitForThemeChange(list);
  card.querySelector('[action="toggle-disabled"]').click();
  await themesChanged;

  await TestUtils.waitForCondition(() =>
      enabledSection.querySelectorAll("addon-card").length == 1);

  is(card.parentNode, disabledSection,
     "The card is now in the disabled section");
  is(enabledSection.querySelectorAll("addon-card").length,
     1, "There is one enabled theme");

  await theme.unload();
  await closeView(win);
});

add_task(async function testBuiltInThemeButtons() {
  let win = await loadInitialView("theme");
  let doc = win.document;

  // Find the addon-list to listen for events.
  let list = doc.querySelector("addon-list");
  let enabledSection = getSection(doc, "enabled");
  let disabledSection = getSection(doc, "disabled");

  let defaultTheme = getCardByAddonId(doc, "default-theme@mozilla.org");
  let darkTheme = getCardByAddonId(doc, "firefox-compact-dark@mozilla.org");

  // Check that themes are in the expected spots.
  is(defaultTheme.parentNode, enabledSection, "The default theme is enabled");
  is(darkTheme.parentNode, disabledSection, "The dark theme is disabled");

  // The default theme shouldn't have remove or disable options.
  let defaultButtons = {
    toggleDisabled: defaultTheme.querySelector('[action="toggle-disabled"]'),
    remove: defaultTheme.querySelector('[action="remove"]'),
  };
  is(defaultButtons.toggleDisabled.hidden, true, "Disable is hidden");
  is(defaultButtons.remove.hidden, true, "Remove is hidden");

  // The dark theme should have an enable button, but not remove.
  let darkButtons = {
    toggleDisabled: darkTheme.querySelector('[action="toggle-disabled"]'),
    remove: darkTheme.querySelector('[action="remove"]'),
  };
  is(darkButtons.toggleDisabled.hidden, false, "Enable is visible");
  is(darkButtons.remove.hidden, true, "Remove is hidden");

  // Enable the dark theme and check the buttons again.
  let themesChanged = waitForThemeChange(list);
  darkButtons.toggleDisabled.click();
  await themesChanged;

  await TestUtils.waitForCondition(() =>
      enabledSection.querySelectorAll("addon-card").length == 1);

  // Check the buttons.
  is(defaultButtons.toggleDisabled.hidden, false, "Enable is visible");
  is(defaultButtons.remove.hidden, true, "Remove is hidden");
  is(darkButtons.toggleDisabled.hidden, false, "Disable is visible");
  is(darkButtons.remove.hidden, true, "Remove is hidden");

  // Disable the dark theme.
  themesChanged = waitForThemeChange(list);
  darkButtons.toggleDisabled.click();
  await themesChanged;

  await TestUtils.waitForCondition(() =>
      enabledSection.querySelectorAll("addon-card").length == 1);

  // The themes are back to their starting posititons.
  is(defaultTheme.parentNode, enabledSection, "Default is enabled");
  is(darkTheme.parentNode, disabledSection, "Dark is disabled");

  await closeView(win);
});

add_task(async function testOnlyTypeIsShown() {
  let win = await loadInitialView("theme");
  let doc = win.document;

  // Find the addon-list to listen for events.
  let list = doc.querySelector("addon-list");

  let extension = ExtensionTestUtils.loadExtension({
    manifest: {
      name: "Test extension",
      applications: {gecko: {id: "test@mochi.test"}},
    },
    useAddonManager: "temporary",
  });

  let skipped = BrowserTestUtils.waitForEvent(
    list, "skip-add", (e) => e.detail == "type-mismatch");
  await extension.startup();
  await skipped;

  let cards = getTestCards(list);
  is(cards.length, 0, "There are no test extension cards");

  await extension.unload();
  await closeView(win);
});

add_task(async function testPluginIcons() {
  const pluginIconUrl = "chrome://global/skin/plugins/pluginGeneric.svg";

  let win = await loadInitialView("plugin");
  let doc = win.document;

  // Check that the icons are set to the plugin icon.
  let icons = doc.querySelectorAll(".card-heading-icon");
  ok(icons.length > 0, "There are some plugins listed");

  for (let icon of icons) {
    is(icon.src, pluginIconUrl, "Plugins use the plugin icon");
  }

  await closeView(win);
});
