/* -*- indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim: set ft=javascript ts=2 et sw=2 tw=80: */
/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

// Return a promise that resolves when a breakpoint has been set.
function setBreakpoint(sourceClient, lineno) {
  return new Promise(function(resolve) {
    sourceClient.setBreakpoint({ line: lineno }, function(response, bpClient) {
      resolve(response);
    });
  });
}

// Return a promise that resolves when a thread rewinds and then pauses.
function rewindUntilPaused(threadClient) {
  var promise = new Promise(function(resolve) {
    threadClient.addOneTimeListener("paused", function(event, packet) {
      resolve();
    });
  });
  threadClient.rewind();
  return promise;
}

// Return a promise that resolves when a thread plays forward and then pauses.
function resumeUntilPaused(threadClient) {
  var promise = new Promise(function(resolve) {
    threadClient.addOneTimeListener("paused", function(event, packet) {
      resolve();
    });
  });
  threadClient.resume();
  return promise;
}

// Return a promise that resolves when a thread evaluates a string in the
// topmost frame, returning the result of the eval.
function evaluateInTopFrame(threadClient, text) {
  return new Promise(function(resolve) {
    threadClient.getFrames(0, 1).then(function({frames}) {
      ok(frames.length == 1, "Got one frame");
      return threadClient.eval(frames[0].actor, text);
    }).then(function(response) {
      ok(response.type == "resumed", "Got resume response from eval");
      threadClient.addOneTimeListener("paused", function(event, packet) {
        ok(packet.type == "paused", "Got pause response after eval resume");
        ok(packet.why.type == "clientEvaluated", "Pause response was for eval");
        ok("return" in packet.why.frameFinished, "Eval returned a value");
        resolve(packet.why.frameFinished["return"]);
      });
    });
  });
}

function startDebugger(tab) {
  let target = TargetFactory.forTab(tab);
  gDevTools.showToolbox(target, "jsdebugger").then(aToolbox => {
    let client = aToolbox.threadClient;
    ok(client.state == "attached", "Thread is attached");
    client.interrupt().then(function() {
      ok(true, "Interrupt succeeded");
      return client.getSources();
    }).then(function({sources}) {
      ok(sources.length == 1, "Got one source");
      ok(/doc_rr_basic.html/.test(sources[0].url), "Source is doc_rr_basic.html");
      let sourceClient = client.source(sources[0]);
      return setBreakpoint(sourceClient, 21);
    }).then(function() {
      ok(true, "Breakpoint set");
      return rewindUntilPaused(client);
    }).then(function() {
      ok(true, "Paused after rewind");
      return evaluateInTopFrame(client, "number");
    }).then(function(result) {
      ok(result == 10, "number == 10 after first rewind");
      return rewindUntilPaused(client);
    }).then(function() {
      ok(true, "Paused after rewind #2");
      return evaluateInTopFrame(client, "number");
    }).then(function(result) {
      ok(result == 9, "number == 9 after second rewind");
      return resumeUntilPaused(client);
    }).then(function() {
      ok(true, "Paused after resume");
      return evaluateInTopFrame(client, "number");
    }).then(function(result) {
      ok(result == 10, "number == 10 after resuming");
      finish();
    });
  });
}

function test() {
  waitForExplicitFinish();

  var tab = gBrowser.addTab(null, { recordExecution: "*" });

  const ppmm = Cc["@mozilla.org/parentprocessmessagemanager;1"]
        .getService(Ci.nsIMessageBroadcaster);
  ppmm.addMessageListener("RecordingFinished", function() {
    startDebugger(tab);
  });

  gBrowser.selectedTab = tab;
  openUILinkIn(EXAMPLE_URL + "doc_rr_basic.html", "current");
}
