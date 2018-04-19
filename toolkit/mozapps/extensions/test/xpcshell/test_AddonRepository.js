/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

// Tests AddonRepository.jsm

ChromeUtils.import("resource://gre/modules/addons/AddonRepository.jsm");

ChromeUtils.import("resource://testing-common/httpd.js");
var gServer = new HttpServer();
gServer.start(-1);

const PREF_GETADDONS_BROWSEADDONS        = "extensions.getAddons.browseAddons";
const PREF_GETADDONS_BROWSESEARCHRESULTS = "extensions.getAddons.search.browseURL";

const PORT          = gServer.identity.primaryPort;
const BASE_URL      = "http://localhost:" + PORT;
const DEFAULT_URL   = "about:blank";

gPort = PORT;

// Path to source URI of installing add-on
const INSTALL_URL2  = "/addons/test_AddonRepository_2.xpi";
// Path to source URI of non-active add-on (state = STATE_AVAILABLE)
const INSTALL_URL3  = "/addons/test_AddonRepository_3.xpi";

// Properties of an individual add-on that should be checked
// Note: name is checked separately
var ADDON_PROPERTIES = ["id", "type", "version", "creator", "developers",
                        "description", "fullDescription",
                        "iconURL", "icons", "screenshots",
                        "supportURL", "contributionURL",
                        "averageRating", "reviewCount", "reviewURL",
                        "weeklyDownloads", "dailyUsers",
                        "sourceURI", "size", "updateDate"];

// Results of getAddonsByIDs
var GET_RESULTS = [{
  id:                     "test1@tests.mozilla.org",
  type:                   "extension",
  version:                "1.1",
  creator:                {
                            name: "Test Creator 1",
                            url:  BASE_URL + "/creator1.html"
                          },
  developers:             [{
                            name: "Test Developer 1",
                            url:  BASE_URL + "/developer1.html"
                          }],
  description:            "Test Summary 1",
  fullDescription:        "Test Description 1",
  iconURL:                BASE_URL + "/icon1.png",
  icons:                  { "32": BASE_URL + "/icon1.png" },
  screenshots:            [{
                            url:             BASE_URL + "/full1-1.png",
                            width:           400,
                            height:          300,
                            thumbnailURL:    BASE_URL + "/thumbnail1-1.png",
                            thumbnailWidth:  200,
                            thumbnailHeight: 150,
                            caption:         "Caption 1 - 1"
                          }, {
                            url:          BASE_URL + "/full2-1.png",
                            thumbnailURL: BASE_URL + "/thumbnail2-1.png",
                            caption:      "Caption 2 - 1"
                          }],
  supportURL:             BASE_URL + "/support1.html",
  contributionURL:        BASE_URL + "/contribution1.html",
  averageRating:          4,
  reviewCount:            1111,
  reviewURL:              BASE_URL + "/review1.html",
  weeklyDownloads:        3333,
  sourceURI:              BASE_URL + INSTALL_URL2,
  size:                   5555,
  updateDate:             new Date(1265033045000),
}, {
  id:                     "test_AddonRepository_1@tests.mozilla.org",
  type:                   "theme",
  version:                "1.4",
  icons:                  {}
}];

// Values for testing AddonRepository.getAddonsByIDs()
var GET_TEST = {
  preference:       PREF_GETADDONS_BYIDS,
  preferenceValue:  BASE_URL + "/%OS%/%VERSION%/%IDS%",
  failedIDs:      ["test1@tests.mozilla.org"],
  failedURL:        "/XPCShell/1/test1%40tests.mozilla.org",
  successfulIDs:  ["test1@tests.mozilla.org",
                     "{00000000-1111-2222-3333-444444444444}",
                     "test_AddonRepository_1@tests.mozilla.org"],
  successfulURL:    "/XPCShell/1/test1%40tests.mozilla.org%2C" +
                    "%7B00000000-1111-2222-3333-444444444444%7D%2C" +
                    "test_AddonRepository_1%40tests.mozilla.org"
};

// Test that actual results and expected results are equal
function check_results(aActualAddons, aExpectedAddons) {
  do_check_addons(aActualAddons, aExpectedAddons, ADDON_PROPERTIES);

  // Additional tests
  aActualAddons.forEach(function check_each_addon(aActualAddon) {
    // Separately check name so better messages are output when test fails
    if (aActualAddon.name == "FAIL")
      do_throw(aActualAddon.id + " - " + aActualAddon.description);
    if (aActualAddon.name != "PASS")
      do_throw(aActualAddon.id + " - invalid add-on name " + aActualAddon.name);

  });
}

