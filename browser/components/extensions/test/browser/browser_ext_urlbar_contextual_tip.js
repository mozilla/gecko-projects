"use strict";

const { UrlbarTestUtils } = ChromeUtils.import(
  "resource://testing-common/UrlbarTestUtils.jsm"
);

const {
  Management: {
    global: { windowTracker },
  },
} = ChromeUtils.import("resource://gre/modules/Extension.jsm", null);

/**
 * The two tests below test that the urlbar permission
 * is required to use the contextual tip API.
 */
add_task(async function test_contextual_tip_without_urlbar_permission() {
  let ext = ExtensionTestUtils.loadExtension({
    isPrivileged: true,
    background() {
      browser.test.assertEq(
        browser.urlbar,
        undefined,
        "'urlbar' permission is required"
      );
    },
  });
  await ext.startup();
  await ext.unload();
});

add_task(async function test_contextual_tip_with_urlbar_permission() {
  let ext = ExtensionTestUtils.loadExtension({
    isPrivileged: true,
    manifest: {
      permissions: ["urlbar"],
    },
    background() {
      browser.test.assertTrue(!!browser.urlbar.contextualTip);
    },
  });
  await ext.startup();
  await ext.unload();
});

/**
 * Tests that calling `set` with a details object will
 * cause the urlbar's view to open,
 * the contextual tip's texts will be updated,
 * and the contextual tip will be hidden when the view closes.
 * It also tests that the contextual tip's texts can be updated
 * when the view is already open and a tip is already displayed.
 */
add_task(async function set_contextual_tip_and_set_contextual_tip_again() {
  let win = await BrowserTestUtils.openNewBrowserWindow();

  let ext = ExtensionTestUtils.loadExtension({
    isPrivileged: true,
    manifest: {
      permissions: ["urlbar"],
    },
    background() {
      browser.urlbar.contextualTip.set({
        title: "Contextual tip's title",
        buttonTitle: "Button",
        linkTitle: "Link",
      });

      browser.test.onMessage.addListener(() => {
        browser.urlbar.contextualTip.set({
          title: "Updated title",
          buttonTitle: "Updated button title",
          linkTitle: "Updated link title",
        });
        browser.test.sendMessage("updated");
      });
    },
  });

  await ext.startup();
  await BrowserTestUtils.waitForCondition(() =>
    UrlbarTestUtils.isPopupOpen(win)
  );

  Assert.equal(
    win.gURLBar.view.contextualTip._elements.title.textContent,
    "Contextual tip's title"
  );
  Assert.equal(
    win.gURLBar.view.contextualTip._elements.button.textContent,
    "Button"
  );
  Assert.equal(
    win.gURLBar.view.contextualTip._elements.link.textContent,
    "Link"
  );

  ext.sendMessage("update tip's texts");
  await ext.awaitMessage("updated");

  // Checks the tip's texts were updated while the urlbar view is open
  Assert.equal(
    win.gURLBar.view.contextualTip._elements.title.textContent,
    "Updated title"
  );
  Assert.equal(
    win.gURLBar.view.contextualTip._elements.button.textContent,
    "Updated button title"
  );
  Assert.equal(
    win.gURLBar.view.contextualTip._elements.link.textContent,
    "Updated link title"
  );

  await UrlbarTestUtils.promisePopupClose(win);
  Assert.ok(
    BrowserTestUtils.is_hidden(
      win.gURLBar.view.contextualTip._elements.container
    )
  );
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window: win,
    waitForFocus,
    value: " ",
  });
  Assert.ok(
    BrowserTestUtils.is_hidden(
      win.gURLBar.view.contextualTip._elements.container
    )
  );
  await UrlbarTestUtils.promisePopupClose(win);

  await ext.unload();
  await BrowserTestUtils.closeWindow(win);
});

