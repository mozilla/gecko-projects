/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

Cu.import("resource://gre/modules/PromiseUtils.jsm");
Cu.import("resource://services-sync/service.js");
Cu.import("resource://services-sync/util.js");
Cu.import("resource://testing-common/services/sync/utils.js");

function do_check_throws(func) {
  var raised = false;
  try {
    func();
  } catch (ex) {
    raised = true;
  }
  do_check_true(raised);
}

add_test(function test_findCluster() {
  _("Test Service._findCluster()");
  try {

    let whenReadyToAuthenticate = PromiseUtils.defer();
    Service.identity.whenReadyToAuthenticate = whenReadyToAuthenticate;
    whenReadyToAuthenticate.resolve(true);

    Service.identity._ensureValidToken = () => Promise.reject(new Error("Connection refused"));

    _("_findCluster() throws on network errors (e.g. connection refused).");
    do_check_throws(function() {
      Service._clusterManager._findCluster();
    });

    Service.identity._ensureValidToken = () => Promise.resolve(true);
    Service.identity._token = { endpoint: "http://weave.user.node" }

    _("_findCluster() returns the user's cluster node");
    let cluster = Service._clusterManager._findCluster();
    do_check_eq(cluster, "http://weave.user.node/");

  } finally {
    Svc.Prefs.resetBranch("");
    run_next_test();
  }
});

add_test(function test_setCluster() {
  _("Test Service._setCluster()");
  try {
    _("Check initial state.");
    do_check_eq(Service.clusterURL, "");

    Service._clusterManager._findCluster = () => "http://weave.user.node/";

    _("Set the cluster URL.");
    do_check_true(Service._clusterManager.setCluster());
    do_check_eq(Service.clusterURL, "http://weave.user.node/");

    _("Setting it again won't make a difference if it's the same one.");
    do_check_false(Service._clusterManager.setCluster());
    do_check_eq(Service.clusterURL, "http://weave.user.node/");

    _("A 'null' response won't make a difference either.");
    Service._clusterManager._findCluster = () => null;
    do_check_false(Service._clusterManager.setCluster());
    do_check_eq(Service.clusterURL, "http://weave.user.node/");

  } finally {
    Svc.Prefs.resetBranch("");
    run_next_test();
  }
});

function run_test() {
  initTestLogging();
  run_next_test();
}
