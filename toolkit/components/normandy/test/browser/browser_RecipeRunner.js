"use strict";

ChromeUtils.import("resource://testing-common/TestUtils.jsm", this);
ChromeUtils.import(
  "resource://gre/modules/components-utils/FilterExpressions.jsm",
  this
);
ChromeUtils.import("resource://normandy/lib/RecipeRunner.jsm", this);
ChromeUtils.import("resource://normandy/lib/ClientEnvironment.jsm", this);
ChromeUtils.import("resource://normandy/lib/CleanupManager.jsm", this);
ChromeUtils.import("resource://normandy/lib/NormandyApi.jsm", this);
ChromeUtils.import("resource://normandy/lib/ActionsManager.jsm", this);
ChromeUtils.import("resource://normandy/lib/AddonStudies.jsm", this);
ChromeUtils.import("resource://normandy/lib/Uptake.jsm", this);
ChromeUtils.import(
  "resource://gre/modules/components-utils/FilterExpressions.jsm",
  this
);

const { RemoteSettings } = ChromeUtils.import(
  "resource://services-settings/remote-settings.js"
);

add_task(async function getFilterContext() {
  const recipe = { id: 17, arguments: { foo: "bar" }, unrelated: false };
  const context = RecipeRunner.getFilterContext(recipe);

  // Test for expected properties in the filter expression context.
  const expectedNormandyKeys = [
    "channel",
    "country",
    "distribution",
    "doNotTrack",
    "isDefaultBrowser",
    "locale",
    "plugins",
    "recipe",
    "request_time",
    "searchEngine",
    "syncDesktopDevices",
    "syncMobileDevices",
    "syncSetup",
    "syncTotalDevices",
    "telemetry",
    "userId",
    "version",
  ];
  for (const key of expectedNormandyKeys) {
    ok(key in context.env, `env.${key} is available`);
    ok(key in context.normandy, `normandy.${key} is available`);
  }
  Assert.deepEqual(
    context.normandy,
    context.env,
    "context offers normandy as backwards-compatible alias for context.environment"
  );

  is(
    context.env.recipe.id,
    recipe.id,
    "environment.recipe is the recipe passed to getFilterContext"
  );
  is(
    ClientEnvironment.recipe,
    undefined,
    "ClientEnvironment has not been mutated"
  );
  delete recipe.unrelated;
  Assert.deepEqual(
    context.env.recipe,
    recipe,
    "environment.recipe drops unrecognized attributes from the recipe"
  );

  // Filter context attributes are cached.
  await SpecialPowers.pushPrefEnv({
    set: [["app.normandy.user_id", "some id"]],
  });
  is(context.env.userId, "some id", "User id is read from prefs when accessed");
  await SpecialPowers.pushPrefEnv({
    set: [["app.normandy.user_id", "real id"]],
  });
  is(context.env.userId, "some id", "userId was cached");
});

add_task(async function test_shouldRunRecipe_filterExpressions() {
  const check = filter =>
    RecipeRunner.shouldRunRecipe({ filter_expression: filter });

  // Errors must result in a false return value.
  ok(
    !(await check("invalid ( + 5yntax")),
    "Invalid filter expressions return false"
  );

  // Non-boolean filter results result in a true return value.
  ok(await check("[1, 2, 3]"), "Non-boolean filter expressions return true");

  // The given recipe must be available to the filter context.
  const recipe = { filter_expression: "normandy.recipe.id == 7", id: 7 };
  ok(
    await RecipeRunner.shouldRunRecipe(recipe),
    "The recipe is available in the filter context"
  );
  recipe.id = 4;
  ok(
    !(await RecipeRunner.shouldRunRecipe(recipe)),
    "The recipe is available in the filter context"
  );
});

decorate_task(
  withStub(FilterExpressions, "eval"),
  withStub(Uptake, "reportRecipe"),
  async function test_shouldRunRecipe_canHandleExceptions(
    evalStub,
    reportRecipeStub
  ) {
    evalStub.throws("this filter was broken somehow");
    const someRecipe = {
      id: "1",
      action: "action",
      filter_expression: "broken",
    };
    const result = await RecipeRunner.shouldRunRecipe(someRecipe);

    Assert.deepEqual(result, false, "broken filters are treated as false");
    Assert.deepEqual(reportRecipeStub.args, [
      [someRecipe, Uptake.RECIPE_FILTER_BROKEN],
    ]);
  }
);

