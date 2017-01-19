/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

this.EXPORTED_SYMBOLS = [
  "btoa", // It comes from a module import.
  "encryptPayload",
  "isConfiguredWithLegacyIdentity",
  "ensureLegacyIdentityManager",
  "setBasicCredentials",
  "makeIdentityConfig",
  "makeFxAccountsInternalMock",
  "configureFxAccountIdentity",
  "configureIdentity",
  "SyncTestingInfrastructure",
  "waitForZeroTimer",
  "promiseZeroTimer",
  "promiseNamedTimer",
  "add_identity_test",
  "MockFxaStorageManager",
  "AccountState", // from a module import
  "sumHistogram",
];

var {utils: Cu} = Components;

Cu.import("resource://services-sync/status.js");
Cu.import("resource://services-sync/identity.js");
Cu.import("resource://services-common/utils.js");
Cu.import("resource://services-crypto/utils.js");
Cu.import("resource://services-sync/util.js");
Cu.import("resource://services-sync/browserid_identity.js");
Cu.import("resource://testing-common/services/common/logging.js");
Cu.import("resource://testing-common/services/sync/fakeservices.js");
Cu.import("resource://gre/modules/FxAccounts.jsm");
Cu.import("resource://gre/modules/FxAccountsClient.jsm");
Cu.import("resource://gre/modules/FxAccountsCommon.js");
Cu.import("resource://gre/modules/Services.jsm");

// and grab non-exported stuff via a backstage pass.
const {AccountState} = Cu.import("resource://gre/modules/FxAccounts.jsm", {});

// A mock "storage manager" for FxAccounts that doesn't actually write anywhere.
function MockFxaStorageManager() {
}

MockFxaStorageManager.prototype = {
  promiseInitialized: Promise.resolve(),

  initialize(accountData) {
    this.accountData = accountData;
  },

  finalize() {
    return Promise.resolve();
  },

  getAccountData() {
    return Promise.resolve(this.accountData);
  },

  updateAccountData(updatedFields) {
    for (let [name, value] of Object.entries(updatedFields)) {
      if (value == null) {
        delete this.accountData[name];
      } else {
        this.accountData[name] = value;
      }
    }
    return Promise.resolve();
  },

  deleteAccountData() {
    this.accountData = null;
    return Promise.resolve();
  }
}

/**
 * First wait >100ms (nsITimers can take up to that much time to fire, so
 * we can account for the timer in delayedAutoconnect) and then two event
 * loop ticks (to account for the Utils.nextTick() in autoConnect).
 */
this.waitForZeroTimer = function waitForZeroTimer(callback) {
  let ticks = 2;
  function wait() {
    if (ticks) {
      ticks -= 1;
      CommonUtils.nextTick(wait);
      return;
    }
    callback();
  }
  CommonUtils.namedTimer(wait, 150, {}, "timer");
}

this.promiseZeroTimer = function() {
  return new Promise(resolve => {
    waitForZeroTimer(resolve);
  });
}

this.promiseNamedTimer = function(wait, thisObj, name) {
  return new Promise(resolve => {
    Utils.namedTimer(resolve, wait, thisObj, name);
  });
}

/**
 * Return true if Sync is configured with the "legacy" identity provider.
 */
this.isConfiguredWithLegacyIdentity = function() {
  let ns = {};
  Cu.import("resource://services-sync/service.js", ns);

  // We can't use instanceof as BrowserIDManager (the "other" identity) inherits
  // from IdentityManager so that would return true - so check the prototype.
  return Object.getPrototypeOf(ns.Service.identity) === IdentityManager.prototype;
}

/**
  * Ensure Sync is configured with the "legacy" identity provider.
  */
this.ensureLegacyIdentityManager = function() {
  let ns = {};
  Cu.import("resource://services-sync/service.js", ns);

  Status.__authManager = ns.Service.identity = new IdentityManager();
  ns.Service._clusterManager = ns.Service.identity.createClusterManager(ns.Service);
}

this.setBasicCredentials =
 function setBasicCredentials(username, password, syncKey) {
  let ns = {};
  Cu.import("resource://services-sync/service.js", ns);

  let auth = ns.Service.identity;
  auth.username = username;
  auth.basicPassword = password;
  auth.syncKey = syncKey;
}

// Return an identity configuration suitable for testing with our identity
// providers.  |overrides| can specify overrides for any default values.
// |server| is optional, but if specified, will be used to form the cluster
// URL for the FxA identity.
this.makeIdentityConfig = function(overrides) {
  // first setup the defaults.
  let result = {
    // Username used in both fxaccount and sync identity configs.
    username: "foo",
    // fxaccount specific credentials.
    fxaccount: {
      user: {
        assertion: "assertion",
        email: "email",
        kA: "kA",
        kB: "kB",
        sessionToken: "sessionToken",
        uid: "a".repeat(32),
        verified: true,
      },
      token: {
        endpoint: null,
        duration: 300,
        id: "id",
        key: "key",
        hashed_fxa_uid: "f".repeat(32), // used during telemetry validation
        // uid will be set to the username.
      }
    },
    sync: {
      // username will come from the top-level username
      password: "whatever",
      syncKey: "abcdeabcdeabcdeabcdeabcdea",
    }
  };

  // Now handle any specified overrides.
  if (overrides) {
    if (overrides.username) {
      result.username = overrides.username;
    }
    if (overrides.sync) {
      // TODO: allow just some attributes to be specified
      result.sync = overrides.sync;
    }
    if (overrides.fxaccount) {
      // TODO: allow just some attributes to be specified
      result.fxaccount = overrides.fxaccount;
    }
  }
  return result;
}

