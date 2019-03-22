---
layout: default
title: API Changelog
description: GeckoView API Changelog.
nav_exclude: true
exclude: true
---

<h1> GeckoView API Changelog. </h1>

## v67
- Added [`setAutomaticFontSizeAdjustment`][67.2] to
  [`GeckoRuntimeSettings`][67.3] for automatically adjusting font size settings
  depending on the OS-level font size setting.

[67.2]: ../GeckoRuntimeSettings.html#setAutomaticFontSizeAdjustment-boolean-
[67.3]: ../GeckoRuntimeSettings.html

- Added [`setFontSizeFactor`][67.4] to [`GeckoRuntimeSettings`][67.3] for
  setting a font size scaling factor, and for enabling font inflation for
  non-mobile-friendly pages.

[67.4]: ../GeckoRuntimeSettings.html#setFontSizeFactor-float-

- Updated video autoplay API to reflect changes in Gecko. Instead of being a
  per-video permission in the [`PermissionDelegate`][67.5], it is a [runtime
  setting][67.6] that either allows or blocks autoplay videos.

[67.5]: ../GeckoSession.PermissionDelegate.html
[67.6]: ../GeckoRuntimeSettings.html#setAutoplayDefault-int-

- Change [`ContentBlocking.AT_ALL`][67.7] and [`ContentBlocking.SB_ALL`][67.8]
  values to mirror the actual constants they encompass.

[67.7]: ../ContentBlocking.html#AT_ALL
[67.8]: ../ContentBlocking.html#SB_ALL

- Added nested [`ContentBlocking`][67.9] runtime settings.

[67.9]: ../ContentBlocking.html

- Added [`RuntimeSettings`][67.10] base class to support nested settings.

[67.10]: ../RuntimeSettings.html

- Added [`baseUri`][67.11] to [`ContentDelegate.ContextElement`][65.21] and
  changed [`linkUri`][67.12] to absolute form.

[67.11]: ../GeckoSession.ContentDelegate.ContextElement.html#baseUri
[67.12]: ../GeckoSession.ContentDelegate.ContextElement.html#linkUri

- Added [`scrollBy`][67.13] and [`scrollTo`][67.14] to [`PanZoomController`][65.4].

[67.13]: ../PanZoomController.html#scrollBy-org.mozilla.geckoview.ScreenLength-org.mozilla.geckoview.ScreenLength-
[67.14]: ../PanZoomController.html#scrollTo-org.mozilla.geckoview.ScreenLength-org.mozilla.geckoview.ScreenLength-

- Added [`GeckoSession.getDefaultUserAgent`][67.1] to expose the build-time
  default user agent synchronously.

- Changed `WebResponse.body` from a `ByteBuffer` to an `InputStream`. Apps that want access
  to the entire response body will now need to read the stream themselves.

- Added `GeckoWebExecutor.FETCH_FLAGS_NO_REDIRECTS`, which will cause `GeckoWebExecutor.fetch()` to not
  automatically follow HTTP redirects (e.g., 302).

- Moved [`GeckoVRManager`][67.2] into the org.mozilla.geckoview package.

[67.1]: ../GeckoSession.html#getDefaultUserAgent--
[67.2]: ../GeckoVRManager.html

- Initial WebExtension support. [`GeckoRuntime#registerWebExtension`][67.15]
  allows embedders to register a local web extension.

[67.15]: ../GeckoRuntime.html#registerWebExtension-org.mozilla.geckoview.WebExtension-

- Added API to [`GeckoView`][65.5] to take screenshot of the visible page. Calling [`capturePixels`][67.16] returns a ['GeckoResult'][65.25] that completes to a [`Bitmap`][67.17] of the current [`Surface`][67.18] contents, or an [`IllegalStateException`][67.19] if the [`GeckoSession`][65.9] is not ready to render content.

[67.16]: ../GeckoView.html#capturePixels
[67.17]: https://developer.android.com/reference/android/graphics/Bitmap
[67.18]: https://developer.android.com/reference/android/view/Surface
[67.19]: https://developer.android.com/reference/java/lang/IllegalStateException

- Added API to capture a screenshot to [`GeckoDisplay`][67.20]. [`capturePixels`][67.21] returns a ['GeckoResult'][65.25] that completes to a [`Bitmap`][67.16] of the current [`Surface`][67.17] contents, or an [`IllegalStateException`][67.18] if the [`GeckoSession`][65.9] is not ready to render content.

[67.20]: ../GeckoDisplay.html
[67.21]: ../GeckoDisplay.html#capturePixels

- Add missing `@Nullable` annotation to return value for
  `GeckoSession.PromptDelegate.ChoiceCallback.onPopupResult()`

## v66
- Removed redundant field `trackingMode` from [`SecurityInformation`][66.6].
  Use `TrackingProtectionDelegate.onTrackerBlocked` for notification of blocked
  elements during page load.

[66.6]: ../GeckoSession.ProgressDelegate.SecurityInformation.html

- Added [`@NonNull`][66.1] or [`@Nullable`][66.2] to all APIs.

[66.1]: https://developer.android.com/reference/android/support/annotation/NonNull
[66.2]: https://developer.android.com/reference/android/support/annotation/Nullable

