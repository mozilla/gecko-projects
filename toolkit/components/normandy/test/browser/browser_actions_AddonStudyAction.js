"use strict";

ChromeUtils.import("resource://gre/modules/Services.jsm", this);
ChromeUtils.import("resource://testing-common/TestUtils.jsm", this);
ChromeUtils.import("resource://normandy/actions/AddonStudyAction.jsm", this);
ChromeUtils.import("resource://normandy/lib/AddonStudies.jsm", this);
ChromeUtils.import("resource://normandy/lib/Uptake.jsm", this);

const FIXTURE_ADDON_ID = "normandydriver@example.com";
const FIXTURE_ADDON_BASE_URL = "http://example.com/browser/toolkit/components/normandy/test/browser/addons/";

const FIXTURE_ADDONS = [
  "normandydriver-1.0",
  "normandydriver-2.0",
];

// Generate fixture add-on details
const FIXTURE_ADDON_DETAILS = {};
FIXTURE_ADDONS.forEach(addon => {
  const filename = `${addon}.xpi`;
  const dir = getChromeDir(getResolvedURI(gTestPath));
  dir.append("addons");
  dir.append(filename);
  const xpiFile = Services.io.newFileURI(dir).QueryInterface(Ci.nsIFileURL).file;

  FIXTURE_ADDON_DETAILS[addon] = {
    url: `${FIXTURE_ADDON_BASE_URL}${filename}`,
    hash: CryptoUtils.getFileHash(xpiFile, "sha256"),
  };
});

function addonStudyRecipeFactory(overrides = {}) {
  let args = {
    name: "Fake name",
    description: "fake description",
    addonUrl: "https://example.com/study.xpi",
    extensionApiId: 1,
  };
  if (Object.hasOwnProperty.call(overrides, "arguments")) {
    args = Object.assign(args, overrides.arguments);
    delete overrides.arguments;
  }
  return recipeFactory(Object.assign({ action: "addon-study", arguments: args }, overrides));
}

function extensionDetailsFactory(overrides = {}) {
  return Object.assign({
    id: 1,
    name: "Normandy Fixture",
    xpi: FIXTURE_ADDON_DETAILS["normandydriver-1.0"].url,
    extension_id: FIXTURE_ADDON_ID,
    version: "1.0",
    hash: FIXTURE_ADDON_DETAILS["normandydriver-1.0"].hash,
    hash_algorithm: "sha256",
  }, overrides);
}

/**
 * Utility function to uninstall addons safely. Preventing the issue mentioned
 * in bug 1485569.
 *
 * addon.uninstall is async, but it also triggers the AddonStudies onUninstall
 * listener, which is not awaited. Wrap it here and trigger a promise once it's
 * done so we can wait until AddonStudies cleanup is finished.
 */
async function safeUninstallAddon(addon) {
  const activeStudies = (await AddonStudies.getAll()).filter(study => study.active);
  const matchingStudy = activeStudies.find(study => study.addonId === addon.id);

  let studyEndedPromise;
  if (matchingStudy) {
    studyEndedPromise = TestUtils.topicObserved("shield-study-ended", (subject, message) => {
      return message === `${matchingStudy.recipeId}`;
    });
  }

  const addonUninstallPromise = addon.uninstall();

  return Promise.all([studyEndedPromise, addonUninstallPromise]);
}

/**
 * Test decorator that is a modified version of the withInstalledWebExtension
 * decorator that safely uninstalls the created addon.
 */
function withInstalledWebExtensionSafe(manifestOverrides = {}) {
  return testFunction => {
    return async function wrappedTestFunction(...args) {
      const decorated = withInstalledWebExtension(manifestOverrides, true)(async ([id, file]) => {
        try {
          await testFunction(...args, [id, file]);
        } finally {
          let addon = await AddonManager.getAddonByID(id);
          if (addon) {
            await safeUninstallAddon(addon);
            addon = await AddonManager.getAddonByID(id);
            ok(!addon, "add-on should be uninstalled");
          }
        }
      });
      await decorated();
    };
  };
}

/**
 * Test decorator to provide a web extension installed from a URL.
 */
function withInstalledWebExtensionFromURL(url) {
  return function wrapper(testFunction) {
    return async function wrappedTestFunction(...args) {
      let startupPromise;
      let addonId;

      const install = await AddonManager.getInstallForURL(url);
      const listener = {
        onInstallStarted(cbInstall) {
          addonId = cbInstall.addon.id;
          startupPromise = AddonTestUtils.promiseWebExtensionStartup(addonId);
        },
      };
      install.addListener(listener);

      await install.install();
      await startupPromise;

      try {
        await testFunction(...args, [addonId, url]);
      } finally {
        const addonToUninstall = await AddonManager.getAddonByID(addonId);
        await safeUninstallAddon(addonToUninstall);
      }
    };
  };
}