/**
 * Tests that calling `browser.urlbar.contextualTip.remove` will
 * hide the contextual tip and close the popup panel if a contextual tip
 * is present and if there's no search suggestions in the popup panel.
 * In this test, the urlbar input field is not in focus and there's no text
 * in the field so we assume that there's no search suggestions.
 */
add_task(async function hiding_contextual_tip_closing_popup_panel() {
  let win = await BrowserTestUtils.openNewBrowserWindow();

  let ext = ExtensionTestUtils.loadExtension({
    isPrivileged: true,
    manifest: {
      permissions: ["urlbar"],
    },
    background() {
      browser.urlbar.contextualTip.set({
        title: "Contextual tip's title",
        buttonTitle: "Button",
        linkTitle: "Link",
      });

      browser.test.onMessage.addListener(async msg => {
        browser.urlbar.contextualTip.remove();
        browser.test.sendMessage("next-test");
      });
    },
  });

  await ext.startup();
  await BrowserTestUtils.waitForCondition(() =>
    UrlbarTestUtils.isPopupOpen(win)
  );

  ext.sendMessage("hide-contextual-tip");
  await ext.awaitMessage("next-test");

  Assert.ok(
    BrowserTestUtils.is_hidden(
      win.gURLBar.view.contextualTip._elements.container
    )
  );
  Assert.ok(!UrlbarTestUtils.isPopupOpen(win));
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window: win,
    waitForFocus,
    value: " ",
  });
  Assert.ok(
    BrowserTestUtils.is_hidden(
      win.gURLBar.view.contextualTip._elements.container
    )
  );
  await UrlbarTestUtils.promisePopupClose(win);

  await ext.unload();
  await BrowserTestUtils.closeWindow(win);
});

/**
 * Tests that calling `browser.urlbar.contextualTip.remove` will
 * hide the contextual tip and leave the popup open if a contextual tip
 * is present and if there are search suggestions in the popup panel.
 * In this test, we enter "hello world" in the urlbar input field and assume
 * there will be at least one search suggestion.
 */
add_task(async function hiding_contextual_tip_and_leaving_popup_panel_open() {
  let win = await BrowserTestUtils.openNewBrowserWindow();

  let ext = ExtensionTestUtils.loadExtension({
    isPrivileged: true,
    manifest: {
      permissions: ["urlbar"],
    },
    background() {
      browser.urlbar.contextualTip.set({
        title: "Contextual tip's title",
        buttonTitle: "Button",
        linkTitle: "Link",
      });

      browser.test.onMessage.addListener(async msg => {
        browser.urlbar.contextualTip.remove();
        browser.test.sendMessage("next-test");
      });
    },
  });

  await ext.startup();
  await BrowserTestUtils.waitForCondition(() =>
    UrlbarTestUtils.isPopupOpen(win)
  );
  Assert.equal(
    win.gURLBar.view.contextualTip._elements.title.textContent,
    "Contextual tip's title"
  );
  Assert.equal(
    win.gURLBar.view.contextualTip._elements.button.textContent,
    "Button"
  );
  Assert.equal(
    win.gURLBar.view.contextualTip._elements.link.textContent,
    "Link"
  );

  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window: win,
    waitForFocus,
    value: "hello world",
  });

  ext.sendMessage("hide-contextual-tip");
  await ext.awaitMessage("next-test");

  Assert.ok(
    BrowserTestUtils.is_hidden(
      win.gURLBar.view.contextualTip._elements.container
    )
  );
  Assert.ok(UrlbarTestUtils.isPopupOpen(win));

  await ext.unload();
  await BrowserTestUtils.closeWindow(win);
});

/**
 * Tests that setting the button title or link title to null will
 * hide the button or link (respectively).
 */
