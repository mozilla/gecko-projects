/* -*- Mode: indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim: set sts=2 sw=2 et tw=80: */
"use strict";

const {AddonTestUtils} = ChromeUtils.import("resource://testing-common/AddonTestUtils.jsm");
const {ExtensionPermissions} = ChromeUtils.import("resource://gre/modules/ExtensionPermissions.jsm");

var gManagerWindow;

AddonTestUtils.initMochitest(this);

function get_test_items() {
  var items = {};

  for (let item of gManagerWindow.document.getElementById("addon-list").childNodes) {
    items[item.mAddon.id] = item;
  }

  return items;
}

function get(aId) {
  return gManagerWindow.document.getElementById(aId);
}

async function hasPrivateAllowed(id) {
  let perms = await ExtensionPermissions.get(id);
  return perms.permissions.length == 1 &&
         perms.permissions[0] == "internal:privateBrowsingAllowed";
}

add_task(function clearInitialTelemetry() {
  // Clear out any telemetry data that existed before this file is run.
  Services.telemetry.clearEvents();
});

add_task(async function test_addon() {
  await SpecialPowers.pushPrefEnv({set: [["extensions.allowPrivateBrowsingByDefault", false]]});

  let addons = new Map([
    ["@test-default", {
      useAddonManager: "temporary",
      manifest: {
        applications: {
          gecko: {id: "@test-default"},
        },
      },
    }],
    ["@test-override", {
      useAddonManager: "temporary",
      manifest: {
        applications: {
          gecko: {id: "@test-override"},
        },
      },
      incognitoOverride: "spanning",
    }],
    ["@test-override-permanent", {
      useAddonManager: "permanent",
      manifest: {
        applications: {
          gecko: {id: "@test-override-permanent"},
        },
      },
      incognitoOverride: "spanning",
    }],
    ["@test-not-allowed", {
      useAddonManager: "temporary",
      manifest: {
        applications: {
          gecko: {id: "@test-not-allowed"},
        },
        incognito: "not_allowed",
      },
    }],
  ]);
  let extensions = [];
  for (let definition of addons.values()) {
    let extension = ExtensionTestUtils.loadExtension(definition);
    extensions.push(extension);
    await extension.startup();
  }

  gManagerWindow = await open_manager("addons://list/extension");
  let doc = gManagerWindow.document;
  let items = get_test_items();
  for (let [id, definition] of addons.entries()) {
    ok(items[id], `${id} listed`);
    let badge = doc.getAnonymousElementByAttribute(items[id], "anonid", "privateBrowsing");
    if (definition.incognitoOverride == "spanning") {
      is_element_visible(badge, `private browsing badge is visible`);
    } else {
      is_element_hidden(badge, `private browsing badge is hidden`);
    }
  }
  await close_manager(gManagerWindow);

  for (let [id, definition] of addons.entries()) {
    gManagerWindow = await open_manager("addons://detail/" + encodeURIComponent(id));
    ok(true, `==== ${id} detail opened`);
    if (definition.manifest.incognito == "not_allowed") {
      is_element_hidden(get("detail-privateBrowsing-row"), "Private browsing should be hidden");
      is_element_hidden(get("detail-privateBrowsing-row-footer"), "Private browsing footer should be hidden");
      ok(!await hasPrivateAllowed(id), "Private browsing permission not set");
      is_element_visible(get("detail-privateBrowsing-disallowed"), "Private browsing should be hidden");
      is_element_visible(get("detail-privateBrowsing-disallowed-footer"), "Private browsing footer should be hidden");
    } else {
      // This assumes PERM_CAN_CHANGE_PRIVATEBROWSING_ACCESS, we test other options in a later test in this file.
      is_element_visible(get("detail-privateBrowsing-row"), "Private browsing should be visible");
      is_element_visible(get("detail-privateBrowsing-row-footer"), "Private browsing footer should be visible");
      let privateBrowsing = gManagerWindow.document.getElementById("detail-privateBrowsing");
      if (definition.incognitoOverride == "spanning") {
        is(privateBrowsing.value, "1", "Private browsing should be on");
        ok(await hasPrivateAllowed(id), "Private browsing permission set");
        EventUtils.synthesizeMouseAtCenter(privateBrowsing.lastChild, { clickCount: 1 }, gManagerWindow);
        await TestUtils.waitForCondition(() => privateBrowsing.value == "0");
        is(privateBrowsing.value, "0", "Private browsing should be off");
        ok(!await hasPrivateAllowed(id), "Private browsing permission removed");
      } else {
        is(privateBrowsing.value, "0", "Private browsing should be off");
        ok(!await hasPrivateAllowed(id), "Private browsing permission not set");
        EventUtils.synthesizeMouseAtCenter(privateBrowsing.firstChild, { clickCount: 1 }, gManagerWindow);
        await TestUtils.waitForCondition(() => privateBrowsing.value == "1");
        is(privateBrowsing.value, "1", "Private browsing should be on");
        ok(await hasPrivateAllowed(id), "Private browsing permission set");
      }
    }
    await close_manager(gManagerWindow);
  }

  for (let extension of extensions) {
    await extension.unload();
  }

  const expectedExtras = {
    action: "privateBrowsingAllowed",
    view: "detail",
    type: "extension",
  };

  assertTelemetryMatches([
    ["action", "aboutAddons", "on", {...expectedExtras, addonId: "@test-default"}],
    ["action", "aboutAddons", "off", {...expectedExtras, addonId: "@test-override"}],
    ["action", "aboutAddons", "off", {...expectedExtras, addonId: "@test-override-permanent"}],
  ], {filterMethods: ["action"]});

  Services.prefs.clearUserPref("extensions.allowPrivateBrowsingByDefault");
});