/**
 * Test decorator that checks that the test cleans up all add-ons installed
 * during the test. Likely needs to be the first decorator used.
 */
function ensureAddonCleanup(testFunction) {
  return async function wrappedTestFunction(...args) {
    const beforeAddons = new Set(await AddonManager.getAllAddons());

    try {
      await testFunction(...args);
    } finally {
      const afterAddons = new Set(await AddonManager.getAllAddons());
      Assert.deepEqual(beforeAddons, afterAddons, "The add-ons should be same before and after the test");
    }
  };
}

// Test that enroll is not called if recipe is already enrolled and update does nothing
// if recipe is unchanged
decorate_task(
  ensureAddonCleanup,
  withMockNormandyApi,
  AddonStudies.withStudies([addonStudyFactory()]),
  withSendEventStub,
  withInstalledWebExtensionSafe({id: FIXTURE_ADDON_ID, version: "1.0"}),
  async function enrollTwiceFail(mockApi, [study], sendEventStub, installedAddon) {
    const recipe = recipeFactory({
      id: study.recipeId,
      type: "addon-study",
      arguments: {
        name: study.name,
        description: study.description,
        addonUrl: study.addonUrl,
        extensionApiId: study.extensionApiId,
      },
    });
    mockApi.extensionDetails = {
      [recipe.arguments.extensionApiId]: extensionDetailsFactory({
        id: recipe.arguments.extensionApiId,
        extension_id: study.addonId,
        hash: study.extensionHash,
      }),
    };
    const action = new AddonStudyAction();
    const enrollSpy = sinon.spy(action, "enroll");
    const updateSpy = sinon.spy(action, "update");
    await action.runRecipe(recipe);
    is(action.lastError, null, "lastError should be null");
    ok(!enrollSpy.called, "enroll should not be called");
    ok(updateSpy.called, "update should be called");
    sendEventStub.assertEvents([]);
  },
);

// Test that if the add-on fails to install, the database is cleaned up and the
// error is correctly reported.
decorate_task(
  ensureAddonCleanup,
  withMockNormandyApi,
  withSendEventStub,
  AddonStudies.withStudies(),
  async function enrollDownloadFail(mockApi, sendEventStub) {
    const recipe = addonStudyRecipeFactory({
      arguments: {
        addonUrl: "https://example.com/404.xpi",
        extensionApiId: 404,
      },
    });
    mockApi.extensionDetails = {
      [recipe.arguments.extensionApiId]: extensionDetailsFactory({
        id: recipe.arguments.extensionApiId,
        xpi: recipe.arguments.addonUrl,
      }),
    };
    const action = new AddonStudyAction();
    await action.runRecipe(recipe);
    is(action.lastError, null, "lastError should be null");

    const studies = await AddonStudies.getAll();
    Assert.deepEqual(studies, [], "the study should not be in the database");

    sendEventStub.assertEvents(
      [["enrollFailed", "addon_study", recipe.arguments.name, {reason: "download-failure", detail: "ERROR_NETWORK_FAILURE"}]]
    );
  }
);

// Ensure that the database is clean and error correctly reported if hash check fails
decorate_task(
  ensureAddonCleanup,
  withMockNormandyApi,
  withSendEventStub,
  AddonStudies.withStudies(),
  async function enrollHashCheckFails(mockApi, sendEventStub) {
    const recipe = addonStudyRecipeFactory({
      arguments: {
        extensionApiId: 1,
        addonUrl: FIXTURE_ADDON_DETAILS["normandydriver-1.0"].url,
      },
    });
    mockApi.extensionDetails = {
      [recipe.arguments.extensionApiId]: extensionDetailsFactory({
        id: recipe.arguments.extensionApiId,
        hash: "badhash",
      }),
    };
    const action = new AddonStudyAction();
    await action.runRecipe(recipe);
    is(action.lastError, null, "lastError should be null");

    const studies = await AddonStudies.getAll();
    Assert.deepEqual(studies, [], "the study should not be in the database");

    sendEventStub.assertEvents(
      [["enrollFailed", "addon_study", recipe.arguments.name, {
        reason: "download-failure",
        detail: "ERROR_INCORRECT_HASH",
      }]],
    );
  }
);

// Ensure that the database is clean and error correctly reported if there is a metadata mismatch
decorate_task(
  ensureAddonCleanup,
  withMockNormandyApi,
  withSendEventStub,
  AddonStudies.withStudies(),
  async function enrollFailsMetadataMismatch(mockApi, sendEventStub) {
    const recipe = addonStudyRecipeFactory({
      arguments: {
        extensionApiId: 1,
        addonUrl: FIXTURE_ADDON_DETAILS["normandydriver-1.0"].url,
      },
    });
    mockApi.extensionDetails = {
      [recipe.arguments.extensionApiId]: extensionDetailsFactory({
        id: recipe.arguments.extensionApiId,
        xpi: recipe.arguments.addonUrl,
        version: "1.5",
        hash: FIXTURE_ADDON_DETAILS["normandydriver-1.0"].hash,
      }),
    };
    const action = new AddonStudyAction();
    await action.runRecipe(recipe);
    is(action.lastError, null, "lastError should be null");

    const studies = await AddonStudies.getAll();
    Assert.deepEqual(studies, [], "the study should not be in the database");

    sendEventStub.assertEvents(
      [["enrollFailed", "addon_study", recipe.arguments.name, {
        reason: "metadata-mismatch",
      }]],
    );
  }
);

