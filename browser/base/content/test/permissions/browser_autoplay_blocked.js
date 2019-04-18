/*
 * Test that a blocked request to autoplay media is shown to the user
 */

const AUTOPLAY_PAGE = getRootDirectory(gTestPath).replace("chrome://mochitests/content", "https://example.com") + "browser_autoplay_blocked.html";

const SLOW_AUTOPLAY_PAGE = getRootDirectory(gTestPath).replace("chrome://mochitests/content", "https://example.com") + "browser_autoplay_blocked_slow.sjs";

const AUTOPLAY_PREF = "media.autoplay.default";
const AUTOPLAY_PERM = "autoplay-media";

function openIdentityPopup() {
  let promise = BrowserTestUtils.waitForEvent(gIdentityHandler._identityPopup, "popupshown");
  gIdentityHandler._identityBox.click();
  return promise;
}

function closeIdentityPopup() {
  let promise = BrowserTestUtils.waitForEvent(gIdentityHandler._identityPopup, "popuphidden");
  gIdentityHandler._identityPopup.hidePopup();
  return promise;
}

function autoplayBlockedIcon() {
  return document.querySelector("#blocked-permissions-container " +
                                ".blocked-permission-icon.autoplay-media-icon");
}

function sleep(ms) {
  /* eslint-disable mozilla/no-arbitrary-setTimeout */
  return new Promise(resolve => setTimeout(resolve, ms));
}

async function blockedIconShown() {
  await TestUtils.waitForCondition(() => {
    return BrowserTestUtils.is_visible(autoplayBlockedIcon());
  });

  ok(BrowserTestUtils.is_visible(autoplayBlockedIcon()), "Blocked icon is shown");
}

add_task(async function setup() {
  registerCleanupFunction(() => {
    Services.perms.removeAll();
    Services.prefs.clearUserPref(AUTOPLAY_PREF);
  });
});

add_task(async function testMainViewVisible() {
  Services.prefs.setIntPref(AUTOPLAY_PREF, Ci.nsIAutoplay.ALLOWED);

  await BrowserTestUtils.withNewTab(AUTOPLAY_PAGE, async function() {
    let permissionsList = document.getElementById("identity-popup-permission-list");
    let emptyLabel = permissionsList.nextElementSibling.nextElementSibling;

    ok(BrowserTestUtils.is_hidden(autoplayBlockedIcon()), "Blocked icon not shown");

    await openIdentityPopup();
    ok(!BrowserTestUtils.is_hidden(emptyLabel), "List of permissions is empty");
    await closeIdentityPopup();
  });

  Services.prefs.setIntPref(AUTOPLAY_PREF, Ci.nsIAutoplay.BLOCKED);

  await BrowserTestUtils.withNewTab(AUTOPLAY_PAGE, async function(browser) {
    let permissionsList = document.getElementById("identity-popup-permission-list");
    let emptyLabel = permissionsList.nextElementSibling.nextElementSibling;

    await blockedIconShown();

    await openIdentityPopup();
    ok(BrowserTestUtils.is_hidden(emptyLabel), "List of permissions is not empty");
    let labelText = SitePermissions.getPermissionLabel(AUTOPLAY_PERM);
    let labels = permissionsList.querySelectorAll(".identity-popup-permission-label");
    is(labels.length, 1, "One permission visible in main view");
    is(labels[0].textContent, labelText, "Correct value");

    let menulist = document.getElementById("identity-popup-popup-menulist");
    Assert.equal(menulist.label, "Block");

    await EventUtils.synthesizeMouseAtCenter(menulist, { type: "mousedown" });
    await BrowserTestUtils.waitForCondition(() => {
      return menulist.getElementsByTagName("menuitem")[0].label === "Allow";
    });

    let menuitem = menulist.getElementsByTagName("menuitem")[0];
    Assert.equal(menuitem.getAttribute("label"), "Allow");

    menuitem.click();
    menulist.menupopup.hidePopup();
    await closeIdentityPopup();

    let uri = Services.io.newURI(AUTOPLAY_PAGE);
    let state = SitePermissions.get(uri, AUTOPLAY_PERM).state;
    Assert.equal(state, SitePermissions.ALLOW);
  });

  Services.perms.removeAll();
});

