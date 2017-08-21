/* -*- js-indent-level: 2; indent-tabs-mode: nil -*- */
/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { DevToolsShim } =
    Components.utils.import("chrome://devtools-shim/content/DevToolsShim.jsm", {});

// Test the DevToolsShim

/**
 * Create a mocked version of DevTools that records all calls made to methods expected
 * to be called by DevToolsShim.
 */
function createMockDevTools() {
  let methods = [
    "on",
    "off",
    "registerTool",
    "registerTheme",
    "unregisterTool",
    "unregisterTheme",
    "emit",
    "saveDevToolsSession",
    "restoreDevToolsSession",
  ];

  let mock = {
    callLog: {}
  };

  for (let method of methods) {
    // Create a stub for method, that only pushes its arguments in the inner callLog
    mock[method] = function (...args) {
      mock.callLog[method].push(args);
    };
    mock.callLog[method] = [];
  }

  return mock;
}

let isInstalledMethodBackup = DevToolsShim.isInstalled;
function mockDevToolsInstalled(isInstalled) {
  DevToolsShim.isInstalled = () => isInstalled;
}

function restoreDevToolsInstalled() {
  DevToolsShim.isInstalled = isInstalledMethodBackup;
}

/**
 * Check if a given method was called an expected number of times, and finally check the
 * arguments provided to the last call, if appropriate.
 */
function checkCalls(mock, method, length, lastArgs) {
  ok(mock.callLog[method].length === length,
      "Devtools.on was called the expected number of times");

  // If we don't want to check the last call or if the method was never called, bail out.
  if (!lastArgs || length === 0) {
    return;
  }

  for (let i = 0; i < lastArgs.length; i++) {
    let expectedArg = lastArgs[i];
    ok(mock.callLog[method][length - 1][i] === expectedArg,
        `Devtools.${method} was called with the expected argument (index ${i})`);
  }
}

function test_register_unregister() {
  mockDevToolsInstalled(true);

  ok(!DevToolsShim.isInitialized(), "DevTools are not initialized");

  DevToolsShim.register(createMockDevTools());
  ok(DevToolsShim.isInitialized(), "DevTools are installed");

  DevToolsShim.unregister();
  ok(!DevToolsShim.isInitialized(), "DevTools are not initialized");

  restoreDevToolsInstalled();
}

function test_on_is_forwarded_to_devtools() {
  ok(!DevToolsShim.isInitialized(), "DevTools are not initialized");

  function cb1() {}
  function cb2() {}
  let mock = createMockDevTools();

  DevToolsShim.on("test_event", cb1);
  DevToolsShim.register(mock);
  checkCalls(mock, "on", 1, ["test_event", cb1]);

  DevToolsShim.on("other_event", cb2);
  checkCalls(mock, "on", 2, ["other_event", cb2]);

  restoreDevToolsInstalled();
}

function test_off_called_before_registering_devtools() {
  mockDevToolsInstalled(true);

  ok(!DevToolsShim.isInitialized(), "DevTools are not initialized");

  function cb1() {}
  let mock = createMockDevTools();

  DevToolsShim.on("test_event", cb1);
  DevToolsShim.off("test_event", cb1);

  DevToolsShim.register(mock);
  checkCalls(mock, "on", 0);

  restoreDevToolsInstalled();
}

function test_off_called_before_with_bad_callback() {
  mockDevToolsInstalled(true);

  ok(!DevToolsShim.isInitialized(), "DevTools are not initialized");

  function cb1() {}
  function cb2() {}
  let mock = createMockDevTools();

  DevToolsShim.on("test_event", cb1);
  DevToolsShim.off("test_event", cb2);

  DevToolsShim.register(mock);
  // on should still be called
  checkCalls(mock, "on", 1, ["test_event", cb1]);
  // Calls to off should not be held and forwarded.
  checkCalls(mock, "off", 0);

  restoreDevToolsInstalled();
}

