"use strict";

let server = createHttpServer({hosts: ["example.com"]});

createAppInfo("xpcshell@tests.mozilla.org", "XPCShell", "45", "45");

Services.prefs.setBoolPref("extensions.getAddons.cache.enabled", true);
Services.prefs.setBoolPref(PREF_EM_CHECK_UPDATE_SECURITY, false);

// Tests that cookies are not sent with background requests.
add_task(async function test_cookies() {
  const ID = "bg-cookies@tests.mozilla.org";

  // Add a new handler to the test web server for the given file path.
  // The handler appends the incoming requests to `results` and replies
  // with the provided body.
  function makeHandler(path, results, body) {
    server.registerPathHandler(path, (request, response) => {
      results.push(request);
      response.write(body);
    });
  }

  let gets = [];
  makeHandler("/get", gets, JSON.stringify({results: []}));
  Services.prefs.setCharPref(PREF_GETADDONS_BYIDS, "http://example.com/get");

  let overrides = [];
  makeHandler("/overrides", overrides, JSON.stringify({results: []}));
  Services.prefs.setCharPref(PREF_COMPAT_OVERRIDES, "http://example.com/overrides");

  let updates = [];
  makeHandler("/update", updates, JSON.stringify({
    addons: {
      [ID]: {
        updates: [
          {
            version: "2.0",
            update_link: "http://example.com/update.xpi",
            applications: {
              gecko: {},
            },
          },
        ],
      },
    },
  }));

  let xpiFetches = [];
  makeHandler("/update.xpi", xpiFetches, "");

  const COOKIE = "test";
  // cookies.add() takes a time in seconds
  let expiration = Date.now() / 1000 + 60 * 60;
  Services.cookies.add("example.com", "/", COOKIE, "testing",
                       false, false, false, expiration, {},
                       Ci.nsICookie2.SAMESITE_UNSET);

  await promiseStartupManager();

  let addon = await promiseInstallWebExtension({
    manifest: {
      version: "1.0",
      applications: {
        gecko: {
          id: ID,
          update_url: "http://example.com/update",
        },
      },
    },
  });

  equal(gets.length, 1, "Saw one addon metadata request");
  equal(gets[0].hasHeader("Cookie"), false, "Metadata request has no cookies");

  equal(overrides.length, 1, "Saw one compat overrides request");
  equal(overrides[0].hasHeader("Cookie"), false,
        "Compat overrides request has no cookies");

  await Promise.all([
    AddonTestUtils.promiseInstallEvent("onDownloadFailed"),
    AddonManagerPrivate.backgroundUpdateCheck(),
  ]);

  equal(updates.length, 1, "Saw one update check request");
  equal(updates[0].hasHeader("Cookie"), false, "Update request has no cookies");

  equal(xpiFetches.length, 1, "Saw one request for updated xpi");
  equal(xpiFetches[0].hasHeader("Cookie"), false,
        "Request for updated XPI has no cookies");

  await addon.uninstall();
});
