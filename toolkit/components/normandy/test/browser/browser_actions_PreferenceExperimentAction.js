/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */
"use strict";

ChromeUtils.import("resource://gre/modules/components-utils/Sampling.jsm", this);
ChromeUtils.import("resource://gre/modules/Services.jsm", this);
ChromeUtils.import("resource://gre/modules/Preferences.jsm", this);
ChromeUtils.import("resource://gre/modules/TelemetryEnvironment.jsm", this);
ChromeUtils.import("resource://normandy/lib/ClientEnvironment.jsm", this);
ChromeUtils.import("resource://normandy/lib/PreferenceExperiments.jsm", this);
ChromeUtils.import("resource://normandy/lib/TelemetryEvents.jsm", this);
ChromeUtils.import("resource://normandy/lib/Uptake.jsm", this);
ChromeUtils.import("resource://normandy/actions/PreferenceExperimentAction.jsm", this);

function argumentsFactory(args) {
  return {
    slug: "test",
    preferenceName: "fake.preference",
    preferenceType: "string",
    preferenceBranchType: "default",
    branches: [
      { slug: "test", value: "foo", ratio: 1 },
    ],
    isHighPopulation: false,
    ...args,
  };
}

function preferenceExperimentFactory(args) {
  return recipeFactory({
    name: "preference-experiment",
    arguments: argumentsFactory(args),
  });
}

decorate_task(
  withStub(Uptake, "reportRecipe"),
  async function run_without_errors(reportRecipe) {
    const action = new PreferenceExperimentAction();
    const recipe = preferenceExperimentFactory();
    await action.runRecipe(recipe);
    await action.finalize();
    // runRecipe catches exceptions thrown by _run(), so
    // explicitly check for reported success here.
    Assert.deepEqual(reportRecipe.args,
                     [[recipe, Uptake.RECIPE_SUCCESS]]);
  }
);

decorate_task(
  withStub(Uptake, "reportRecipe"),
  withStub(Uptake, "reportAction"),
  withPrefEnv({set: [["app.shield.optoutstudies.enabled", false]]}),
  async function checks_disabled(reportRecipe, reportAction) {
    const action = new PreferenceExperimentAction();
    action.log = mockLogger();

    const recipe = preferenceExperimentFactory();
    await action.runRecipe(recipe);

    Assert.deepEqual(action.log.info.args,
                     [["User has opted out of preference experiments. Disabling this action."]]);
    Assert.deepEqual(action.log.warn.args,
                     [["Skipping recipe preference-experiment because PreferenceExperimentAction " +
                       "was disabled during preExecution."]]);

    await action.finalize();
    Assert.deepEqual(action.log.debug.args,
                     [["Skipping post-execution hook for PreferenceExperimentAction because it is disabled."]]);
    Assert.deepEqual(reportRecipe.args,
                     [[recipe, Uptake.RECIPE_ACTION_DISABLED]]);
    Assert.deepEqual(reportAction.args,
                     [[action.name, Uptake.ACTION_SUCCESS]]);
  }
);

decorate_task(
  withStub(PreferenceExperiments, "start"),
  PreferenceExperiments.withMockExperiments([]),
  async function enroll_user_if_never_been_in_experiment(startStub) {
    const action = new PreferenceExperimentAction();
    const recipe = preferenceExperimentFactory({
      slug: "test",
      preferenceName: "fake.preference",
      preferenceBranchType: "user",
      branches: [
        { slug: "branch1", value: "branch1", ratio: 1 },
        { slug: "branch2", value: "branch2", ratio: 1 },
      ],
    });
    sinon.stub(action, "chooseBranch").callsFake(async function(slug, branches) {
      return branches[0];
    });

    await action.runRecipe(recipe);
    await action.finalize();

    Assert.deepEqual(startStub.args, [[{
      name: "test",
      branch: "branch1",
      preferenceName: "fake.preference",
      preferenceValue: "branch1",
      preferenceBranchType: "user",
      preferenceType: "string",
      experimentType: "exp",
    }]]);
  }
);

decorate_task(
  withStub(PreferenceExperiments, "markLastSeen"),
  PreferenceExperiments.withMockExperiments([{name: "test", expired: false}]),
  async function markSeen_if_experiment_active(markLastSeenStub) {
    const action = new PreferenceExperimentAction();
    const recipe = preferenceExperimentFactory({
      slug: "test",
    });

    await action.runRecipe(recipe);
    await action.finalize();

    Assert.deepEqual(markLastSeenStub.args, [["test"]]);
  }
);

