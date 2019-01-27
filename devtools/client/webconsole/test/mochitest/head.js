/* -*- indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim: set ft=javascript ts=2 et sw=2 tw=80: */
/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */
/* eslint no-unused-vars: [2, {"vars": "local"}] */

"use strict";

// Import helpers registering the test-actor in remote targets
/* globals registerTestActor, getTestActor, Task, openToolboxForTab, gBrowser */
Services.scriptloader.loadSubScript(
  "chrome://mochitests/content/browser/devtools/client/shared/test/test-actor-registry.js",
  this);

// shared-head.js handles imports, constants, and utility functions
// Load the shared-head file first.
/* import-globals-from ../../../shared/test/shared-head.js */
Services.scriptloader.loadSubScript(
  "chrome://mochitests/content/browser/devtools/client/shared/test/shared-head.js",
  this);

// Import helpers for the new debugger
/* import-globals-from ../../../debugger/new/test/mochitest/helpers/context.js */
Services.scriptloader.loadSubScript(
  "chrome://mochitests/content/browser/devtools/client/debugger/new/test/mochitest/helpers/context.js",
  this);

var {HUDService} = require("devtools/client/webconsole/hudservice");
var WCUL10n = require("devtools/client/webconsole/webconsole-l10n");
const DOCS_GA_PARAMS = `?${new URLSearchParams({
  "utm_source": "mozilla",
  "utm_medium": "firefox-console-errors",
  "utm_campaign": "default",
})}`;
const GA_PARAMS = `?${new URLSearchParams({
  "utm_source": "mozilla",
  "utm_medium": "devtools-webconsole",
  "utm_campaign": "default",
})}`;

const wcActions = require("devtools/client/webconsole/actions/index");

registerCleanupFunction(async function() {
  Services.prefs.clearUserPref("devtools.webconsole.ui.filterbar");

  // Reset all filter prefs between tests. First flushPrefEnv in case one of the
  // filter prefs has been pushed for the test
  await SpecialPowers.flushPrefEnv();
  Services.prefs.getChildList("devtools.webconsole.filter").forEach(pref => {
    Services.prefs.clearUserPref(pref);
  });
  const browserConsole = HUDService.getBrowserConsole();
  if (browserConsole) {
    if (browserConsole.jsterm) {
      browserConsole.jsterm.hud.clearOutput(true);
    }
    await HUDService.toggleBrowserConsole();
  }
});

/**
 * Add a new tab and open the toolbox in it, and select the webconsole.
 *
 * @param string url
 *        The URL for the tab to be opened.
 * @param Boolean clearJstermHistory
 *        true (default) if the jsterm history should be cleared.
 * @return Promise
 *         Resolves when the tab has been added, loaded and the toolbox has been opened.
 *         Resolves to the toolbox.
 */
async function openNewTabAndConsole(url, clearJstermHistory = true) {
  const toolbox = await openNewTabAndToolbox(url, "webconsole");
  const hud = toolbox.getCurrentPanel().hud;

  if (clearJstermHistory) {
    // Clearing history that might have been set in previous tests.
    await hud.ui.consoleOutput.dispatchClearHistory();
  }

  return hud;
}

/**
 * Subscribe to the store and log out stringinfied versions of messages.
 * This is a helper function for debugging, to make is easier to see what
 * happened during the test in the log.
 *
 * @param object hud
 */
function logAllStoreChanges(hud) {
  const store = hud.ui.consoleOutput.getStore();
  // Adding logging each time the store is modified in order to check
  // the store state in case of failure.
  store.subscribe(() => {
    const messages = [...store.getState().messages.messagesById.values()];
    const debugMessages = messages.map(({id, type, parameters, messageText}) => {
      return {id, type, parameters, messageText};
    });
    info("messages : " + JSON.stringify(debugMessages));
  });
}

/**
 * Wait for messages in the web console output, resolving once they are received.
 *
 * @param object options
 *        - hud: the webconsole
 *        - messages: Array[Object]. An array of messages to match.
            Current supported options:
 *            - text: Partial text match in .message-body
 *        - selector: {String} a selector that should match the message node. Defaults to
 *                             ".message".
 */
