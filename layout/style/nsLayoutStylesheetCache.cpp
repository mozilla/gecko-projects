/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsLayoutStylesheetCache.h"

#include "nsAppDirectoryServiceDefs.h"
#include "mozilla/MemoryReporting.h"
#include "mozilla/Preferences.h"
#include "mozilla/StyleSheet.h"
#include "mozilla/StyleSheetInlines.h"
#include "mozilla/Telemetry.h"
#include "mozilla/css/Loader.h"
#include "mozilla/dom/SRIMetadata.h"
#include "MainThreadUtils.h"
#include "nsColor.h"
#include "nsIConsoleService.h"
#include "nsIFile.h"
#include "nsIObserverService.h"
#include "nsIXULRuntime.h"
#include "nsNetUtil.h"
#include "nsPresContext.h"
#include "nsPrintfCString.h"
#include "nsServiceManagerUtils.h"
#include "nsXULAppAPI.h"

using namespace mozilla;
using namespace mozilla::css;

NS_IMPL_ISUPPORTS(nsLayoutStylesheetCache, nsIObserver, nsIMemoryReporter)

nsresult nsLayoutStylesheetCache::Observe(nsISupports* aSubject,
                                          const char* aTopic,
                                          const char16_t* aData) {
  if (!strcmp(aTopic, "profile-before-change")) {
    mUserContentSheet = nullptr;
    mUserChromeSheet = nullptr;
  } else if (!strcmp(aTopic, "profile-do-change")) {
    InitFromProfile();
  } else if (strcmp(aTopic, "chrome-flush-skin-caches") == 0 ||
             strcmp(aTopic, "chrome-flush-caches") == 0) {
    mScrollbarsSheet = nullptr;
    mFormsSheet = nullptr;
  } else {
    MOZ_ASSERT_UNREACHABLE("Unexpected observer topic.");
  }
  return NS_OK;
}