decorate_task(
  withSpy(FilterExpressions, "eval"),
  withStub(RecipeRunner, "getCapabilities"),
  async function test_shouldRunRecipe_checksCapabilities(
    evalSpy,
    getCapabilitiesStub
  ) {
    getCapabilitiesStub.returns(new Set(["test-capability"]));

    let result = await RecipeRunner.shouldRunRecipe({
      filter_expression: "true",
    });
    ok(result, "Recipes with no capabilities should pass");
    ok(evalSpy.called, "Filter should be evaluated");

    evalSpy.resetHistory();
    result = await RecipeRunner.shouldRunRecipe({
      capabilities: [],
      filter_expression: "true",
    });
    ok(result, "Recipes with empty capabilities should pass");
    ok(evalSpy.called, "Filter should be evaluated");

    evalSpy.resetHistory();
    result = await RecipeRunner.shouldRunRecipe({
      capabilities: ["test-capability"],
      filter_expression: "true",
    });
    ok(result, "Recipes with a matching capability should pass");
    ok(evalSpy.called, "Filter should be evaluated");

    evalSpy.resetHistory();
    result = await RecipeRunner.shouldRunRecipe({
      capabilities: ["impossible-capability"],
      filter_expression: "true",
    });
    ok(!result, "Recipes with non-matching capabilities should not pass");
    ok(!evalSpy.called, "Filter should not be evaluated");
  }
);

decorate_task(
  withMockNormandyApi,
  withStub(ClientEnvironment, "getClientClassification"),
  async function testClientClassificationCache(api, getStub) {
    getStub.returns(Promise.resolve(false));

    await SpecialPowers.pushPrefEnv({
      set: [["app.normandy.api_url", "https://example.com/selfsupport-dummy"]],
    });

    // When the experiment pref is false, eagerly call getClientClassification.
    await SpecialPowers.pushPrefEnv({
      set: [["app.normandy.experiments.lazy_classify", false]],
    });
    ok(!getStub.called, "getClientClassification hasn't been called");
    await RecipeRunner.run();
    ok(getStub.called, "getClientClassification was called eagerly");

    // When the experiment pref is true, do not eagerly call getClientClassification.
    await SpecialPowers.pushPrefEnv({
      set: [["app.normandy.experiments.lazy_classify", true]],
    });
    getStub.reset();
    ok(!getStub.called, "getClientClassification hasn't been called");
    await RecipeRunner.run();
    ok(!getStub.called, "getClientClassification was not called eagerly");
  }
);

decorate_task(
  withStub(Uptake, "reportRunner"),
  withStub(RecipeRunner, "loadRecipes"),
  withStub(ActionsManager.prototype, "finalize"),
  async function testRunEvents(
    reportRunnerStub,
    loadRecipesStub,
    finalizeStub
  ) {
    loadRecipesStub.returns(Promise.resolve([]));
    const startPromise = TestUtils.topicObserved("recipe-runner:start");
    const endPromise = TestUtils.topicObserved("recipe-runner:end");

    await RecipeRunner.run();

    // Will timeout if notifications were not received.
    await startPromise;
    await endPromise;
  }
);

decorate_task(
  withStub(RecipeRunner, "getCapabilities"),
  async function test_run_includesCapabilities(getCapabilitiesStub) {
    const rsCollection = await RecipeRunner._remoteSettingsClientForTesting.openCollection();
    await rsCollection.clear();
    const fakeSig = { signature: "abc" };
    await rsCollection.create(
      { id: "match", recipe: { id: 1 }, signature: fakeSig },
      { synced: true }
    );
    await rsCollection.db.saveLastModified(42);
    rsCollection.db.close();

    let capabilities = new Set(["test-capability"]);
    getCapabilitiesStub.returns(capabilities);
    await RecipeRunner.run();
    ok(getCapabilitiesStub.called, "getCapabilities should be called");
  }
);