function waitForMessages({ hud, messages, selector = ".message" }) {
  return new Promise(resolve => {
    const matchedMessages = [];
    hud.ui.on("new-messages",
      function messagesReceived(newMessages) {
        for (const message of messages) {
          if (message.matched) {
            continue;
          }

          for (const newMessage of newMessages) {
            const messageBody =
              newMessage.node.querySelector(`.message-body`);
            if (
              messageBody &&
              newMessage.node.matches(selector) &&
              messageBody.textContent.includes(message.text)
            ) {
              matchedMessages.push(newMessage);
              message.matched = true;
              const messagesLeft = messages.length - matchedMessages.length;
              info(`Matched a message with text: "${message.text}", ` + (messagesLeft > 0
                ? `still waiting for ${messagesLeft} messages.`
                : `all messages received.`)
              );
              break;
            }
          }

          if (matchedMessages.length === messages.length) {
            hud.ui.off("new-messages", messagesReceived);
            resolve(matchedMessages);
            return;
          }
        }
      });
  });
}

/**
 * Wait for a message with the provided text and showing the provided repeat count.
 *
 * @param {Object} hud : the webconsole
 * @param {String} text : text included in .message-body
 * @param {Number} repeat : expected repeat count in .message-repeats
 */
function waitForRepeatedMessage(hud, text, repeat) {
  return waitFor(() => {
    // Wait for a message matching the provided text.
    const node = findMessage(hud, text);
    if (!node) {
      return false;
    }

    // Check if there is a repeat node with the expected count.
    const repeatNode = node.querySelector(".message-repeats");
    if (repeatNode && parseInt(repeatNode.textContent, 10) === repeat) {
      return node;
    }

    return false;
  });
}

/**
 * Wait for a single message in the web console output, resolving once it is received.
 *
 * @param {Object} hud : the webconsole
 * @param {String} text : text included in .message-body
 * @param {String} selector : A selector that should match the message node.
 */
async function waitForMessage(hud, text, selector) {
  const messages = await waitForMessages({
    hud,
    messages: [{text}],
    selector,
  });
  return messages[0];
}

/**
 * Execute an input expression and wait for a message with the expected text (and an
 * optional selector) to be displayed in the output.
 *
 * @param {Object} hud : The webconsole.
 * @param {String} input : The input expression to execute.
 * @param {String} matchingText : A string that should match the message body content.
 * @param {String} selector : A selector that should match the message node.
 */
function executeAndWaitForMessage(hud, input, matchingText, selector = ".message") {
  const onMessage = waitForMessage(hud, matchingText, selector);
  hud.jsterm.execute(input);
  return onMessage;
}

/**
 * Wait for a predicate to return a result.
 *
 * @param function condition
 *        Invoked once in a while until it returns a truthy value. This should be an
 *        idempotent function, since we have to run it a second time after it returns
 *        true in order to return the value.
 * @param string message [optional]
 *        A message to output if the condition fails.
 * @param number interval [optional]
 *        How often the predicate is invoked, in milliseconds.
 * @return object
 *         A promise that is resolved with the result of the condition.
 */
async function waitFor(condition, message = "waitFor", interval = 10, maxTries = 500) {
  await BrowserTestUtils.waitForCondition(condition, message, interval, maxTries);
  return condition();
}

/**
 * Find a message in the output.
 *
 * @param object hud
 *        The web console.
 * @param string text
 *        A substring that can be found in the message.
 * @param selector [optional]
 *        The selector to use in finding the message.
 * @return {Node} the node corresponding the found message
 */
function findMessage(hud, text, selector = ".message") {
  const elements = findMessages(hud, text, selector);
  return elements.pop();
}

/**
 * Find multiple messages in the output.
 *
 * @param object hud
 *        The web console.
 * @param string text
 *        A substring that can be found in the message.
 * @param selector [optional]
 *        The selector to use in finding the message.
 */
function findMessages(hud, text, selector = ".message") {
  const messages = hud.ui.outputNode.querySelectorAll(selector);
  const elements = Array.prototype.filter.call(
    messages,
    (el) => el.textContent.includes(text)
  );
  return elements;
}

/**
 * Simulate a context menu event on the provided element, and wait for the console context
 * menu to open. Returns a promise that resolves the menu popup element.
 *
 * @param object hud
 *        The web console.
 * @param element element
 *        The dom element on which the context menu event should be synthesized.
 * @return promise
 */
async function openContextMenu(hud, element) {
  const onConsoleMenuOpened = hud.ui.consoleOutput.once("menu-open");
  synthesizeContextMenuEvent(element);
  await onConsoleMenuOpened;
  const doc = hud.ui.consoleOutput.owner.chromeWindow.document;
  return doc.getElementById("webconsole-menu");
}

