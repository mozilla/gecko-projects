"use strict";

ChromeUtils.import("resource://gre/modules/AppConstants.jsm");
ChromeUtils.import("resource://gre/modules/Services.jsm");
ChromeUtils.import("resource://testing-common/ExtensionXPCShellUtils.jsm");

const remote = AppConstants.platform !== "android";

ExtensionTestUtils.init(this);

let contentPage;

function getContents(sharedMap = Services.cpmm.sharedData) {
  return {
    keys: Array.from(sharedMap.keys()),
    values: Array.from(sharedMap.values()),
    entries: Array.from(sharedMap.entries()),
    getValues: Array.from(sharedMap.keys(),
                          key => sharedMap.get(key)),
  };
}

function checkMap(contents, expected) {
  expected = Array.from(expected);

  equal(contents.keys.length, expected.length,
        "Got correct number of keys");
  equal(contents.values.length, expected.length,
        "Got correct number of values");
  equal(contents.entries.length, expected.length,
        "Got correct number of entries");

  for (let [i, [key, val]] of contents.entries.entries()) {
    equal(key, contents.keys[i], `keys()[${i}] matches entries()[${i}]`);
    deepEqual(val, contents.values[i], `values()[${i}] matches entries()[${i}]`);
  }

  expected.sort(([a], [b]) => a.localeCompare(b));
  contents.entries.sort(([a], [b]) => a.localeCompare(b));

  for (let [i, [key, val]] of contents.entries.entries()) {
    equal(key, expected[i][0], `expected[${i}].key matches entries()[${i}].key`);
    deepEqual(val, expected[i][1], `expected[${i}].value matches entries()[${i}].value`);
  }
}

function checkParentMap(expected) {
  info("Checking parent map");
  checkMap(getContents(Services.ppmm.sharedData), expected);
}

async function checkContentMaps(expected, parentOnly = false) {
  info("Checking in-process content map");
  checkMap(getContents(Services.cpmm.sharedData), expected);

  if (!parentOnly) {
    info("Checking out-of-process content map");
    let contents = await contentPage.spawn(undefined, getContents);
    checkMap(contents, expected);
  }
}

add_task(async function setup() {
  contentPage = await ExtensionTestUtils.loadContentPage("about:blank", {remote});
  registerCleanupFunction(() => contentPage.close());
});

add_task(async function test_sharedMap() {
  let {sharedData} = Services.ppmm;

  info("Check that parent and child maps are both initially empty");

  checkParentMap([]);
  await checkContentMaps([]);

  let expected = [
    ["foo-a", {"foo": "a"}],
    ["foo-b", {"foo": "b"}],
    ["bar-c", null],
    ["bar-d", 42],
  ];

  function setKey(key, val) {
    sharedData.set(key, val);
    expected = expected.filter(([k]) => k != key);
    expected.push([key, val]);
  }
  function deleteKey(key) {
    sharedData.delete(key);
    expected = expected.filter(([k]) => k != key);
  }

  for (let [key, val] of expected) {
    sharedData.set(key, val);
  }

  info("Add some entries, test that they are initially only available in the parent");

  checkParentMap(expected);
  await checkContentMaps([]);

  info("Flush. Check that changes are visible in both parent and children");

  sharedData.flush();

  checkParentMap(expected);
  await checkContentMaps(expected);

  info("Add another entry. Check that it is initially only available in the parent");

  let oldExpected = Array.from(expected);

  setKey("baz-a", {meh: "meh"});

  // When we do several checks in a row, we can't check the values in
  // the content process, since the async checks may allow the idle
  // flush task to run, and update it before we're ready.

  checkParentMap(expected);
  checkContentMaps(oldExpected, true);

  info("Add another entry. Check that both new entries are only available in the parent");

  setKey("baz-a", {meh: 12});

  checkParentMap(expected);
  checkContentMaps(oldExpected, true);

  info("Delete an entry. Check that all changes are only visible in the parent");

  deleteKey("foo-b");

  checkParentMap(expected);
  checkContentMaps(oldExpected, true);

  info("Flush. Check that all entries are available in both parent and children");

  sharedData.flush();

  checkParentMap(expected);
  await checkContentMaps(expected);


  info("Test that entries are automatically flushed on idle:");

  info("Add a new entry. Check that it is initially only available in the parent");

  // Test the idle flush task.
  oldExpected = Array.from(expected);

  setKey("thing", "stuff");

  checkParentMap(expected);
  checkContentMaps(oldExpected, true);

  info("Wait for an idle timeout. Check that changes are now visible in all children");

  await new Promise(resolve => ChromeUtils.idleDispatch(resolve));

  checkParentMap(expected);
  await checkContentMaps(expected);
});
