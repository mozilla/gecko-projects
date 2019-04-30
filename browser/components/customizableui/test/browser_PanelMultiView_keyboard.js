/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Test the keyboard behavior of PanelViews.
 */

const {PanelMultiView} = ChromeUtils.import("resource:///modules/PanelMultiView.jsm");

let gAnchor;
let gPanel;
let gPanelMultiView;
let gMainView;
let gMainButton1;
let gMainMenulist;
let gMainTextbox;
let gMainButton2;
let gMainButton3;
let gMainTabOrder;
let gMainArrowOrder;
let gSubView;
let gSubButton;
let gSubTextarea;

async function openPopup() {
  let shown = BrowserTestUtils.waitForEvent(gMainView, "ViewShown");
  PanelMultiView.openPopup(gPanel, gAnchor, "bottomcenter topright");
  await shown;
}

async function hidePopup() {
  let hidden = BrowserTestUtils.waitForEvent(gPanel, "popuphidden");
  PanelMultiView.hidePopup(gPanel);
  await hidden;
}

async function showSubView() {
  let shown = BrowserTestUtils.waitForEvent(gSubView, "ViewShown");
  gPanelMultiView.showSubView(gSubView);
  await shown;
}

async function expectFocusAfterKey(aKey, aFocus) {
  let res = aKey.match(/^(Shift\+)?(.+)$/);
  let shift = Boolean(res[1]);
  let key;
  if (res[2].length == 1) {
    key = res[2]; // Character.
  } else {
    key = "KEY_" + res[2]; // Tab, ArrowRight, etc.
  }
  info("Waiting for focus on " + aFocus.id);
  let focused = BrowserTestUtils.waitForEvent(aFocus, "focus");
  EventUtils.synthesizeKey(key, {shiftKey: shift});
  await focused;
  ok(true, aFocus.id + " focused after " + aKey + " pressed");
}

add_task(async function setup() {
  let navBar = document.getElementById("nav-bar");
  gAnchor = document.createXULElement("toolbarbutton");
  navBar.appendChild(gAnchor);
  gPanel = document.createXULElement("panel");
  navBar.appendChild(gPanel);
  gPanelMultiView = document.createXULElement("panelmultiview");
  gPanelMultiView.setAttribute("mainViewId", "testMainView");
  gPanel.appendChild(gPanelMultiView);

  gMainView = document.createXULElement("panelview");
  gMainView.id = "testMainView";
  gPanelMultiView.appendChild(gMainView);
  gMainButton1 = document.createXULElement("button");
  gMainButton1.id = "gMainButton1";
  gMainView.appendChild(gMainButton1);
  gMainMenulist = document.createXULElement("menulist");
  gMainMenulist.id = "gMainMenulist";
  gMainView.appendChild(gMainMenulist);
  let menuPopup = document.createXULElement("menupopup");
  gMainMenulist.appendChild(menuPopup);
  let item = document.createXULElement("menuitem");
  item.setAttribute("value", "1");
  item.setAttribute("selected", "true");
  menuPopup.appendChild(item);
  item = document.createXULElement("menuitem");
  item.setAttribute("value", "2");
  menuPopup.appendChild(item);
  gMainTextbox = document.createXULElement("textbox");
  gMainTextbox.id = "gMainTextbox";
  gMainView.appendChild(gMainTextbox);
  gMainTextbox.setAttribute("value", "value");
  gMainButton2 = document.createXULElement("button");
  gMainButton2.id = "gMainButton2";
  gMainView.appendChild(gMainButton2);
  gMainButton3 = document.createXULElement("button");
  gMainButton3.id = "gMainButton3";
  gMainView.appendChild(gMainButton3);
  gMainTabOrder = [gMainButton1, gMainMenulist, gMainTextbox, gMainButton2,
                   gMainButton3];
  gMainArrowOrder = [gMainButton1, gMainButton2, gMainButton3];

  gSubView = document.createXULElement("panelview");
  gSubView.id = "testSubView";
  gPanelMultiView.appendChild(gSubView);
  gSubButton = document.createXULElement("button");
  gSubView.appendChild(gSubButton);
  gSubTextarea = document.createElementNS("http://www.w3.org/1999/xhtml",
                                          "textarea");
  gSubTextarea.id = "gSubTextarea";
  gSubView.appendChild(gSubTextarea);
  gSubTextarea.value = "value";

  registerCleanupFunction(() => {
    gAnchor.remove();
    gPanel.remove();
  });
});

// Test that the tab key focuses all expected controls.
add_task(async function testTab() {
  await openPopup();
  for (let elem of gMainTabOrder) {
    await expectFocusAfterKey("Tab", elem);
  }
  // Wrap around.
  await expectFocusAfterKey("Tab", gMainTabOrder[0]);
  await hidePopup();
});

// Test that the shift+tab key focuses all expected controls.
add_task(async function testShiftTab() {
  await openPopup();
  for (let i = gMainTabOrder.length - 1; i >= 0; --i) {
    await expectFocusAfterKey("Shift+Tab", gMainTabOrder[i]);
  }
  // Wrap around.
  await expectFocusAfterKey("Shift+Tab",
                            gMainTabOrder[gMainTabOrder.length - 1]);
  await hidePopup();
});