add_task(async function hide_button_and_link() {
  let win = await BrowserTestUtils.openNewBrowserWindow();

  let ext = ExtensionTestUtils.loadExtension({
    isPrivileged: true,
    manifest: {
      permissions: ["urlbar"],
    },
    background() {
      browser.urlbar.contextualTip.set({
        title: "contextual tip's title",
        buttonTitle: "contextual tip's button title",
        linkTitle: "contextual tip's link title",
      });
      browser.test.onMessage.addListener(msg => {
        browser.urlbar.contextualTip.set({
          title: "contextual tip's title",
        });
        browser.test.sendMessage("next-test");
      });
    },
  });

  await ext.startup();
  await BrowserTestUtils.waitForCondition(() =>
    UrlbarTestUtils.isPopupOpen(win)
  );

  Assert.equal(
    win.gURLBar.view.contextualTip._elements.title.textContent,
    "contextual tip's title"
  );
  Assert.equal(
    win.gURLBar.view.contextualTip._elements.button.textContent,
    "contextual tip's button title"
  );
  Assert.equal(
    win.gURLBar.view.contextualTip._elements.link.textContent,
    "contextual tip's link title"
  );

  ext.sendMessage("hide button and link");
  await ext.awaitMessage("next-test");

  Assert.ok(
    BrowserTestUtils.is_hidden(win.gURLBar.view.contextualTip._elements.button)
  );
  Assert.ok(
    BrowserTestUtils.is_hidden(win.gURLBar.view.contextualTip._elements.link)
  );

  await ext.unload();
  await BrowserTestUtils.closeWindow(win);
});

/**
 * Tests that if there's no `title` property in the argument for
 * `set` then a type error is thrown.
 */
add_task(async function test_showing_contextual_tip() {
  let ext = ExtensionTestUtils.loadExtension({
    isPrivileged: true,
    manifest: {
      permissions: ["urlbar"],
    },
    background() {
      browser.test.assertThrows(
        () =>
          browser.urlbar.contextualTip.set({
            buttonTitle: "button title",
            linkTitle: "link title",
          }),
        /Type error for parameter/,
        "Throws error because there's no title property in the argument"
      );
    },
  });
  await ext.startup();
  await ext.unload();
});

/**
 * Tests that when the extension is removed, `onShutdown` will remove
 * the contextual_tip from the DOM.
 */
add_task(async function contextual_tip_removed_onShutdown() {
  let win = await BrowserTestUtils.openNewBrowserWindow();

  let ext = ExtensionTestUtils.loadExtension({
    isPrivileged: true,
    manifest: {
      permissions: ["urlbar"],
    },
    background() {
      browser.urlbar.contextualTip.set({
        title: "Hello World!",
        buttonTitle: "Click Me",
        linkTitle: "Link",
      });
    },
  });

  await ext.startup();
  await BrowserTestUtils.waitForCondition(
    () => !!win.gURLBar.view.contextualTip._elements
  );
  Assert.ok(win.document.getElementById("urlbar-contextual-tip"));
  await ext.unload();

  // There should be no contextual tip in the DOM after onShutdown

  Assert.ok(!win.document.getElementById("urlbar-contextual-tip"));

  await BrowserTestUtils.closeWindow(win);
});

/**
 * The following tests for the contextual tip's
 * `onLinkClicked` and `onButtonClicked` events.
 */

/**
 * Tests that a click listener can be set on the given type of element.
 * @param {string} elementType Either "button" or "link"
 * @param {boolean} shouldAddListenerBeforeTip
 *   Indicates whether the click listener should be added before or after
 *   the setting the contextual tip.
 */