// Test that in the case of a study add-on conflicting with a non-study add-on, the study does not enroll
decorate_task(
  ensureAddonCleanup,
  withMockNormandyApi,
  withSendEventStub,
  withInstalledWebExtensionSafe({ version: "0.1", id: FIXTURE_ADDON_ID }),
  AddonStudies.withStudies(),
  async function conflictingEnrollment(mockApi, sendEventStub, [installedAddonId, installedAddonFile]) {
    is(installedAddonId, FIXTURE_ADDON_ID, "Generated, installed add-on should have the same ID as the fixture");
    const addonUrl = FIXTURE_ADDON_DETAILS["normandydriver-1.0"].url;
    const recipe = addonStudyRecipeFactory({
      arguments: {
        name: "conflicting",
        extensionApiId: 1,
        addonUrl,
      },
    });
    mockApi.extensionDetails = {
      [recipe.arguments.extensionApiId]: extensionDetailsFactory({
        id: recipe.arguments.extensionApiId,
      }),
    };
    const action = new AddonStudyAction();
    await action.runRecipe(recipe);
    is(action.lastError, null, "lastError should be null");

    const addon = await AddonManager.getAddonByID(FIXTURE_ADDON_ID);
    is(addon.version, "0.1", "The installed add-on should not be replaced");

    Assert.deepEqual(await AddonStudies.getAll(), [], "There should be no enrolled studies");

    sendEventStub.assertEvents(
      [["enrollFailed", "addon_study", recipe.arguments.name, { reason: "conflicting-addon-id" }]]
    );
  },
);

// Test a successful enrollment
decorate_task(
  ensureAddonCleanup,
  withSendEventStub,
  withMockNormandyApi,
  AddonStudies.withStudies(),
  async function successfulEnroll(mockApi, sendEventStub, studies) {
    const webExtStartupPromise = AddonTestUtils.promiseWebExtensionStartup(FIXTURE_ADDON_ID);
    const addonUrl = FIXTURE_ADDON_DETAILS["normandydriver-1.0"].url;

    let addon = await AddonManager.getAddonByID(FIXTURE_ADDON_ID);
    is(addon, null, "Before enroll, the add-on is not installed");

    const recipe = addonStudyRecipeFactory({
      arguments: {
        name: "success",
        extensionApiId: 1,
        addonUrl,
      },
    });
    const extensionDetails = extensionDetailsFactory({
      id: recipe.arguments.extensionApiId,
    });
    mockApi.extensionDetails = {
      [recipe.arguments.extensionApiId]: extensionDetails,
    };
    const action = new AddonStudyAction();
    await action.runRecipe(recipe);
    is(action.lastError, null, "lastError should be null");

    await webExtStartupPromise;
    addon = await AddonManager.getAddonByID(FIXTURE_ADDON_ID);
    ok(addon, "After start is called, the add-on is installed");

    Assert.deepEqual(addon.installTelemetryInfo, {source: "internal"},
                     "Got the expected installTelemetryInfo");

    const study = await AddonStudies.get(recipe.id);
    Assert.deepEqual(
      study,
      {
        recipeId: recipe.id,
        name: recipe.arguments.name,
        description: recipe.arguments.description,
        addonId: FIXTURE_ADDON_ID,
        addonVersion: "1.0",
        addonUrl,
        active: true,
        studyStartDate: study.studyStartDate,
        extensionApiId: recipe.arguments.extensionApiId,
        extensionHash: extensionDetails.hash,
        extensionHashAlgorithm: extensionDetails.hash_algorithm,
      },
      "study data should be stored",
    );
    ok(study.studyStartDate, "a start date should be assigned");
    is(study.studyEndDate, null, "an end date should not be assigned");

    sendEventStub.assertEvents(
      [["enroll", "addon_study", recipe.arguments.name, { addonId: FIXTURE_ADDON_ID, addonVersion: "1.0" }]]
    );

    // cleanup
    await safeUninstallAddon(addon);
    Assert.deepEqual(AddonManager.getAllAddons(), [], "add-on should be uninstalled.");
  },
);

