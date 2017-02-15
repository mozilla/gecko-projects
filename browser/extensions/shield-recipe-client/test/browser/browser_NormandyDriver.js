"use strict";

const {utils: Cu} = Components;
Cu.import("resource://shield-recipe-client/test/browser/Utils.jsm", this);
Cu.import("resource://gre/modules/Console.jsm", this);

add_task(Utils.withDriver(Assert, function* uuids(driver) {
  // Test that it is a UUID
  const uuid1 = driver.uuid();
  ok(Utils.UUID_REGEX.test(uuid1), "valid uuid format");

  // Test that UUIDs are different each time
  const uuid2 = driver.uuid();
  isnot(uuid1, uuid2, "uuids are unique");
}));

add_task(Utils.withDriver(Assert, function* userId(driver) {
  // Test that userId is a UUID
  ok(Utils.UUID_REGEX.test(driver.userId), "userId is a uuid");
}));

add_task(Utils.withDriver(Assert, function* syncDeviceCounts(driver) {
  let client = yield driver.client();
  is(client.syncMobileDevices, 0, "syncMobileDevices defaults to zero");
  is(client.syncDesktopDevices, 0, "syncDesktopDevices defaults to zero");
  is(client.syncTotalDevices, 0, "syncTotalDevices defaults to zero");

  yield SpecialPowers.pushPrefEnv({
    set: [
      ["services.sync.numClients", 9],
      ["services.sync.clients.devices.mobile", 5],
      ["services.sync.clients.devices.desktop", 4],
    ],
  });

  client = yield driver.client();
  is(client.syncMobileDevices, 5, "syncMobileDevices is read when set");
  is(client.syncDesktopDevices, 4, "syncDesktopDevices is read when set");
  is(client.syncTotalDevices, 9, "syncTotalDevices is read when set");
}));

add_task(Utils.withDriver(Assert, function* distribution(driver) {
  let client = yield driver.client();
  is(client.distribution, "default", "distribution has a default value");

  yield SpecialPowers.pushPrefEnv({set: [["distribution.id", "funnelcake"]]});
  client = yield driver.client();
  is(client.distribution, "funnelcake", "distribution is read from preferences");
}));
