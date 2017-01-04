/* -*- indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim: set ft=javascript ts=2 et sw=2 tw=80: */
/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

/* import-globals-from shared-head.js */
"use strict";

const TEST_URL = "data:text/html;charset=utf8,test for dynamically " +
                 "registering and unregistering tools";
var doc = null, toolbox = null, panelWin = null, modifiedPrefs = [];

function test() {
  addTab(TEST_URL).then(tab => {
    let target = TargetFactory.forTab(tab);
    gDevTools.showToolbox(target)
      .then(testSelectTool)
      .then(testToggleToolboxButtons)
      .then(testPrefsAreRespectedWhenReopeningToolbox)
      .then(cleanup, errorHandler);
  });
}

function testPrefsAreRespectedWhenReopeningToolbox() {
  let deferred = defer();
  let target = TargetFactory.forTab(gBrowser.selectedTab);

  info("Closing toolbox to test after reopening");
  gDevTools.closeToolbox(target).then(() => {
    let tabTarget = TargetFactory.forTab(gBrowser.selectedTab);
    gDevTools.showToolbox(tabTarget)
      .then(testSelectTool)
      .then(() => {
        info("Toolbox has been reopened.  Checking UI state.");
        testPreferenceAndUIStateIsConsistent();
        deferred.resolve();
      });
  });

  return deferred.promise;
}

function testSelectTool(devtoolsToolbox) {
  let deferred = defer();
  info("Selecting the options panel");

  toolbox = devtoolsToolbox;
  doc = toolbox.doc;
  toolbox.once("options-selected", (event, tool) => {
    ok(true, "Options panel selected via selectTool method");
    panelWin = tool.panelWin;
    deferred.resolve();
  });
  toolbox.selectTool("options");

  return deferred.promise;
}

function testPreferenceAndUIStateIsConsistent() {
  let checkNodes = [...panelWin.document.querySelectorAll(
    "#enabled-toolbox-buttons-box input[type=checkbox]")];
  let toolboxButtonNodes = [...doc.querySelectorAll(".command-button")];

  // The noautohide button is only displayed in the browser toolbox
  let toolbarButtons = toolbox.toolbarButtons.filter(
    tool => tool.id != "command-button-noautohide");

  for (let tool of toolbarButtons) {
    let isVisible = getBoolPref(tool.visibilityswitch);

    let button = toolboxButtonNodes.find(toolboxButton => toolboxButton.id === tool.id);
    is(!!button, isVisible,
      "Button visibility matches pref for " + tool.id);

    let check = checkNodes.filter(node => node.id === tool.id)[0];
    is(check.checked, isVisible,
      "Checkbox should be selected based on current pref for " + tool.id);
  }
}

function testToggleToolboxButtons() {
  let checkNodes = [...panelWin.document.querySelectorAll(
    "#enabled-toolbox-buttons-box input[type=checkbox]")];

  // The noautohide button is only displayed in the browser toolbox, and the element
  // picker button is not toggleable.
  let toolbarButtons = toolbox.toolbarButtons.filter(
    tool => tool.id != "command-button-noautohide");

  let visibleToolbarButtons = toolbox.toolbarButtons.filter(tool => tool.isVisible);

  let toolbarButtonNodes = [...doc.querySelectorAll(".command-button")].filter(
    btn => btn.id != "command-button-noautohide");

  is(checkNodes.length, toolbarButtons.length,
    "All of the buttons are toggleable.");
  is(visibleToolbarButtons.length, toolbarButtonNodes.length,
    "All of the DOM buttons are toggleable.");

  for (let tool of toolbarButtons) {
    let id = tool.id;
    let matchedCheckboxes = checkNodes.filter(node => node.id === id);
    let matchedButtons = toolbarButtonNodes.filter(button => button.id === id);
    is(matchedCheckboxes.length, 1,
      "There should be a single toggle checkbox for: " + id);
    if (tool.isVisible) {
      is(matchedButtons.length, 1,
        "There should be a DOM button for the visible: " + id);
      is(matchedButtons[0].getAttribute("title"), tool.description,
        "The tooltip for button matches the tool definition.");
    } else {
      is(matchedButtons.length, 0,
        "There should not be a DOM button for the invisible: " + id);
    }

    is(matchedCheckboxes[0].nextSibling.textContent, tool.description,
      "The label for checkbox matches the tool definition.");
  }

  // Store modified pref names so that they can be cleared on error.
  for (let tool of toolbarButtons) {
    let pref = tool.visibilityswitch;
    modifiedPrefs.push(pref);
  }

  // Try checking each checkbox, making sure that it changes the preference
  for (let node of checkNodes) {
    let tool = toolbarButtons.filter(
      commandButton => commandButton.id === node.id)[0];
    let isVisible = getBoolPref(tool.visibilityswitch);

    testPreferenceAndUIStateIsConsistent();
    node.click();
    testPreferenceAndUIStateIsConsistent();

    let isVisibleAfterClick = getBoolPref(tool.visibilityswitch);

    is(isVisible, !isVisibleAfterClick,
      "Clicking on the node should have toggled visibility preference for " +
      tool.visibilityswitch);
  }

  return promise.resolve();
}

function getBoolPref(key) {
  return Services.prefs.getBoolPref(key);
}

function cleanup() {
  toolbox.destroy().then(function () {
    gBrowser.removeCurrentTab();
    for (let pref of modifiedPrefs) {
      Services.prefs.clearUserPref(pref);
    }
    toolbox = doc = panelWin = modifiedPrefs = null;
    finish();
  });
}

function errorHandler(error) {
  ok(false, "Unexpected error: " + error);
  cleanup();
}