// Test a successful update
decorate_task(
  ensureAddonCleanup,
  withMockNormandyApi,
  AddonStudies.withStudies([addonStudyFactory({
    addonId: FIXTURE_ADDON_ID,
    extensionHash: FIXTURE_ADDON_DETAILS["normandydriver-1.0"].hash,
    extensionHashAlgorithm: "sha256",
    addonVersion: "1.0",
  })]),
  withSendEventStub,
  withInstalledWebExtensionSafe({id: FIXTURE_ADDON_ID, version: "1.0"}),
  async function successfulUpdate(mockApi, [study], sendEventStub, installedAddon) {
    const addonUrl = FIXTURE_ADDON_DETAILS["normandydriver-2.0"].url;
    const recipe = recipeFactory({
      id: study.recipeId,
      type: "addon-study",
      arguments: {
        name: study.name,
        description: study.description,
        extensionApiId: study.extensionApiId,
        addonUrl,
      },
    });
    const hash = FIXTURE_ADDON_DETAILS["normandydriver-2.0"].hash;
    mockApi.extensionDetails = {
      [recipe.arguments.extensionApiId]: extensionDetailsFactory({
        id: recipe.arguments.extensionApiId,
        extension_id: study.addonId,
        xpi: addonUrl,
        hash,
        version: "2.0",
      }),
    };
    const action = new AddonStudyAction();
    const enrollSpy = sinon.spy(action, "enroll");
    const updateSpy = sinon.spy(action, "update");
    await action.runRecipe(recipe);
    is(action.lastError, null, "lastError should be null");
    ok(!enrollSpy.called, "enroll should not be called");
    ok(updateSpy.called, "update should be called");
    sendEventStub.assertEvents(
      [["update", "addon_study", recipe.arguments.name, {
        addonId: FIXTURE_ADDON_ID,
        addonVersion: "2.0",
      }]],
    );

    const updatedStudy = await AddonStudies.get(recipe.id);
    Assert.deepEqual(
      updatedStudy,
      {
        ...study,
        addonVersion: "2.0",
        addonUrl,
        extensionApiId: recipe.arguments.extensionApiId,
        extensionHash: hash,
      },
      "study data should be updated",
    );

    const addon = await AddonManager.getAddonByID(FIXTURE_ADDON_ID);
    ok(addon.version === "2.0", "add-on should be updated");
  },
);

// Test update fails when addon ID does not match
decorate_task(
  ensureAddonCleanup,
  withMockNormandyApi,
  AddonStudies.withStudies([addonStudyFactory({
    addonId: "test@example.com",
    extensionHash: "01d",
    extensionHashAlgorithm: "sha256",
    addonVersion: "0.1",
  })]),
  withSendEventStub,
  withInstalledWebExtensionSafe({id: FIXTURE_ADDON_ID, version: "0.1"}),
  async function updateFailsAddonIdMismatch(mockApi, [study], sendEventStub, installedAddon) {
    const recipe = recipeFactory({
      id: study.recipeId,
      type: "addon-study",
      arguments: {
        name: study.name,
        description: study.description,
        extensionApiId: study.extensionApiId,
        addonUrl: FIXTURE_ADDON_DETAILS["normandydriver-1.0"].url,
      },
    });
    mockApi.extensionDetails = {
      [recipe.arguments.extensionApiId]: extensionDetailsFactory({
        id: recipe.arguments.extensionApiId,
        extension_id: FIXTURE_ADDON_ID,
        xpi: FIXTURE_ADDON_DETAILS["normandydriver-1.0"].url,
      }),
    };
    const action = new AddonStudyAction();
    const updateSpy = sinon.spy(action, "update");
    await action.runRecipe(recipe);
    is(action.lastError, null, "lastError should be null");
    ok(updateSpy.called, "update should be called");
    sendEventStub.assertEvents(
      [["updateFailed", "addon_study", recipe.arguments.name, {
        reason: "addon-id-mismatch",
      }]],
    );

    const updatedStudy = await AddonStudies.get(recipe.id);
    Assert.deepEqual(updatedStudy, study, "study data should be unchanged");

    let addon = await AddonManager.getAddonByID(FIXTURE_ADDON_ID);
    ok(addon.version === "0.1", "add-on should be unchanged");
  },
);