decorate_task(
  withStub(NormandyApi, "verifyObjectSignature"),
  withStub(ActionsManager.prototype, "runRecipe"),
  withStub(ActionsManager.prototype, "finalize"),
  withStub(Uptake, "reportRecipe"),
  async function testReadFromRemoteSettings(
    verifyObjectSignatureStub,
    runRecipeStub,
    finalizeStub,
    reportRecipeStub
  ) {
    const matchRecipe = {
      name: "match",
      action: "matchAction",
      filter_expression: "true",
    };
    const noMatchRecipe = {
      name: "noMatch",
      action: "noMatchAction",
      filter_expression: "false",
    };
    const missingRecipe = {
      name: "missing",
      action: "missingAction",
      filter_expression: "true",
    };

    const rsCollection = await RecipeRunner._remoteSettingsClientForTesting.openCollection();
    await rsCollection.clear();
    const fakeSig = { signature: "abc" };
    await rsCollection.create(
      { id: "match", recipe: matchRecipe, signature: fakeSig },
      { synced: true }
    );
    await rsCollection.create(
      { id: "noMatch", recipe: noMatchRecipe, signature: fakeSig },
      { synced: true }
    );
    await rsCollection.create(
      { id: "missing", recipe: missingRecipe, signature: fakeSig },
      { synced: true }
    );
    await rsCollection.db.saveLastModified(42);
    rsCollection.db.close();

    await RecipeRunner.run();

    Assert.deepEqual(
      verifyObjectSignatureStub.args,
      [[matchRecipe, fakeSig, "recipe"], [missingRecipe, fakeSig, "recipe"]],
      "recipes with matching should have their signature verified"
    );
    Assert.deepEqual(
      runRecipeStub.args,
      [[matchRecipe], [missingRecipe]],
      "recipes with matching filters should be executed"
    );
    Assert.deepEqual(
      reportRecipeStub.args,
      [[noMatchRecipe, Uptake.RECIPE_DIDNT_MATCH_FILTER]],
      "Filtered-out recipes should be reported"
    );
  }
);

decorate_task(
  withStub(NormandyApi, "verifyObjectSignature"),
  withStub(ActionsManager.prototype, "runRecipe"),
  withStub(RecipeRunner, "getCapabilities"),
  async function testReadFromRemoteSettings(
    verifyObjectSignatureStub,
    runRecipeStub,
    getCapabilitiesStub
  ) {
    getCapabilitiesStub.returns(new Set(["compatible"]));
    const compatibleRecipe = {
      name: "match",
      filter_expression: "true",
      capabilities: ["compatible"],
    };
    const incompatibleRecipe = {
      name: "noMatch",
      filter_expression: "true",
      capabilities: ["incompatible"],
    };

    const rsCollection = await RecipeRunner._remoteSettingsClientForTesting.openCollection();
    await rsCollection.clear();
    const fakeSig = { signature: "abc" };
    await rsCollection.create(
      { id: "match", recipe: compatibleRecipe, signature: fakeSig },
      { synced: true }
    );
    await rsCollection.create(
      { id: "noMatch", recipe: incompatibleRecipe, signature: fakeSig },
      { synced: true }
    );
    await rsCollection.db.saveLastModified(42);
    rsCollection.db.close();

    await RecipeRunner.run();

    Assert.deepEqual(
      runRecipeStub.args,
      [[compatibleRecipe]],
      "only recipes with compatible capabilities should be executed"
    );
  }
);

decorate_task(
  withStub(ActionsManager.prototype, "runRecipe"),
  withStub(NormandyApi, "get"),
  withStub(Uptake, "reportRunner"),
  async function testBadSignatureFromRemoteSettings(
    runRecipeStub,
    normandyGetStub,
    reportRunnerStub
  ) {
    normandyGetStub.resolves({
      async text() {
        return "---CERT x5u----";
      },
    });

    const matchRecipe = {
      name: "badSig",
      action: "matchAction",
      filter_expression: "true",
    };

    const rsCollection = await RecipeRunner._remoteSettingsClientForTesting.openCollection();
    await rsCollection.clear();
    const badSig = { x5u: "http://localhost/x5u", signature: "abc" };
    await rsCollection.create(
      { id: "badSig", recipe: matchRecipe, signature: badSig },
      { synced: true }
    );
    await rsCollection.db.saveLastModified(42);
    rsCollection.db.close();

    await RecipeRunner.run();

    ok(!runRecipeStub.called, "no recipe is executed");
    Assert.deepEqual(
      reportRunnerStub.args,
      [[Uptake.RUNNER_INVALID_SIGNATURE]],
      "RecipeRunner should report uptake telemetry"
    );
  }
);