async function add_onclick_to_element(elementType, shouldAddListenerBeforeTip) {
  let win = await BrowserTestUtils.openNewBrowserWindow();
  const windowId = windowTracker.getId(win);

  let ext = ExtensionTestUtils.loadExtension({
    isPrivileged: true,
    manifest: {
      permissions: ["urlbar"],
    },
    background() {
      browser.test.onMessage.addListener(
        (msg, elementType, shouldAddListenerBeforeTip) => {
          if (msg == "elementType") {
            const capitalizedElementType =
              elementType[0].toUpperCase() + elementType.substring(1);

            const addListener = () => {
              browser.urlbar.contextualTip[
                `on${capitalizedElementType}Clicked`
              ].addListener(windowId => {
                browser.test.sendMessage(`on-${elementType}-clicked`, windowId);
              });
            };

            if (shouldAddListenerBeforeTip) {
              addListener();
            }

            browser.urlbar.contextualTip.set({
              title: "Title",
              [`${elementType}Title`]: "Click Me!",
            });

            if (!shouldAddListenerBeforeTip) {
              addListener();
            }

            browser.test.sendMessage("next-test");
          }
        }
      );
    },
  });

  await ext.startup();

  ext.sendMessage("elementType", elementType, shouldAddListenerBeforeTip);
  await ext.awaitMessage("next-test");

  await BrowserTestUtils.waitForCondition(
    () => !!win.gURLBar.view.contextualTip._elements
  );

  win.gURLBar.view.contextualTip._elements[elementType].click();
  const windowIdFromExtension = await ext.awaitMessage(
    `on-${elementType}-clicked`
  );

  Assert.equal(windowId, windowIdFromExtension);

  await ext.unload();
  await BrowserTestUtils.closeWindow(win);
}

/**
 * Tests that a click listener can be added to the button
 * through `onButtonClicked` and to the link through `onLinkClicked`.
 */
add_task(async () => {
  await add_onclick_to_element("button");
});
add_task(async () => {
  await add_onclick_to_element("link");
});

/**
 * Tests that a click listener can be added to the button
 * through `onButtonClicked` and to the link through `onLinkClicked`
 * before a contextual tip has been created.
 */
add_task(async () => {
  await add_onclick_to_element("button", true);
});
add_task(async () => {
  await add_onclick_to_element("link", true);
});

/**
 * The tests for below are for the contextual_tip's icon support.
 */

/**
 * Tests that the contextual tip's default theme icon can be set
 * to a favicon's url.
 */
add_task(async function test_set_icon_given_favicon() {
  let win = await BrowserTestUtils.openNewBrowserWindow();

  let ext = ExtensionTestUtils.loadExtension({
    isPrivileged: true,
    manifest: {
      permissions: ["urlbar"],
    },
    background() {
      browser.urlbar.contextualTip.set({
        icon: { defaultIcon: "data:image/png;base64,SFRUUCBpY29uIGRhdGE=" },
        title: "the title is required",
      });
      browser.test.sendMessage(
        "ready",
        browser.runtime.getURL("data:image/png;base64,SFRUUCBpY29uIGRhdGE=")
      );
    },
  });

  await ext.startup();
  await BrowserTestUtils.waitForCondition(() =>
    UrlbarTestUtils.isPopupOpen(win)
  );
  const iconURL = await ext.awaitMessage("ready");
  const backgroundImageProperty = `url("${iconURL}")`;
  const iconElement = win.gURLBar.view.contextualTip._elements.icon;
  Assert.equal(
    backgroundImageProperty,
    win.getComputedStyle(iconElement).backgroundImage
  );

  await ext.unload();
  await BrowserTestUtils.closeWindow(win);
});

/**
 * Tests that the contextual tip's default theme icon can be set
 * to the relative path of an icon.
 */
add_task(async function test_set_icon_given_path() {
  let win = await BrowserTestUtils.openNewBrowserWindow();

  let ext = ExtensionTestUtils.loadExtension({
    isPrivileged: true,
    manifest: {
      permissions: ["urlbar"],
    },
    background() {
      browser.urlbar.contextualTip.set({
        icon: { defaultIcon: "icons/icon.png" },
        title: "the title is required",
      });
      browser.test.sendMessage(
        "ready",
        browser.runtime.getURL("icons/icon.png")
      );
    },
  });

  await ext.startup();
  await BrowserTestUtils.waitForCondition(() =>
    UrlbarTestUtils.isPopupOpen(win)
  );
  const iconURL = await ext.awaitMessage("ready");
  const backgroundImageProperty = `url("${iconURL}")`;
  const iconElement = win.gURLBar.view.contextualTip._elements.icon;
  Assert.equal(
    backgroundImageProperty,
    win.getComputedStyle(iconElement).backgroundImage
  );

  await ext.unload();
  await BrowserTestUtils.closeWindow(win);
});