decorate_task(
  withStub(PreferenceExperiments, "markLastSeen"),
  PreferenceExperiments.withMockExperiments([{name: "test", expired: true}]),
  async function dont_markSeen_if_experiment_expired(markLastSeenStub) {
    const action = new PreferenceExperimentAction();
    const recipe = preferenceExperimentFactory({
      slug: "test",
    });

    await action.runRecipe(recipe);
    await action.finalize();

    Assert.deepEqual(markLastSeenStub.args, [], "markLastSeen was not called");
  }
);

decorate_task(
  withStub(PreferenceExperiments, "start"),
  async function do_nothing_if_enrollment_paused(startStub) {
    const action = new PreferenceExperimentAction();
    const recipe = preferenceExperimentFactory({
      isEnrollmentPaused: true,
    });

    await action.runRecipe(recipe);
    await action.finalize();

    Assert.deepEqual(startStub.args, [], "start was not called");
  }
);

decorate_task(
  withStub(PreferenceExperiments, "stop"),
  PreferenceExperiments.withMockExperiments([
    {name: "seen", expired: false},
    {name: "unseen", expired: false},
  ]),
  async function stop_experiments_not_seen(stopStub) {
    const action = new PreferenceExperimentAction();
    const recipe = preferenceExperimentFactory({
      slug: "seen",
    });

    await action.runRecipe(recipe);
    await action.finalize();

    Assert.deepEqual(stopStub.args,
                     [["unseen", {resetValue: true, reason: "recipe-not-seen"}]]);
  }
);

decorate_task(
  withStub(PreferenceExperiments, "start"),
  withStub(Uptake, "reportRecipe"),
  PreferenceExperiments.withMockExperiments([
    {
      name: "conflict",
      preferenceName: "conflict.pref",
      expired: false,
    },
  ]),
  async function do_nothing_if_preference_is_already_being_tested(startStub, reportRecipeStub) {
    const action = new PreferenceExperimentAction();
    const recipe = preferenceExperimentFactory({
      slug: "new",
      preferenceName: "conflict.pref",
    });
    action.chooseBranch = sinon.stub().callsFake(async function(slug, branches) {
      return branches[0];
    });

    await action.runRecipe(recipe);
    await action.finalize();

    Assert.deepEqual(reportRecipeStub.args,
                     [[recipe, Uptake.RECIPE_EXECUTION_ERROR]]);
    Assert.deepEqual(startStub.args, [], "start not called");
    // No way to get access to log message/Error thrown
  }
);

decorate_task(
  withStub(PreferenceExperiments, "start"),
  PreferenceExperiments.withMockExperiments([]),
  async function experimentType_with_isHighPopulation_false(startStub) {
    const action = new PreferenceExperimentAction();
    const recipe = preferenceExperimentFactory({
      isHighPopulation: false,
    });

    await action.runRecipe(recipe);
    await action.finalize();

    Assert.deepEqual(startStub.args[0][0].experimentType, "exp");
  }
);

decorate_task(
  withStub(PreferenceExperiments, "start"),
  PreferenceExperiments.withMockExperiments([]),
  async function experimentType_with_isHighPopulation_true(startStub) {
    const action = new PreferenceExperimentAction();
    const recipe = preferenceExperimentFactory({
      isHighPopulation: true,
    });

    await action.runRecipe(recipe);
    await action.finalize();

    Assert.deepEqual(startStub.args[0][0].experimentType, "exp-highpop");
  }
);

decorate_task(
  withStub(Sampling, "ratioSample"),
  async function chooseBranch_uses_ratioSample(ratioSampleStub) {
    ratioSampleStub.returns(Promise.resolve(1));
    const action = new PreferenceExperimentAction();
    const branches = [
      { value: "branch0", ratio: 1 },
      { value: "branch1", ratio: 2 },
    ];
    const sandbox = sinon.createSandbox();
    let result;
    try {
      sandbox.stub(ClientEnvironment, "userId").get(() => "fake-id");
      result = await action.chooseBranch("exp-slug", branches);
    } finally {
      sandbox.restore();
    }

    Assert.deepEqual(ratioSampleStub.args,
                     [["fake-id-exp-slug-branch", [1, 2]]]);
    Assert.deepEqual(result, branches[1]);
  }
);