// Test init() during normal operation
decorate_task(
  withPrefEnv({
    set: [
      ["datareporting.healthreport.uploadEnabled", true], // telemetry enabled
      ["app.normandy.dev_mode", false],
      ["app.normandy.first_run", false],
    ],
  }),
  withStub(RecipeRunner, "run"),
  withStub(RecipeRunner, "registerTimer"),
  async function testInit(runStub, registerTimerStub) {
    await RecipeRunner.init();
    ok(
      !runStub.called,
      "RecipeRunner.run is called immediately when not in dev mode or first run"
    );
    ok(registerTimerStub.called, "RecipeRunner.init registers a timer");
  }
);

// test init() in dev mode
decorate_task(
  withPrefEnv({
    set: [
      ["datareporting.healthreport.uploadEnabled", true], // telemetry enabled
      ["app.normandy.dev_mode", true],
    ],
  }),
  withStub(RecipeRunner, "run"),
  withStub(RecipeRunner, "registerTimer"),
  withStub(RecipeRunner._remoteSettingsClientForTesting, "sync"),
  async function testInitDevMode(runStub, registerTimerStub, syncStub) {
    await RecipeRunner.init();
    Assert.deepEqual(
      runStub.args,
      [[{ trigger: "devMode" }]],
      "RecipeRunner.run should be called immediately when in dev mode"
    );
    ok(registerTimerStub.called, "RecipeRunner.init should register a timer");
    ok(
      syncStub.called,
      "RecipeRunner.init should sync remote settings in dev mode"
    );
  }
);

// Test init() first run
decorate_task(
  withPrefEnv({
    set: [
      ["datareporting.healthreport.uploadEnabled", true], // telemetry enabled
      ["app.normandy.dev_mode", false],
      ["app.normandy.first_run", true],
    ],
  }),
  withStub(RecipeRunner, "run"),
  withStub(RecipeRunner, "registerTimer"),
  withStub(RecipeRunner, "watchPrefs"),
  async function testInitFirstRun(runStub, registerTimerStub, watchPrefsStub) {
    await RecipeRunner.init();
    Assert.deepEqual(
      runStub.args,
      [[{ trigger: "firstRun" }]],
      "RecipeRunner.run is called immediately on first run"
    );
    ok(
      !Services.prefs.getBoolPref("app.normandy.first_run"),
      "On first run, the first run pref is set to false"
    );
    ok(
      registerTimerStub.called,
      "RecipeRunner.registerTimer registers a timer"
    );

    // RecipeRunner.init() sets this pref to false, but SpecialPowers
    // relies on the preferences it manages to actually change when it
    // tries to change them. Settings this back to true here allows
    // that to happen. Not doing this causes popPrefEnv to hang forever.
    Services.prefs.setBoolPref("app.normandy.first_run", true);
  }
);

// Test that new build IDs trigger immediate recipe runs
decorate_task(
  withPrefEnv({
    set: [
      ["datareporting.healthreport.uploadEnabled", true], // telemetry enabled
      ["app.normandy.last_seen_buildid", "not-the-current-buildid"],
    ],
  }),
  withStub(RecipeRunner, "run"),
  withStub(RecipeRunner, "registerTimer"),
  withStub(RecipeRunner, "watchPrefs"),
  async function testInitFirstRun(runStub, registerTimerStub, watchPrefsStub) {
    await RecipeRunner.init();
    Assert.deepEqual(
      runStub.args,
      [[{ trigger: "newBuildID" }]],
      "RecipeRunner.run is called immediately on a new build ID"
    );
    ok(
      registerTimerStub.called,
      "RecipeRunner.registerTimer registers a timer"
    );
  }
);

