/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * methods for dealing with CSS properties and tables of the keyword
 * values they accept
 */

#include "nsCSSProps.h"

#include "mozilla/ArrayUtils.h"
#include "mozilla/Casting.h"

#include "nsCSSKeywords.h"
#include "nsLayoutUtils.h"
#include "nsIWidget.h"
#include "nsStyleConsts.h"  // For system widget appearance types

#include "mozilla/dom/Animation.h"
#include "mozilla/dom/AnimationEffectBinding.h"  // for PlaybackDirection
#include "mozilla/LookAndFeel.h"                 // for system colors

#include "nsString.h"
#include "nsStaticNameTable.h"

#include "mozilla/Preferences.h"
#include "mozilla/StaticPrefs_layout.h"

using namespace mozilla;

typedef nsCSSProps::KTableEntry KTableEntry;

static int32_t gPropertyTableRefCount;
static nsStaticCaseInsensitiveNameTable* gFontDescTable;
static nsStaticCaseInsensitiveNameTable* gCounterDescTable;
static nsDataHashtable<nsCStringHashKey, nsCSSPropertyID>*
    gPropertyIDLNameTable;

static const char* const kCSSRawFontDescs[] = {
#define CSS_FONT_DESC(name_, method_) #name_,
#include "nsCSSFontDescList.h"
#undef CSS_FONT_DESC
};

static const char* const kCSSRawCounterDescs[] = {
#define CSS_COUNTER_DESC(name_, method_) #name_,
#include "nsCSSCounterDescList.h"
#undef CSS_COUNTER_DESC
};

static nsStaticCaseInsensitiveNameTable* CreateStaticTable(
    const char* const aRawTable[], int32_t aLength) {
  auto table = new nsStaticCaseInsensitiveNameTable(aRawTable, aLength);
#ifdef DEBUG
  // Partially verify the entries.
  for (int32_t index = 0; index < aLength; ++index) {
    nsAutoCString temp(aRawTable[index]);
    MOZ_ASSERT(-1 == temp.FindChar('_'),
               "underscore char in case insensitive name table");
  }
#endif
  return table;
}

void nsCSSProps::AddRefTable(void) {
  if (0 == gPropertyTableRefCount++) {
    MOZ_ASSERT(!gFontDescTable, "pre existing array!");
    MOZ_ASSERT(!gCounterDescTable, "pre existing array!");
    MOZ_ASSERT(!gPropertyIDLNameTable, "pre existing array!");

    gFontDescTable = CreateStaticTable(kCSSRawFontDescs, eCSSFontDesc_COUNT);
    gCounterDescTable =
        CreateStaticTable(kCSSRawCounterDescs, eCSSCounterDesc_COUNT);

    gPropertyIDLNameTable =
        new nsDataHashtable<nsCStringHashKey, nsCSSPropertyID>;
    for (nsCSSPropertyID p = nsCSSPropertyID(0);
         size_t(p) < ArrayLength(kIDLNameTable); p = nsCSSPropertyID(p + 1)) {
      if (kIDLNameTable[p]) {
        gPropertyIDLNameTable->Put(nsDependentCString(kIDLNameTable[p]), p);
      }
    }

    static bool prefObserversInited = false;
    if (!prefObserversInited) {
      prefObserversInited = true;
      for (const PropertyPref* pref = kPropertyPrefTable;
           pref->mPropID != eCSSProperty_UNKNOWN; pref++) {
        nsCString prefName;
        prefName.AssignLiteral(pref->mPref, strlen(pref->mPref));
        bool* enabled = &gPropertyEnabled[pref->mPropID];
        Preferences::AddBoolVarCache(enabled, prefName);
      }
    }
  }
}

#undef DEBUG_SHORTHANDS_CONTAINING

void nsCSSProps::ReleaseTable(void) {
  if (0 == --gPropertyTableRefCount) {
    delete gFontDescTable;
    gFontDescTable = nullptr;

    delete gCounterDescTable;
    gCounterDescTable = nullptr;

    delete gPropertyIDLNameTable;
    gPropertyIDLNameTable = nullptr;
  }
}

