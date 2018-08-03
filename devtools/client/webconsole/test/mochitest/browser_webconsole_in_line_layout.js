/* -*- indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim: set ft=javascript ts=2 et sw=2 tw=80: */
/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Test that the in-line layout works as expected

const TEST_URI = "data:text/html,<meta charset=utf8>Test in-line console layout";

const MINIMUM_MESSAGE_HEIGHT = 19;

add_task(async function() {
  // Run test with legacy JsTerm
  await pushPref("devtools.webconsole.jsterm.codeMirror", false);
  await performTests();
  // And then run it with the CodeMirror-powered one.
  await pushPref("devtools.webconsole.jsterm.codeMirror", true);
  await performTests();
});

async function performTests() {
  // The style is only enabled in the new jsterm.
  await pushPref("devtools.webconsole.jsterm.codeMirror", true);
  const hud = await openNewTabAndConsole(TEST_URI);
  const {jsterm, ui} = hud;
  const {document} = ui;
  const wrapper = document.querySelector(".webconsole-output-wrapper");
  const [
    filterBarNode,
    outputNode,
    ,
    inputNode,
  ] = wrapper.querySelector(".webconsole-flex-wrapper").childNodes;

  testWrapperLayout(wrapper);

  is(outputNode.offsetHeight, 0, "output node has no height");
  is(filterBarNode.offsetHeight + inputNode.offsetHeight, wrapper.offsetHeight,
    "The entire height is taken by filter bar and input");

  await setFilterBarVisible(hud, true);
  testWrapperLayout(wrapper);
  is(filterBarNode.offsetHeight + inputNode.offsetHeight, wrapper.offsetHeight,
    "The entire height is still taken by filter bar and input");

  info("Logging a message in the content window");
  const onLogMessage = waitForMessage(hud, "simple text message");
  ContentTask.spawn(gBrowser.selectedBrowser, null, () => {
    content.wrappedJSObject.console.log("simple text message");
  });
  const logMessage = await onLogMessage;
  testWrapperLayout(wrapper);
  is(outputNode.clientHeight, logMessage.node.clientHeight,
    "Output node is only the height of the message it contains");

  info("Logging multiple messages to make the output overflow");
  const onLastMessage = waitForMessage(hud, "message-100");
  ContentTask.spawn(gBrowser.selectedBrowser, null, () => {
    for (let i = 1; i <= 100; i++) {
      content.wrappedJSObject.console.log("message-" + i);
    }
  });
  await onLastMessage;
  ok(outputNode.scrollHeight > outputNode.clientHeight, "Output node overflows");
  testWrapperLayout(wrapper);

  info("Make sure setting a tall value in the input does not break the layout");
  jsterm.setInputValue("multiline\n".repeat(200));
  is(outputNode.clientHeight, MINIMUM_MESSAGE_HEIGHT,
    "One message is still visible in the output node");
  testWrapperLayout(wrapper);

  info("Hide secondary filter bar");
  await setFilterBarVisible(hud, false);
  is(outputNode.clientHeight, MINIMUM_MESSAGE_HEIGHT,
    "One message is still visible in the output node");
  testWrapperLayout(wrapper);

  const filterBarHeight = filterBarNode.clientHeight;

  info("Show the hidden messages label");
  const onHiddenMessagesLabelVisible = waitFor(() =>
    document.querySelector(".webconsole-filterbar-filtered-messages"));
  ui.filterBox.focus();
  ui.filterBox.select();
  EventUtils.sendString("message-");
  await onHiddenMessagesLabelVisible;

  info("Shrink the window so the label is on its own line");
  const toolbox = hud.ui.consoleOutput.toolbox;
  const hostWindow = toolbox.win.parent;
  hostWindow.resizeTo(300, window.screen.availHeight);

  ok(filterBarNode.clientHeight > filterBarHeight, "The filter bar is taller");
  testWrapperLayout(wrapper);

  info("Show filter bar");
  await setFilterBarVisible(hud, true);
  testWrapperLayout(wrapper);

  info("Hide filter bar");
  await setFilterBarVisible(hud, false);

  info("Expand the window so hidden label isn't on its own line anymore");
  hostWindow.resizeTo(window.screen.availWidth, window.screen.availHeight);
  testWrapperLayout(wrapper);

  jsterm.setInputValue("");
  testWrapperLayout(wrapper);

  ui.clearOutput();
  testWrapperLayout(wrapper);
  is(outputNode.offsetHeight, 0, "output node has no height");
  is(filterBarNode.offsetHeight + inputNode.offsetHeight, wrapper.offsetHeight,
    "The entire height is taken by filter bar and input");
}

function testWrapperLayout(wrapper) {
  is(wrapper.offsetHeight, wrapper.scrollHeight, "there's no scrollbar on the wrapper");
  ok(wrapper.offsetHeight <= wrapper.ownerDocument.body.offsetHeight,
    "console is not taller than document body");
  const childSumHeight = [...wrapper.childNodes].reduce(
    (height, node) => height + node.offsetHeight, 0);
  ok(wrapper.offsetHeight >= childSumHeight,
    "the sum of the height of wrapper child nodes is not taller than wrapper's one");
}