/**
 * Hide the webconsole context menu popup. Returns a promise that will resolve when the
 * context menu popup is hidden or immediately if the popup can't be found.
 *
 * @param object hud
 *        The web console.
 * @return promise
 */
function hideContextMenu(hud) {
  const doc = hud.ui.consoleOutput.owner.chromeWindow.document;
  const popup = doc.getElementById("webconsole-menu");
  if (!popup) {
    return Promise.resolve();
  }

  const onPopupHidden = once(popup, "popuphidden");
  popup.hidePopup();
  return onPopupHidden;
}

function loadDocument(url, browser = gBrowser.selectedBrowser) {
  BrowserTestUtils.loadURI(browser, url);
  return BrowserTestUtils.browserLoaded(browser);
}

/**
* Returns a promise that resolves when the node passed as an argument mutate
* according to the passed configuration.
*
* @param {Node} node - The node to observe mutations on.
* @param {Object} observeConfig - A configuration object for MutationObserver.observe.
* @returns {Promise}
*/
function waitForNodeMutation(node, observeConfig = {}) {
  return new Promise(resolve => {
    const observer = new MutationObserver(mutations => {
      resolve(mutations);
      observer.disconnect();
    });
    observer.observe(node, observeConfig);
  });
}

/**
 * Search for a given message.  When found, simulate a click on the
 * message's location, checking to make sure that the debugger opens
 * the corresponding URL.
 *
 * @param {Object} hud
 *        The webconsole
 * @param {Object} toolbox
 *        The toolbox
 * @param {String} text
 *        The text to search for.  This should be contained in the
 *        message.  The searching is done with @see findMessage.
 */
async function testOpenInDebugger(hud, toolbox, text) {
  info(`Finding message for open-in-debugger test; text is "${text}"`);
  const messageNode = await waitFor(() => findMessage(hud, text));
  const frameLinkNode = messageNode.querySelector(".message-location .frame-link");
  ok(frameLinkNode, "The message does have a location link");
  await checkClickOnNode(hud, toolbox, frameLinkNode);
}

/**
 * Helper function for testOpenInDebugger.
 */
async function checkClickOnNode(hud, toolbox, frameLinkNode) {
  info("checking click on node location");

  const url = frameLinkNode.getAttribute("data-url");
  ok(url, `source url found ("${url}")`);

  const line = frameLinkNode.getAttribute("data-line");
  ok(line, `source line found ("${line}")`);

  const onSourceInDebuggerOpened = once(hud.ui, "source-in-debugger-opened");

  EventUtils.sendMouseEvent({ type: "click" },
    frameLinkNode.querySelector(".frame-link-filename"));

  await onSourceInDebuggerOpened;

  const dbg = toolbox.getPanel("jsdebugger");
  is(
    dbg._selectors.getSelectedSource(dbg._getState()).url,
    url,
    "expected source url"
  );
}

/**
 * Returns true if the give node is currently focused.
 */
function hasFocus(node) {
  return node.ownerDocument.activeElement == node
    && node.ownerDocument.hasFocus();
}

/**
 * Set the value of the JsTerm and its caret position, and wait for the autocompletion
 * to be updated.
 *
 * @param {JsTerm} jsterm
 * @param {String} value : The value to set the jsterm to.
 * @param {Integer} caretPosition : The index where to place the cursor. A negative
 *                  number will place the caret at (value.length - offset) position.
 *                  Default to value.length (caret set at the end).
 * @returns {Promise} resolves when the jsterm is completed.
 */
async function setInputValueForAutocompletion(
  jsterm,
  value,
  caretPosition = value.length,
) {
  jsterm.setInputValue("");
  jsterm.focus();

  const updated = jsterm.once("autocomplete-updated");
  EventUtils.sendString(value);
  await updated;

  if (caretPosition < 0) {
    caretPosition = value.length + caretPosition;
  }

  if (Number.isInteger(caretPosition)) {
    if (jsterm.inputNode) {
      const {inputNode} = jsterm;
      inputNode.value = value;
      inputNode.setSelectionRange(caretPosition, caretPosition);
    }

    if (jsterm.editor) {
      jsterm.editor.setCursor(jsterm.editor.getPosition(caretPosition));
    }
  }
}

/**
 * Set the value of the JsTerm and wait for the confirm dialog to be displayed.
 *
 * @param {Toolbox} toolbox
 * @param {JsTerm} jsterm
 * @param {String} value : The value to set the jsterm to.
 *                  Default to value.length (caret set at the end).
 * @returns {Promise<HTMLElement>} resolves with dialog element when it is opened.
 */