/* static */
bool nsCSSProps::IsCustomPropertyName(const nsACString& aProperty) {
  return aProperty.Length() >= CSS_CUSTOM_NAME_PREFIX_LENGTH &&
         StringBeginsWith(aProperty, NS_LITERAL_CSTRING("--"));
}

nsCSSPropertyID nsCSSProps::LookupPropertyByIDLName(
    const nsACString& aPropertyIDLName, EnabledState aEnabled) {
  MOZ_ASSERT(gPropertyIDLNameTable, "no lookup table, needs addref");
  nsCSSPropertyID res;
  if (!gPropertyIDLNameTable->Get(aPropertyIDLName, &res)) {
    return eCSSProperty_UNKNOWN;
  }
  MOZ_ASSERT(res < eCSSProperty_COUNT);
  if (!IsEnabled(res, aEnabled)) {
    return eCSSProperty_UNKNOWN;
  }
  return res;
}

nsCSSFontDesc nsCSSProps::LookupFontDesc(const nsACString& aFontDesc) {
  MOZ_ASSERT(gFontDescTable, "no lookup table, needs addref");
  nsCSSFontDesc which = nsCSSFontDesc(gFontDescTable->Lookup(aFontDesc));

  if (which == eCSSFontDesc_Display &&
      !StaticPrefs::layout_css_font_display_enabled()) {
    which = eCSSFontDesc_UNKNOWN;
  }
  return which;
}

const nsCString& nsCSSProps::GetStringValue(nsCSSFontDesc aFontDescID) {
  MOZ_ASSERT(gFontDescTable, "no lookup table, needs addref");
  if (gFontDescTable) {
    return gFontDescTable->GetStringValue(int32_t(aFontDescID));
  } else {
    static nsDependentCString sNullStr("");
    return sNullStr;
  }
}

const nsCString& nsCSSProps::GetStringValue(nsCSSCounterDesc aCounterDesc) {
  MOZ_ASSERT(gCounterDescTable, "no lookup table, needs addref");
  if (gCounterDescTable) {
    return gCounterDescTable->GetStringValue(int32_t(aCounterDesc));
  } else {
    static nsDependentCString sNullStr("");
    return sNullStr;
  }
}

/***************************************************************************/

const KTableEntry nsCSSProps::kFontSmoothingKTable[] = {
    {eCSSKeyword_auto, NS_FONT_SMOOTHING_AUTO},
    {eCSSKeyword_grayscale, NS_FONT_SMOOTHING_GRAYSCALE},
    {eCSSKeyword_UNKNOWN, nsCSSKTableEntry::SENTINEL_VALUE},
};

const KTableEntry nsCSSProps::kTextAlignKTable[] = {
    {eCSSKeyword_left, NS_STYLE_TEXT_ALIGN_LEFT},
    {eCSSKeyword_right, NS_STYLE_TEXT_ALIGN_RIGHT},
    {eCSSKeyword_center, NS_STYLE_TEXT_ALIGN_CENTER},
    {eCSSKeyword_justify, NS_STYLE_TEXT_ALIGN_JUSTIFY},
    {eCSSKeyword__moz_center, NS_STYLE_TEXT_ALIGN_MOZ_CENTER},
    {eCSSKeyword__moz_right, NS_STYLE_TEXT_ALIGN_MOZ_RIGHT},
    {eCSSKeyword__moz_left, NS_STYLE_TEXT_ALIGN_MOZ_LEFT},
    {eCSSKeyword_start, NS_STYLE_TEXT_ALIGN_START},
    {eCSSKeyword_end, NS_STYLE_TEXT_ALIGN_END},
    {eCSSKeyword_UNKNOWN, nsCSSKTableEntry::SENTINEL_VALUE},
};

const KTableEntry nsCSSProps::kTextDecorationStyleKTable[] = {
    {eCSSKeyword__moz_none, NS_STYLE_TEXT_DECORATION_STYLE_NONE},
    {eCSSKeyword_solid, NS_STYLE_TEXT_DECORATION_STYLE_SOLID},
    {eCSSKeyword_double, NS_STYLE_TEXT_DECORATION_STYLE_DOUBLE},
    {eCSSKeyword_dotted, NS_STYLE_TEXT_DECORATION_STYLE_DOTTED},
    {eCSSKeyword_dashed, NS_STYLE_TEXT_DECORATION_STYLE_DASHED},
    {eCSSKeyword_wavy, NS_STYLE_TEXT_DECORATION_STYLE_WAVY},
    {eCSSKeyword_UNKNOWN, nsCSSKTableEntry::SENTINEL_VALUE},
};