// Test that the down arrow key skips menulists and textboxes.
add_task(async function testDownArrow() {
  await openPopup();
  for (let elem of gMainArrowOrder) {
    await expectFocusAfterKey("ArrowDown", elem);
  }
  // Wrap around.
  await expectFocusAfterKey("ArrowDown", gMainArrowOrder[0]);
  await hidePopup();
});

// Test that the up arrow key skips menulists and textboxes.
add_task(async function testUpArrow() {
  await openPopup();
  for (let i = gMainArrowOrder.length - 1; i >= 0; --i) {
    await expectFocusAfterKey("ArrowUp", gMainArrowOrder[i]);
  }
  // Wrap around.
  await expectFocusAfterKey("ArrowUp",
                            gMainArrowOrder[gMainArrowOrder.length - 1]);
  await hidePopup();
});

// Test that the home/end keys move to the first/last controls.
add_task(async function testHomeEnd() {
  await openPopup();
  await expectFocusAfterKey("Home", gMainArrowOrder[0]);
  await expectFocusAfterKey("End",
                            gMainArrowOrder[gMainArrowOrder.length - 1]);
  await hidePopup();
});

// Test that the up/down arrow keys work as expected in menulists.
add_task(async function testArrowsMenulist() {
  await openPopup();
  gMainMenulist.focus();
  is(document.activeElement, gMainMenulist, "menulist focused");
  is(gMainMenulist.value, "1", "menulist initial value 1");
  if (AppConstants.platform == "macosx") {
    // On Mac, down/up arrows just open the menulist.
    let popup = gMainMenulist.menupopup;
    for (let key of ["ArrowDown", "ArrowUp"]) {
      let shown = BrowserTestUtils.waitForEvent(popup, "popupshown");
      EventUtils.synthesizeKey("KEY_" + key);
      await shown;
      ok(gMainMenulist.open, "menulist open after " + key);
      let hidden = BrowserTestUtils.waitForEvent(popup, "popuphidden");
      EventUtils.synthesizeKey("KEY_Escape");
      await hidden;
      ok(!gMainMenulist.open, "menulist closed after Escape");
    }
  } else {
    // On other platforms, down/up arrows change the value without opening the
    // menulist.
    EventUtils.synthesizeKey("KEY_ArrowDown");
    is(document.activeElement, gMainMenulist,
       "menulist still focused after ArrowDown");
    is(gMainMenulist.value, "2", "menulist value 2 after ArrowDown");
    EventUtils.synthesizeKey("KEY_ArrowUp");
    is(document.activeElement, gMainMenulist,
       "menulist still focused after ArrowUp");
    is(gMainMenulist.value, "1", "menulist value 1 after ArrowUp");
  }
  await hidePopup();
});

// Test that pressing space in a textbox inserts a space (instead of trying to
// activate the control).
add_task(async function testSpaceTextbox() {
  await openPopup();
  gMainTextbox.focus();
  gMainTextbox.selectionStart = gMainTextbox.selectionEnd = 0;
  EventUtils.synthesizeKey(" ");
  is(gMainTextbox.value, " value", "Space typed into textbox");
  gMainTextbox.value = "value";
  await hidePopup();
});

// Tests that the left arrow key normally moves back to the previous view.
add_task(async function testLeftArrow() {
  await openPopup();
  await showSubView();
  let shown = BrowserTestUtils.waitForEvent(gMainView, "ViewShown");
  EventUtils.synthesizeKey("KEY_ArrowLeft");
  await shown;
  ok("Moved to previous view after ArrowLeft");
  await hidePopup();
});

// Tests that the left arrow key moves the caret in a textarea in a subview
// (instead of going back to the previous view).
add_task(async function testLeftArrowTextarea() {
  await openPopup();
  await showSubView();
  gSubTextarea.focus();
  is(document.activeElement, gSubTextarea, "textarea focused");
  EventUtils.synthesizeKey("KEY_End");
  is(gSubTextarea.selectionStart, 5, "selectionStart 5 after End");
  EventUtils.synthesizeKey("KEY_ArrowLeft");
  is(gSubTextarea.selectionStart, 4, "selectionStart 4 after ArrowLeft");
  is(document.activeElement, gSubTextarea, "textarea still focused");
  await hidePopup();
});

// Test navigation to a button which is initially disabled and later enabled.
add_task(async function testDynamicButton() {
  gMainButton2.disabled = true;
  await openPopup();
  await expectFocusAfterKey("ArrowDown", gMainButton1);
  await expectFocusAfterKey("ArrowDown", gMainButton3);
  gMainButton2.disabled = false;
  await expectFocusAfterKey("ArrowUp", gMainButton2);
  await hidePopup();
});

add_task(async function testActivation() {
  function checkActivated(elem, activationFn, reason) {
    let activated = false;
    elem.onclick = function() { activated = true; };
    activationFn();
    ok(activated, "Should have activated button after " + reason);
    elem.onclick = null;
  }
  await openPopup();
  await expectFocusAfterKey("ArrowDown", gMainButton1);
  checkActivated(gMainButton1, () => EventUtils.synthesizeKey("KEY_Enter"), "pressing enter");
  checkActivated(gMainButton1, () => EventUtils.synthesizeKey(" "), "pressing space");
  checkActivated(gMainButton1, () => EventUtils.synthesizeKey("KEY_Enter", {code: "NumpadEnter"}), "pressing numpad enter");
  await hidePopup();
});