// Test update fails when original addon does not exist
decorate_task(
  ensureAddonCleanup,
  withMockNormandyApi,
  AddonStudies.withStudies([addonStudyFactory({
    extensionHash: "01d",
    extensionHashAlgorithm: "sha256",
    addonVersion: "0.1",
  })]),
  withSendEventStub,
  withInstalledWebExtensionSafe({id: "test@example.com", version: "0.1"}),
  async function updateFailsAddonDoesNotExist(mockApi, [study], sendEventStub, installedAddon) {
    const recipe = recipeFactory({
      id: study.recipeId,
      type: "addon-study",
      arguments: {
        name: study.name,
        description: study.description,
        extensionApiId: study.extensionApiId,
        addonUrl: FIXTURE_ADDON_DETAILS["normandydriver-1.0"].url,
      },
    });
    mockApi.extensionDetails = {
      [recipe.arguments.extensionApiId]: extensionDetailsFactory({
        id: recipe.arguments.extensionApiId,
        extension_id: study.addonId,
        xpi: FIXTURE_ADDON_DETAILS["normandydriver-1.0"].url,
      }),
    };
    const action = new AddonStudyAction();
    const updateSpy = sinon.spy(action, "update");
    await action.runRecipe(recipe);
    is(action.lastError, null, "lastError should be null");
    ok(updateSpy.called, "update should be called");
    sendEventStub.assertEvents(
      [["updateFailed", "addon_study", recipe.arguments.name, {
        reason: "addon-does-not-exist",
      }]],
    );

    const updatedStudy = await AddonStudies.get(recipe.id);
    Assert.deepEqual(updatedStudy, study, "study data should be unchanged");

    let addon = await AddonManager.getAddonByID(FIXTURE_ADDON_ID);
    ok(!addon, "new add-on should not be installed");

    addon = await AddonManager.getAddonByID("test@example.com");
    ok(addon, "old add-on should still be installed");
  },
);

// Test update fails when download fails
decorate_task(
  ensureAddonCleanup,
  withMockNormandyApi,
  AddonStudies.withStudies([addonStudyFactory({
    addonId: FIXTURE_ADDON_ID,
    extensionHash: "01d",
    extensionHashAlgorithm: "sha256",
    addonVersion: "0.1",
  })]),
  withSendEventStub,
  withInstalledWebExtensionSafe({id: FIXTURE_ADDON_ID, version: "0.1"}),
  async function updateDownloadFailure(mockApi, [study], sendEventStub, installedAddon) {
    const recipe = recipeFactory({
      id: study.recipeId,
      type: "addon-study",
      arguments: {
        name: study.name,
        description: study.description,
        extensionApiId: study.extensionApiId,
        addonUrl: "https://example.com/404.xpi",
      },
    });
    mockApi.extensionDetails = {
      [recipe.arguments.extensionApiId]: extensionDetailsFactory({
        id: recipe.arguments.extensionApiId,
        extension_id: study.addonId,
        xpi: "https://example.com/404.xpi",
      }),
    };
    const action = new AddonStudyAction();
    const updateSpy = sinon.spy(action, "update");
    await action.runRecipe(recipe);
    is(action.lastError, null, "lastError should be null");
    ok(updateSpy.called, "update should be called");
    sendEventStub.assertEvents(
      [["updateFailed", "addon_study", recipe.arguments.name, {
        reason: "download-failure",
        detail: "ERROR_NETWORK_FAILURE",
      }]],
    );

    const updatedStudy = await AddonStudies.get(recipe.id);
    Assert.deepEqual(updatedStudy, study, "study data should be unchanged");

    const addon = await AddonManager.getAddonByID(FIXTURE_ADDON_ID);
    ok(addon.version === "0.1", "add-on should be unchanged");
  },
);

// Test update fails when hash check fails
decorate_task(
  ensureAddonCleanup,
  withMockNormandyApi,
  AddonStudies.withStudies([addonStudyFactory({
    addonId: FIXTURE_ADDON_ID,
    extensionHash: "01d",
    extensionHashAlgorithm: "sha256",
    addonVersion: "0.1",
  })]),
  withSendEventStub,
  withInstalledWebExtensionSafe({id: FIXTURE_ADDON_ID, version: "0.1"}),
  async function updateFailsHashCheckFail(mockApi, [study], sendEventStub, installedAddon) {
    const recipe = recipeFactory({
      id: study.recipeId,
      type: "addon-study",
      arguments: {
        name: study.name,
        description: study.description,
        extensionApiId: study.extensionApiId,
        addonUrl: FIXTURE_ADDON_DETAILS["normandydriver-1.0"].url,
      },
    });
    mockApi.extensionDetails = {
      [recipe.arguments.extensionApiId]: extensionDetailsFactory({
        id: recipe.arguments.extensionApiId,
        extension_id: study.addonId,
        xpi: FIXTURE_ADDON_DETAILS["normandydriver-1.0"].url,
        hash: "badhash",
      }),
    };
    const action = new AddonStudyAction();
    const updateSpy = sinon.spy(action, "update");
    await action.runRecipe(recipe);
    is(action.lastError, null, "lastError should be null");
    ok(updateSpy.called, "update should be called");
    sendEventStub.assertEvents(
      [["updateFailed", "addon_study", recipe.arguments.name, {
        reason: "download-failure",
        detail: "ERROR_INCORRECT_HASH",
      }]],
    );

    const updatedStudy = await AddonStudies.get(recipe.id);
    Assert.deepEqual(updatedStudy, study, "study data should be unchanged");

    const addon = await AddonManager.getAddonByID(FIXTURE_ADDON_ID);
    ok(addon.version === "0.1", "add-on should be unchanged");
  },
);

