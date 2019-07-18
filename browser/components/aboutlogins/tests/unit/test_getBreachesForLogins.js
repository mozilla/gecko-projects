/**
 * Test AboutLoginsParent.getBreachesForLogins
 */

"use strict";

const { Services } = ChromeUtils.import("resource://gre/modules/Services.jsm");
const { AboutLoginsParent } = ChromeUtils.import(
  "resource:///modules/AboutLoginsParent.jsm"
);

const TEST_BREACHES = [
  {
    AddedDate: "2018-12-20T23:56:26Z",
    BreachDate: "2018-12-16",
    Domain: "breached.com",
    Name: "Breached",
    PwnCount: 1643100,
    _status: "synced",
    id: "047940fe-d2fd-4314-b636-b4a952ee0043",
    last_modified: "1541615610052",
    schema: "1541615609018",
  },
  {
    AddedDate: "2018-12-20T23:56:26Z",
    BreachDate: "2018-12-16",
    Domain: "breached-subdomain.host.com",
    Name: "Only a Sub-Domain was Breached",
    PwnCount: 2754200,
    _status: "synced",
    id: "047940fe-d2fd-4314-b636-b4a952ee0044",
    last_modified: "1541615610052",
    schema: "1541615609018",
  },
];

const NOT_BREACHED_LOGIN = LoginTestUtils.testData.formLogin({
  origin: "https://www.example.com",
  formActionOrigin: "https://www.example.com",
  username: "username",
  password: "password",
  timePasswordChanged: Date.now(),
});
const BREACHED_LOGIN = LoginTestUtils.testData.formLogin({
  origin: "https://www.breached.com",
  formActionOrigin: "https://www.breached.com",
  username: "username",
  password: "password",
  timePasswordChanged: new Date("2018-12-15").getTime(),
});
const NOT_BREACHED_SUBDOMAIN_LOGIN = LoginTestUtils.testData.formLogin({
  origin: "https://not-breached-subdomain.host.com",
  formActionOrigin: "https://not-breached-subdomain.host.com",
  username: "username",
  password: "password",
});
const BREACHED_SUBDOMAIN_LOGIN = LoginTestUtils.testData.formLogin({
  origin: "https://breached-subdomain.host.com",
  formActionOrigin: "https://breached-subdomain.host.com",
  username: "username",
  password: "password",
  timePasswordChanged: new Date("2018-12-15").getTime(),
});

add_task(async function test_getBreachesForLogins_notBreachedLogin() {
  Services.logins.addLogin(NOT_BREACHED_LOGIN);

  const breachesByLoginGUID = await AboutLoginsParent.getBreachesForLogins(
    [NOT_BREACHED_LOGIN],
    TEST_BREACHES
  );
  Assert.strictEqual(
    breachesByLoginGUID.size,
    0,
    "Should be 0 breached logins."
  );
});

add_task(async function test_getBreachesForLogins_breachedLogin() {
  Services.logins.addLogin(BREACHED_LOGIN);

  const breachesByLoginGUID = await AboutLoginsParent.getBreachesForLogins(
    [NOT_BREACHED_LOGIN, BREACHED_LOGIN],
    TEST_BREACHES
  );
  Assert.strictEqual(
    breachesByLoginGUID.size,
    1,
    "Should be 1 breached login: " + BREACHED_LOGIN.origin
  );
});

add_task(async function test_getBreachesForLogins_notBreachedSubdomain() {
  Services.logins.addLogin(NOT_BREACHED_SUBDOMAIN_LOGIN);

  const breachesByLoginGUID = await AboutLoginsParent.getBreachesForLogins(
    [NOT_BREACHED_LOGIN, NOT_BREACHED_SUBDOMAIN_LOGIN],
    TEST_BREACHES
  );
  Assert.strictEqual(
    breachesByLoginGUID.size,
    0,
    "Should be 0 breached logins."
  );
});

add_task(async function test_getBreachesForLogins_breachedSubdomain() {
  Services.logins.addLogin(BREACHED_SUBDOMAIN_LOGIN);

  const breachesByLoginGUID = await AboutLoginsParent.getBreachesForLogins(
    [NOT_BREACHED_SUBDOMAIN_LOGIN, BREACHED_SUBDOMAIN_LOGIN],
    TEST_BREACHES
  );
  Assert.strictEqual(
    breachesByLoginGUID.size,
    1,
    "Should be 1 breached login: " + BREACHED_SUBDOMAIN_LOGIN.origin
  );
});