- Added methods for each setting in [`GeckoSessionSettings`][66.3]

[66.3]: ../GeckoSessionSettings.html

- Added [`GeckoSessionSettings`][66.4] for enabling desktop viewport. Desktop
  viewport is no longer set by [`USER_AGENT_MODE_DESKTOP`][66.5] and must be set
  separately.

[66.4]: ../GeckoSessionSettings.html
[66.5]: ../GeckoSessionSettings.html#USER_AGENT_MODE_DESKTOP

- Added [`@UiThread`][65.6] to [`GeckoSession.releaseSession`][66.7] and
  [`GeckoSession.setSession`][66.8]

[66.7]: ../GeckoView.html#releaseSession--
[66.8]: ../GeckoView.html#setSession-org.mozilla.geckoview.GeckoSession-

## v65
- Added experimental ad-blocking category to `GeckoSession.TrackingProtectionDelegate`.

- Moved [`CompositorController`][65.1], [`DynamicToolbarAnimator`][65.2],
  [`OverscrollEdgeEffect`][65.3], [`PanZoomController`][65.4] from
  `org.mozilla.gecko.gfx` to [`org.mozilla.geckoview`][65.5]

[65.1]: ../CompositorController.html
[65.2]: ../DynamicToolbarAnimator.html
[65.3]: ../OverscrollEdgeEffect.html
[65.4]: ../PanZoomController.html
[65.5]: ../package-summary.html

- Added [`@UiThread`][65.6], [`@AnyThread`][65.7] annotations to all APIs

[65.6]: https://developer.android.com/reference/android/support/annotation/UiThread
[65.7]: https://developer.android.com/reference/android/support/annotation/AnyThread

- Changed `GeckoRuntimeSettings#getLocale` to [`getLocales`][65.8] and related
  APIs.

[65.8]: ../GeckoRuntimeSettings.html#getLocales--

- Merged `org.mozilla.gecko.gfx.LayerSession` into [`GeckoSession`][65.9]

[65.9]: ../GeckoSession.html

- Added [`GeckoSession.MediaDelegate`][65.10] and [`MediaElement`][65.11]. This
  allow monitoring and control of web media elements (play, pause, seek, etc).

[65.10]: ../GeckoSession.MediaDelegate.html
[65.11]: ../MediaElement.html

- Removed unused `access` parameter from
  [`GeckoSession.PermissionDelegate#onContentPermissionRequest`][65.12]

[65.12]: ../GeckoSession.PermissionDelegate.html#onContentPermissionRequest-org.mozilla.geckoview.GeckoSession-java.lang.String-int-org.mozilla.geckoview.GeckoSession.PermissionDelegate.Callback-

- Added [`WebMessage`][65.13], [`WebRequest`][65.14], [`WebResponse`][65.15],
  and [`GeckoWebExecutor`][65.16]. This exposes Gecko networking to apps. It
  includes speculative connections, name resolution, and a Fetch-like HTTP API.

[65.13]: ../WebMessage.html
[65.14]: ../WebRequest.html
[65.15]: ../WebResponse.html
[65.16]: ../GeckoWebExecutor.html

- Added [`GeckoSession.HistoryDelegate`][65.17]. This allows apps to implement
  their own history storage system and provide visited link status.

[65.17]: ../GeckoSession.HistoryDelegate.html

- Added [`ContentDelegate#onFirstComposite`][65.18] to get first composite
  callback after a compositor start.

[65.18]: ../GeckoSession.ContentDelegate.html#onFirstComposite-org.mozilla.geckoview.GeckoSession-

- Changed `LoadRequest.isUserTriggered` to [`isRedirect`][65.19].

[65.19]: ../GeckoSession.NavigationDelegate.LoadRequest.html#isRedirect

- Added [`GeckoSession.LOAD_FLAGS_BYPASS_CLASSIFIER`][65.20] to bypass the URI
  classifier.

[65.20]: ../GeckoSession.html#LOAD_FLAGS_BYPASS_CLASSIFIER

- Added a `protected` empty constructor to all field-only classes so that apps
  can mock these classes in tests.

- Added [`ContentDelegate.ContextElement`][65.21] to extend the information
  passed to [`ContentDelegate#onContextMenu`][65.22]. Extended information
  includes the element's title and alt attributes.

[65.21]: ../GeckoSession.ContentDelegate.ContextElement.html
[65.22]: ../GeckoSession.ContentDelegate.html#onContextMenu-org.mozilla.geckoview.GeckoSession-int-int-org.mozilla.geckoview.GeckoSession.ContentDelegate.ContextElement-

- Changed [`ContentDelegate.ContextElement`][65.21] `TYPE_` constants to public
  access.

- Changed [`ContentDelegate.ContextElement`][65.21],
  [`GeckoSession.FinderResult`][65.23] to non-final class.

[65.23]: ../GeckoSession.FinderResult.html

- Update [`CrashReporter#sendCrashReport`][65.24] to return the crash ID as a
  [`GeckoResult<String>`][65.25].

[65.24]: ../CrashReporter.html#sendCrashReport-android.content.Context-android.os.Bundle-java.lang.String-
[65.25]: ../GeckoResult.html

[api-version]: 577c3b0d000b0b1da57f9af32331f1e9045939b9
