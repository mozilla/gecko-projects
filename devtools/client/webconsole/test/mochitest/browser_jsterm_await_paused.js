/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

// Test that top-level await expression work as expected when debugger is paused.

"use strict";

// Import helpers for the new debugger
/* import-globals-from ../../../debugger/new/test/mochitest/helpers.js */
Services.scriptloader.loadSubScript(
  "chrome://mochitests/content/browser/devtools/client/debugger/new/test/mochitest/helpers.js",
  this);

const TEST_URI =
  `data:text/html;charset=utf-8,Web Console test top-level await when debugger paused`;

add_task(async function() {
  // Enable await mapping.
  await pushPref("devtools.debugger.features.map-await-expression", true);

  // Run test with legacy JsTerm
  await pushPref("devtools.webconsole.jsterm.codeMirror", false);
  await performTests();
  // And then run it with the CodeMirror-powered one.
  await pushPref("devtools.webconsole.jsterm.codeMirror", true);
  await performTests();
});

async function performTests() {
  // Force the split console to be closed.
  await pushPref("devtools.toolbox.splitconsoleEnabled", false);
  const hud = await openNewTabAndConsole(TEST_URI);
  const {jsterm} = hud;

  const pauseExpression = `(() => {
    var foo = ["bar"];
    /* Will pause the script and open the debugger panel */
    debugger;
    return "pauseExpression-res";
  })()`;
  jsterm.execute(pauseExpression);

  // wait for the debugger to be opened and paused.
  const target = await TargetFactory.forTab(gBrowser.selectedTab);
  const toolbox = gDevTools.getToolbox(target);

  await waitFor(() => toolbox.getPanel("jsdebugger"));
  const dbg = createDebuggerContext(toolbox);
  await waitForPaused(dbg);

  await toolbox.openSplitConsole();

  const awaitExpression = `await new Promise(res => {
    setTimeout(() => res(["res", ...foo]), 1000);
  })`;

  const onAwaitResultMessage = waitForMessage(hud, `[ "res", "bar" ]`, ".message.result");
  jsterm.execute(awaitExpression);
  // We send an evaluation just after the await one to ensure the await evaluation was
  // done. We can't await on the previous execution because it waits for the result to
  // be send, which won't happen until we resume the debugger.
  await jsterm.execute(`"smoke"`);

  // Give the engine some time to evaluate the await expression before resuming.
  await waitForTick();

  // Click on the resume button to not be paused anymore.
  await resume(dbg);

  await onAwaitResultMessage;
  const messages = hud.ui.outputNode.querySelectorAll(".message.result .message-body");
  const messagesText = Array.from(messages).map(n => n.textContent);
  const expectedMessages = [
    // Result of "smoke"
    `"smoke"`,
    // The result of pauseExpression (after smoke since pauseExpression iife was paused)
    `"pauseExpression-res"`,
    // Result of await
    `Array [ "res", "bar" ]`,
  ];
  is(JSON.stringify(messagesText, null, 2), JSON.stringify(expectedMessages, null, 2),
    "The output contains the the expected messages, in the expected order");
}