async function setInputValueForGetterConfirmDialog(toolbox, jsterm, value) {
  await setInputValueForAutocompletion(jsterm, value);
  await waitFor(() => isConfirmDialogOpened(toolbox));
  ok(true, "The confirm dialog is displayed");
  return getConfirmDialog(toolbox);
}

/**
 * Checks if the jsterm has the expected completion value.
 *
 * @param {JsTerm} jsterm
 * @param {String} expectedValue
 * @param {String} assertionInfo: Description of the assertion passed to `is`.
 */
function checkJsTermCompletionValue(jsterm, expectedValue, assertionInfo) {
  const completionValue = getJsTermCompletionValue(jsterm);
  if (completionValue === null) {
    ok(false, "Couldn't retrieve the completion value");
  }

  info(`Expects "${expectedValue}", is "${completionValue}"`);

  if (jsterm.completeNode) {
    is(completionValue, expectedValue, assertionInfo);
  } else {
    // CodeMirror jsterm doesn't need to add prefix-spaces.
    is(completionValue, expectedValue.trim(), assertionInfo);
  }
}

/**
 * Checks if the cursor on jsterm is at the expected position.
 *
 * @param {JsTerm} jsterm
 * @param {Integer} expectedCursorIndex
 * @param {String} assertionInfo: Description of the assertion passed to `is`.
 */
function checkJsTermCursor(jsterm, expectedCursorIndex, assertionInfo) {
  if (jsterm.inputNode) {
    const {selectionStart, selectionEnd} = jsterm.inputNode;
    is(selectionStart, expectedCursorIndex, assertionInfo);
    ok(selectionStart === selectionEnd);
  } else {
    is(jsterm.editor.getCursor().ch, expectedCursorIndex, assertionInfo);
  }
}

/**
 * Checks the jsterm value and the cursor position given an expected string containing
 * a "|" to indicate the expected cursor position.
 *
 * @param {JsTerm} jsterm
 * @param {String} expectedStringWithCursor:
 *                  String with a "|" to indicate the expected cursor position.
 *                  For example, this is how you assert an empty value with the focus "|",
 *                  and this indicates the value should be "test" and the cursor at the
 *                  end of the input: "test|".
 * @param {String} assertionInfo: Description of the assertion passed to `is`.
 */
function checkJsTermValueAndCursor(jsterm, expectedStringWithCursor, assertionInfo) {
  info(`Checking jsterm state: \n${expectedStringWithCursor}`);
  if (!expectedStringWithCursor.includes("|")) {
    ok(false,
      `expectedStringWithCursor must contain a "|" char to indicate cursor position`);
  }

  const inputValue = expectedStringWithCursor.replace("|", "");
  is(jsterm.getInputValue(), inputValue, "jsterm has expected value");
  if (jsterm.inputNode) {
    is(jsterm.inputNode.selectionStart, jsterm.inputNode.selectionEnd);
    is(jsterm.inputNode.selectionStart, expectedStringWithCursor.indexOf("|"),
      assertionInfo);
  } else {
    const lines = expectedStringWithCursor.split("\n");
    const lineWithCursor = lines.findIndex(line => line.includes("|"));
    const {ch, line} = jsterm.editor.getCursor();
    is(line, lineWithCursor, assertionInfo + " - correct line");
    is(ch, lines[lineWithCursor].indexOf("|"), assertionInfo + " - correct ch");
  }
}

/**
 * Returns the jsterm completion value, whether there's CodeMirror enabled or not.
 *
 * @param {JsTerm} jsterm
 * @returns {String}
 */
function getJsTermCompletionValue(jsterm) {
  if (jsterm.completeNode) {
    return jsterm.completeNode.value;
  }

  if (jsterm.editor) {
    return jsterm.editor.getAutoCompletionText();
  }

  return null;
}

/**
 * Returns a boolean indicating if the jsterm is focused, whether there's CodeMirror
 * enabled or not.
 *
 * @param {JsTerm} jsterm
 * @returns {Boolean}
 */
function isJstermFocused(jsterm) {
  const document = jsterm.outputNode.ownerDocument;
  const documentIsFocused = document.hasFocus();

  if (jsterm.inputNode) {
    return document.activeElement == jsterm.inputNode && documentIsFocused;
  }

  if (jsterm.editor) {
    return documentIsFocused && jsterm.editor.hasFocus();
  }

  return false;
}