this.makeFxAccountsInternalMock = function(config) {
  return {
    newAccountState(credentials) {
      // We only expect this to be called with null indicating the (mock)
      // storage should be read.
      if (credentials) {
        throw new Error("Not expecting to have credentials passed");
      }
      let storageManager = new MockFxaStorageManager();
      storageManager.initialize(config.fxaccount.user);
      let accountState = new AccountState(storageManager);
      return accountState;
    },
    _getAssertion(audience) {
      return Promise.resolve("assertion");
    },
  };
};

// Configure an instance of an FxAccount identity provider with the specified
// config (or the default config if not specified).
this.configureFxAccountIdentity = function(authService,
                                           config = makeIdentityConfig(),
                                           fxaInternal = makeFxAccountsInternalMock(config)) {
  // until we get better test infrastructure for bid_identity, we set the
  // signedin user's "email" to the username, simply as many tests rely on this.
  config.fxaccount.user.email = config.username;

  let fxa = new FxAccounts(fxaInternal);

  let MockFxAccountsClient = function() {
    FxAccountsClient.apply(this);
  };
  MockFxAccountsClient.prototype = {
    __proto__: FxAccountsClient.prototype,
    accountStatus() {
      return Promise.resolve(true);
    }
  };
  let mockFxAClient = new MockFxAccountsClient();
  fxa.internal._fxAccountsClient = mockFxAClient;

  let mockTSC = { // TokenServerClient
    getTokenFromBrowserIDAssertion(uri, assertion, cb) {
      config.fxaccount.token.uid = config.username;
      cb(null, config.fxaccount.token);
    },
  };
  authService._fxaService = fxa;
  authService._tokenServerClient = mockTSC;
  // Set the "account" of the browserId manager to be the "email" of the
  // logged in user of the mockFXA service.
  authService._signedInUser = config.fxaccount.user;
  authService._account = config.fxaccount.user.email;
}

this.configureIdentity = async function(identityOverrides, server) {
  let config = makeIdentityConfig(identityOverrides, server);
  let ns = {};
  Cu.import("resource://services-sync/service.js", ns);

  if (server) {
    ns.Service.serverURL = server.baseURI;
  }

  ns.Service._clusterManager = ns.Service.identity.createClusterManager(ns.Service);

  if (ns.Service.identity instanceof BrowserIDManager) {
    // do the FxAccounts thang...

    // If a server was specified, ensure FxA has a correct cluster URL available.
    if (server && !config.fxaccount.token.endpoint) {
      let ep = server.baseURI;
      if (!ep.endsWith("/")) {
        ep += "/";
      }
      ep += "1.1/" + config.username + "/";
      config.fxaccount.token.endpoint = ep;
    }

    configureFxAccountIdentity(ns.Service.identity, config);
    await ns.Service.identity.initializeWithCurrentIdentity();
    // and cheat to avoid requiring each test do an explicit login - give it
    // a cluster URL.
    if (config.fxaccount.token.endpoint) {
      ns.Service.clusterURL = config.fxaccount.token.endpoint;
    }
    return;
  }
  // old style identity provider.
  if (server) {
    ns.Service.clusterURL = server.baseURI + "/";
  }
  ns.Service.identity.username = config.username;
  ns.Service._updateCachedURLs();
  setBasicCredentials(config.username, config.sync.password, config.sync.syncKey);
}

this.SyncTestingInfrastructure = async function(server, username, password) {
  let ns = {};
  Cu.import("resource://services-sync/service.js", ns);

  let config = makeIdentityConfig({ username, password });
  await configureIdentity(config, server);
  return {
    logStats: initTestLogging(),
    fakeFilesystem: new FakeFilesystemService({}),
    fakeGUIDService: new FakeGUIDService(),
    fakeCryptoService: new FakeCryptoService(),
  }
}

/**
 * Turn WBO cleartext into fake "encrypted" payload as it goes over the wire.
 */
this.encryptPayload = function encryptPayload(cleartext) {
  if (typeof cleartext == "object") {
    cleartext = JSON.stringify(cleartext);
  }

  return {
    ciphertext: cleartext, // ciphertext == cleartext with fake crypto
    IV: "irrelevant",
    hmac: fakeSHA256HMAC(cleartext, CryptoUtils.makeHMACKey("")),
  };
}

// This helper can be used instead of 'add_test' or 'add_task' to run the
// specified test function twice - once with the old-style sync identity
// manager and once with the new-style BrowserID identity manager, to ensure
// it works in both cases.
//
// * The test itself should be passed as 'test' - ie, test code will generally
//   pass |this|.
// * The test function is a regular test function - although note that it must
//   be a generator - async operations should yield them, and run_next_test
//   mustn't be called.
this.add_identity_test = function(test, testFunction) {
  function note(what) {
    let msg = "running test " + testFunction.name + " with " + what + " identity manager";
    test.do_print(msg);
  }
  let ns = {};
  Cu.import("resource://services-sync/service.js", ns);
  // one task for the "old" identity manager.
  test.add_task(async function() {
    note("sync");
    let oldIdentity = Status._authManager;
    ensureLegacyIdentityManager();
    await testFunction();
    Status.__authManager = ns.Service.identity = oldIdentity;
  });
  // another task for the FxAccounts identity manager.
  test.add_task(async function() {
    note("FxAccounts");
    let oldIdentity = Status._authManager;
    Status.__authManager = ns.Service.identity = new BrowserIDManager();
    await testFunction();
    Status.__authManager = ns.Service.identity = oldIdentity;
  });
}

this.sumHistogram = function(name, options = {}) {
  let histogram = options.key ? Services.telemetry.getKeyedHistogramById(name) :
                  Services.telemetry.getHistogramById(name);
  let snapshot = histogram.snapshot(options.key);
  let sum = -Infinity;
  if (snapshot) {
    sum = snapshot.sum;
  }
  histogram.clear();
  return sum;
}