function test_registering_tool() {
  mockDevToolsInstalled(true);

  ok(!DevToolsShim.isInitialized(), "DevTools are not initialized");

  let tool1 = {};
  let tool2 = {};
  let tool3 = {};
  let mock = createMockDevTools();

  // Pre-register tool1
  DevToolsShim.registerTool(tool1);

  // Pre-register tool3, but unregister right after
  DevToolsShim.registerTool(tool3);
  DevToolsShim.unregisterTool(tool3);

  DevToolsShim.register(mock);
  checkCalls(mock, "registerTool", 1, [tool1]);

  DevToolsShim.registerTool(tool2);
  checkCalls(mock, "registerTool", 2, [tool2]);

  DevToolsShim.unregister();

  // Create a new mock and check the tools are not added once again.
  mock = createMockDevTools();
  DevToolsShim.register(mock);
  checkCalls(mock, "registerTool", 0);

  restoreDevToolsInstalled();
}

function test_registering_theme() {
  mockDevToolsInstalled(true);

  ok(!DevToolsShim.isInitialized(), "DevTools are not initialized");

  let theme1 = {};
  let theme2 = {};
  let theme3 = {};
  let mock = createMockDevTools();

  // Pre-register theme1
  DevToolsShim.registerTheme(theme1);

  // Pre-register theme3, but unregister right after
  DevToolsShim.registerTheme(theme3);
  DevToolsShim.unregisterTheme(theme3);

  DevToolsShim.register(mock);
  checkCalls(mock, "registerTheme", 1, [theme1]);

  DevToolsShim.registerTheme(theme2);
  checkCalls(mock, "registerTheme", 2, [theme2]);

  DevToolsShim.unregister();

  // Create a new mock and check the themes are not added once again.
  mock = createMockDevTools();
  DevToolsShim.register(mock);
  checkCalls(mock, "registerTheme", 0);

  restoreDevToolsInstalled();
}

function test_events() {
  mockDevToolsInstalled(true);

  ok(!DevToolsShim.isInitialized(), "DevTools are not initialized");

  let mock = createMockDevTools();
  // Check emit was not called.
  checkCalls(mock, "emit", 0);

  // Check emit is called once with the devtools-registered event.
  DevToolsShim.register(mock);
  checkCalls(mock, "emit", 1, ["devtools-registered"]);

  // Check emit is called once with the devtools-unregistered event.
  DevToolsShim.unregister();
  checkCalls(mock, "emit", 2, ["devtools-unregistered"]);

  restoreDevToolsInstalled();
}

function test_scratchpad_apis() {
  mockDevToolsInstalled(false);

  ok(!DevToolsShim.isInstalled(), "DevTools are not installed");
  ok(!DevToolsShim.isInitialized(), "DevTools are not initialized");

  // Ensure that saveDevToolsSession doesn't initialize the tools
  DevToolsShim.saveDevToolsSession({});

  ok(!DevToolsShim.isInstalled(), "DevTools are not installed");
  ok(!DevToolsShim.isInitialized(), "DevTools are not initialized");

  // Check that restoreDevToolsSession doesn't crash.
  DevToolsShim.restoreDevToolsSession({
    scratchpads: [{}],
    browserConsole: true,
  });

  // Check that saveDevToolsSession doesn't crash.
  DevToolsShim.saveDevToolsSession({});

  mockDevToolsInstalled(true);

  ok(DevToolsShim.isInstalled(), "DevTools are installed");
  ok(!DevToolsShim.isInitialized(), "DevTools are not initialized");

  let mock = createMockDevTools();
  DevToolsShim._initDevTools = () => {
    // Next call to restoreDevToolsSession is expected to initialize DevTools, which we
    // simulate here by registering our mock.
    DevToolsShim.register(mock);
  };

  let scratchpadSessions = [{}];
  DevToolsShim.restoreDevToolsSession(scratchpadSessions);
  checkCalls(mock, "restoreDevToolsSession", 1, [scratchpadSessions]);

  ok(DevToolsShim.isInitialized(), "DevTools are initialized");

  DevToolsShim.saveDevToolsSession({});
  checkCalls(mock, "saveDevToolsSession", 1, []);

  restoreDevToolsInstalled();
}

function run_test() {
  test_register_unregister();
  DevToolsShim.unregister();

  test_on_is_forwarded_to_devtools();
  DevToolsShim.unregister();

  test_off_called_before_registering_devtools();
  DevToolsShim.unregister();

  test_off_called_before_with_bad_callback();
  DevToolsShim.unregister();

  test_registering_tool();
  DevToolsShim.unregister();

  test_registering_theme();
  DevToolsShim.unregister();

  test_scratchpad_apis();
  DevToolsShim.unregister();

  test_events();
}
