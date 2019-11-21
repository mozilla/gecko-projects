/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */
/* eslint-disable no-shadow, max-nested-callbacks */

"use strict";

var gDebuggee;
var gThreadFront;

// This test ensures that we can create SourceActors and SourceFronts properly,
// and that they can communicate over the protocol to fetch the source text for
// a given script.

add_task(
  threadFrontTest(
    async ({ threadFront, debuggee }) => {
      gThreadFront = threadFront;
      gDebuggee = debuggee;
      test_source();
    },
    { waitForFinish: true }
  )
);

const SOURCE_URL = "http://example.com/foobar.js";
const SOURCE_CONTENT = "stopMe()";

function test_source() {
  DebuggerServer.LONG_STRING_LENGTH = 200;

  gThreadFront.once("paused", function(packet) {
    gThreadFront.getSources().then(function(response) {
      Assert.ok(!!response);
      Assert.ok(!!response.sources);

      const source = response.sources.filter(function(s) {
        return s.url === SOURCE_URL;
      })[0];

      Assert.ok(!!source);

      const sourceFront = gThreadFront.source(source);
      sourceFront.source().then(function(response) {
        Assert.ok(!!response);
        Assert.ok(!!response.contentType);
        Assert.ok(response.contentType.includes("javascript"));

        Assert.ok(!!response.source);
        Assert.equal(SOURCE_CONTENT, response.source);

        gThreadFront.resume().then(function() {
          threadFrontTestFinished();
        });
      });
    });
  });

  Cu.evalInSandbox(
    "" +
      function stopMe(arg1) {
        debugger;
      },
    gDebuggee,
    "1.8",
    getFileUrl("test_source-01.js")
  );

  Cu.evalInSandbox(SOURCE_CONTENT, gDebuggee, "1.8", SOURCE_URL);
}