add_task(async function test_addon_preferences_button() {
  await SpecialPowers.pushPrefEnv({set: [["extensions.allowPrivateBrowsingByDefault", false]]});

  let addons = new Map([
    ["test-inline-options@mozilla.com", {
      useAddonManager: "temporary",
      manifest: {
        name: "Extension with inline options",
        applications: {gecko: {id: "test-inline-options@mozilla.com"}},
        options_ui: {page: "options.html", open_in_tab: false},
      },
    }],
    ["test-newtab-options@mozilla.com", {
      useAddonManager: "temporary",
      manifest: {
        name: "Extension with options page in a new tab",
        applications: {gecko: {id: "test-newtab-options@mozilla.com"}},
        options_ui: {page: "options.html", open_in_tab: true},
      },
    }],
    ["test-not-allowed@mozilla.com", {
      useAddonManager: "temporary",
      manifest: {
        name: "Extension not allowed in PB windows",
        incognito: "not_allowed",
        applications: {gecko: {id: "test-not-allowed@mozilla.com"}},
        options_ui: {page: "options.html", open_in_tab: true},
      },
    }],
  ]);

  async function runTest(openInPrivateWin) {
    const win = await BrowserTestUtils.openNewBrowserWindow({
      private: openInPrivateWin,
    });

    gManagerWindow = await open_manager(
      "addons://list/extension", undefined, undefined, undefined, win);

    const doc = gManagerWindow.document;
    const checkPrefsVisibility = (id, hasInlinePrefs, expectVisible) => {
      if (!hasInlinePrefs) {
        const detailsPrefBtn = doc.getElementById("detail-prefs-btn");
        is(!detailsPrefBtn.hidden, expectVisible,
           `The ${id} prefs button in the addon details has the expected visibility`);
      } else {
        const hasInlineOptionsBrowser = !!doc.getElementById("addon-options");
        is(hasInlineOptionsBrowser, expectVisible,
           `The ${id} inline prefs in the addon details has the expected visibility`);
      }
    };

    const setAddonPrivateBrowsingAccess = async (id, allowPrivateBrowsing) => {
      const privateBrowsing = doc.getElementById("detail-privateBrowsing");

      is(privateBrowsing.value,
         allowPrivateBrowsing ? "0" : "1",
         `Private browsing should be initially ${allowPrivateBrowsing ? "off" : "on"}`);

      // Get the DOM element we want to click on (to allow or disallow the
      // addon on private browsing windows).
      const controlEl = allowPrivateBrowsing ?
        privateBrowsing.firstChild : privateBrowsing.lastChild;

      EventUtils.synthesizeMouseAtCenter(controlEl, { clickCount: 1 }, gManagerWindow);

      // Wait the private browsing access to be reflected in the about:addons
      // addon details page.
      await TestUtils.waitForCondition(
        () => privateBrowsing.value == allowPrivateBrowsing ? "1" : "0",
        "Waiting privateBrowsing value to be updated");

      is(privateBrowsing.value,
         allowPrivateBrowsing ? "1" : "0",
         `Private browsing should be initially ${allowPrivateBrowsing ? "on" : "off"}`);

      is(await hasPrivateAllowed(id), allowPrivateBrowsing,
         `Private browsing permission ${allowPrivateBrowsing ? "added" : "removed"}`);
    };

    const extensions = [];
    for (const definition of addons.values()) {
      const extension = ExtensionTestUtils.loadExtension(definition);
      extensions.push(extension);
      await extension.startup();
    }

    const items = get_test_items();

    for (const [id, definition] of addons.entries()) {
      // Check the preferences button in the addon list page.
      is(items[id]._preferencesBtn.hidden, openInPrivateWin,
        `The ${id} prefs button in the addon list has the expected visibility`);

      // Check the preferences button or inline frame in the addon
      // details page.
      info(`Opening addon details for ${id}`);
      const hasInlinePrefs = !definition.manifest.options_ui.open_in_tab;
      const onceViewChanged = BrowserTestUtils.waitForEvent(gManagerWindow, "ViewChanged");
      gManagerWindow.loadView(`addons://detail/${encodeURIComponent(id)}`);
      await onceViewChanged;

      checkPrefsVisibility(id, hasInlinePrefs, !openInPrivateWin);

      // While testing in a private window, also check that the preferences
      // are going to be visible when we toggle the PB access for the addon.
      if (openInPrivateWin && definition.manifest.incognito !== "not_allowed") {
        await Promise.all([
          BrowserTestUtils.waitForEvent(gManagerWindow, "ViewChanged"),
          setAddonPrivateBrowsingAccess(id, true),
        ]);
        checkPrefsVisibility(id, hasInlinePrefs, true);

        await Promise.all([
          BrowserTestUtils.waitForEvent(gManagerWindow, "ViewChanged"),
          setAddonPrivateBrowsingAccess(id, false),
        ]);
        checkPrefsVisibility(id, hasInlinePrefs, false);
      }
    }

    for (const extension of extensions) {
      await extension.unload();
    }

    await close_manager(gManagerWindow);
    await BrowserTestUtils.closeWindow(win);
  }

  // run tests in private and non-private windows.
  await runTest(true);
  await runTest(false);
});

