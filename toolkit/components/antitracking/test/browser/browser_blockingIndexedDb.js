AntiTracking.runTest("IndexedDB",
  // blocking callback
  async _ => {
    try {
      indexedDB.open("test", "1");
      ok(false, "IDB should be blocked");
    } catch (e) {
      ok(true, "IDB should be blocked");
      is(e.name, "SecurityError", "We want a security error message.");
    }
  },
  // non-blocking callback
  async _ => {
    indexedDB.open("test", "1");
    ok(true, "IDB should be allowed");
  },
  // Cleanup callback
  async _ => {
    await new Promise(resolve => {
      Services.clearData.deleteData(Ci.nsIClearDataService.CLEAR_ALL, value => resolve());
    });
  });

AntiTracking.runTest("IndexedDB in workers",
  async _ => {
    function blockCode() {
      try {
        indexedDB.open("test", "1");
        postMessage(false);
      } catch (e) {
        postMessage(e.name == "SecurityError");
      }
    }

    let blob = new Blob([blockCode.toString() + "; blockCode();"]);
    ok(blob, "Blob has been created");

    let blobURL = URL.createObjectURL(blob);
    ok(blobURL, "Blob URL has been created");

    let worker = new Worker(blobURL);
    ok(worker, "Worker has been created");

    await new Promise((resolve, reject) => {
      worker.onmessage = function(e) {
        if (e) {
          resolve();
        } else {
          reject();
        }
      };
    });
  },
  async _ => {
    function nonBlockCode() {
      indexedDB.open("test", "1");
      postMessage(true);
    }

    let blob = new Blob([nonBlockCode.toString() + "; nonBlockCode();"]);
    ok(blob, "Blob has been created");

    let blobURL = URL.createObjectURL(blob);
    ok(blobURL, "Blob URL has been created");

    let worker = new Worker(blobURL);
    ok(worker, "Worker has been created");

    await new Promise((resolve, reject) => {
      worker.onmessage = function(e) {
        if (e) {
          resolve();
        } else {
          reject();
        }
      };
    });
  },
  async _ => {
    await new Promise(resolve => {
      Services.clearData.deleteData(Ci.nsIClearDataService.CLEAR_ALL, value => resolve());
    });
  });

AntiTracking.runTest("IndexedDB and Storage Access API",
  // blocking callback
  async _ => {
    let hasAccess = await document.hasStorageAccess();
    ok(!hasAccess, "Doesn't yet have storage access");
    try {
      indexedDB.open("test", "1");
      ok(false, "IDB should be blocked");
    } catch (e) {
      ok(true, "IDB should be blocked");
      is(e.name, "SecurityError", "We want a security error message.");
    }

    let dwu = SpecialPowers.getDOMWindowUtils(window);
    let helper = dwu.setHandlingUserInput(true);

    let p;
    try {
      p = document.requestStorageAccess();
    } finally {
      helper.destruct();
    }
    await p;

    hasAccess = await document.hasStorageAccess();
    ok(hasAccess, "Now has storage access");

    indexedDB.open("test", "1");
    ok(true, "IDB should be allowed");
  },
  // non-blocking callback
  async _ => {
    let hasAccess = await document.hasStorageAccess();
    ok(!hasAccess, "Doesn't yet have storage access");
    indexedDB.open("test", "1");
    ok(true, "IDB should be allowed");

    let dwu = SpecialPowers.getDOMWindowUtils(window);
    let helper = dwu.setHandlingUserInput(true);

    let p;
    try {
      p = document.requestStorageAccess();
    } finally {
      helper.destruct();
    }
    await p;

    hasAccess = await document.hasStorageAccess();
    ok(hasAccess, "Now has storage access");

    // For non-tracking windows, calling the API is a no-op
    indexedDB.open("test", "1");
    ok(true, "IDB should be allowed");
  },
  // Cleanup callback
  async _ => {
    await new Promise(resolve => {
      Services.clearData.deleteData(Ci.nsIClearDataService.CLEAR_ALL, value => resolve());
    });
  },
  null, false, false);

AntiTracking.runTest("IndexedDB in workers and Storage Access API",
  async _ => {
    function blockCode() {
      try {
        indexedDB.open("test", "1");
        postMessage(false);
      } catch (e) {
        postMessage(e.name == "SecurityError");
      }
    }
    function nonBlockCode() {
      indexedDB.open("test", "1");
      postMessage(true);
    }

    let hasAccess = await document.hasStorageAccess();
    ok(!hasAccess, "Doesn't yet have storage access");

    let blob = new Blob([blockCode.toString() + "; blockCode();"]);
    ok(blob, "Blob has been created");

    let blobURL = URL.createObjectURL(blob);
    ok(blobURL, "Blob URL has been created");

    let worker = new Worker(blobURL);
    ok(worker, "Worker has been created");

    await new Promise((resolve, reject) => {
      worker.onmessage = function(e) {
        if (e) {
          resolve();
        } else {
          reject();
        }
      };
    });

    let dwu = SpecialPowers.getDOMWindowUtils(window);
    let helper = dwu.setHandlingUserInput(true);

    let p;
    try {
      p = document.requestStorageAccess();
    } finally {
      helper.destruct();
    }
    await p;

    hasAccess = await document.hasStorageAccess();
    ok(hasAccess, "Now has storage access");

    blob = new Blob([nonBlockCode.toString() + "; nonBlockCode();"]);
    ok(blob, "Blob has been created");

    blobURL = URL.createObjectURL(blob);
    ok(blobURL, "Blob URL has been created");

    worker = new Worker(blobURL);
    ok(worker, "Worker has been created");

    await new Promise((resolve, reject) => {
      worker.onmessage = function(e) {
        if (e) {
          resolve();
        } else {
          reject();
        }
      };
    });
  },
  async _ => {
    function nonBlockCode() {
      indexedDB.open("test", "1");
      postMessage(true);
    }

    let hasAccess = await document.hasStorageAccess();
    ok(!hasAccess, "Doesn't yet have storage access");

    let blob = new Blob([nonBlockCode.toString() + "; nonBlockCode();"]);
    ok(blob, "Blob has been created");

    let blobURL = URL.createObjectURL(blob);
    ok(blobURL, "Blob URL has been created");

    let worker = new Worker(blobURL);
    ok(worker, "Worker has been created");

    await new Promise((resolve, reject) => {
      worker.onmessage = function(e) {
        if (e) {
          resolve();
        } else {
          reject();
        }
      };
    });

    let dwu = SpecialPowers.getDOMWindowUtils(window);
    let helper = dwu.setHandlingUserInput(true);

    let p;
    try {
      p = document.requestStorageAccess();
    } finally {
      helper.destruct();
    }
    await p;

    hasAccess = await document.hasStorageAccess();
    ok(hasAccess, "Now has storage access");

    // For non-tracking windows, calling the API is a no-op

    worker = new Worker(blobURL);
    ok(worker, "Worker has been created");

    await new Promise((resolve, reject) => {
      worker.onmessage = function(e) {
        if (e) {
          resolve();
        } else {
          reject();
        }
      };
    });
  },
  async _ => {
    await new Promise(resolve => {
      Services.clearData.deleteData(Ci.nsIClearDataService.CLEAR_ALL, value => resolve());
    });
  },
  null, false, false);
