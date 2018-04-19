/* global toolbox */

info(`START: ${new Error().lineNumber}`);

Task.spawn(function* () {
  Services.prefs.clearUserPref("devtools.debugger.tabs")
  Services.prefs.clearUserPref("devtools.debugger.pending-selected-location")

  info("Waiting for debugger load");
  yield toolbox.selectTool("jsdebugger");
  let dbg = createDebuggerContext(toolbox);
  let window = dbg.win;
  let document = window.document;

  yield waitForSources(dbg, testUrl);
//  yield waitForSourceCount(dbg, 6);

  info("Loaded, selecting the test script to debug");
  // First expand the domain
  let domain = [...document.querySelectorAll(".tree-node")].find(node => {
    return node.textContent.trim() == "mozilla.org";
  });
  let arrow = domain.querySelector(".arrow");
  arrow.click();

  let fileName = testUrl.match(/browser-toolbox-test.*\.js/)[0];

  let script = [...document.querySelectorAll(".tree-node")].find(node => {
    return node.textContent.includes(fileName);
  });
  script = script.querySelector(".node");
  script.click();

  let onPaused = waitForPaused(dbg);
  yield addBreakpoint(dbg, fileName, 2);

  yield onPaused;

  assertPausedLocation(dbg, fileName, 2);

  yield stepIn(dbg);

  assertPausedLocation(dbg, fileName, 3);

  // Remove the breakpoint before resuming in order to prevent hitting the breakpoint
  // again during test closing.
  let source = findSource(dbg, fileName);
  yield removeBreakpoint(dbg, source.id, 2);

  yield resume(dbg);

  info("Close the browser toolbox");
  toolbox.destroy();
});