add_task(async function test_addon_postinstall_incognito_hidden_checkbox() {
  await SpecialPowers.pushPrefEnv({set: [
    ["extensions.allowPrivateBrowsingByDefault", false],
    ["extensions.langpacks.signatures.required", false],
  ]});

  const TEST_ADDONS = [
    {
      manifest: {
        name: "Extension incognito default opt-in",
        applications: {gecko: {id: "ext-incognito-default-opt-in@mozilla.com"}},
      },
    },
    {
      manifest: {
        name: "Extension incognito not_allowed",
        applications: {gecko: {id: "ext-incognito-not-allowed@mozilla.com"}},
        incognito: "not_allowed",
      },
    },
    {
      manifest: {
        name: "Static Theme",
        applications: {gecko: {id: "static-theme@mozilla.com"}},
        theme: {
          colors: {
            accentcolor: "#FFFFFF",
            textcolor: "#000",
          },
        },
      },
    },
    {
      manifest: {
        name: "Dictionary",
        applications: {gecko: {id: "dictionary@mozilla.com"}},
        dictionaries: {
          "und": "dictionaries/und.dic",
        },
      },
      files: {
        "dictionaries/und.dic": "",
        "dictionaries/und.aff": "",
      },
    },
    {
      manifest: {
        name: "Langpack",
        applications: {gecko: {id: "langpack@mozilla.com"}},
        langpack_id: "und",
        languages: {
          und: {
            chrome_resources: {
              global: "chrome/und/locale/und/global",
            },
            version: "20190326174300",
          },
        },
      },
    },
  ];

  for (let definition of TEST_ADDONS) {
    let {id} = definition.manifest.applications.gecko;
    info(`Testing incognito checkbox visibility on ${id} post install notification`);

    const xpi = AddonTestUtils.createTempWebExtensionFile(definition);
    let install = await AddonManager.getInstallForFile(xpi);

    await Promise.all([
      waitAppMenuNotificationShown("addon-installed", id, true),
      install.install().then(() => {
        Services.obs.notifyObservers({
          addon: install.addon, target: gBrowser.selectedBrowser,
        }, "webextension-install-notify");
      }),
    ]);

    const {addon} = install;
    const {permissions} = addon;
    const canChangePBAccess = Boolean(permissions & AddonManager.PERM_CAN_CHANGE_PRIVATEBROWSING_ACCESS);

    if (id === "ext-incognito-default-opt-in@mozilla.com") {
      ok(canChangePBAccess, `${id} should have the PERM_CAN_CHANGE_PRIVATEBROWSING_ACCESS permission`);
    } else {
      ok(!canChangePBAccess, `${id} should not have the PERM_CAN_CHANGE_PRIVATEBROWSING_ACCESS permission`);
    }

    // This tests the visibility of various private detail rows.
    gManagerWindow = await open_manager("addons://detail/" + encodeURIComponent(id));
    info(`addon ${id} detail opened`);
    if (addon.type === "extension") {
      is(!is_hidden(get("detail-privateBrowsing-row")), canChangePBAccess, "Private permission row visibility is correct");
      is(!is_hidden(get("detail-privateBrowsing-row-footer")), canChangePBAccess, "Private permission footer visibility is correct");
      let required = addon.incognito === "spanning";
      is(!is_hidden(get("detail-privateBrowsing-required")), !canChangePBAccess && required, "Private required row visibility is correct");
      is(!is_hidden(get("detail-privateBrowsing-required-footer")), !canChangePBAccess && required, "Private required footer visibility is correct");
      is(!is_hidden(get("detail-privateBrowsing-disallowed")), !canChangePBAccess && !required, "Private disallowed row visibility is correct");
      is(!is_hidden(get("detail-privateBrowsing-disallowed-footer")), !canChangePBAccess && !required, "Private disallowed footer visibility is correct");
    } else {
      is_element_hidden(get("detail-privateBrowsing-row"), "Private browsing should be hidden");
      is_element_hidden(get("detail-privateBrowsing-row-footer"), "Private browsing footer should be hidden");
      is_element_hidden(get("detail-privateBrowsing-required"), "Private required should be hidden");
      is_element_hidden(get("detail-privateBrowsing-required-footer"), "Private required footer should be hidden");
      is_element_hidden(get("detail-privateBrowsing-disallowed"), "Private disallowed should be hidden");
      is_element_hidden(get("detail-privateBrowsing-disallowed-footer"), "Private disallowed footer should be hidden");
    }
    await close_manager(gManagerWindow);

    await addon.uninstall();
  }
});