// Test update fails on downgrade when study version is greater than extension version
decorate_task(
  ensureAddonCleanup,
  withMockNormandyApi,
  AddonStudies.withStudies([addonStudyFactory({
    addonId: FIXTURE_ADDON_ID,
    extensionHash: "01d",
    extensionHashAlgorithm: "sha256",
    addonVersion: "2.0",
  })]),
  withSendEventStub,
  withInstalledWebExtensionSafe({id: FIXTURE_ADDON_ID, version: "2.0"}),
  async function upgradeFailsNoDowngrades(mockApi, [study], sendEventStub, installedAddon) {
    const recipe = recipeFactory({
      id: study.recipeId,
      type: "addon-study",
      arguments: {
        name: study.name,
        description: study.description,
        extensionApiId: study.extensionApiId,
        addonUrl: FIXTURE_ADDON_DETAILS["normandydriver-1.0"].url,
      },
    });
    mockApi.extensionDetails = {
      [recipe.arguments.extensionApiId]: extensionDetailsFactory({
        id: recipe.arguments.extensionApiId,
        extension_id: study.addonId,
        xpi: FIXTURE_ADDON_DETAILS["normandydriver-1.0"].url,
        version: "1.0",
      }),
    };
    const action = new AddonStudyAction();
    const updateSpy = sinon.spy(action, "update");
    await action.runRecipe(recipe);
    is(action.lastError, null, "lastError should be null");
    ok(updateSpy.called, "update should be called");
    sendEventStub.assertEvents(
      [["updateFailed", "addon_study", recipe.arguments.name, {
        reason: "no-downgrade",
      }]],
    );

    const updatedStudy = await AddonStudies.get(recipe.id);
    Assert.deepEqual(updatedStudy, study, "study data should be unchanged");

    const addon = await AddonManager.getAddonByID(FIXTURE_ADDON_ID);
    ok(addon.version === "2.0", "add-on should be unchanged");
  },
);

// Test update fails when there is a version mismatch with metadata
decorate_task(
  ensureAddonCleanup,
  withMockNormandyApi,
  AddonStudies.withStudies([addonStudyFactory({
    addonId: FIXTURE_ADDON_ID,
    extensionHash: FIXTURE_ADDON_DETAILS["normandydriver-1.0"].hash,
    extensionHashAlgorithm: "sha256",
    addonVersion: "1.0",
  })]),
  withSendEventStub,
  withInstalledWebExtensionFromURL(FIXTURE_ADDON_DETAILS["normandydriver-1.0"].url),
  async function upgradeFailsMetadataMismatchVersion(mockApi, [study], sendEventStub, installedAddon) {
    const recipe = recipeFactory({
      id: study.recipeId,
      type: "addon-study",
      arguments: {
        name: study.name,
        description: study.description,
        extensionApiId: study.extensionApiId,
        addonUrl: FIXTURE_ADDON_DETAILS["normandydriver-2.0"].url,
      },
    });
    mockApi.extensionDetails = {
      [recipe.arguments.extensionApiId]: extensionDetailsFactory({
        id: recipe.arguments.extensionApiId,
        extension_id: study.addonId,
        xpi: FIXTURE_ADDON_DETAILS["normandydriver-2.0"].url,
        version: "3.0",
        hash: FIXTURE_ADDON_DETAILS["normandydriver-2.0"].hash,
      }),
    };
    const action = new AddonStudyAction();
    const updateSpy = sinon.spy(action, "update");
    await action.runRecipe(recipe);
    is(action.lastError, null, "lastError should be null");
    ok(updateSpy.called, "update should be called");
    sendEventStub.assertEvents(
      [["updateFailed", "addon_study", recipe.arguments.name, {
        reason: "metadata-mismatch",
      }]],
    );

    const updatedStudy = await AddonStudies.get(recipe.id);
    Assert.deepEqual(updatedStudy, study, "study data should be unchanged");

    const addon = await AddonManager.getAddonByID(FIXTURE_ADDON_ID);
    ok(addon.version === "1.0", "add-on should be unchanged");

    let addonSourceURI = addon.getResourceURI();
    if (addonSourceURI instanceof Ci.nsIJARURI) {
      addonSourceURI = addonSourceURI.JARFile;
    }
    const xpiFile = addonSourceURI.QueryInterface(Ci.nsIFileURL).file;
    const installedHash = CryptoUtils.getFileHash(xpiFile, study.extensionHashAlgorithm);
    ok(installedHash === study.extensionHash, "add-on should be unchanged");
  },
);

// Test that unenrolling fails if the study doesn't exist
decorate_task(
  ensureAddonCleanup,
  AddonStudies.withStudies(),
  async function unenrollNonexistent(studies) {
    const action = new AddonStudyAction();
    await Assert.rejects(
      action.unenroll(42),
      /no study found/i,
      "unenroll should fail when no study exists"
    );
  }
);