/**
 * Open the JavaScript debugger.
 *
 * @param object options
 *        Options for opening the debugger:
 *        - tab: the tab you want to open the debugger for.
 * @return object
 *         A promise that is resolved once the debugger opens, or rejected if
 *         the open fails. The resolution callback is given one argument, an
 *         object that holds the following properties:
 *         - target: the Target object for the Tab.
 *         - toolbox: the Toolbox instance.
 *         - panel: the jsdebugger panel instance.
 */
async function openDebugger(options = {}) {
  if (!options.tab) {
    options.tab = gBrowser.selectedTab;
  }

  const target = await TargetFactory.forTab(options.tab);
  let toolbox = gDevTools.getToolbox(target);
  const dbgPanelAlreadyOpen = toolbox && toolbox.getPanel("jsdebugger");
  if (dbgPanelAlreadyOpen) {
    await toolbox.selectTool("jsdebugger");

    return {
      target,
      toolbox,
      panel: toolbox.getCurrentPanel(),
    };
  }

  toolbox = await gDevTools.showToolbox(target, "jsdebugger");
  const panel = toolbox.getCurrentPanel();

  // Do not clear VariableView lazily so it doesn't disturb test ending.
  if (panel._view) {
    panel._view.Variables.lazyEmpty = false;
  }

  // Old debugger
  if (panel.panelWin && panel.panelWin.DebuggerController) {
    await panel.panelWin.DebuggerController.waitForSourcesLoaded();
  } else {
    // New debugger
    await toolbox.threadClient.getSources();
  }
  return {target, toolbox, panel};
}

async function openInspector(options = {}) {
  if (!options.tab) {
    options.tab = gBrowser.selectedTab;
  }

  const target = await TargetFactory.forTab(options.tab);
  const toolbox = await gDevTools.showToolbox(target, "inspector");

  return toolbox.getCurrentPanel();
}

/**
 * Open the Web Console for the given tab, or the current one if none given.
 *
 * @param Element tab
 *        Optional tab element for which you want open the Web Console.
 *        Defaults to current selected tab.
 * @return Promise
 *         A promise that is resolved with the console hud once the web console is open.
 */
async function openConsole(tab) {
  const target = await TargetFactory.forTab(tab || gBrowser.selectedTab);
  const toolbox = await gDevTools.showToolbox(target, "webconsole");
  return toolbox.getCurrentPanel().hud;
}

/**
 * Close the Web Console for the given tab.
 *
 * @param Element [tab]
 *        Optional tab element for which you want close the Web Console.
 *        Defaults to current selected tab.
 * @return object
 *         A promise that is resolved once the web console is closed.
 */
async function closeConsole(tab = gBrowser.selectedTab) {
  const target = await TargetFactory.forTab(tab);
  const toolbox = gDevTools.getToolbox(target);
  if (toolbox) {
    await toolbox.destroy();
  }
}

/**
 * Fake clicking a link and return the URL we would have navigated to.
 * This function should be used to check external links since we can't access
 * network in tests.
 * This can also be used to test that a click will not be fired.
 *
 * @param ElementNode element
 *        The <a> element we want to simulate click on.
 * @param Object clickEventProps
 *        The custom properties which would be used to dispatch a click event
 * @returns Promise
 *          A Promise that is resolved when the link click simulation occured or
 *          when the click is not dispatched.
 *          The promise resolves with an object that holds the following properties
 *          - link: url of the link or null(if event not fired)
 *          - where: "tab" if tab is active or "tabshifted" if tab is inactive
 *            or null(if event not fired)
 */
function simulateLinkClick(element, clickEventProps) {
  return overrideOpenLink(() => {
    if (clickEventProps) {
      // Click on the link using the event properties.
      element.dispatchEvent(clickEventProps);
    } else {
      // Click on the link.
      element.click();
    }
  });
}

/**
 * Override the browserWindow open*Link function, executes the passed function and either
 * wait for:
 * - the link to be "opened"
 * - 1s before timing out
 * Then it puts back the original open*Link functions in browserWindow.
 *
 * @returns {Promise<Object>}: A promise resolving with an object of the following shape:
 * - link: The link that was "opened"
 * - where: If the link was opened in the background (null) or not ("tab").
 */