/**
 * Tests that when a call to `set` is made and
 * the `icon` is omitted from its arguments, then
 * the contexual tip will use the icon path specified in the
 * extension's `icons` manifest property.
 */
add_task(async function test_set_icon_given_path() {
  let win = await BrowserTestUtils.openNewBrowserWindow();

  let ext = ExtensionTestUtils.loadExtension({
    isPrivileged: true,
    manifest: {
      permissions: ["urlbar"],
      icons: {
        "19": "extension_icon.png",
      },
    },
    background() {
      browser.urlbar.contextualTip.set({
        title: "the title is required",
      });
      browser.test.sendMessage(
        "ready",
        browser.runtime.getURL("extension_icon.png")
      );
    },
  });

  await ext.startup();
  await BrowserTestUtils.waitForCondition(() =>
    UrlbarTestUtils.isPopupOpen(win)
  );
  const iconURL = await ext.awaitMessage("ready");
  const backgroundImageProperty = `url("${iconURL}")`;
  const iconElement = win.gURLBar.view.contextualTip._elements.icon;
  Assert.equal(
    backgroundImageProperty,
    win.getComputedStyle(iconElement).backgroundImage
  );
  await ext.unload();
  await BrowserTestUtils.closeWindow(win);
});

/**
 * Tests that the contextual tip will set the appropriate theme icon when
 * there's a `icon.themeIcons` array in the argument passed into a call to
 * `set` where the `icon.themeIcons` array contains one
 * theme icon object of size 19.
 */
add_task(async function test_themeIcons() {
  let win = await BrowserTestUtils.openNewBrowserWindow();

  let ext = ExtensionTestUtils.loadExtension({
    isPrivileged: true,
    manifest: {
      permissions: ["urlbar"],
    },
    background() {
      browser.urlbar.contextualTip.set({
        icon: {
          defaultIcon: "icons/default.png",
          themeIcons: [
            {
              light: "icons/light.png",
              dark: "icons/dark.png",
              size: 19,
            },
          ],
        },
        title: "the title is required",
      });
      browser.test.sendMessage("ready", {
        expectedDefaultIconURL: browser.runtime.getURL("icons/default.png"),
        expectedLightIconURL: browser.runtime.getURL("icons/light.png"),
        expectedDarkIconURL: browser.runtime.getURL("icons/dark.png"),
      });
    },
  });

  await ext.startup();
  await BrowserTestUtils.waitForCondition(() =>
    UrlbarTestUtils.isPopupOpen(win)
  );
  const {
    expectedDefaultIconURL,
    expectedLightIconURL,
    expectedDarkIconURL,
  } = await ext.awaitMessage("ready");

  // Tests that the default icon appears.
  let iconElement = win.gURLBar.view.contextualTip._elements.icon;
  Assert.equal(
    `url("${expectedDefaultIconURL}")`,
    win.getComputedStyle(iconElement).backgroundImage
  );

  // Tests that the dark icon appears on a light theme.
  // This test uses popup colors from
  // `browser/themes/addons/light/manifest.json`.
  let lightThemeExt = ExtensionTestUtils.loadExtension({
    manifest: {
      theme: {
        colors: {
          popup: "#fff",
          popup_text: "#0c0c0d",
          popup_border: "#ccc",
        },
      },
    },
  });
  await lightThemeExt.startup();
  iconElement = win.gURLBar.view.contextualTip._elements.icon;
  Assert.equal(
    `url("${expectedDarkIconURL}")`,
    win.getComputedStyle(iconElement).backgroundImage
  );
  await lightThemeExt.unload();

  // Tests that the light icon appears on a dark theme.
  // This test uses popup colors from `browser/themes/addons/dark/manifest.json`
  let darkThemeExt = ExtensionTestUtils.loadExtension({
    manifest: {
      theme: {
        colors: {
          popup: "#4a4a4f",
          popup_text: "rgb(249, 249, 250)",
          popup_border: "#27272b",
        },
      },
    },
  });
  await darkThemeExt.startup();
  iconElement = win.gURLBar.view.contextualTip._elements.icon;
  Assert.equal(
    `url("${expectedLightIconURL}")`,
    win.getComputedStyle(iconElement).backgroundImage
  );
  await darkThemeExt.unload();

  await ext.unload();
  await BrowserTestUtils.closeWindow(win);
});

