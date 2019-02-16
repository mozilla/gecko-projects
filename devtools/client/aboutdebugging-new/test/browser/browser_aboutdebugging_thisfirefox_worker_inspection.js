/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { gDevToolsBrowser } = require("devtools/client/framework/devtools-browser");

add_task(async function() {
  const thisFirefoxClient = createThisFirefoxClientMock();
  // Prepare a worker mock.
  const testWorkerTargetFront = {
    actorID: "test-worker-id",
  };
  const testWorker = {
    name: "Test Worker",
    workerTargetFront: testWorkerTargetFront,
  };
  // Add a worker mock as other worker.
  thisFirefoxClient.listWorkers = () => ({
    otherWorkers: [testWorker],
    serviceWorkers: [],
    sharedWorkers: [],
  });
  // Override getActor function of client which is used in inspect action for worker.
  thisFirefoxClient.client.getActor = id => {
    return id === testWorkerTargetFront.actorID ? testWorkerTargetFront : null;
  };

  const runtimeClientFactoryMock = createRuntimeClientFactoryMock();
  runtimeClientFactoryMock.createClientForRuntime = runtime => {
    const { RUNTIMES } = require("devtools/client/aboutdebugging-new/src/constants");
    if (runtime.id === RUNTIMES.THIS_FIREFOX) {
      return thisFirefoxClient;
    }
    throw new Error("Unexpected runtime id " + runtime.id);
  };

  info("Enable mocks");
  enableRuntimeClientFactoryMock(runtimeClientFactoryMock);
  const originalOpenWorkerForToolbox =  gDevToolsBrowser.openWorkerToolbox;
  registerCleanupFunction(() => {
    disableRuntimeClientFactoryMock();
    gDevToolsBrowser.openWorkerToolbox = originalOpenWorkerForToolbox;
  });

  const { document, tab, window } = await openAboutDebugging();
  await selectThisFirefoxPage(document, window.AboutDebugging.store);

  const workerTarget = findDebugTargetByText(testWorker.name, document);
  ok(workerTarget, "Worker target appeared for the test worker");
  const inspectButton = workerTarget.querySelector(".js-debug-target-inspect-button");
  ok(inspectButton, "Inspect button for the worker appeared");

  info("Check whether the correct actor front will be opened in worker toolbox");
  const waitForWorkerInspection = new Promise(resolve => {
    // Override openWorkerToolbox of gDevToolsBrowser to check the parameter.
    gDevToolsBrowser.openWorkerToolbox = front => {
      if (front === testWorkerTargetFront) {
        resolve();
      }
    };
  });

  inspectButton.click();

  await waitForWorkerInspection;
  ok(true, "Correct actor front will be opened in worker toolbox");

  await removeTab(tab);
});