function overrideOpenLink(fn) {
  const browserWindow = Services.wm.getMostRecentWindow(gDevTools.chromeWindowType);

  // Override LinkIn methods to prevent navigating.
  const oldOpenTrustedLinkIn = browserWindow.openTrustedLinkIn;
  const oldOpenWebLinkIn = browserWindow.openWebLinkIn;

  const onOpenLink = new Promise((resolve) => {
    const openLinkIn = function(link, where) {
      browserWindow.openTrustedLinkIn = oldOpenTrustedLinkIn;
      browserWindow.openWebLinkIn = oldOpenWebLinkIn;
      resolve({link: link, where});
    };
    browserWindow.openWebLinkIn = browserWindow.openTrustedLinkIn = openLinkIn;
    fn();
  });

  // Declare a timeout Promise that we can use to make sure openTrustedLinkIn or
  // openWebLinkIn was not called.
  let timeoutId;
  const onTimeout = new Promise(function(resolve) {
    timeoutId = setTimeout(() => {
      browserWindow.openTrustedLinkIn = oldOpenTrustedLinkIn;
      browserWindow.openWebLinkIn = oldOpenWebLinkIn;
      timeoutId = null;
      resolve({link: null, where: null});
    }, 1000);
  });

  onOpenLink.then(() => {
    if (timeoutId) {
      clearTimeout(timeoutId);
    }
  });
  return Promise.race([onOpenLink, onTimeout]);
}

/**
 * Open a new browser window and return a promise that resolves when the new window has
 * fired the "browser-delayed-startup-finished" event.
 *
 * @param {Object} options: An options object that will be passed to OpenBrowserWindow.
 * @returns Promise
 *          A Promise that resolves when the window is ready.
 */
function openNewBrowserWindow(options) {
  const win = OpenBrowserWindow(options);
  return new Promise(resolve => {
    Services.obs.addObserver(function observer(subject, topic) {
      if (win == subject) {
        Services.obs.removeObserver(observer, topic);
        resolve(win);
      }
    }, "browser-delayed-startup-finished");
  });
}

/**
 * Open a network request logged in the webconsole in the netmonitor panel.
 *
 * @param {Object} toolbox
 * @param {Object} hud
 * @param {String} url
 *        URL of the request as logged in the netmonitor.
 * @param {String} urlInConsole
 *        (optional) Use if the logged URL in webconsole is different from the real URL.
 */
async function openMessageInNetmonitor(toolbox, hud, url, urlInConsole) {
  // By default urlInConsole should be the same as the complete url.
  urlInConsole = urlInConsole || url;

  const message = await waitFor(() => findMessage(hud, urlInConsole));

  const onNetmonitorSelected = toolbox.once("netmonitor-selected", (event, panel) => {
    return panel;
  });

  const menuPopup = await openContextMenu(hud, message);
  const openInNetMenuItem =
    menuPopup.querySelector("#console-menu-open-in-network-panel");
  ok(openInNetMenuItem, "open in network panel item is enabled");
  openInNetMenuItem.click();

  const {panelWin} = await onNetmonitorSelected;
  ok(true, "The netmonitor panel is selected when clicking on the network message");

  const { store, windowRequire } = panelWin;
  const nmActions = windowRequire("devtools/client/netmonitor/src/actions/index");
  const { getSelectedRequest } =
    windowRequire("devtools/client/netmonitor/src/selectors/index");

  store.dispatch(nmActions.batchEnable(false));

  await waitUntil(() => {
    const selected = getSelectedRequest(store.getState());
    return selected && selected.url === url;
  });

  ok(true, "The attached url is correct.");
}

function selectNode(hud, node) {
  const outputContainer = hud.ui.outputNode.querySelector(".webconsole-output");

  // We must first blur the input or else we can't select anything.
  outputContainer.ownerDocument.activeElement.blur();

  const selection = outputContainer.ownerDocument.getSelection();
  const range = document.createRange();
  range.selectNodeContents(node);
  selection.removeAllRanges();
  selection.addRange(range);

  return selection;
}

async function waitForBrowserConsole() {
  return new Promise(resolve => {
    Services.obs.addObserver(function observer(subject) {
      Services.obs.removeObserver(observer, "web-console-created");
      subject.QueryInterface(Ci.nsISupportsString);

      const hud = HUDService.getBrowserConsole();
      ok(hud, "browser console is open");
      is(subject.data, hud.hudId, "notification hudId is correct");

      executeSoon(() => resolve(hud));
    }, "web-console-created");
  });
}

/**
 * Get the state of a console filter.
 *
 * @param {Object} hud
 */
