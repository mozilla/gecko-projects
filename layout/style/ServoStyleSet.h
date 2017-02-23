/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_ServoStyleSet_h
#define mozilla_ServoStyleSet_h

#include "mozilla/EnumeratedArray.h"
#include "mozilla/EventStates.h"
#include "mozilla/ServoBindingTypes.h"
#include "mozilla/ServoElementSnapshot.h"
#include "mozilla/StyleSheetInlines.h"
#include "mozilla/SheetType.h"
#include "mozilla/UniquePtr.h"
#include "nsCSSPseudoElements.h"
#include "nsChangeHint.h"
#include "nsIAtom.h"
#include "nsTArray.h"

namespace mozilla {
namespace dom {
class Element;
} // namespace dom
class CSSStyleSheet;
class ServoRestyleManager;
class ServoStyleSheet;
struct Keyframe;
} // namespace mozilla
class nsIDocument;
class nsStyleContext;
class nsPresContext;
struct nsTimingFunction;
struct TreeMatchContext;

namespace mozilla {

/**
 * The set of style sheets that apply to a document, backed by a Servo
 * Stylist.  A ServoStyleSet contains ServoStyleSheets.
 */
class ServoStyleSet
{
  friend class ServoRestyleManager;
public:
  static bool IsInServoTraversal(bool aAssertServoTraversalOrMainThread = true)
  {
    // The callers of this function are generally main-thread-only _except_
    // for potentially running during the Servo traversal, in which case they may
    // take special paths that avoid writing to caches and the like. In order
    // to allow those callers to branch efficiently without checking TLS, we
    // maintain this static boolean. However, the danger is that those callers
    // are generally unprepared to deal with non-Servo-but-also-non-main-thread
    // callers, and are likely to take the main-thread codepath if this function
    // returns false. So we assert against other non-main-thread callers here.
    MOZ_ASSERT_IF(aAssertServoTraversalOrMainThread,
                  sInServoTraversal || NS_IsMainThread());
    return sInServoTraversal;
  }

  ServoStyleSet();

  void Init(nsPresContext* aPresContext);
  void BeginShutdown();
  void Shutdown();

  bool GetAuthorStyleDisabled() const;
  nsresult SetAuthorStyleDisabled(bool aStyleDisabled);

  void BeginUpdate();
  nsresult EndUpdate();

  already_AddRefed<nsStyleContext>
  ResolveStyleFor(dom::Element* aElement,
                  nsStyleContext* aParentContext,
                  LazyComputeBehavior aMayCompute);

  already_AddRefed<nsStyleContext>
  ResolveStyleFor(dom::Element* aElement,
                  nsStyleContext* aParentContext,
                  LazyComputeBehavior aMayCompute,
                  TreeMatchContext& aTreeMatchContext);

  already_AddRefed<nsStyleContext>
  ResolveStyleForText(nsIContent* aTextNode,
                      nsStyleContext* aParentContext);

  already_AddRefed<nsStyleContext>
  ResolveStyleForOtherNonElement(nsStyleContext* aParentContext);

  already_AddRefed<nsStyleContext>
  ResolvePseudoElementStyle(dom::Element* aOriginatingElement,
                            mozilla::CSSPseudoElementType aType,
                            nsStyleContext* aParentContext,
                            dom::Element* aPseudoElement);

  // Resolves style for a (possibly-pseudo) Element without assuming that the
  // style has been resolved, and without worrying about setting the style
  // context up to live in the style context tree (a null parent is used).
  already_AddRefed<nsStyleContext>
  ResolveTransientStyle(dom::Element* aElement,
                        mozilla::CSSPseudoElementType aPseudoType);

  // aFlags is an nsStyleSet flags bitfield
  already_AddRefed<nsStyleContext>
  ResolveAnonymousBoxStyle(nsIAtom* aPseudoTag, nsStyleContext* aParentContext,
                           uint32_t aFlags = 0);

  // manage the set of style sheets in the style set
  nsresult AppendStyleSheet(SheetType aType, ServoStyleSheet* aSheet);
  nsresult PrependStyleSheet(SheetType aType, ServoStyleSheet* aSheet);
  nsresult RemoveStyleSheet(SheetType aType, ServoStyleSheet* aSheet);
  nsresult ReplaceSheets(SheetType aType,
                         const nsTArray<RefPtr<ServoStyleSheet>>& aNewSheets);
  nsresult InsertStyleSheetBefore(SheetType aType,
                                  ServoStyleSheet* aNewSheet,
                                  ServoStyleSheet* aReferenceSheet);