#define STYLE_SHEET(identifier_, url_, lazy_)                                  \
  NotNull<StyleSheet*> nsLayoutStylesheetCache::identifier_##Sheet() {         \
    if (lazy_ && !m##identifier_##Sheet) {                                     \
      LoadSheetURL(url_, &m##identifier_##Sheet, eAgentSheetFeatures, eCrash); \
    }                                                                          \
    return WrapNotNull(m##identifier_##Sheet);                                 \
  }
#include "mozilla/UserAgentStyleSheetList.h"
#undef STYLE_SHEET

StyleSheet* nsLayoutStylesheetCache::GetUserContentSheet() {
  return mUserContentSheet;
}

StyleSheet* nsLayoutStylesheetCache::GetUserChromeSheet() {
  return mUserChromeSheet;
}

StyleSheet* nsLayoutStylesheetCache::ChromePreferenceSheet() {
  if (!mChromePreferenceSheet) {
    BuildPreferenceSheet(&mChromePreferenceSheet,
                         PreferenceSheet::ChromePrefs());
  }

  return mChromePreferenceSheet;
}

StyleSheet* nsLayoutStylesheetCache::ContentPreferenceSheet() {
  if (!mContentPreferenceSheet) {
    BuildPreferenceSheet(&mContentPreferenceSheet,
                         PreferenceSheet::ContentPrefs());
  }

  return mContentPreferenceSheet;
}

void nsLayoutStylesheetCache::Shutdown() {
  gCSSLoader = nullptr;
  NS_WARNING_ASSERTION(!gStyleCache || !gUserContentSheetURL,
                       "Got the URL but never used?");
  gStyleCache = nullptr;
  gUserContentSheetURL = nullptr;
}

void nsLayoutStylesheetCache::SetUserContentCSSURL(nsIURI* aURI) {
  MOZ_ASSERT(XRE_IsContentProcess(), "Only used in content processes.");
  gUserContentSheetURL = aURI;
}

MOZ_DEFINE_MALLOC_SIZE_OF(LayoutStylesheetCacheMallocSizeOf)

NS_IMETHODIMP
nsLayoutStylesheetCache::CollectReports(nsIHandleReportCallback* aHandleReport,
                                        nsISupports* aData, bool aAnonymize) {
  MOZ_COLLECT_REPORT("explicit/layout/style-sheet-cache", KIND_HEAP,
                     UNITS_BYTES,
                     SizeOfIncludingThis(LayoutStylesheetCacheMallocSizeOf),
                     "Memory used for some built-in style sheets.");

  return NS_OK;
}

size_t nsLayoutStylesheetCache::SizeOfIncludingThis(
    mozilla::MallocSizeOf aMallocSizeOf) const {
  size_t n = aMallocSizeOf(this);

#define MEASURE(s) n += s ? s->SizeOfIncludingThis(aMallocSizeOf) : 0;

#define STYLE_SHEET(identifier_, url_, lazy_) MEASURE(m##identifier_##Sheet);
#include "mozilla/UserAgentStyleSheetList.h"
#undef STYLE_SHEET

  MEASURE(mChromePreferenceSheet);
  MEASURE(mContentPreferenceSheet);
  MEASURE(mUserChromeSheet);
  MEASURE(mUserContentSheet);

  // Measurement of the following members may be added later if DMD finds it is
  // worthwhile:
  // - gCSSLoader

  return n;
}

nsLayoutStylesheetCache::nsLayoutStylesheetCache() {
  nsCOMPtr<nsIObserverService> obsSvc = mozilla::services::GetObserverService();
  NS_ASSERTION(obsSvc, "No global observer service?");

  if (obsSvc) {
    obsSvc->AddObserver(this, "profile-before-change", false);
    obsSvc->AddObserver(this, "profile-do-change", false);
    obsSvc->AddObserver(this, "chrome-flush-skin-caches", false);
    obsSvc->AddObserver(this, "chrome-flush-caches", false);
  }

  InitFromProfile();

  // And make sure that we load our UA sheets.  No need to do this
  // per-profile, since they're profile-invariant.
#define STYLE_SHEET(identifier_, url_, lazy_)                                \
  if (!lazy_) {                                                              \
    LoadSheetURL(url_, &m##identifier_##Sheet, eAgentSheetFeatures, eCrash); \
  }
#include "mozilla/UserAgentStyleSheetList.h"
#undef STYLE_SHEET

  if (XRE_IsParentProcess()) {
    // We know we need xul.css for the UI, so load that now too:
    XULSheet();
  }

  if (gUserContentSheetURL) {
    MOZ_ASSERT(XRE_IsContentProcess(), "Only used in content processes.");
    LoadSheet(gUserContentSheetURL, &mUserContentSheet, eUserSheetFeatures,
              eLogToConsole);
    gUserContentSheetURL = nullptr;
  }

  // The remaining sheets are created on-demand do to their use being rarer
  // (which helps save memory for Firefox OS apps) or because they need to
  // be re-loadable in DependentPrefChanged.
}

nsLayoutStylesheetCache::~nsLayoutStylesheetCache() {
  mozilla::UnregisterWeakMemoryReporter(this);
}

void nsLayoutStylesheetCache::InitMemoryReporter() {
  mozilla::RegisterWeakMemoryReporter(this);
}

/* static */
nsLayoutStylesheetCache* nsLayoutStylesheetCache::Singleton() {
  MOZ_ASSERT(NS_IsMainThread());

  if (!gStyleCache) {
    gStyleCache = new nsLayoutStylesheetCache;
    gStyleCache->InitMemoryReporter();

    // For each pref that controls a CSS feature that a UA style sheet depends
    // on (such as a pref that enables a property that a UA style sheet uses),
    // register DependentPrefChanged as a callback to ensure that the relevant
    // style sheets will be re-parsed.
    // Preferences::RegisterCallback(&DependentPrefChanged,
    //                               "layout.css.example-pref.enabled");
  }

  return gStyleCache;
}

void nsLayoutStylesheetCache::InitFromProfile() {
  nsCOMPtr<nsIXULRuntime> appInfo =
      do_GetService("@mozilla.org/xre/app-info;1");
  if (appInfo) {
    bool inSafeMode = false;
    appInfo->GetInSafeMode(&inSafeMode);
    if (inSafeMode) return;
  }
  nsCOMPtr<nsIFile> contentFile;
  nsCOMPtr<nsIFile> chromeFile;

  NS_GetSpecialDirectory(NS_APP_USER_CHROME_DIR, getter_AddRefs(contentFile));
  if (!contentFile) {
    // if we don't have a profile yet, that's OK!
    return;
  }

  contentFile->Clone(getter_AddRefs(chromeFile));
  if (!chromeFile) return;

  contentFile->Append(NS_LITERAL_STRING("userContent.css"));
  chromeFile->Append(NS_LITERAL_STRING("userChrome.css"));

  LoadSheetFile(contentFile, &mUserContentSheet, eUserSheetFeatures,
                eLogToConsole);
  LoadSheetFile(chromeFile, &mUserChromeSheet, eUserSheetFeatures,
                eLogToConsole);

  if (XRE_IsParentProcess()) {
    // We're interested specifically in potential chrome customizations,
    // so we only need data points from the parent process
    Telemetry::Accumulate(Telemetry::USER_CHROME_CSS_LOADED,
                          mUserChromeSheet != nullptr);
  }
}

void nsLayoutStylesheetCache::LoadSheetURL(const char* aURL,
                                           RefPtr<StyleSheet>* aSheet,
                                           SheetParsingMode aParsingMode,
                                           FailureAction aFailureAction) {
  nsCOMPtr<nsIURI> uri;
  NS_NewURI(getter_AddRefs(uri), aURL);
  LoadSheet(uri, aSheet, aParsingMode, aFailureAction);
  if (!aSheet) {
    NS_ERROR(nsPrintfCString("Could not load %s", aURL).get());
  }
}

void nsLayoutStylesheetCache::LoadSheetFile(nsIFile* aFile,
                                            RefPtr<StyleSheet>* aSheet,
                                            SheetParsingMode aParsingMode,
                                            FailureAction aFailureAction) {
  bool exists = false;
  aFile->Exists(&exists);

  if (!exists) return;

  nsCOMPtr<nsIURI> uri;
  NS_NewFileURI(getter_AddRefs(uri), aFile);

  LoadSheet(uri, aSheet, aParsingMode, aFailureAction);
}

static void ErrorLoadingSheet(nsIURI* aURI, const char* aMsg,
                              FailureAction aFailureAction) {
  nsPrintfCString errorMessage("%s loading built-in stylesheet '%s'", aMsg,
                               aURI ? aURI->GetSpecOrDefault().get() : "");
  if (aFailureAction == eLogToConsole) {
    nsCOMPtr<nsIConsoleService> cs =
        do_GetService(NS_CONSOLESERVICE_CONTRACTID);
    if (cs) {
      cs->LogStringMessage(NS_ConvertUTF8toUTF16(errorMessage).get());
      return;
    }
  }

  MOZ_CRASH_UNSAFE(errorMessage.get());
}

void nsLayoutStylesheetCache::LoadSheet(nsIURI* aURI,
                                        RefPtr<StyleSheet>* aSheet,
                                        SheetParsingMode aParsingMode,
                                        FailureAction aFailureAction) {
  if (!aURI) {
    ErrorLoadingSheet(aURI, "null URI", eCrash);
    return;
  }

  if (!gCSSLoader) {
    gCSSLoader = new Loader;
    if (!gCSSLoader) {
      ErrorLoadingSheet(aURI, "no Loader", eCrash);
      return;
    }
  }

  // Note: The parallel parsing code assume that UA sheets are always loaded
  // synchronously like they are here, and thus that we'll never attempt
  // parallel parsing on them. If that ever changes, we'll either need to find a
  // different way to prohibit parallel parsing for UA sheets, or handle
  // -moz-bool-pref and various other things in the parallel parsing code.
  nsresult rv = gCSSLoader->LoadSheetSync(aURI, aParsingMode, true, aSheet);
  if (NS_FAILED(rv)) {
    ErrorLoadingSheet(
        aURI,
        nsPrintfCString("LoadSheetSync failed with error %" PRIx32,
                        static_cast<uint32_t>(rv))
            .get(),
        aFailureAction);
  }
}

/* static */
void nsLayoutStylesheetCache::InvalidatePreferenceSheets() {
  if (gStyleCache) {
    gStyleCache->mContentPreferenceSheet = nullptr;
    gStyleCache->mChromePreferenceSheet = nullptr;
  }
}

void nsLayoutStylesheetCache::BuildPreferenceSheet(
    RefPtr<StyleSheet>* aSheet, const PreferenceSheet::Prefs& aPrefs) {
  *aSheet = new StyleSheet(eAgentSheetFeatures, CORS_NONE,
                           mozilla::net::RP_Unset, dom::SRIMetadata());

  StyleSheet* sheet = *aSheet;

  nsCOMPtr<nsIURI> uri;
  NS_NewURI(getter_AddRefs(uri), "about:PreferenceStyleSheet", nullptr);
  MOZ_ASSERT(uri, "URI creation shouldn't fail");

  sheet->SetURIs(uri, uri, uri);
  sheet->SetComplete();

  static const uint32_t kPreallocSize = 1024;

  nsCString sheetText;
  sheetText.SetCapacity(kPreallocSize);

#define NS_GET_R_G_B(color_) \
  NS_GET_R(color_), NS_GET_G(color_), NS_GET_B(color_)

  sheetText.AppendLiteral(
      "@namespace url(http://www.w3.org/1999/xhtml);\n"
      "@namespace svg url(http://www.w3.org/2000/svg);\n");

  // Rules for link styling.
  nscolor linkColor = aPrefs.mLinkColor;
  nscolor activeColor = aPrefs.mActiveLinkColor;
  nscolor visitedColor = aPrefs.mVisitedLinkColor;

  sheetText.AppendPrintf(
      "*|*:link { color: #%02x%02x%02x; }\n"
      "*|*:any-link:active { color: #%02x%02x%02x; }\n"
      "*|*:visited { color: #%02x%02x%02x; }\n",
      NS_GET_R_G_B(linkColor), NS_GET_R_G_B(activeColor),
      NS_GET_R_G_B(visitedColor));

  bool underlineLinks = aPrefs.mUnderlineLinks;
  sheetText.AppendPrintf("*|*:any-link%s { text-decoration: %s; }\n",
                         underlineLinks ? ":not(svg|a)" : "",
                         underlineLinks ? "underline" : "none");

  // Rules for focus styling.

  bool focusRingOnAnything = aPrefs.mFocusRingOnAnything;
  uint8_t focusRingWidth = aPrefs.mFocusRingWidth;
  uint8_t focusRingStyle = aPrefs.mFocusRingStyle;

  if ((focusRingWidth != 1 && focusRingWidth <= 4) || focusRingOnAnything) {
    if (focusRingWidth != 1) {
      // If the focus ring width is different from the default, fix buttons
      // with rings.
      sheetText.AppendPrintf(
          "button::-moz-focus-inner, input[type=\"reset\"]::-moz-focus-inner, "
          "input[type=\"button\"]::-moz-focus-inner, "
          "input[type=\"submit\"]::-moz-focus-inner { "
          "border: %dpx %s transparent !important; }\n",
          focusRingWidth, focusRingStyle == 0 ? "solid" : "dotted");

      sheetText.AppendLiteral(
          "button:focus::-moz-focus-inner, "
          "input[type=\"reset\"]:focus::-moz-focus-inner, "
          "input[type=\"button\"]:focus::-moz-focus-inner, "
          "input[type=\"submit\"]:focus::-moz-focus-inner { "
          "border-color: ButtonText !important; }\n");
    }

    sheetText.AppendPrintf(
        "%s { outline: %dpx %s !important; %s}\n",
        focusRingOnAnything ? ":focus" : "*|*:link:focus, *|*:visited:focus",
        focusRingWidth,
        focusRingStyle == 0 ?  // solid
            "solid -moz-mac-focusring"
                            : "dotted WindowText",
        focusRingStyle == 0 ?  // solid
            "-moz-outline-radius: 3px; outline-offset: 1px; "
                            : "");
  }

  if (aPrefs.mUseFocusColors) {
    nscolor focusText = aPrefs.mFocusTextColor;
    nscolor focusBG = aPrefs.mFocusBackgroundColor;
    sheetText.AppendPrintf(
        "*:focus, *:focus > font { color: #%02x%02x%02x !important; "
        "background-color: #%02x%02x%02x !important; }\n",
        NS_GET_R_G_B(focusText), NS_GET_R_G_B(focusBG));
  }

  NS_ASSERTION(sheetText.Length() <= kPreallocSize,
               "kPreallocSize should be big enough to build preference style "
               "sheet without reallocation");

  // NB: The pref sheet never has @import rules, thus no loader.
  sheet->ParseSheetSync(nullptr, sheetText,
                        /* aLoadData = */ nullptr,
                        /* aLineNumber = */ 0);

#undef NS_GET_R_G_B
}

mozilla::StaticRefPtr<nsLayoutStylesheetCache>
    nsLayoutStylesheetCache::gStyleCache;

mozilla::StaticRefPtr<mozilla::css::Loader> nsLayoutStylesheetCache::gCSSLoader;

mozilla::StaticRefPtr<nsIURI> nsLayoutStylesheetCache::gUserContentSheetURL;