async function getFilterState(hud) {
  const filterBar = await setFilterBarVisible(hud, true);
  const buttons = filterBar.querySelectorAll("button");
  const result = { };

  for (const button of buttons) {
    const classes = new Set(button.classList.values());
    const checked = classes.has("checked");

    classes.delete("devtools-button");
    classes.delete("checked");

    const category = classes.values().next().value;

    result[category] = checked;
  }

  return result;
}

/**
 * Set the state of a console filter.
 *
 * @param {Object} hud
 * @param {Object} settings
 *        Category settings in the following format:
 *          {
 *            error: true,
 *            warn: true,
 *            log: true,
 *            info: true,
 *            debug: true,
 *            css: false,
 *            netxhr: false,
 *            net: false
 *          }
 */
async function setFilterState(hud, settings) {
  const filterBar = await setFilterBarVisible(hud, true);

  for (const category in settings) {
    const setActive = settings[category];
    const button = filterBar.querySelector(`.${category}`);

    if (!button) {
      ok(false, `setFilterState() called with a category of ${category}, ` +
                `which doesn't exist.`);
    }

    info(`Setting the ${category} category to ${setActive ? "checked" : "disabled"}`);

    const isChecked = button.classList.contains("checked");

    if (setActive !== isChecked) {
      button.click();

      await waitFor(() => {
        return button.classList.contains("checked") === setActive;
      });
    }
  }
}

/**
 * Set the visibility of the filter bar.
 *
 * @param {Object} hud
 * @param {Boolean} state
 *        Set filter bar visibility
 */
async function setFilterBarVisible(hud, state) {
  info(`Setting the filter bar visibility to ${state}`);

  const outputNode = hud.ui.outputNode;
  const toolbar = await waitFor(() => {
    return outputNode.querySelector(".webconsole-filterbar-primary");
  });
  let filterBar = outputNode.querySelector(".webconsole-filterbar-secondary");

  // Show filter bar if state is true
  if (state) {
    if (!filterBar) {
      // Click the filter icon to show the filter bar.
      toolbar.querySelector(".devtools-filter-icon").click();
      filterBar = await waitFor(() => {
        return outputNode.querySelector(".webconsole-filterbar-secondary");
      });
    }
    return filterBar;
  }

  // Hide filter bar if it is visible.
  if (filterBar) {
    // Click the filter icon to hide the filter bar.
    toolbar.querySelector(".devtools-filter-icon").click();
    await waitFor(() => {
      return !outputNode.querySelector(".webconsole-filterbar-secondary");
    });
  }

  return null;
}

/**
 * Reset the filters at the end of a test that has changed them. This is
 * important when using the `--verify` test option as when it is used you need
 * to manually reset the filters.
 *
 * The css, netxhr and net filters are disabled by default.
 *
 * @param {Object} hud
 */
async function resetFilters(hud) {
  info("Resetting filters to their default state");

  const store = hud.ui.consoleOutput.getStore();
  store.dispatch(wcActions.filtersClear());
}

/**
 * Open the reverse search input by simulating the appropriate keyboard shortcut.
 *
 * @param {Object} hud
 * @returns {DOMNode} The reverse search dom node.
 */
async function openReverseSearch(hud) {
  info("Open the reverse search UI with a keyboard shortcut");
  const onReverseSearchUiOpen = waitFor(() => getReverseSearchElement(hud));
  const isMacOS = AppConstants.platform === "macosx";
  if (isMacOS) {
    EventUtils.synthesizeKey("r", {ctrlKey: true});
  } else {
    EventUtils.synthesizeKey("VK_F9");
  }

  const element = await onReverseSearchUiOpen;
  return element;
}

function getReverseSearchElement(hud) {
  const {outputNode} = hud.ui;
  return outputNode.querySelector(".reverse-search");
}

function getReverseSearchInfoElement(hud) {
  const reverseSearchElement = getReverseSearchElement(hud);
  if (!reverseSearchElement) {
    return null;
  }

  return reverseSearchElement.querySelector(".reverse-search-info");
}

/**
 * Returns a boolean indicating if the reverse search input is focused.
 *
 * @param {JsTerm} jsterm
 * @returns {Boolean}
 */
function isReverseSearchInputFocused(hud) {
  const {outputNode} = hud.ui;
  const document = outputNode.ownerDocument;
  const documentIsFocused = document.hasFocus();
  const reverseSearchInput = outputNode.querySelector(".reverse-search-input");

  return document.activeElement == reverseSearchInput && documentIsFocused;
}

