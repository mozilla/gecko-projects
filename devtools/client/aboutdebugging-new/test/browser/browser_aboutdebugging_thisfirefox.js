/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const EXPECTED_TARGET_PANES = [
  "Temporary Extensions",
  "Extensions",
  "Tabs",
  "Service Workers",
  "Shared Workers",
  "Other Workers",
];

/**
 * Check that the This Firefox runtime page contains the expected categories if
 * the preference to enable local tab debugging is true.
 */
add_task(async function testThisFirefoxWithLocalTab() {
  const { document, tab, window } = await openAboutDebugging({ enableLocalTabs: true });
  await selectThisFirefoxPage(document, window.AboutDebugging.store);

  // Expect all target panes to be displayed including tabs.
  await checkThisFirefoxTargetPanes(document, EXPECTED_TARGET_PANES);

  await removeTab(tab);
});

/**
 * Check that the This Firefox runtime page contains the expected categories if
 * the preference to enable local tab debugging is false.
 */
add_task(async function testThisFirefoxWithoutLocalTab() {
  const { document, tab, window } = await openAboutDebugging({ enableLocalTabs: false });
  await selectThisFirefoxPage(document, window.AboutDebugging.store);

  // Expect all target panes but tabs to be displayed.
  const expectedTargetPanesWithoutTabs = EXPECTED_TARGET_PANES.filter(p => p !== "Tabs");
  await checkThisFirefoxTargetPanes(document, expectedTargetPanesWithoutTabs);

  await removeTab(tab);
});

async function checkThisFirefoxTargetPanes(doc, expectedTargetPanes) {
  const win = doc.ownerGlobal;
  // Check that the selected sidebar item is "This Firefox"/"This Nightly"/...
  const selectedSidebarItem = doc.querySelector(".js-sidebar-item-selected");
  ok(selectedSidebarItem, "An item is selected in the sidebar");

  const thisFirefoxString = getThisFirefoxString(win);
  is(selectedSidebarItem.textContent, thisFirefoxString,
    "The selected sidebar item is " + thisFirefoxString);

  const paneTitlesEls = doc.querySelectorAll(".js-debug-target-pane-title");
  is(paneTitlesEls.length, expectedTargetPanes.length,
    "This Firefox has the expected number of debug target categories");

  const paneTitles = [...paneTitlesEls].map(el => el.textContent);

  for (let i = 0; i < expectedTargetPanes.length; i++) {
    const expectedPaneTitle = expectedTargetPanes[i];
    const actualPaneTitle = paneTitles[i];
    ok(actualPaneTitle.startsWith(expectedPaneTitle),
       `Expected debug target category found: ${ expectedPaneTitle }`);
  }
}

