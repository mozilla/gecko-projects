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
pref("dom.w3c_touch_events.enabled", 0);
pref("font.size.inflation.minTwips", 0);
pref("browser.enable_click_image_resizing", true);
pref("layout.css.scroll-snap.enabled", true);
pref("dom.mozInputMethod.enabled", false);
pref("browser.autofocus", true);
pref("touchcaret.enabled", false);
pref("layers.async-pan-zoom.enabled", false);

pref("gfx.vsync.hw-vsync.enabled", true);
pref("gfx.vsync.compositor", true);

// To be removed once bug 942756 is fixed.
pref("devtools.debugger.unix-domain-socket", "6000");

pref("devtools.debugger.forbid-certified-apps", false);
pref("devtools.debugger.prompt-connection", false);

// Update url.
pref("app.update.url", "https://aus4.mozilla.org/update/3/%PRODUCT%/%VERSION%/%BUILD_ID%/%BUILD_TARGET%/%LOCALE%/%CHANNEL%/%OS_VERSION%/%DISTRIBUTION%/%DISTRIBUTION_VERSION%/update.xml");

pref("b2g.nativeWindowGeometry.width", 700);
pref("b2g.nativeWindowGeometry.height", 600);
pref("b2g.nativeWindowGeometry.screenX", 0);
pref("b2g.nativeWindowGeometry.screenY", 0);
pref("b2g.nativeWindowGeometry.fullscreen", false);
