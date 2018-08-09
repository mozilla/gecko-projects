const domLocalization =
  Cc["@mozilla.org/intl/domlocalization;1"].createInstance(
    Ci.mozIDOMLocalization);

const { Services } = ChromeUtils.import("resource://gre/modules/Services.jsm", {});
const { L10nRegistry, FileSource } =
  ChromeUtils.import("resource://gre/modules/L10nRegistry.jsm", {});

const fs = {
  "/localization/en-US/browser/menu.ftl": "key = [en] Value",
};
const originalLoad = L10nRegistry.load;
const originalRequested = Services.locale.getRequestedLocales();

L10nRegistry.load = async function(url) {
  return fs.hasOwnProperty(url) ? fs[url] : false;
};

const source = new FileSource("test", ["en-US"], "/localization/{locale}");
L10nRegistry.registerSource(source);

add_task(function test_methods_presence() {
  equal(typeof domLocalization.addResourceIds, "function");
  equal(typeof domLocalization.removeResourceIds, "function");

  equal(typeof domLocalization.formatMessages, "function");
  equal(typeof domLocalization.formatValues, "function");
  equal(typeof domLocalization.formatValue, "function");

  equal(typeof domLocalization.translateFragment, "function");
  equal(typeof domLocalization.translateElements, "function");

  equal(typeof domLocalization.connectRoot, "function");
  equal(typeof domLocalization.translateRoots, "function");

  equal(typeof domLocalization.ready, "object");
});

add_task(function test_add_remove_resources() {
  equal(domLocalization.addResourceIds(["./path1.ftl", "./path2.ftl"], 2), 2);
  equal(domLocalization.removeResourceIds(["./path1.ftl", "./path2.ftl"], 2), 0);
});

add_task(async function test_format_messages() {
  domLocalization.addResourceIds(["/browser/menu.ftl"], 1);

  let msgs = await domLocalization.formatMessages([{"id": "key"}], 1);
  equal(msgs.length, 1);
  equal(msgs[0].value, "[en] Value");
});

add_task(async function test_format_values() {
  let msgs = await domLocalization.formatValues([{"id": "key"}], 1);
  equal(msgs.length, 1);
  equal(msgs[0], "[en] Value");
});

add_task(async function test_format_value() {
  let msg = await domLocalization.formatValue("key");
  equal(msg, "[en] Value");
});

add_task(async function test_ready() {
  await domLocalization.ready;
  equal(1, 1);
});

add_task(function cleanup() {
  L10nRegistry.sources.clear();
  L10nRegistry.load = originalLoad;
  Services.locale.setRequestedLocales(originalRequested);
});