add_task(async function testGloballyBlockedOnNewWindow() {
  Services.prefs.setIntPref(AUTOPLAY_PREF, Ci.nsIAutoplay.BLOCKED);

  let uri = Services.io.newURI(AUTOPLAY_PAGE);

  let tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, uri.spec);
  await blockedIconShown();

  Assert.deepEqual(SitePermissions.get(uri, AUTOPLAY_PERM, tab.linkedBrowser), {
    state: SitePermissions.BLOCK,
    scope: SitePermissions.SCOPE_PERSISTENT,
  });

  let promiseWin = BrowserTestUtils.waitForNewWindow();
  gBrowser.replaceTabWithWindow(tab);
  let win = await promiseWin;
  tab = win.gBrowser.selectedTab;

  Assert.deepEqual(SitePermissions.get(uri, AUTOPLAY_PERM, tab.linkedBrowser), {
    state: SitePermissions.BLOCK,
    scope: SitePermissions.SCOPE_PERSISTENT,
  });

  SitePermissions.remove(uri, AUTOPLAY_PERM, tab.linkedBrowser);
  await BrowserTestUtils.closeWindow(win);
});

add_task(async function testBFCache() {
  Services.prefs.setIntPref(AUTOPLAY_PREF, Ci.nsIAutoplay.BLOCKED);

  await BrowserTestUtils.withNewTab("about:home", async function(browser) {
    await BrowserTestUtils.loadURI(browser, AUTOPLAY_PAGE);
    await blockedIconShown();

    gBrowser.goBack();
    await TestUtils.waitForCondition(() => {
      return BrowserTestUtils.is_hidden(autoplayBlockedIcon());
    });

    // Not sure why using `gBrowser.goForward()` doesn't trigger document's
    // visibility changes in some debug build on try server, which makes us not
    // to receive the blocked event.
    await ContentTask.spawn(browser, null, () => {
      content.history.forward();
    });
    await blockedIconShown();
  });

  Services.perms.removeAll();
});

add_task(async function testChangingBlockingSettingDuringNavigation() {
  Services.prefs.setIntPref(AUTOPLAY_PREF, Ci.nsIAutoplay.BLOCKED);

  await BrowserTestUtils.withNewTab("about:home", async function(browser) {
    await BrowserTestUtils.loadURI(browser, AUTOPLAY_PAGE);
    await blockedIconShown();
    Services.prefs.setIntPref(AUTOPLAY_PREF, Ci.nsIAutoplay.ALLOWED);

    gBrowser.goBack();
    await TestUtils.waitForCondition(() => {
      return BrowserTestUtils.is_hidden(autoplayBlockedIcon());
    });

    gBrowser.goForward();

    // Sleep here to prevent false positives, the icon gets shown with an
    // async `GloballyAutoplayBlocked` event. The sleep gives it a little
    // time for it to show otherwise there is a chance it passes before it
    // would have shown.
    await sleep(100);
    ok(BrowserTestUtils.is_hidden(autoplayBlockedIcon()), "Blocked icon is hidden");
  });

  Services.perms.removeAll();
});

add_task(async function testSlowLoadingPage() {
  Services.prefs.setIntPref(AUTOPLAY_PREF, Ci.nsIAutoplay.BLOCKED);

  let tab1 = await BrowserTestUtils.openNewForegroundTab(gBrowser, "about:home");
  let tab2 = await BrowserTestUtils.openNewForegroundTab(gBrowser, SLOW_AUTOPLAY_PAGE);
  await blockedIconShown();

  await BrowserTestUtils.switchTab(gBrowser, tab1);
  // Wait until the blocked icon is hidden by switching tabs
  await TestUtils.waitForCondition(() => {
    return BrowserTestUtils.is_hidden(autoplayBlockedIcon());
  });
  await BrowserTestUtils.switchTab(gBrowser, tab2);
  await blockedIconShown();

  BrowserTestUtils.removeTab(tab1);
  BrowserTestUtils.removeTab(tab2);

  Services.perms.removeAll();
});
