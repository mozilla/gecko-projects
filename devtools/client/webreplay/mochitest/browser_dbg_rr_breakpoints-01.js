/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */
/* eslint-disable no-undef */

"use strict";

// Test basic breakpoint functionality in web replay.
add_task(async function() {
  const dbg = await attachRecordingDebugger("doc_rr_basic.html", {
    waitForRecording: true,
  });
  const { threadFront, target } = dbg;

  const bp = await setBreakpoint(threadFront, "doc_rr_basic.html", 21);

  // Visit a lot of breakpoints so that we are sure we have crossed major
  // checkpoint boundaries.
  await rewindToLine(threadFront, 21);
  await checkEvaluateInTopFrame(target, "number", 10);
  await rewindToLine(threadFront, 21);
  await checkEvaluateInTopFrame(target, "number", 9);
  await rewindToLine(threadFront, 21);
  await checkEvaluateInTopFrame(target, "number", 8);
  await rewindToLine(threadFront, 21);
  await checkEvaluateInTopFrame(target, "number", 7);
  await rewindToLine(threadFront, 21);
  await checkEvaluateInTopFrame(target, "number", 6);
  await resumeToLine(threadFront, 21);
  await checkEvaluateInTopFrame(target, "number", 7);
  await resumeToLine(threadFront, 21);
  await checkEvaluateInTopFrame(target, "number", 8);
  await resumeToLine(threadFront, 21);
  await checkEvaluateInTopFrame(target, "number", 9);
  await resumeToLine(threadFront, 21);
  await checkEvaluateInTopFrame(target, "number", 10);

  await threadFront.removeBreakpoint(bp);
  await shutdownDebugger(dbg);
});