const RESOLUTION_PREF = "layout.css.devPixelsPerPx";

/**
 * Tests that the contextual_tip will use icons of size 16 for
 * low-resolution screens when there's a `icon.themeIcons` array in
 * the argument passed into a call to `set` where the
 * `icon.themeIcons` array contains two theme icon objects of sizes 16 and 32.
 */
add_task(async function test_themeIcons_size_16() {
  let win = await BrowserTestUtils.openNewBrowserWindow();

  let ext = ExtensionTestUtils.loadExtension({
    isPrivileged: true,
    manifest: {
      permissions: ["urlbar"],
    },
    background() {
      browser.urlbar.contextualTip.set({
        icon: {
          defaultIcon: "icons/default.png",
          themeIcons: [
            {
              light: "icons/light-16.png",
              dark: "icons/dark-16.png",
              size: 16,
            },
            {
              light: "icons/light-32.png",
              dark: "icons/dark-32.png",
              size: 32,
            },
          ],
        },
        title: "the title is required",
      });
      browser.test.sendMessage("ready", {
        expectedDefaultIconURL: browser.runtime.getURL("icons/default.png"),
        expectedLightIconURL: browser.runtime.getURL("icons/light-16.png"),
        expectedDarkIconURL: browser.runtime.getURL("icons/dark-16.png"),
      });
    },
  });

  // Testing for icons of size 16 on a low-resolution display.
  await SpecialPowers.pushPrefEnv({ set: [[RESOLUTION_PREF, 1]] });

  await ext.startup();
  await BrowserTestUtils.waitForCondition(() =>
    UrlbarTestUtils.isPopupOpen(win)
  );

  const {
    expectedDefaultIconURL,
    expectedLightIconURL,
    expectedDarkIconURL,
  } = await ext.awaitMessage("ready");

  // Tests that the default icon appears.
  let iconElement = win.gURLBar.view.contextualTip._elements.icon;
  Assert.equal(
    `url("${expectedDefaultIconURL}")`,
    win.getComputedStyle(iconElement).backgroundImage
  );

  // Tests that the dark icon appears on a light theme.
  // This test uses popup colors from
  // `browser/themes/addons/light/manifest.json`.
  let lightThemeExt = ExtensionTestUtils.loadExtension({
    manifest: {
      theme: {
        colors: {
          popup: "#fff",
          popup_text: "#0c0c0d",
          popup_border: "#ccc",
        },
      },
    },
  });
  await lightThemeExt.startup();
  iconElement = win.gURLBar.view.contextualTip._elements.icon;
  Assert.equal(
    `url("${expectedDarkIconURL}")`,
    win.getComputedStyle(iconElement).backgroundImage
  );
  await lightThemeExt.unload();

  // Tests that the light icon appears on a dark theme.
  // This test uses popup colors from `browser/themes/addons/dark/manifest.json`
  let darkThemeExt = ExtensionTestUtils.loadExtension({
    manifest: {
      theme: {
        colors: {
          popup: "#4a4a4f",
          popup_text: "rgb(249, 249, 250)",
          popup_border: "#27272b",
        },
      },
    },
  });
  await darkThemeExt.startup();
  iconElement = win.gURLBar.view.contextualTip._elements.icon;
  Assert.equal(
    `url("${expectedLightIconURL}")`,
    win.getComputedStyle(iconElement).backgroundImage
  );
  await darkThemeExt.unload();

  await SpecialPowers.popPrefEnv();
  await ext.unload();
  await BrowserTestUtils.closeWindow(win);
});