/**
 * Selects a node in the inspector.
 *
 * @param {Object} toolbox
 * @param {Object} testActor: A test actor registered on the target. Needed to click on
 *                            the content element.
 * @param {String} selector: The selector for the node we want to select.
 */
async function selectNodeWithPicker(toolbox, testActor, selector) {
  const inspector = toolbox.getPanel("inspector");
  const inspectorFront = inspector.inspector;

  const onPickerStarted = inspectorFront.nodePicker.once("picker-started");
  inspectorFront.nodePicker.start();
  await onPickerStarted;

  info(`Picker mode started, now clicking on "${selector}" to select that node`);
  const onPickerStopped = inspectorFront.nodePicker.once("picker-stopped");
  const onInspectorUpdated = inspector.once("inspector-updated");

  testActor.synthesizeMouse({
    selector,
    center: true,
    options: {},
  });

  await onPickerStopped;
  await onInspectorUpdated;
}

/**
 * Clicks on the arrow of a single object inspector node if it exists.
 *
 * @param {HTMLElement} node: Object inspector node (.tree-node)
 */
function expandObjectInspectorNode(node) {
  const arrow = getObjectInspectorNodeArrow(node);
  if (!arrow) {
    ok(false, "Node can't be expanded");
    return;
  }
  arrow.click();
}

/**
 * Retrieve the arrow of a single object inspector node.
 *
 * @param {HTMLElement} node: Object inspector node (.tree-node)
 * @return {HTMLElement|null} the arrow element
 */
function getObjectInspectorNodeArrow(node) {
  return node.querySelector(".arrow");
}

/**
 * Check if a single object inspector node is expandable.
 *
 * @param {HTMLElement} node: Object inspector node (.tree-node)
 * @return {Boolean} true if the node can be expanded
 */
function isObjectInspectorNodeExpandable(node) {
  return !!getObjectInspectorNodeArrow(node);
}

/**
 * Retrieve the nodes for a given object inspector element.
 *
 * @param {HTMLElement} oi: Object inspector element
 * @return {NodeList} the object inspector nodes
 */
function getObjectInspectorNodes(oi) {
  return oi.querySelectorAll(".tree-node");
}

/**
 * Retrieve the "children" nodes for a given object inspector node.
 *
 * @param {HTMLElement} node: Object inspector node (.tree-node)
 * @return {Array<HTMLElement>} the direct children (i.e. the ones that are one level
 *                              deeper than the passed node)
 */
function getObjectInspectorChildrenNodes(node) {
  const getLevel = n => parseInt(n.getAttribute("aria-level"), 10);
  const level = getLevel(node);
  const childLevel = level + 1;
  const children = [];

  let currentNode = node;
  while (currentNode.nextSibling && getLevel(currentNode.nextSibling) === childLevel) {
    currentNode = currentNode.nextSibling;
    children.push(currentNode);
  }

  return children;
}

/**
 * Retrieve the invoke getter button for a given object inspector node.
 *
 * @param {HTMLElement} node: Object inspector node (.tree-node)
 * @return {HTMLElement|null} the invoke button element
 */
function getObjectInspectorInvokeGetterButton(node) {
  return node.querySelector(".invoke-getter");
}

/**
 * Retrieve the first node that match the passed node label, for a given object inspector
 * element.
 *
 * @param {HTMLElement} oi: Object inspector element
 * @param {String} nodeLabel: label of the searched node
 * @return {HTMLElement|null} the Object inspector node with the matching label
 */
function findObjectInspectorNode(oi, nodeLabel) {
  return [...oi.querySelectorAll(".tree-node")].find(node => {
    const label = node.querySelector(".object-label");
    if (!label) {
      return false;
    }
    return label.textContent === nodeLabel;
  });
}

/**
 * Return an array of the label of the autocomplete popup items.
 *
 * @param {AutocompletPopup} popup
 * @returns {Array<String>}
 */
function getAutocompletePopupLabels(popup) {
  return popup.getItems().map(item => item.label);
}

/**
 * Return the "Confirm Dialog" element.
 *
 * @param toolbox
 * @returns {HTMLElement|null}
 */
function getConfirmDialog(toolbox) {
  const {doc} = toolbox;
  return doc.querySelector(".invoke-confirm");
}

/**
 * Returns true if the Confirm Dialog is opened.
 * @param toolbox
 * @returns {Boolean}
 */
function isConfirmDialogOpened(toolbox) {
  const tooltip = getConfirmDialog(toolbox);
  if (!tooltip) {
    return false;
  }

  return tooltip.classList.contains("tooltip-visible");
}

