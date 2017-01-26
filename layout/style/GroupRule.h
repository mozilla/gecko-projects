/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * internal interface representing CSS style rules that contain other
 * rules, such as @media rules
 */

#ifndef mozilla_css_GroupRule_h__
#define mozilla_css_GroupRule_h__

#include "mozilla/Attributes.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/IncrementalClearCOMRuleArray.h"
#include "mozilla/MemoryReporting.h"
#include "mozilla/css/Rule.h"
#include "nsCycleCollectionParticipant.h"

class nsPresContext;
class nsMediaQueryResultCacheKey;

namespace mozilla {

class StyleSheet;

namespace dom {
class CSSRuleList;
} // namespace dom

namespace css {

class GroupRuleRuleList;

// inherits from Rule so it can be shared between
// MediaRule and DocumentRule
class GroupRule : public Rule
{
protected:
  GroupRule(uint32_t aLineNumber, uint32_t aColumnNumber);
  GroupRule(const GroupRule& aCopy);
  virtual ~GroupRule();
public:

  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(GroupRule, Rule)
  NS_DECL_ISUPPORTS_INHERITED
  virtual bool IsCCLeaf() const override;

#ifdef DEBUG
  virtual void List(FILE* out = stdout, int32_t aIndent = 0) const override;
#endif
  virtual void SetStyleSheet(StyleSheet* aSheet) override;

public:
  void AppendStyleRule(Rule* aRule);

  int32_t StyleRuleCount() const { return mRules.Count(); }
  Rule* GetStyleRuleAt(int32_t aIndex) const;

  typedef IncrementalClearCOMRuleArray::nsCOMArrayEnumFunc RuleEnumFunc;
  bool EnumerateRulesForwards(RuleEnumFunc aFunc, void * aData) const;

  /*
   * The next three methods should never be called unless you have first
   * called WillDirty() on the parent stylesheet.  After they are
   * called, DidDirty() needs to be called on the sheet.
   */
  nsresult DeleteStyleRuleAt(uint32_t aIndex);
  nsresult InsertStyleRuleAt(uint32_t aIndex, Rule* aRule);

  virtual bool UseForPresentation(nsPresContext* aPresContext,
                                    nsMediaQueryResultCacheKey& aKey) = 0;

  // non-virtual -- it is only called by subclasses
  size_t SizeOfExcludingThis(mozilla::MallocSizeOf aMallocSizeOf) const;
  virtual size_t SizeOfIncludingThis(mozilla::MallocSizeOf aMallocSizeOf) const override = 0;

  static bool
  CloneRuleInto(Rule* aRule, void* aArray)
  {
    RefPtr<Rule> clone = aRule->Clone();
    static_cast<IncrementalClearCOMRuleArray*>(aArray)->AppendObject(clone);
    return true;
  }

  // WebIDL API
  dom::CSSRuleList* CssRules();
  uint32_t InsertRule(const nsAString& aRule, uint32_t aIndex,
                      ErrorResult& aRv);
  void DeleteRule(uint32_t aIndex, ErrorResult& aRv);

protected:
  // to help implement nsIDOMCSSRule
  void AppendRulesToCssText(nsAString& aCssText) const;

  // to implement common methods on nsIDOMCSSMediaRule and
  // nsIDOMCSSMozDocumentRule
  nsresult GetCssRules(nsIDOMCSSRuleList* *aRuleList);
  nsresult InsertRule(const nsAString & aRule, uint32_t aIndex,
                      uint32_t* _retval);
  nsresult DeleteRule(uint32_t aIndex);

  IncrementalClearCOMRuleArray mRules;
  RefPtr<GroupRuleRuleList> mRuleCollection; // lazily constructed
};

// Implementation of WebIDL CSSConditionRule.
class ConditionRule : public GroupRule
{
protected:
  ConditionRule(uint32_t aLineNumber, uint32_t aColumnNumber);
  ConditionRule(const ConditionRule& aCopy);
  virtual ~ConditionRule();

public:

  // GetConditionText signature matches nsIDOMCSSConditionRule, so subclasses
  // can implement this easily.  The implementations should never return
  // anything other than NS_OK.
  NS_IMETHOD GetConditionText(nsAString& aConditionText) = 0;
  virtual void SetConditionText(const nsAString& aConditionText,
                                ErrorResult& aRv) = 0;
};

} // namespace css
} // namespace mozilla

#endif /* mozilla_css_GroupRule_h__ */