/**
 * Tests that the contextual_tip will use icons of size 32 for
 * high-resolution screens when there's a `icon.themeIcons` array in
 * the argument passed into a call to `set` where the
 * `icon.themeIcons` array contains two theme icon objects of sizes 16 and 32.
 */
add_task(async function test_themeIcons_size_32() {
  let win = await BrowserTestUtils.openNewBrowserWindow();

  let ext = ExtensionTestUtils.loadExtension({
    isPrivileged: true,
    manifest: {
      permissions: ["urlbar"],
    },
    background() {
      browser.urlbar.contextualTip.set({
        icon: {
          defaultIcon: "icons/default.png",
          themeIcons: [
            {
              light: "icons/light-16.png",
              dark: "icons/dark-16.png",
              size: 16,
            },
            {
              light: "icons/light-32.png",
              dark: "icons/dark-32.png",
              size: 32,
            },
          ],
        },
        title: "the title is required",
      });
      browser.test.sendMessage("ready", {
        expectedDefaultIconURL: browser.runtime.getURL("icons/default.png"),
        expectedLightIconURL: browser.runtime.getURL("icons/light-32.png"),
        expectedDarkIconURL: browser.runtime.getURL("icons/dark-32.png"),
      });
    },
  });

  // Testing for icons of size 32 on a high-resolution display.
  await SpecialPowers.pushPrefEnv({ set: [[RESOLUTION_PREF, 2]] });

  await ext.startup();
  await BrowserTestUtils.waitForCondition(() =>
    UrlbarTestUtils.isPopupOpen(win)
  );
  const {
    expectedDefaultIconURL,
    expectedLightIconURL,
    expectedDarkIconURL,
  } = await ext.awaitMessage("ready");

  // Tests that the default icon appears.
  let iconElement = win.gURLBar.view.contextualTip._elements.icon;
  Assert.equal(
    `url("${expectedDefaultIconURL}")`,
    win.getComputedStyle(iconElement).backgroundImage
  );

  // Tests that the dark icon appears on a light theme.
  // This test uses popup colors from
  // `browser/themes/addons/light/manifest.json`.
  let lightThemeExt = ExtensionTestUtils.loadExtension({
    manifest: {
      theme: {
        colors: {
          popup: "#fff",
          popup_text: "#0c0c0d",
          popup_border: "#ccc",
        },
      },
    },
  });
  await lightThemeExt.startup();
  iconElement = win.gURLBar.view.contextualTip._elements.icon;
  Assert.equal(
    `url("${expectedDarkIconURL}")`,
    win.getComputedStyle(iconElement).backgroundImage
  );
  await lightThemeExt.unload();

  // Tests that the light icon appears on a dark theme.
  // This test uses popup colors from `browser/themes/addons/dark/manifest.json`
  let darkThemeExt = ExtensionTestUtils.loadExtension({
    manifest: {
      theme: {
        colors: {
          popup: "#4a4a4f",
          popup_text: "rgb(249, 249, 250)",
          popup_border: "#27272b",
        },
      },
    },
  });
  await darkThemeExt.startup();
  iconElement = win.gURLBar.view.contextualTip._elements.icon;
  Assert.equal(
    `url("${expectedLightIconURL}")`,
    win.getComputedStyle(iconElement).backgroundImage
  );
  await darkThemeExt.unload();

  await SpecialPowers.popPrefEnv();
  await ext.unload();
  await BrowserTestUtils.closeWindow(win);
});
