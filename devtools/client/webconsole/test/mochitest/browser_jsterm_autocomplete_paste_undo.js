/* -*- indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim: set ft=javascript ts=2 et sw=2 tw=80: */
/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const TEST_URI = "data:text/html;charset=utf-8,<p>test for bug 642615</p>";

XPCOMUtils.defineLazyServiceGetter(
  this,
  "clipboardHelper",
  "@mozilla.org/widget/clipboardhelper;1",
  "nsIClipboardHelper"
);
const stringToCopy = "foobazbarBug642615";

add_task(async function() {
  const {jsterm, ui} = await openNewTabAndConsole(TEST_URI);
  ui.clearOutput();
  ok(!jsterm.completeNode.value, "no completeNode.value");

  jsterm.setInputValue("doc");

  info("wait for completion value after typing 'docu'");
  let onAutocompleteUpdated = jsterm.once("autocomplete-updated");
  EventUtils.sendString("u");
  await onAutocompleteUpdated;

  const completionValue = jsterm.completeNode.value;

  info(`Copy "${stringToCopy}" in clipboard`);
  await waitForClipboardPromise(() =>
    clipboardHelper.copyString(stringToCopy), stringToCopy);

  jsterm.setInputValue("docu");
  info("wait for completion update after clipboard paste");
  onAutocompleteUpdated = jsterm.once("autocomplete-updated");
  goDoCommand("cmd_paste");

  await onAutocompleteUpdated;

  ok(!jsterm.completeNode.value, "no completion value after paste");

  info("wait for completion update after undo");
  onAutocompleteUpdated = jsterm.once("autocomplete-updated");

  goDoCommand("cmd_undo");

  await onAutocompleteUpdated;

  is(jsterm.completeNode.value, completionValue, "same completeNode.value after undo");

  info("wait for completion update after clipboard paste (ctrl-v)");
  onAutocompleteUpdated = jsterm.once("autocomplete-updated");

  EventUtils.synthesizeKey("v", {accelKey: true});

  await onAutocompleteUpdated;
  ok(!jsterm.completeNode.value, "no completion value after paste (ctrl-v)");
});
