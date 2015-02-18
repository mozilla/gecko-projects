// See http://mxr.mozilla.org/mozilla-central/source/dom/webidl/KeyEvent.webidl
// for keyCode values.
// Default value is F5
pref("b2g.reload_key", '{ "key": 116, "shift": false, "ctrl": false, "alt": false, "meta": false }');
pref("b2g.default.start_manifest_url", "https://mozilla.github.io/browser.html/manifest.webapp");

pref("javascript.options.discardSystemSource", false);
pref("dom.ipc.tabs.disabled", false);
pref("browser.dom.window.dump.enabled", true);
pref("selectioncaret.enabled", false);
pref("browser.ignoreNativeFrameTextSelection", false);
pref("dom.meta-viewport.enabled", false);
pref("full-screen-api.ignore-widgets", false);
pref("image.high_quality_downscaling.enabled", true);

// To be removed once bug 942756 is fixed.
pref("devtools.debugger.unix-domain-socket", "6000");

pref("devtools.debugger.forbid-certified-apps", false);
