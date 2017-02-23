/* -*- Mode: indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim: set sts=2 sw=2 et tw=80: */
"use strict";

let extData = {
  manifest: {
    "sidebar_action": {
      "default_panel": "sidebar.html",
    },
  },
  useAddonManager: "temporary",

  files: {
    "sidebar.html": `
      <!DOCTYPE html>
      <html>
      <head><meta charset="utf-8"/>
      <script src="sidebar.js"></script>
      </head>
      <body>
      A Test Sidebar
      </body></html>
    `,

    "sidebar.js": function() {
      window.onload = () => {
        browser.test.sendMessage("sidebar");
      };
    },
  },

  background: function() {
    browser.test.onMessage.addListener(msg => {
      if (msg === "set-panel") {
        browser.sidebarAction.setPanel({panel: ""}).then(() => {
          browser.test.notifyFail("empty panel settable");
        }).catch(() => {
          browser.test.notifyPass("unable to set empty panel");
        });
      }
    });
  },
};

add_task(function* sidebar_initial_install() {
  ok(document.getElementById("sidebar-box").hidden, "sidebar box is not visible");
  let extension = ExtensionTestUtils.loadExtension(extData);
  yield extension.startup();
  // Test sidebar is opened on install
  yield extension.awaitMessage("sidebar");
  ok(!document.getElementById("sidebar-box").hidden, "sidebar box is visible");
  // Test toolbar button is available
  ok(document.getElementById("sidebar-button"), "sidebar button is in UI");

  yield extension.unload();
  // Test that the sidebar was closed on unload.
  ok(document.getElementById("sidebar-box").hidden, "sidebar box is not visible");

  // Move toolbar button back to customization.
  CustomizableUI.removeWidgetFromArea("sidebar-button", CustomizableUI.AREA_NAVBAR);
  ok(!document.getElementById("sidebar-button"), "sidebar button is not in UI");
});


add_task(function* sidebar_two_sidebar_addons() {
  let extension2 = ExtensionTestUtils.loadExtension(extData);
  yield extension2.startup();
  // Test sidebar is opened on install
  yield extension2.awaitMessage("sidebar");
  ok(!document.getElementById("sidebar-box").hidden, "sidebar box is visible");
  // Test toolbar button is NOT available after first install
  ok(!document.getElementById("sidebar-button"), "sidebar button is not in UI");

  // Test second sidebar install opens new sidebar
  let extension3 = ExtensionTestUtils.loadExtension(extData);
  yield extension3.startup();
  // Test sidebar is opened on install
  yield extension3.awaitMessage("sidebar");
  ok(!document.getElementById("sidebar-box").hidden, "sidebar box is visible");
  yield extension3.unload();

  // We just close the sidebar on uninstall of the current sidebar.
  ok(document.getElementById("sidebar-box").hidden, "sidebar box is not visible");

  yield extension2.unload();
});

add_task(function* sidebar_windows() {
  let extension = ExtensionTestUtils.loadExtension(extData);
  yield extension.startup();
  // Test sidebar is opened on install
  yield extension.awaitMessage("sidebar");
  ok(!document.getElementById("sidebar-box").hidden, "sidebar box is visible in first window");
  // Check that the menuitem has our image styling.
  let elements = document.getElementsByClassName("webextension-menuitem");
  is(elements.length, 1, "have one menuitem");
  let style = elements[0].getAttribute("style");
  ok(style.includes("webextension-menuitem-image"), "this menu has style");

  let secondSidebar = extension.awaitMessage("sidebar");

  // SidebarUI relies on window.opener being set, which is normal behavior when
  // using menu or key commands to open a new browser window.
  let win = yield BrowserTestUtils.openNewBrowserWindow({opener: window});

  yield secondSidebar;
  ok(!win.document.getElementById("sidebar-box").hidden, "sidebar box is visible in second window");
  // Check that the menuitem has our image styling.
  elements = win.document.getElementsByClassName("webextension-menuitem");
  is(elements.length, 1, "have one menuitem");
  style = elements[0].getAttribute("style");
  ok(style.includes("webextension-menuitem-image"), "this menu has style");

  yield extension.unload();
  yield BrowserTestUtils.closeWindow(win);
});

add_task(function* sidebar_empty_panel() {
  let extension = ExtensionTestUtils.loadExtension(extData);
  yield extension.startup();
  // Test sidebar is opened on install
  yield extension.awaitMessage("sidebar");
  ok(!document.getElementById("sidebar-box").hidden, "sidebar box is visible in first window");
  extension.sendMessage("set-panel");
  yield extension.awaitFinish();
  yield extension.unload();
});

add_task(function* sidebar_tab_query_bug_1340739() {
  let data = {
    manifest: {
      "permissions": [
        "tabs",
      ],
      "sidebar_action": {
        "default_panel": "sidebar.html",
      },
    },
    useAddonManager: "temporary",
    files: {
      "sidebar.html": `
        <!DOCTYPE html>
        <html>
        <head><meta charset="utf-8"/>
        <script src="sidebar.js"></script>
        </head>
        <body>
        A Test Sidebar
        </body></html>
      `,
      "sidebar.js": function() {
        Promise.all([
          browser.tabs.query({}).then((tabs) => {
            browser.test.assertEq(1, tabs.length, "got tab without currentWindow");
          }),
          browser.tabs.query({currentWindow: true}).then((tabs) => {
            browser.test.assertEq(1, tabs.length, "got tab with currentWindow");
          }),
        ]).then(() => {
          browser.test.sendMessage("sidebar");
        });
      },
    },
  };

  let extension = ExtensionTestUtils.loadExtension(data);
  yield extension.startup();
  yield extension.awaitMessage("sidebar");
  yield extension.unload();
});

add_task(function* cleanup() {
  // This is set on initial sidebar install.
  Services.prefs.clearUserPref("extensions.sidebar-button.shown");
});
