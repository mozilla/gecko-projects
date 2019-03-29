/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at <http://mozilla.org/MPL/2.0/>. */

function toggleBreakpoint(dbg, index) {
  const bp = findAllElements(dbg, "breakpointItems")[index];
  const input = bp.querySelector("input");
  input.click();
}

async function disableBreakpoint(dbg, index) {
  const disabled = waitForDispatch(dbg, "DISABLE_BREAKPOINT");
  toggleBreakpoint(dbg, index);
  await disabled;
}

async function enableBreakpoint(dbg, index) {
  const enabled = waitForDispatch(dbg, "ENABLE_BREAKPOINT");
  toggleBreakpoint(dbg, index);
  await enabled;
}

// Test enabling and disabling a breakpoint using the check boxes
add_task(async function() {
  const dbg = await initDebugger("doc-scripts.html", "simple2");

  // Create two breakpoints
  await selectSource(dbg, "simple2");
  await addBreakpoint(dbg, "simple2", 3);
  await addBreakpoint(dbg, "simple2", 5);

  // Disable the first one
  await disableBreakpoint(dbg, 0);
  const bp1 = findBreakpoint(dbg, "simple2", 3);
  let bp2 = findBreakpoint(dbg, "simple2", 5);
  is(bp1.disabled, true, "first breakpoint is disabled");
  is(bp2.disabled, false, "second breakpoint is enabled");

  // Disable and Re-Enable the second one
  await disableBreakpoint(dbg, 1);
  await enableBreakpoint(dbg, 1);
  bp2 = findBreakpoint(dbg, "simple2", 5);
  is(bp2.disabled, false, "second breakpoint is enabled");
});

// Test enabling and disabling a breakpoint using the context menu
add_task(async function() {
  const dbg = await initDebugger("doc-scripts.html");
  await selectSource(dbg, "simple2");
  await addBreakpoint(dbg, "simple2", 3);
  await addBreakpoint(dbg, "simple2", 5);

  assertEmptyLines(dbg, [1, 2]);
  assertBreakpointSnippet(dbg, 3, "return x + y;");

  rightClickElement(dbg, "breakpointItem", 3);
  const disableBreakpointDispatch = waitForDispatch(dbg, "DISABLE_BREAKPOINT");
  selectContextMenuItem(dbg, selectors.breakpointContextMenu.disableSelf);
  await disableBreakpointDispatch;

  let bp1 = findBreakpoint(dbg, "simple2", 3);
  let bp2 = findBreakpoint(dbg, "simple2", 5);
  is(bp1.disabled, true, "first breakpoint is disabled");
  is(bp2.disabled, false, "second breakpoint is enabled");

  rightClickElement(dbg, "breakpointItem", 3);
  const enableBreakpointDispatch = waitForDispatch(dbg, "ENABLE_BREAKPOINT");
  selectContextMenuItem(dbg, selectors.breakpointContextMenu.enableSelf);
  await enableBreakpointDispatch;

  bp1 = findBreakpoint(dbg, "simple2", 3);
  bp2 = findBreakpoint(dbg, "simple2", 5);
  is(bp1.disabled, false, "first breakpoint is enabled");
  is(bp2.disabled, false, "second breakpoint is enabled");
});
