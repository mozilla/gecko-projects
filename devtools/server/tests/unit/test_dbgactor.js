/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const xpcInspector = Cc["@mozilla.org/jsinspector;1"].getService(Ci.nsIJSInspector);

add_task(threadClientTest(async ({ threadClient, debuggee, client, targetFront }) => {
  Assert.equal(xpcInspector.eventLoopNestLevel, 0);

  await new Promise(resolve => {
    client.addListener("paused", function(name, packet) {
      Assert.equal(name, "paused");
      Assert.equal(false, "error" in packet);
      Assert.equal(packet.from, threadClient.actor);
      Assert.equal(packet.type, "paused");
      Assert.ok("actor" in packet);
      Assert.ok("why" in packet);
      Assert.equal(packet.why.type, "debuggerStatement");

      // Reach around the protocol to check that the debuggee is in the state
      // we expect.
      Assert.ok(debuggee.a);
      Assert.ok(!debuggee.b);

      Assert.equal(xpcInspector.eventLoopNestLevel, 1);

      // Let the debuggee continue execution.
      threadClient.resume().then(resolve);
    });

    // Now that we know we're resumed, we can make the debuggee do something.
    Cu.evalInSandbox("var a = true; var b = false; debugger; var b = true;", debuggee);
    // Now make sure that we've run the code after the debugger statement...
    Assert.ok(debuggee.b);
  });

  Assert.equal(xpcInspector.eventLoopNestLevel, 0);
}));
