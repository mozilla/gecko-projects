/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// See Bug 585991.

const TEST_URI = `data:text/html;charset=utf-8,
<head>
  <script>
    window.foo = [1,2,3];
  </script>
</head>
<body>bug 585991 - Autocomplete popup on array</body>`;

add_task(async function() {
  const hud = await openNewTabAndConsole(TEST_URI);
  const { autocompletePopup: popup } = hud.jsterm;

  const onPopUpOpen = popup.once("popup-opened");

  info("wait for popup to show");
  setInputValue(hud, "foo");
  EventUtils.sendString(".");

  await onPopUpOpen;

  const popupItems = popup.getItems().map(e => e.label);
  is(
    popupItems.includes("0"),
    false,
    "Completing on an array doesn't show numbers."
  );

  info("press Escape to close the popup");
  const onPopupClose = popup.once("popup-closed");
  EventUtils.synthesizeKey("KEY_Escape");

  await onPopupClose;
});