  int32_t SheetCount(SheetType aType) const;
  ServoStyleSheet* StyleSheetAt(SheetType aType, int32_t aIndex) const;

  nsresult RemoveDocStyleSheet(ServoStyleSheet* aSheet);
  nsresult AddDocStyleSheet(ServoStyleSheet* aSheet, nsIDocument* aDocument);

  // check whether there is ::before/::after style for an element
  already_AddRefed<nsStyleContext>
  ProbePseudoElementStyle(dom::Element* aOriginatingElement,
                          mozilla::CSSPseudoElementType aType,
                          nsStyleContext* aParentContext);

  already_AddRefed<nsStyleContext>
  ProbePseudoElementStyle(dom::Element* aOriginatingElement,
                          mozilla::CSSPseudoElementType aType,
                          nsStyleContext* aParentContext,
                          TreeMatchContext& aTreeMatchContext,
                          dom::Element* aPseudoElement = nullptr);

  // Test if style is dependent on content state
  nsRestyleHint HasStateDependentStyle(dom::Element* aElement,
                                       EventStates aStateMask);
  nsRestyleHint HasStateDependentStyle(
    dom::Element* aElement, mozilla::CSSPseudoElementType aPseudoType,
    dom::Element* aPseudoElement, EventStates aStateMask);

  /**
   * Performs a Servo traversal to compute style for all dirty nodes in the
   * document.  This will traverse all of the document's style roots (that
   * is, its document element, and the roots of the document-level native
   * anonymous content).  Returns true if a post-traversal is required.
   */
  bool StyleDocument();

  /**
   * Eagerly styles a subtree of unstyled nodes that was just appended to the
   * tree. This is used in situations where we need the style immediately and
   * cannot wait for a future batch restyle.
   */
  void StyleNewSubtree(Element* aRoot);

  /**
   * Like the above, but skips the root node, and only styles unstyled children.
   * When potentially appending multiple children, it's preferable to call
   * StyleNewChildren on the node rather than making multiple calls to
   * StyleNewSubtree on each child, since it allows for more parallelism.
   */
  void StyleNewChildren(Element* aParent);

  /**
   * Records that the contents of style sheets have changed since the last
   * restyle.  Calling this will ensure that the Stylist rebuilds its
   * selector maps.
   */
  void NoteStyleSheetsChanged();

#ifdef DEBUG
  void AssertTreeIsClean();
#else
  void AssertTreeIsClean() {}
#endif

  /**
   * Rebuild the style data. This will force a stylesheet flush, and also
   * recompute the default computed styles.
   */
  void RebuildData();

  /**
   * Resolve style for the given element, and return it as a
   * ServoComputedValues, not an nsStyleContext.
   */
  already_AddRefed<ServoComputedValues> ResolveServoStyle(dom::Element* aElement);

  bool FillKeyframesForName(const nsString& aName,
                            const nsTimingFunction& aTimingFunction,
                            const ServoComputedValues* aComputedValues,
                            nsTArray<Keyframe>& aKeyframes);

private:
  already_AddRefed<nsStyleContext> GetContext(already_AddRefed<ServoComputedValues>,
                                              nsStyleContext* aParentContext,
                                              nsIAtom* aPseudoTag,
                                              CSSPseudoElementType aPseudoType,
                                              dom::Element* aElementForAnimation);

  already_AddRefed<nsStyleContext> GetContext(nsIContent* aContent,
                                              nsStyleContext* aParentContext,
                                              nsIAtom* aPseudoTag,
                                              CSSPseudoElementType aPseudoType,
                                              LazyComputeBehavior aMayCompute);

  /**
   * Resolve all ServoDeclarationBlocks attached to mapped
   * presentation attributes cached on the document.
   * Call this before jumping into Servo's style system.
   */
  void ResolveMappedAttrDeclarationBlocks();

  /**
   * Perform all lazy operations required before traversing
   * a subtree.  Returns whether a post-traversal is required.
   */
  bool PrepareAndTraverseSubtree(RawGeckoElementBorrowed aRoot,
                                 mozilla::TraversalRootBehavior aRootBehavior);

  nsPresContext* mPresContext;
  UniquePtr<RawServoStyleSet> mRawSet;
  EnumeratedArray<SheetType, SheetType::Count,
                  nsTArray<RefPtr<ServoStyleSheet>>> mSheets;
  int32_t mBatching;

  static bool sInServoTraversal;
};

} // namespace mozilla

#endif // mozilla_ServoStyleSet_h
