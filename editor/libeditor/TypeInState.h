/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_TypeInState_h
#define mozilla_TypeInState_h

#include "mozilla/EditorDOMPoint.h"
#include "mozilla/UniquePtr.h"
#include "nsCOMPtr.h"
#include "nsCycleCollectionParticipant.h"
#include "nsGkAtoms.h"
#include "nsISupportsImpl.h"
#include "nsString.h"
#include "nsTArray.h"
#include "nscore.h"

// Workaround for windows headers
#ifdef SetProp
#  undef SetProp
#endif

class nsAtom;
class nsINode;

namespace mozilla {
namespace dom {
class Selection;
}  // namespace dom

struct PropItem {
  nsAtom* tag;
  nsAtom* attr;
  nsString value;

  PropItem() : tag(nullptr), attr(nullptr) { MOZ_COUNT_CTOR(PropItem); }
  PropItem(nsAtom* aTag, nsAtom* aAttr, const nsAString& aValue)
      : tag(aTag),
        attr(aAttr != nsGkAtoms::_empty ? aAttr : nullptr),
        value(aValue) {
    MOZ_COUNT_CTOR(PropItem);
  }
  ~PropItem() { MOZ_COUNT_DTOR(PropItem); }
};

struct MOZ_STACK_CLASS StyleCache final {
  nsAtom* mTag;
  nsAtom* mAttr;
  nsString mValue;
  bool mPresent;

  StyleCache() : mTag(nullptr), mAttr(nullptr), mPresent(false) {}
  StyleCache(nsAtom* aTag, nsAtom* aAttr)
      : mTag(aTag), mAttr(aAttr), mPresent(false) {}

  inline void Clear() {
    mPresent = false;
    mValue.Truncate();
  }
};

class MOZ_STACK_CLASS AutoStyleCacheArray final
    : public AutoTArray<StyleCache, 19> {
 public:
  AutoStyleCacheArray()
      : AutoTArray<StyleCache, 19>(
            {StyleCache(nsGkAtoms::b, nullptr),
             StyleCache(nsGkAtoms::i, nullptr),
             StyleCache(nsGkAtoms::u, nullptr),
             StyleCache(nsGkAtoms::font, nsGkAtoms::face),
             StyleCache(nsGkAtoms::font, nsGkAtoms::size),
             StyleCache(nsGkAtoms::font, nsGkAtoms::color),
             StyleCache(nsGkAtoms::tt, nullptr),
             StyleCache(nsGkAtoms::em, nullptr),
             StyleCache(nsGkAtoms::strong, nullptr),
             StyleCache(nsGkAtoms::dfn, nullptr),
             StyleCache(nsGkAtoms::code, nullptr),
             StyleCache(nsGkAtoms::samp, nullptr),
             StyleCache(nsGkAtoms::var, nullptr),
             StyleCache(nsGkAtoms::cite, nullptr),
             StyleCache(nsGkAtoms::abbr, nullptr),
             StyleCache(nsGkAtoms::acronym, nullptr),
             StyleCache(nsGkAtoms::backgroundColor, nullptr),
             StyleCache(nsGkAtoms::sub, nullptr),
             StyleCache(nsGkAtoms::sup, nullptr)}) {}

  void Clear() {
    for (auto& styleCache : *this) {
      styleCache.Clear();
    }
  }
};

class TypeInState final {
 public:
  NS_INLINE_DECL_CYCLE_COLLECTING_NATIVE_REFCOUNTING(TypeInState)
  NS_DECL_CYCLE_COLLECTION_NATIVE_CLASS(TypeInState)

  TypeInState();
  void Reset();

  nsresult UpdateSelState(dom::Selection* aSelection);

  void OnSelectionChange(dom::Selection& aSelection);

  void SetProp(nsAtom* aProp, nsAtom* aAttr, const nsAString& aValue);

  void ClearAllProps();
  void ClearProp(nsAtom* aProp, nsAtom* aAttr);

  /**
   * TakeClearProperty() hands back next property item on the clear list.
   * Caller assumes ownership of PropItem and must delete it.
   */
  UniquePtr<PropItem> TakeClearProperty();

  /**
   * TakeSetProperty() hands back next property item on the set list.
   * Caller assumes ownership of PropItem and must delete it.
   */
  UniquePtr<PropItem> TakeSetProperty();

  /**
   * TakeRelativeFontSize() hands back relative font value, which is then
   * cleared out.
   */
  int32_t TakeRelativeFontSize();

  void GetTypingState(bool& isSet, bool& theSetting, nsAtom* aProp,
                      nsAtom* aAttr = nullptr, nsString* outValue = nullptr);

  static bool FindPropInList(nsAtom* aProp, nsAtom* aAttr, nsAString* outValue,
                             nsTArray<PropItem*>& aList, int32_t& outIndex);

 protected:
  virtual ~TypeInState();

  void RemovePropFromSetList(nsAtom* aProp, nsAtom* aAttr);
  void RemovePropFromClearedList(nsAtom* aProp, nsAtom* aAttr);
  bool IsPropSet(nsAtom* aProp, nsAtom* aAttr, nsAString* outValue);
  bool IsPropSet(nsAtom* aProp, nsAtom* aAttr, nsAString* outValue,
                 int32_t& outIndex);
  bool IsPropCleared(nsAtom* aProp, nsAtom* aAttr);
  bool IsPropCleared(nsAtom* aProp, nsAtom* aAttr, int32_t& outIndex);

  nsTArray<PropItem*> mSetArray;
  nsTArray<PropItem*> mClearedArray;
  EditorDOMPoint mLastSelectionPoint;
  int32_t mRelativeFontSize;
};

}  // namespace mozilla

#endif  // #ifndef mozilla_TypeInState_h
