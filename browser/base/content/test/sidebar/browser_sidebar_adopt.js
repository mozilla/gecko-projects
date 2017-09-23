
/* This test checks that the SidebarFocused event doesn't fire in adopted
 * windows when the sidebar gets opened during window opening, to make sure
 * that sidebars don't steal focus from the page in this case (Bug 1394207).
 * There's another case not covered here that has the same expected behavior -
 * during the initial browser startup - but it would be hard to do with a mochitest. */

registerCleanupFunction(() => {
  SidebarUI.hide();
});

function failIfSidebarFocusedFires() {
  ok(false, "This event shouldn't have fired");
}

add_task(async function() {
  info("Opening the sidebar and expecting both SidebarShown and SidebarFocused events");

  let initialShown = BrowserTestUtils.waitForEvent(window, "SidebarShown");
  let initialFocus = BrowserTestUtils.waitForEvent(window, "SidebarFocused");

  await SidebarUI.show("viewBookmarksSidebar");
  await initialShown;
  await initialFocus;

  ok(true, "SidebarShown and SidebarFocused events fired on a new window");
});

add_task(async function() {
  info("Opening a new window and expecting the SidebarFocused event to not fire");

  let promiseNewWindow = BrowserTestUtils.waitForNewWindow(false);
  BrowserTestUtils.openNewBrowserWindow({opener: window});
  let win = await promiseNewWindow;

  let adoptedShown = BrowserTestUtils.waitForEvent(win, "SidebarShown");
  win.addEventListener("SidebarFocused", failIfSidebarFocusedFires);

  registerCleanupFunction(async function() {
    win.removeEventListener("SidebarFocused", failIfSidebarFocusedFires);
    await BrowserTestUtils.closeWindow(win);
  });

  await adoptedShown;
  ok(true, "SidebarShown event fired on an adopted window");
});
