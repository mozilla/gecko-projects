/* Any copyright is dedicated to the Public Domain.
 *    http://creativecommons.org/publicdomain/zero/1.0/ */

/*
 * Test that a search engine's identifier can be extracted from the filename.
 */

"use strict";

const SEARCH_APP_DIR = 1;

function run_test() {
  do_load_manifest("data/chrome.manifest");

  configureToLoadJarEngines();

  run_next_test();
}

add_test(function test_identifier() {
  Services.search.init(async function initComplete(aResult) {
    do_print("init'd search service");
    do_check_true(Components.isSuccessCode(aResult));

    await installTestEngine();
    let profileEngine = Services.search.getEngineByName(kTestEngineName);
    let jarEngine = Services.search.getEngineByName("bug645970");

    do_check_true(profileEngine instanceof Ci.nsISearchEngine);
    do_check_true(jarEngine instanceof Ci.nsISearchEngine);

    // An engine loaded from the profile directory won't have an identifier,
    // because it's not built-in.
    do_check_eq(profileEngine.identifier, null);

    // An engine loaded from a JAR will have an identifier corresponding to
    // the filename inside the JAR. (In this case it's the same as the name.)
    do_check_eq(jarEngine.identifier, "bug645970");

    run_next_test();
  });
});