// Test that prefs are watched correctly
decorate_task(
  withPrefEnv({
    set: [
      ["app.normandy.dev_mode", false],
      ["app.normandy.first_run", false],
      ["app.normandy.enabled", true],
    ],
  }),
  withStub(RecipeRunner, "run"),
  withStub(RecipeRunner, "enable"),
  withStub(RecipeRunner, "disable"),
  withStub(CleanupManager, "addCleanupHandler"),

  async function testPrefWatching(
    runStub,
    enableStub,
    disableStub,
    addCleanupHandlerStub
  ) {
    await RecipeRunner.init();
    is(enableStub.callCount, 1, "Enable should be called initially");
    is(disableStub.callCount, 0, "Disable should not be called initially");

    await SpecialPowers.pushPrefEnv({ set: [["app.normandy.enabled", false]] });
    is(enableStub.callCount, 1, "Enable should not be called again");
    is(
      disableStub.callCount,
      1,
      "RecipeRunner should disable when Shield is disabled"
    );

    await SpecialPowers.pushPrefEnv({ set: [["app.normandy.enabled", true]] });
    is(
      enableStub.callCount,
      2,
      "RecipeRunner should re-enable when Shield is enabled"
    );
    is(disableStub.callCount, 1, "Disable should not be called again");

    await SpecialPowers.pushPrefEnv({
      set: [["app.normandy.api_url", "http://example.com"]],
    }); // does not start with https://
    is(enableStub.callCount, 2, "Enable should not be called again");
    is(
      disableStub.callCount,
      2,
      "RecipeRunner should disable when an invalid api url is given"
    );

    await SpecialPowers.pushPrefEnv({
      set: [["app.normandy.api_url", "https://example.com"]],
    }); // ends with https://
    is(
      enableStub.callCount,
      3,
      "RecipeRunner should re-enable when a valid api url is given"
    );
    is(disableStub.callCount, 2, "Disable should not be called again");

    is(
      runStub.callCount,
      0,
      "RecipeRunner.run should not be called during this test"
    );
  }
);

// Test that enable and disable are idempotent
decorate_task(
  withStub(RecipeRunner, "registerTimer"),
  withStub(RecipeRunner, "unregisterTimer"),

  async function testPrefWatching(registerTimerStub, unregisterTimerStub) {
    const originalEnabled = RecipeRunner.enabled;

    try {
      RecipeRunner.enabled = false;
      RecipeRunner.enable();
      RecipeRunner.enable();
      RecipeRunner.enable();
      is(registerTimerStub.callCount, 1, "Enable should be idempotent");

      RecipeRunner.enabled = true;
      RecipeRunner.disable();
      RecipeRunner.disable();
      RecipeRunner.disable();
      is(registerTimerStub.callCount, 1, "Disable should be idempotent");
    } finally {
      RecipeRunner.enabled = originalEnabled;
    }
  }
);

decorate_task(
  withPrefEnv({
    set: [["app.normandy.onsync_skew_sec", 0]],
  }),
  withStub(RecipeRunner, "run"),
  async function testRunOnSyncRemoteSettings(runStub) {
    const rsClient = RecipeRunner._remoteSettingsClientForTesting;
    await RecipeRunner.init();
    ok(
      RecipeRunner._alreadySetUpRemoteSettings,
      "remote settings should be set up in the runner"
    );

    // Runner disabled
    RecipeRunner.disable();
    await rsClient.emit("sync", {});
    ok(!runStub.called, "run() should not be called if disabled");
    runStub.reset();

    // Runner enabled
    RecipeRunner.enable();
    await rsClient.emit("sync", {});
    ok(runStub.called, "run() should be called if enabled");
    runStub.reset();

    // Runner disabled
    RecipeRunner.disable();
    await rsClient.emit("sync", {});
    ok(!runStub.called, "run() should not be called if disabled");
    runStub.reset();

    // Runner re-enabled
    RecipeRunner.enable();
    await rsClient.emit("sync", {});
    ok(runStub.called, "run() should be called if runner is re-enabled");
  }
);

decorate_task(
  withPrefEnv({
    set: [
      ["app.normandy.onsync_skew_sec", 600], // 10 minutes, much longer than the test will take to run
    ],
  }),
  withStub(RecipeRunner, "run"),
  async function testOnSyncRunDelayed(runStub) {
    ok(
      !RecipeRunner._syncSkewTimeout,
      "precondition: No timer should be active"
    );
    const rsClient = RecipeRunner._remoteSettingsClientForTesting;
    await rsClient.emit("sync", {});
    ok(runStub.notCalled, "run() should be not called yet");
    ok(RecipeRunner._syncSkewTimeout, "A timer should be set");
    clearInterval(RecipeRunner._syncSkewTimeout); // cleanup
  }
);

