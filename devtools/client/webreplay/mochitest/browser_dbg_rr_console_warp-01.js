/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */
/* eslint-disable no-undef */

"use strict";

// Test basic console time warping functionality in web replay.
add_task(async function() {
  const dbg = await attachRecordingDebugger("doc_rr_error.html", {
    waitForRecording: true,
  });

  const { threadFront, target } = dbg;
  const console = await getDebuggerSplitConsole(dbg);
  const hud = console.hud;

  await warpToMessage(hud, dbg, "Number 5");
  await threadFront.interrupt();

  await checkEvaluateInTopFrame(target, "number", 5);

  // Initially we are paused inside the 'new Error()' call on line 19. The
  // first reverse step takes us to the start of that line.
  await reverseStepOverToLine(threadFront, 19);
  await reverseStepOverToLine(threadFront, 18);
  const bp = await setBreakpoint(threadFront, "doc_rr_error.html", 12);
  await rewindToLine(threadFront, 12);
  await checkEvaluateInTopFrame(target, "number", 4);
  await resumeToLine(threadFront, 12);
  await checkEvaluateInTopFrame(target, "number", 5);

  await threadFront.removeBreakpoint(bp);
  await shutdownDebugger(dbg);
});
