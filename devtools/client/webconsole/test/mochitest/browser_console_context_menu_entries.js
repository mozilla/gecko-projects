/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Check that we display the expected context menu entries.

const TEST_URI = "http://example.com/browser/devtools/client/webconsole/" +
                 "test/mochitest/test-console.html";

add_task(async function() {
  // Enable net messages in the console for this test.
  await pushPref("devtools.browserconsole.filter.net", true);
  // This is required for testing the text input in the browser console:
  await pushPref("devtools.chrome.enabled", true);

  // Run test with legacy JsTerm
  await pushPref("devtools.webconsole.jsterm.codeMirror", false);
  await performTests();
  // And then run it with the CodeMirror-powered one.
  await pushPref("devtools.webconsole.jsterm.codeMirror", true);
  await performTests();
});

async function performTests() {
  await addTab(TEST_URI);
  const hud = await HUDService.toggleBrowserConsole();

  info("Reload the content window to produce a network log");
  const onNetworkMessage = waitForMessage(hud, "test-console.html");
  ContentTask.spawn(gBrowser.selectedBrowser, null, () => {
    content.wrappedJSObject.location.reload();
  });
  const networkMessage = await onNetworkMessage;

  info("Open and check the context menu for the network message");
  let menuPopup = await openContextMenu(hud, networkMessage.node);
  ok(menuPopup, "The context menu is displayed on a network message");

  let expectedContextMenu = addPrefBasedEntries([
    "#console-menu-copy-url (a)",
    "#console-menu-open-url (T)",
    "#console-menu-store (S) [disabled]",
    "#console-menu-copy (C)",
    "#console-menu-copy-object (o) [disabled]",
    "#console-menu-select (A)",
  ]);
  is(getSimplifiedContextMenu(menuPopup).join("\n"), expectedContextMenu.join("\n"),
    "The context menu has the expected entries for a network message");

  info("Logging a text message in the content window");
  const onLogMessage = waitForMessage(hud, "simple text message");
  ContentTask.spawn(gBrowser.selectedBrowser, null, () => {
    content.wrappedJSObject.console.log("simple text message");
  });

  const logMessage = await onLogMessage;
  menuPopup = await openContextMenu(hud, logMessage.node);
  ok(menuPopup, "The context menu is displayed on a log message");

  expectedContextMenu = addPrefBasedEntries([
    "#console-menu-store (S) [disabled]",
    "#console-menu-copy (C)",
    "#console-menu-copy-object (o) [disabled]",
    "#console-menu-select (A)",
  ]);
  is(getSimplifiedContextMenu(menuPopup).join("\n"), expectedContextMenu.join("\n"),
    "The context menu has the expected entries for a simple log message");

  menuPopup = await openContextMenu(hud, hud.jsterm.node || hud.jsterm.inputNode);

  expectedContextMenu = [
    "#editmenu-undo (editmenu-undo) [disabled]",
    "#editmenu-cut (editmenu-cut)",
    "#editmenu-copy (editmenu-copy)",
    "#editmenu-paste (editmenu-paste)",
    "#editmenu-delete (editmenu-delete) [disabled]",
    "#editmenu-selectAll (editmenu-select-all) [disabled]",
  ];
  is(getL10NContextMenu(menuPopup).join("\n"), expectedContextMenu.join("\n"),
    "The context menu has the correct edit menu items");

  await hideContextMenu(hud);
  // Close the browser console.
  await HUDService.toggleBrowserConsole();
}

function addPrefBasedEntries(expectedEntries) {
  if (Services.prefs.getBoolPref("devtools.webconsole.sidebarToggle", false)) {
    expectedEntries.push("#console-menu-open-sidebar (V) [disabled]");
  }

  return expectedEntries;
}

function getL10NContextMenu(popupElement) {
  return [...popupElement.querySelectorAll("menuitem")]
    .map(entry => {
      const l10nID = entry.getAttribute("data-l10n-id");
      const disabled = entry.hasAttribute("disabled");
      return `#${entry.id} (${l10nID})${disabled ? " [disabled]" : ""}`;
    });
}

function getSimplifiedContextMenu(popupElement) {
  return [...popupElement.querySelectorAll("menuitem")]
    .map(entry => {
      const key = entry.getAttribute("accesskey");
      const disabled = entry.hasAttribute("disabled");
      return `#${entry.id} (${key})${disabled ? " [disabled]" : ""}`;
    });
}