// Test that unenrolling an inactive experiment fails
decorate_task(
  ensureAddonCleanup,
  AddonStudies.withStudies([
    addonStudyFactory({active: false}),
  ]),
  withSendEventStub,
  async ([study], sendEventStub) => {
    const action = new AddonStudyAction();
    await Assert.rejects(
      action.unenroll(study.recipeId),
      /cannot stop study.*already inactive/i,
      "unenroll should fail when the requested study is inactive"
    );
  }
);

// test a successful unenrollment
const testStopId = "testStop@example.com";
decorate_task(
  ensureAddonCleanup,
  AddonStudies.withStudies([
    addonStudyFactory({active: true, addonId: testStopId, studyEndDate: null}),
  ]),
  withInstalledWebExtension({id: testStopId}, /* expectUninstall: */ true),
  withSendEventStub,
  async function unenrollTest([study], [addonId, addonFile], sendEventStub) {
    let addon = await AddonManager.getAddonByID(addonId);
    ok(addon, "the add-on should be installed before unenrolling");

    const action = new AddonStudyAction();
    await action.unenroll(study.recipeId, "test-reason");

    const newStudy = AddonStudies.get(study.recipeId);
    is(!newStudy, false, "stop should mark the study as inactive");
    ok(newStudy.studyEndDate !== null, "the study should have an end date");

    addon = await AddonManager.getAddonByID(addonId);
    is(addon, null, "the add-on should be uninstalled after unenrolling");

    sendEventStub.assertEvents(
      [["unenroll", "addon_study", study.name, {
        addonId,
        addonVersion: study.addonVersion,
        reason: "test-reason",
      }]]
    );
  },
);

// If the add-on for a study isn't installed, a warning should be logged, but the action is still successful
decorate_task(
  ensureAddonCleanup,
  AddonStudies.withStudies([
    addonStudyFactory({active: true, addonId: "missingAddon@example.com", studyEndDate: null}),
  ]),
  withSendEventStub,
  async function unenrollMissingAddonTest([study], sendEventStub) {
    const action = new AddonStudyAction();

    SimpleTest.waitForExplicitFinish();
    SimpleTest.monitorConsole(() => SimpleTest.finish(), [{message: /could not uninstall addon/i}]);
    await action.unenroll(study.recipeId);

    sendEventStub.assertEvents(
      [["unenroll", "addon_study", study.name, {
        addonId: study.addonId,
        addonVersion: study.addonVersion,
        reason: "unknown",
      }]]
    );

    SimpleTest.endMonitorConsole();
  },
);

// Test that the action respects the study opt-out
decorate_task(
  ensureAddonCleanup,
  withMockNormandyApi,
  withSendEventStub,
  withMockPreferences,
  AddonStudies.withStudies(),
  async function testOptOut(mockApi, sendEventStub, mockPreferences) {
    mockPreferences.set("app.shield.optoutstudies.enabled", false);
    const action = new AddonStudyAction();
    const enrollSpy = sinon.spy(action, "enroll");
    const recipe = addonStudyRecipeFactory();
    mockApi.extensionDetails = {
      [recipe.arguments.extensionApiId]: extensionDetailsFactory({
        id: recipe.arguments.extensionApiId,
      }),
    };
    await action.runRecipe(recipe);
    is(action.state, AddonStudyAction.STATE_DISABLED, "the action should be disabled");
    await action.finalize();
    is(action.state, AddonStudyAction.STATE_FINALIZED, "the action should be finalized");
    is(action.lastError, null, "lastError should be null");
    Assert.deepEqual(enrollSpy.args, [], "enroll should not be called");
    sendEventStub.assertEvents([]);
  },
);

// Test that the action does not enroll paused recipes
decorate_task(
  ensureAddonCleanup,
  withMockNormandyApi,
  withSendEventStub,
  AddonStudies.withStudies(),
  async function testEnrollmentPaused(mockApi, sendEventStub) {
    const action = new AddonStudyAction();
    const enrollSpy = sinon.spy(action, "enroll");
    const recipe = addonStudyRecipeFactory({arguments: {isEnrollmentPaused: true}});
    const extensionDetails = extensionDetailsFactory({
      id: recipe.arguments.extensionApiId,
    });
    mockApi.extensionDetails = {
      [recipe.arguments.extensionApiId]: extensionDetails,
    };
    await action.runRecipe(recipe);
    const addon = await AddonManager.getAddonByID(extensionDetails.extension_id);
    is(addon, null, "the add-on should not have been installed");
    await action.finalize();
    ok(enrollSpy.called, "enroll should be called");
    sendEventStub.assertEvents([]);
  },
);