decorate_task(
  withStub(RecipeRunner, "loadRecipes"),
  async function testRunCanRunOnlyOnce(loadRecipesStub) {
    loadRecipesStub.returns(
      // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
      new Promise(resolve => setTimeout(() => resolve([]), 10))
    );

    // // Run 2 in parallel.
    await Promise.all([RecipeRunner.run(), RecipeRunner.run()]);

    is(loadRecipesStub.callCount, 1, "run() is no-op if already running");
  }
);

decorate_task(
  withPrefEnv({
    set: [
      // Enable update timer logs.
      ["app.update.log", true],
      ["app.normandy.onsync_skew_sec", 0],
    ],
  }),
  withStub(RecipeRunner, "loadRecipes"),
  withStub(ActionsManager.prototype, "finalize"),
  withStub(Uptake, "reportRunner"),
  async function testSyncDelaysTimer(
    loadRecipesStub,
    finalizeStub,
    reportRecipeStub
  ) {
    loadRecipesStub.returns(Promise.resolve([]));

    // Mark any existing timer as having run just now.
    for (const { value } of Services.catMan.enumerateCategory("update-timer")) {
      const timerID = value.split(",")[2];
      console.log(`Mark timer ${timerID} as ran recently`);
      // See https://searchfox.org/mozilla-central/rev/11cfa0462/toolkit/components/timermanager/UpdateTimerManager.jsm#8
      const timerLastUpdatePref = `app.update.lastUpdateTime.${timerID}`;
      const lastUpdateTime = Math.round(Date.now() / 1000);
      Services.prefs.setIntPref(timerLastUpdatePref, lastUpdateTime);
    }

    // Set a timer interval as small as possible so that the UpdateTimerManager
    // will pick the recipe runner as the most imminent timer to run on `notify()`.
    Services.prefs.setIntPref("app.normandy.run_interval_seconds", 1);
    // This will refresh the timer interval.
    RecipeRunner.unregisterTimer();
    RecipeRunner.registerTimer();

    is(loadRecipesStub.callCount, 0, "run() shouldn't have run yet");

    // Simulate timer notification.
    const service = Cc["@mozilla.org/updates/timer-manager;1"].getService(
      Ci.nsITimerCallback
    );
    const newTimer = () => {
      const t = Cc["@mozilla.org/timer;1"].createInstance(Ci.nsITimer);
      t.initWithCallback(() => {}, 10, Ci.nsITimer.TYPE_ONE_SHOT);
      return t;
    };
    // Run timer once, to make sure this test works as expected.
    const startTime = Date.now();
    const endPromise = TestUtils.topicObserved("recipe-runner:end");
    service.notify(newTimer());
    await endPromise; // will timeout if run() not called.
    const timerLatency = Date.now() - startTime;

    is(loadRecipesStub.callCount, 1, "run() should be called from timer");

    // Run once from sync event.
    const rsClient = RecipeRunner._remoteSettingsClientForTesting;
    await rsClient.emit("sync", {}); // waits for listeners to run.

    is(loadRecipesStub.callCount, 2, "run() should be called from sync");

    // Run timer again.
    service.notify(newTimer());
    // Wait at least as long as the latency we had above. Ten times as a margin.
    // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
    await new Promise(resolve => setTimeout(resolve, timerLatency * 10));

    is(loadRecipesStub.callCount, 2, "run() does not run again from timer");
  }
);

// Test that the capabilities for context variables are generated correctly.
decorate_task(async function testAutomaticCapabilities() {
  const capabilities = await RecipeRunner.getCapabilities();

  ok(
    capabilities.has("jexl.context.env.country"),
    "context variables from Normandy's client context should be included"
  );
  ok(
    capabilities.has("jexl.context.env.version"),
    "context variables from the superclass context should be included"
  );
  ok(
    !capabilities.has("jexl.context.env.getClientClassification"),
    "non-getter functions should not be included"
  );
  ok(
    !capabilities.has("jexl.context.env.prototype"),
    "built-in, non-enumerable properties should not be included"
  );
});
