/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Note: This test may cause intermittents if run at exactly midnight.

const { OS } = ChromeUtils.import("resource://gre/modules/osfile.jsm");
const { Sqlite } = ChromeUtils.import("resource://gre/modules/Sqlite.jsm");
XPCOMUtils.defineLazyServiceGetter(
  this,
  "TrackingDBService",
  "@mozilla.org/tracking-db-service;1",
  "nsITrackingDBService"
);

XPCOMUtils.defineLazyGetter(this, "DB_PATH", function() {
  return OS.Path.join(OS.Constants.Path.profileDir, "protections.sqlite");
});

const SQL = {
  insertCustomTimeEvent:
    "INSERT INTO events (type, count, timestamp)" +
    "VALUES (:type, :count, date(:timestamp));",

  selectAll: "SELECT * FROM events",
};

add_task(async function setup() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.contentblocking.database.enabled", true]],
  });
});

add_task(async function test_graph_display() {
  // This creates the schema.
  await TrackingDBService.saveEvents(JSON.stringify({}));
  let db = await Sqlite.openConnection({ path: DB_PATH });

  let date = new Date().toISOString();
  await db.execute(SQL.insertCustomTimeEvent, {
    type: TrackingDBService.TRACKERS_ID,
    count: 1,
    timestamp: date,
  });
  await db.execute(SQL.insertCustomTimeEvent, {
    type: TrackingDBService.CRYPTOMINERS_ID,
    count: 2,
    timestamp: date,
  });
  await db.execute(SQL.insertCustomTimeEvent, {
    type: TrackingDBService.FINGERPRINTERS_ID,
    count: 3,
    timestamp: date,
  });
  await db.execute(SQL.insertCustomTimeEvent, {
    type: TrackingDBService.TRACKING_COOKIES_ID,
    count: 4,
    timestamp: date,
  });

  date = new Date(Date.now() - 1 * 24 * 60 * 60 * 1000).toISOString();
  await db.execute(SQL.insertCustomTimeEvent, {
    type: TrackingDBService.TRACKERS_ID,
    count: 4,
    timestamp: date,
  });
  await db.execute(SQL.insertCustomTimeEvent, {
    type: TrackingDBService.CRYPTOMINERS_ID,
    count: 3,
    timestamp: date,
  });
  await db.execute(SQL.insertCustomTimeEvent, {
    type: TrackingDBService.FINGERPRINTERS_ID,
    count: 2,
    timestamp: date,
  });

  date = new Date(Date.now() - 2 * 24 * 60 * 60 * 1000).toISOString();
  await db.execute(SQL.insertCustomTimeEvent, {
    type: TrackingDBService.TRACKERS_ID,
    count: 4,
    timestamp: date,
  });
  await db.execute(SQL.insertCustomTimeEvent, {
    type: TrackingDBService.CRYPTOMINERS_ID,
    count: 3,
    timestamp: date,
  });
  await db.execute(SQL.insertCustomTimeEvent, {
    type: TrackingDBService.TRACKING_COOKIES_ID,
    count: 1,
    timestamp: date,
  });

  date = new Date(Date.now() - 3 * 24 * 60 * 60 * 1000).toISOString();
  await db.execute(SQL.insertCustomTimeEvent, {
    type: TrackingDBService.TRACKERS_ID,
    count: 3,
    timestamp: date,
  });
  await db.execute(SQL.insertCustomTimeEvent, {
    type: TrackingDBService.FINGERPRINTERS_ID,
    count: 2,
    timestamp: date,
  });
  await db.execute(SQL.insertCustomTimeEvent, {
    type: TrackingDBService.TRACKING_COOKIES_ID,
    count: 1,
    timestamp: date,
  });

  date = new Date(Date.now() - 4 * 24 * 60 * 60 * 1000).toISOString();
  await db.execute(SQL.insertCustomTimeEvent, {
    type: TrackingDBService.CRYPTOMINERS_ID,
    count: 2,
    timestamp: date,
  });
  await db.execute(SQL.insertCustomTimeEvent, {
    type: TrackingDBService.FINGERPRINTERS_ID,
    count: 2,
    timestamp: date,
  });
  await db.execute(SQL.insertCustomTimeEvent, {
    type: TrackingDBService.TRACKING_COOKIES_ID,
    count: 1,
    timestamp: date,
  });

  date = new Date(Date.now() - 5 * 24 * 60 * 60 * 1000).toISOString();
  await db.execute(SQL.insertCustomTimeEvent, {
    type: TrackingDBService.TRACKERS_ID,
    count: 3,
    timestamp: date,
  });
  await db.execute(SQL.insertCustomTimeEvent, {
    type: TrackingDBService.CRYPTOMINERS_ID,
    count: 3,
    timestamp: date,
  });
  await db.execute(SQL.insertCustomTimeEvent, {
    type: TrackingDBService.FINGERPRINTERS_ID,
    count: 2,
    timestamp: date,
  });
  await db.execute(SQL.insertCustomTimeEvent, {
    type: TrackingDBService.TRACKING_COOKIES_ID,
    count: 8,
    timestamp: date,
  });

  let tab = await BrowserTestUtils.openNewForegroundTab({
    url: "about:protections",
    gBrowser,
  });
  await ContentTask.spawn(tab.linkedBrowser, {}, async function() {
    const DATA_TYPES = ["cryptominer", "fingerprinter", "tracker", "cookie"];
    let allBars = null;
    await ContentTaskUtils.waitForCondition(() => {
      allBars = content.document.querySelectorAll(".graph-bar");
      return allBars.length;
    }, "The graph has been built");

    is(allBars.length, 7, "7 bars have been found on the graph");

    // today has each type
    // yesterday will have no tracking cookies
    // 2 days ago will have no fingerprinters
    // 3 days ago will have no cryptominers
    // 4 days ago will have no trackers
    // 5 days ago will have no social (when we add social)
    // 6 days ago will be empty
    is(
      allBars[6].querySelectorAll(".inner-bar").length,
      DATA_TYPES.length,
      "today has all of the data types shown"
    );
    is(
      allBars[6].querySelector(".tracker-bar").style.height,
      "10%",
      "trackers take 10%"
    );
    is(
      allBars[6].querySelector(".cryptominer-bar").style.height,
      "20%",
      "cryptominers take 20%"
    );
    is(
      allBars[6].querySelector(".fingerprinter-bar").style.height,
      "30%",
      "fingerprinters take 30%"
    );
    is(
      allBars[6].querySelector(".cookie-bar").style.height,
      "40%",
      "cross site tracking cookies take 40%"
    );

    is(
      allBars[5].querySelectorAll(".inner-bar").length,
      DATA_TYPES.length - 1,
      "1 day ago is missing one type"
    );
    ok(
      !allBars[5].querySelector(".cookie-bar"),
      "there is no cross site tracking cookie section 1 day ago."
    );

    is(
      allBars[4].querySelectorAll(".inner-bar").length,
      DATA_TYPES.length - 1,
      "2 days ago is missing one type"
    );
    ok(
      !allBars[4].querySelector(".fingerprinter-bar"),
      "there is no fingerprinter section 1 day ago."
    );

    is(
      allBars[3].querySelectorAll(".inner-bar").length,
      DATA_TYPES.length - 1,
      "3 days ago is missing one type"
    );
    ok(
      !allBars[3].querySelector(".cryptominer-bar"),
      "there is no cryptominer section 1 day ago."
    );

    is(
      allBars[2].querySelectorAll(".inner-bar").length,
      DATA_TYPES.length - 1,
      "4 days ago is missing one type"
    );
    ok(
      !allBars[2].querySelector(".tracker-bar"),
      "there is no tracker section 1 day ago."
    );

    // TODO test for social missing

    is(
      allBars[0].querySelectorAll(".inner-bar").length,
      0,
      "6 days ago has no content"
    );
    ok(allBars[0].classList.contains("empty"), "6 days ago is an empty bar");
  });

  // Use the TrackingDBService API to delete the data.
  await TrackingDBService.clearAll();
  // Make sure the data was deleted.
  let rows = await db.execute(SQL.selectAll);
  is(rows.length, 0, "length is 0");
  await db.close();
  BrowserTestUtils.removeTab(tab);
});