// Test that the action updates paused recipes
decorate_task(
  ensureAddonCleanup,
  withMockNormandyApi,
  AddonStudies.withStudies([addonStudyFactory({
    addonId: FIXTURE_ADDON_ID,
    extensionHash: FIXTURE_ADDON_DETAILS["normandydriver-1.0"].hash,
    extensionHashAlgorithm: "sha256",
    addonVersion: "1.0",
  })]),
  withSendEventStub,
  withInstalledWebExtensionSafe({id: FIXTURE_ADDON_ID, version: "1.0"}),
  async function testUpdateEnrollmentPaused(mockApi, [study], sendEventStub, installedAddon) {
    const addonUrl = FIXTURE_ADDON_DETAILS["normandydriver-2.0"].url;
    const recipe = recipeFactory({
      id: study.recipeId,
      type: "addon-study",
      arguments: {
        isEnrollmentPaused: true,
        name: study.name,
        description: study.description,
        extensionApiId: study.extensionApiId,
        addonUrl,
      },
    });
    mockApi.extensionDetails = {
      [recipe.arguments.extensionApiId]: extensionDetailsFactory({
        id: recipe.arguments.extensionApiId,
        extension_id: study.addonId,
        xpi: addonUrl,
        hash: FIXTURE_ADDON_DETAILS["normandydriver-2.0"].hash,
        version: "2.0",
      }),
    };
    const action = new AddonStudyAction();
    const enrollSpy = sinon.spy(action, "enroll");
    const updateSpy = sinon.spy(action, "update");
    await action.runRecipe(recipe);
    is(action.lastError, null, "lastError should be null");
    ok(!enrollSpy.called, "enroll should not be called");
    ok(updateSpy.called, "update should be called");
    sendEventStub.assertEvents(
      [["update", "addon_study", recipe.arguments.name, {
        addonId: FIXTURE_ADDON_ID,
        addonVersion: "2.0",
      }]],
    );

    const addon = await AddonManager.getAddonByID(FIXTURE_ADDON_ID);
    ok(addon.version === "2.0", "add-on should be updated");
  },
);

// Test that update method works for legacy studies with no hash
decorate_task(
  ensureAddonCleanup,
  withMockNormandyApi,
  AddonStudies.withStudies([addonStudyFactory({
    addonUrl: FIXTURE_ADDON_DETAILS["normandydriver-1.0"].url,
    addonId: FIXTURE_ADDON_ID,
    addonVersion: "1.0",
  })]),
  withSendEventStub,
  withInstalledWebExtensionFromURL(FIXTURE_ADDON_DETAILS["normandydriver-1.0"].url),
  async function updateWorksLegacyStudy(mockApi, [study], sendEventStub, installedAddon) {
    delete study.extensionHash;
    delete study.extensionHashAlgorithm;
    await AddonStudies.update(study);

    const recipe = recipeFactory({
      id: study.recipeId,
      type: "addon-study",
      arguments: {
        name: study.name,
        description: study.description,
        extensionApiId: study.extensionApiId,
        addonUrl: FIXTURE_ADDON_DETAILS["normandydriver-2.0"].url,
      },
    });

    const hash = FIXTURE_ADDON_DETAILS["normandydriver-2.0"].hash;
    mockApi.extensionDetails = {
      [recipe.arguments.extensionApiId]: extensionDetailsFactory({
        id: recipe.arguments.extensionApiId,
        extension_id: study.addonId,
        xpi: FIXTURE_ADDON_DETAILS["normandydriver-2.0"].url,
        hash,
        version: "2.0",
      }),
    };

    const action = new AddonStudyAction();
    const enrollSpy = sinon.spy(action, "enroll");
    const updateSpy = sinon.spy(action, "update");
    await action.runRecipe(recipe);

    is(action.lastError, null, "lastError should be null");
    ok(!enrollSpy.called, "enroll should not be called");
    ok(updateSpy.called, "update should be called");
    sendEventStub.assertEvents(
      [["update", "addon_study", "Test study", {
        addonId: "normandydriver@example.com",
        addonVersion: "2.0",
      }]],
    );

    const updatedStudy = await AddonStudies.get(recipe.id);
    Assert.deepEqual(
      updatedStudy,
      {
        ...study,
        addonVersion: "2.0",
        addonUrl: FIXTURE_ADDON_DETAILS["normandydriver-2.0"].url,
        extensionApiId: recipe.arguments.extensionApiId,
        extensionHash: hash,
        extensionHashAlgorithm: "sha256",
      },
      "study data should be updated",
    );
  },
);

// Test that enroll is not called if recipe is already enrolled
decorate_task(
  ensureAddonCleanup,
  AddonStudies.withStudies([addonStudyFactory()]),
  async function enrollTwiceFail([study]) {
    const action = new AddonStudyAction();
    const unenrollSpy = sinon.stub(action, "unenroll");
    await action.finalize();
    is(action.lastError, null, "lastError should be null");
    Assert.deepEqual(unenrollSpy.args, [[study.recipeId, "recipe-not-seen"]], "unenroll should be called");
  },
);