function run_test() {
  // Setup for test
  do_test_pending();
  createAppInfo("xpcshell@tests.mozilla.org", "XPCShell", "1", "1.9");

  startupManager();

  // Install an add-on so can check that it isn't returned in the results
  installAllFiles([do_get_addon("test_AddonRepository_1")], function addon_1_install_callback() {
    restartManager();

    // Register other add-on XPI files
    gServer.registerFile(INSTALL_URL2,
                        do_get_addon("test_AddonRepository_2"));
    gServer.registerFile(INSTALL_URL3,
                        do_get_addon("test_AddonRepository_3"));

    // Register files used to test search failure
    mapUrlToFile(GET_TEST.failedURL,
                 do_get_file("data/test_AddonRepository_fail.json"),
                 gServer);

    // Register files used to test search success
    mapUrlToFile(GET_TEST.successfulURL,
                 do_get_file("data/test_AddonRepository_getAddonsByIDs.json"),
                 gServer);

    // Create an active AddonInstall so can check that it isn't returned in the results
    AddonManager.getInstallForURL(BASE_URL + INSTALL_URL2, function addon_2_get(aInstall) {
      try {
        aInstall.install();
      } catch (e) {
        info("Failed to install add-on " + aInstall.sourceURI.spec);
        do_report_unexpected_exception(e);
      }

      // Create a non-active AddonInstall so can check that it is returned in the results
      AddonManager.getInstallForURL(BASE_URL + INSTALL_URL3,
                                    run_test_1, "application/x-xpinstall");
    }, "application/x-xpinstall");
  });
}

function end_test() {
  let testDir = gProfD.clone();
  testDir.append("extensions");
  testDir.append("staged");
  gServer.stop(function() {
    function loop() {
      if (!testDir.exists()) {
        info("Staged directory has been cleaned up");
        do_test_finished();
      }
      info("Waiting 1 second until cleanup is complete");
      do_timeout(1000, loop);
    }
    loop();
  });
}

// Tests homepageURL and getSearchURL()
function run_test_1() {
  function check_urls(aPreference, aGetURL, aTests) {
    aTests.forEach(function(aTest) {
      Services.prefs.setCharPref(aPreference, aTest.preferenceValue);
      Assert.equal(aGetURL(aTest), aTest.expectedURL);
    });
  }

  var urlTests = [{
    preferenceValue:  BASE_URL,
    expectedURL:      BASE_URL
  }, {
    preferenceValue:  BASE_URL + "/%OS%/%VERSION%",
    expectedURL:      BASE_URL + "/XPCShell/1"
  }];

  // Extra tests for AddonRepository.getSearchURL();
  var searchURLTests = [{
    searchTerms:      "test",
    preferenceValue:  BASE_URL + "/search?q=%TERMS%",
    expectedURL:      BASE_URL + "/search?q=test"
  }, {
    searchTerms:      "test search",
    preferenceValue:  BASE_URL + "/%TERMS%",
    expectedURL:      BASE_URL + "/test%20search"
  }, {
    searchTerms:      "odd=search:with&weird\"characters",
    preferenceValue:  BASE_URL + "/%TERMS%",
    expectedURL:      BASE_URL + "/odd%3Dsearch%3Awith%26weird%22characters"
  }];

  // Setup tests for homepageURL and getSearchURL()
  var tests = [{
    initiallyUndefined: true,
    preference:         PREF_GETADDONS_BROWSEADDONS,
    urlTests,
    getURL:             () => AddonRepository.homepageURL
  }, {
    initiallyUndefined: false,
    preference:         PREF_GETADDONS_BROWSESEARCHRESULTS,
    urlTests:           urlTests.concat(searchURLTests),
    getURL:             function getSearchURL(aTest) {
                          var searchTerms = aTest && aTest.searchTerms ? aTest.searchTerms
                                                                       : "unused terms";
                          return AddonRepository.getSearchURL(searchTerms);
                        }
  }];

  tests.forEach(function url_test(aTest) {
    if (aTest.initiallyUndefined) {
      // Preference is not defined by default
      Assert.equal(Services.prefs.getPrefType(aTest.preference),
                   Services.prefs.PREF_INVALID);
      Assert.equal(aTest.getURL(), DEFAULT_URL);
    }

    check_urls(aTest.preference, aTest.getURL, aTest.urlTests);
  });

  run_test_getAddonsByID_fails();
}

// Tests failure of AddonRepository.getAddonsByIDs()
function run_test_getAddonsByID_fails() {
  Services.prefs.setCharPref(GET_TEST.preference, GET_TEST.preferenceValue);
  AddonRepository.getAddonsByIDs(GET_TEST.failedIDs).then(result => {
    do_throw("getAddonsByIDs should not have succeeded");
    end_test();
  }).catch(err => {
    run_test_getAddonsByID_succeeds();
  });
}

// Tests success of AddonRepository.getAddonsByIDs()
function run_test_getAddonsByID_succeeds() {
  AddonRepository.getAddonsByIDs(GET_TEST.successfulIDs).then(result => {
    check_results(result, GET_RESULTS);
    end_test();
  }).catch(err => {
    do_throw(err);
    end_test();
  });
}
