/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_ServoStyleSheet_h
#define mozilla_ServoStyleSheet_h

#include "mozilla/dom/SRIMetadata.h"
#include "mozilla/RefPtr.h"
#include "mozilla/ServoBindingTypes.h"
#include "mozilla/StyleSheet.h"
#include "mozilla/StyleSheetInfo.h"
#include "nsStringFwd.h"

namespace mozilla {

class ServoCSSRuleList;

namespace css {
class Loader;
}

// -------------------------------
// Servo Style Sheet Inner Data Container
//

struct ServoStyleSheetInner : public StyleSheetInfo
{
  ServoStyleSheetInner(CORSMode aCORSMode,
                       ReferrerPolicy aReferrerPolicy,
                       const dom::SRIMetadata& aIntegrity);

  RefPtr<RawServoStyleSheet> mSheet;
};


/**
 * CSS style sheet object that is a wrapper for a Servo Stylesheet.
 */
class ServoStyleSheet : public StyleSheet
{
public:
  ServoStyleSheet(css::SheetParsingMode aParsingMode,
                  CORSMode aCORSMode,
                  net::ReferrerPolicy aReferrerPolicy,
                  const dom::SRIMetadata& aIntegrity);

  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(ServoStyleSheet, StyleSheet)

  bool HasRules() const;

  MOZ_MUST_USE nsresult ParseSheet(css::Loader* aLoader,
                                   const nsAString& aInput,
                                   nsIURI* aSheetURI,
                                   nsIURI* aBaseURI,
                                   nsIPrincipal* aSheetPrincipal,
                                   uint32_t aLineNumber);

  /**
   * Called instead of ParseSheet to initialize the Servo stylesheet object
   * for a failed load. Either ParseSheet or LoadFailed must be called before
   * adding a ServoStyleSheet to a ServoStyleSet.
   */
  void LoadFailed();

  RawServoStyleSheet* RawSheet() const {
    return Inner()->mSheet;
  }
  void SetSheetForImport(RawServoStyleSheet* aSheet) {
    MOZ_ASSERT(!Inner()->mSheet);
    Inner()->mSheet = aSheet;
  }

  // WebIDL CSSStyleSheet API
  // Can't be inline because we can't include ImportRule here.  And can't be
  // called GetOwnerRule because that would be ambiguous with the ImportRule
  // version.
  css::Rule* GetDOMOwnerRule() const final;

  void WillDirty() {}
  void DidDirty() {}

  bool IsModified() const final { return false; }

  virtual already_AddRefed<StyleSheet> Clone(StyleSheet* aCloneParent,
    css::ImportRule* aCloneOwnerRule,
    nsIDocument* aCloneDocument,
    nsINode* aCloneOwningNode) const final;

protected:
  virtual ~ServoStyleSheet();

  ServoStyleSheetInner* Inner() const
  {
    return static_cast<ServoStyleSheetInner*>(mInner);
  }

  // Internal methods which do not have security check and completeness check.
  dom::CSSRuleList* GetCssRulesInternal(ErrorResult& aRv);
  uint32_t InsertRuleInternal(const nsAString& aRule,
                              uint32_t aIndex, ErrorResult& aRv);
  void DeleteRuleInternal(uint32_t aIndex, ErrorResult& aRv);

  void EnabledStateChangedInternal() {}

private:
  ServoStyleSheet(const ServoStyleSheet& aCopy,
                  ServoStyleSheet* aParentToUse,
                  css::ImportRule* aOwnerRuleToUse,
                  nsIDocument* aDocumentToUse,
                  nsINode* aOwningNodeToUse);

  void DropRuleList();

  RefPtr<ServoCSSRuleList> mRuleList;

  friend class StyleSheet;
};

} // namespace mozilla

#endif // mozilla_ServoStyleSheet_h
