/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */
/* eslint-disable no-shadow, max-nested-callbacks */

"use strict";

/**
 * Test that we don't stop at debugger statements inside black boxed sources.
 */

var gDebuggee;
var gThreadFront;

add_task(
  threadFrontTest(
    async ({ threadFront, debuggee }) => {
      gThreadFront = threadFront;
      gDebuggee = debuggee;
      test_black_box();
    },
    { waitForFinish: true }
  )
);

const BLACK_BOXED_URL = "http://example.com/blackboxme.js";
const SOURCE_URL = "http://example.com/source.js";

function test_black_box() {
  gThreadFront.once("paused", async function(packet) {
    const source = await getSourceById(gThreadFront, packet.frame.where.actor);
    gThreadFront.setBreakpoint({ sourceUrl: source.url, line: 4 }, {});
    await gThreadFront.resume();
    test_black_box_dbg_statement();
  });

  /* eslint-disable no-multi-spaces, no-undef */
  // prettier-ignore
  Cu.evalInSandbox(
    "" + function doStuff(k) { // line 1
      debugger;                // line 2 - Break here
      k(100);                  // line 3
    },                         // line 4
    gDebuggee,
    "1.8",
    BLACK_BOXED_URL,
    1
  );
  // prettier-ignore
  Cu.evalInSandbox(
    "" + function runTest() { // line 1
      doStuff(                // line 2
        function(n) {        // line 3
          Math.abs(n);        // line 4 - Break here
        }                     // line 5
      );                      // line 6
    }                         // line 7
    + "\n debugger;",         // line 8
    gDebuggee,
    "1.8",
    SOURCE_URL,
    1
  );
  /* eslint-enable no-multi-spaces, no-undef */
}

async function test_black_box_dbg_statement() {
  await gThreadFront.getSources();
  const sourceFront = await getSource(gThreadFront, BLACK_BOXED_URL);

  await blackBox(sourceFront);

  gThreadFront.once("paused", async function(packet) {
    Assert.equal(
      packet.why.type,
      "breakpoint",
      "We should pass over the debugger statement."
    );

    const source = await getSourceById(gThreadFront, packet.frame.where.actor);
    gThreadFront.removeBreakpoint({ sourceUrl: source.url, line: 4 }, {});

    await gThreadFront.resume();
    await test_unblack_box_dbg_statement(sourceFront);
  });
  gDebuggee.runTest();
}

async function test_unblack_box_dbg_statement(sourceFront) {
  await unBlackBox(sourceFront);

  gThreadFront.once("paused", async function(packet) {
    Assert.equal(
      packet.why.type,
      "debuggerStatement",
      "We should stop at the debugger statement again"
    );
    await gThreadFront.resume();
    threadFrontTestFinished();
  });
  gDebuggee.runTest();
}