int32_t nsCSSProps::FindIndexOfKeyword(nsCSSKeyword aKeyword,
                                       const KTableEntry aTable[]) {
  if (eCSSKeyword_UNKNOWN == aKeyword) {
    // NOTE: we can have keyword tables where eCSSKeyword_UNKNOWN is used
    // not only for the sentinel, but also in the middle of the table to
    // knock out values that have been disabled by prefs, e.g. kDisplayKTable.
    // So we deal with eCSSKeyword_UNKNOWN up front to avoid returning a valid
    // index in the loop below.
    return -1;
  }
  for (int32_t i = 0;; ++i) {
    const KTableEntry& entry = aTable[i];
    if (entry.IsSentinel()) {
      break;
    }
    if (aKeyword == entry.mKeyword) {
      return i;
    }
  }
  return -1;
}

bool nsCSSProps::FindKeyword(nsCSSKeyword aKeyword, const KTableEntry aTable[],
                             int32_t& aResult) {
  int32_t index = FindIndexOfKeyword(aKeyword, aTable);
  if (index >= 0) {
    aResult = aTable[index].mValue;
    return true;
  }
  return false;
}

nsCSSKeyword nsCSSProps::ValueToKeywordEnum(int32_t aValue,
                                            const KTableEntry aTable[]) {
#ifdef DEBUG
  typedef decltype(aTable[0].mValue) table_value_type;
  NS_ASSERTION(table_value_type(aValue) == aValue, "Value out of range");
#endif
  for (int32_t i = 0;; ++i) {
    const KTableEntry& entry = aTable[i];
    if (entry.IsSentinel()) {
      break;
    }
    if (aValue == entry.mValue) {
      return entry.mKeyword;
    }
  }
  return eCSSKeyword_UNKNOWN;
}

const nsCString& nsCSSProps::ValueToKeyword(int32_t aValue,
                                            const KTableEntry aTable[]) {
  nsCSSKeyword keyword = ValueToKeywordEnum(aValue, aTable);
  if (keyword == eCSSKeyword_UNKNOWN) {
    static nsDependentCString sNullStr("");
    return sNullStr;
  } else {
    return nsCSSKeywords::GetStringValue(keyword);
  }
}

const CSSPropFlags nsCSSProps::kFlagsTable[eCSSProperty_COUNT] = {
#define CSS_PROP_LONGHAND(name_, id_, method_, flags_, ...) flags_,
#define CSS_PROP_SHORTHAND(name_, id_, method_, flags_, ...) flags_,
#include "mozilla/ServoCSSPropList.h"
#undef CSS_PROP_SHORTHAND
#undef CSS_PROP_LONGHAND
};

/* static */
bool nsCSSProps::gPropertyEnabled[eCSSProperty_COUNT_with_aliases] = {
// If the property has any "ENABLED_IN" flag set, it is disabled by
// default. Note that, if a property has pref, whatever its default
// value is, it will later be changed in nsCSSProps::AddRefTable().
// If the property has "ENABLED_IN" flags but doesn't have a pref,
// it is an internal property which is disabled elsewhere.
#define IS_ENABLED_BY_DEFAULT(flags_) \
  (!((flags_) & (CSSPropFlags::EnabledMask | CSSPropFlags::Inaccessible)))

#define CSS_PROP_LONGHAND(name_, id_, method_, flags_, ...) \
  IS_ENABLED_BY_DEFAULT(flags_),
#define CSS_PROP_SHORTHAND(name_, id_, method_, flags_, ...) \
  IS_ENABLED_BY_DEFAULT(flags_),
#define CSS_PROP_ALIAS(...) true,
#include "mozilla/ServoCSSPropList.h"
#undef CSS_PROP_ALIAS
#undef CSS_PROP_SHORTHAND
#undef CSS_PROP_LONGHAND

#undef IS_ENABLED_BY_DEFAULT
};

#include "nsCSSPropsGenerated.inc"
