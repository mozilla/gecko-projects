/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/ServoStyleSet.h"

#include "mozilla/DocumentStyleRootIterator.h"
#include "mozilla/ServoRestyleManager.h"
#include "mozilla/dom/AnonymousContent.h"
#include "mozilla/dom/ChildIterator.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/ElementInlines.h"
#include "mozilla/dom/KeyframeEffectReadOnly.h"
#include "nsCSSAnonBoxes.h"
#include "nsCSSPseudoElements.h"
#include "nsHTMLStyleSheet.h"
#include "nsIDocumentInlines.h"
#include "nsPrintfCString.h"
#include "nsStyleContext.h"
#include "nsStyleSet.h"

using namespace mozilla;
using namespace mozilla::dom;

ServoStyleSet::ServoStyleSet()
  : mPresContext(nullptr)
  , mBatching(0)
{
}

ServoStyleSet::~ServoStyleSet()
{
}

void
ServoStyleSet::Init(nsPresContext* aPresContext)
{
  mPresContext = aPresContext;
  mRawSet.reset(Servo_StyleSet_Init(aPresContext));

  // Now that we have an mRawSet, go ahead and notify about whatever stylesheets
  // we have so far.
  for (auto& sheetArray : mSheets) {
    for (auto& sheet : sheetArray) {
      // There's no guarantee this will create a list on the servo side whose
      // ordering matches the list that would have been created had all those
      // sheets been appended/prepended/etc after we had mRawSet.  But hopefully
      // that's OK (e.g. because servo doesn't care about the relative ordering
      // of sheets from different cascade levels in the list?).
      MOZ_ASSERT(sheet->RawSheet(), "We should only append non-null raw sheets.");
      Servo_StyleSet_AppendStyleSheet(mRawSet.get(), sheet->RawSheet(), false);
    }
  }

  // No need to Servo_StyleSet_FlushStyleSheets because we just created the
  // mRawSet, so there was nothing to flush.
}

void
ServoStyleSet::BeginShutdown()
{
  nsIDocument* doc = mPresContext->Document();

  // It's important to do this before mRawSet is released, since that will cause
  // a RuleTree GC, which needs to happen after we have dropped all of the
  // document's strong references to RuleNodes.  We also need to do it here,
  // in BeginShutdown, and not in Shutdown, since Shutdown happens after the
  // frame tree has been destroyed, but before the script runners that delete
  // native anonymous content (which also could be holding on the RuleNodes)
  // have run.  By clearing style here, before the frame tree is destroyed,
  // the AllChildrenIterator will find the anonymous content.
  //
  // Note that this is pretty bad for performance; we should find a way to
  // get by with the ServoNodeDatas being dropped as part of the document
  // going away.
  DocumentStyleRootIterator iter(doc);
  while (Element* root = iter.GetNextStyleRoot()) {
    ServoRestyleManager::ClearServoDataFromSubtree(root);
  }

  // We can also have some cloned canvas custom content stored in the document
  // (as done in nsCanvasFrame::DestroyFrom), due to bug 1348480, when we create
  // the clone (wastefully) during PresShell destruction.  Clear data from that
  // clone.
  for (RefPtr<AnonymousContent>& ac : doc->GetAnonymousContents()) {
    ServoRestyleManager::ClearServoDataFromSubtree(ac->GetContentNode());
  }
}

void
ServoStyleSet::Shutdown()
{
  // Make sure we drop our cached style contexts before the presshell arena
  // starts going away.
  ClearNonInheritingStyleContexts();
  mRawSet = nullptr;
}

bool
ServoStyleSet::GetAuthorStyleDisabled() const
{
  return false;
}

nsresult
ServoStyleSet::SetAuthorStyleDisabled(bool aStyleDisabled)
{
  MOZ_CRASH("stylo: not implemented");
}

void
ServoStyleSet::BeginUpdate()
{
  ++mBatching;
}

nsresult
ServoStyleSet::EndUpdate()
{
  MOZ_ASSERT(mBatching > 0);
  if (--mBatching > 0) {
    return NS_OK;
  }

  Servo_StyleSet_FlushStyleSheets(mRawSet.get());
  return NS_OK;
}

already_AddRefed<nsStyleContext>
ServoStyleSet::ResolveStyleFor(Element* aElement,
                               nsStyleContext* aParentContext,
                               LazyComputeBehavior aMayCompute)
{
  return GetContext(aElement, aParentContext, nullptr,
                    CSSPseudoElementType::NotPseudo, aMayCompute);
}

already_AddRefed<nsStyleContext>
ServoStyleSet::GetContext(nsIContent* aContent,
                          nsStyleContext* aParentContext,
                          nsIAtom* aPseudoTag,
                          CSSPseudoElementType aPseudoType,
                          LazyComputeBehavior aMayCompute)
{
  MOZ_ASSERT(aContent->IsElement());
  Element* element = aContent->AsElement();


  PreTraverseSync();
  RefPtr<ServoComputedValues> computedValues;
  if (aMayCompute == LazyComputeBehavior::Allow) {
    computedValues = ResolveStyleLazily(element, nullptr);
  } else {
    computedValues = ResolveServoStyle(element);
  }

  MOZ_ASSERT(computedValues);
  return GetContext(computedValues.forget(), aParentContext, aPseudoTag, aPseudoType,
                    element);
}

already_AddRefed<nsStyleContext>
ServoStyleSet::GetContext(already_AddRefed<ServoComputedValues> aComputedValues,
                          nsStyleContext* aParentContext,
                          nsIAtom* aPseudoTag,
                          CSSPseudoElementType aPseudoType,
                          Element* aElementForAnimation)
{
  // XXXbholley: nsStyleSet does visited handling here.

  // XXXbholley: Figure out the correct thing to pass here. Does this fixup
  // duplicate something that servo already does?
  // See bug 1344914.
  bool skipFixup = false;

  return NS_NewStyleContext(aParentContext, mPresContext, aPseudoTag,
                            aPseudoType, Move(aComputedValues), skipFixup);
}

void
ServoStyleSet::ResolveMappedAttrDeclarationBlocks()
{
  if (nsHTMLStyleSheet* sheet = mPresContext->Document()->GetAttributeStyleSheet()) {
    sheet->CalculateMappedServoDeclarations();
  }

  mPresContext->Document()->ResolveScheduledSVGPresAttrs();
}

void
ServoStyleSet::PreTraverseSync()
{
  ResolveMappedAttrDeclarationBlocks();

  // This is lazily computed and pseudo matching needs to access
  // it so force computation early.
  mPresContext->Document()->GetDocumentState();
}

void
ServoStyleSet::PreTraverse()
{
  PreTraverseSync();

  // Process animation stuff that we should avoid doing during the parallel
  // traversal.
  mPresContext->EffectCompositor()->PreTraverse();
}

bool
ServoStyleSet::PrepareAndTraverseSubtree(RawGeckoElementBorrowed aRoot,
                                         mozilla::TraversalRootBehavior aRootBehavior)
{
  // Get the Document's root element to ensure that the cache is valid before
  // calling into the (potentially-parallel) Servo traversal, where a cache hit
  // is necessary to avoid a data race when updating the cache.
  mozilla::Unused << aRoot->OwnerDoc()->GetRootElement();

  MOZ_ASSERT(!sInServoTraversal);
  sInServoTraversal = true;

  bool isInitial = !aRoot->HasServoData();
  bool postTraversalRequired =
    Servo_TraverseSubtree(aRoot, mRawSet.get(), aRootBehavior);
  MOZ_ASSERT_IF(isInitial, !postTraversalRequired);

  // If there are still animation restyles needed, trigger a second traversal to
  // update CSS animations' styles.
  if (mPresContext->EffectCompositor()->PreTraverse()) {
    if (Servo_TraverseSubtree(aRoot, mRawSet.get(), aRootBehavior)) {
      if (isInitial) {
        // We're doing initial styling, and the additional animation
        // traversal changed the styles that were set by the first traversal.
        // This would normally require a post-traversal to update the style
        // contexts, and the DOM now has dirty descendant bits and RestyleData
        // in expectation of that post-traversal. But since this is actually
        // the initial styling, there are no style contexts to update and no
        // frames to apply the change hints to, so we don't need to do that
        // post-traversal. Instead, just drop this state and tell the caller
        // that no post-traversal is required.
        MOZ_ASSERT(!postTraversalRequired);
        ServoRestyleManager::ClearRestyleStateFromSubtree(const_cast<Element*>(aRoot));
      } else {
        postTraversalRequired = true;
      }
    }
  }

  sInServoTraversal = false;
  return postTraversalRequired;
}

already_AddRefed<nsStyleContext>
ServoStyleSet::ResolveStyleFor(Element* aElement,
                               nsStyleContext* aParentContext,
                               LazyComputeBehavior aMayCompute,
                               TreeMatchContext& aTreeMatchContext)
{
  // aTreeMatchContext is used to speed up selector matching,
  // but if the element already has a ServoComputedValues computed in
  // advance, then we shouldn't need to use it.
  return ResolveStyleFor(aElement, aParentContext, aMayCompute);
}

already_AddRefed<nsStyleContext>
ServoStyleSet::ResolveStyleForText(nsIContent* aTextNode,
                                   nsStyleContext* aParentContext)
{
  MOZ_ASSERT(aTextNode && aTextNode->IsNodeOfType(nsINode::eTEXT));
  MOZ_ASSERT(aTextNode->GetParent());
  MOZ_ASSERT(aParentContext);

  // Gecko expects text node style contexts to be like elements that match no
  // rules: inherit the inherit structs, reset the reset structs. This is cheap
  // enough to do on the main thread, which means that the parallel style system
  // can avoid worrying about text nodes.
  const ServoComputedValues* parentComputedValues =
    aParentContext->StyleSource().AsServoComputedValues();
  RefPtr<ServoComputedValues> computedValues =
    Servo_ComputedValues_Inherit(mRawSet.get(), parentComputedValues).Consume();

  return GetContext(computedValues.forget(), aParentContext,
                    nsCSSAnonBoxes::mozText,
                    CSSPseudoElementType::InheritingAnonBox,
                    nullptr);
}

already_AddRefed<nsStyleContext>
ServoStyleSet::ResolveStyleForFirstLetterContinuation(nsStyleContext* aParentContext)
{
  const ServoComputedValues* parent = aParentContext->StyleSource().AsServoComputedValues();
  RefPtr<ServoComputedValues> computedValues =
    Servo_ComputedValues_Inherit(mRawSet.get(), parent).Consume();
  MOZ_ASSERT(computedValues);

  return GetContext(computedValues.forget(), aParentContext,
                    nsCSSAnonBoxes::firstLetterContinuation,
                    CSSPseudoElementType::InheritingAnonBox,
                    nullptr);
}

already_AddRefed<nsStyleContext>
ServoStyleSet::ResolveStyleForPlaceholder()
{
  RefPtr<nsStyleContext>& cache =
    mNonInheritingStyleContexts[nsCSSAnonBoxes::NonInheriting::oofPlaceholder];
  if (cache) {
    RefPtr<nsStyleContext> retval = cache;
    return retval.forget();
  }

  RefPtr<ServoComputedValues> computedValues =
    Servo_ComputedValues_Inherit(mRawSet.get(), nullptr).Consume();
  MOZ_ASSERT(computedValues);

  RefPtr<nsStyleContext> retval =
    GetContext(computedValues.forget(), nullptr,
               nsCSSAnonBoxes::oofPlaceholder,
               CSSPseudoElementType::NonInheritingAnonBox,
               nullptr);
  cache = retval;
  return retval.forget();
}

already_AddRefed<nsStyleContext>
ServoStyleSet::ResolvePseudoElementStyle(Element* aOriginatingElement,
                                         CSSPseudoElementType aType,
                                         nsStyleContext* aParentContext,
                                         Element* aPseudoElement)
{
  if (aPseudoElement) {
    NS_WARNING("stylo: We don't support CSS_PSEUDO_ELEMENT_SUPPORTS_USER_ACTION_STATE yet");
  }

  // NB: We ignore aParentContext, on the assumption that pseudo element styles
  // should just inherit from aOriginatingElement's primary style, which Servo
  // already knows.
  MOZ_ASSERT(aType < CSSPseudoElementType::Count);
  nsIAtom* pseudoTag = nsCSSPseudoElements::GetPseudoAtom(aType);

  RefPtr<ServoComputedValues> computedValues =
    Servo_ResolvePseudoStyle(aOriginatingElement, pseudoTag,
                             /* is_probe = */ false, mRawSet.get()).Consume();
  MOZ_ASSERT(computedValues);

  bool isBeforeOrAfter = aType == CSSPseudoElementType::before ||
                         aType == CSSPseudoElementType::after;
  return GetContext(computedValues.forget(), aParentContext, pseudoTag, aType,
                    isBeforeOrAfter ? aOriginatingElement : nullptr);
}

already_AddRefed<nsStyleContext>
ServoStyleSet::ResolveTransientStyle(Element* aElement, CSSPseudoElementType aType)
{
  nsIAtom* pseudoTag = nullptr;
  if (aType != CSSPseudoElementType::NotPseudo) {
    pseudoTag = nsCSSPseudoElements::GetPseudoAtom(aType);
  }

  RefPtr<ServoComputedValues> computedValues =
    ResolveStyleLazily(aElement, pseudoTag);

  return GetContext(computedValues.forget(), nullptr, pseudoTag, aType,
                    nullptr);
}

// aFlags is an nsStyleSet flags bitfield
already_AddRefed<nsStyleContext>
ServoStyleSet::ResolveInheritingAnonymousBoxStyle(nsIAtom* aPseudoTag,
                                                  nsStyleContext* aParentContext,
                                                  uint32_t aFlags)
{
  MOZ_ASSERT(nsCSSAnonBoxes::IsAnonBox(aPseudoTag) &&
             !nsCSSAnonBoxes::IsNonInheritingAnonBox(aPseudoTag));

  MOZ_ASSERT(aFlags == 0 ||
             aFlags == nsStyleSet::eSkipParentDisplayBasedStyleFixup);
  bool skipFixup = aFlags & nsStyleSet::eSkipParentDisplayBasedStyleFixup;

  const ServoComputedValues* parentStyle =
    aParentContext ? aParentContext->StyleSource().AsServoComputedValues()
                   : nullptr;
  RefPtr<ServoComputedValues> computedValues =
    Servo_ComputedValues_GetForAnonymousBox(parentStyle, aPseudoTag, skipFixup,
                                            mRawSet.get()).Consume();
#ifdef DEBUG
  if (!computedValues) {
    nsString pseudo;
    aPseudoTag->ToString(pseudo);
    NS_ERROR(nsPrintfCString("stylo: could not get anon-box: %s",
             NS_ConvertUTF16toUTF8(pseudo).get()).get());
    MOZ_CRASH();
  }
#endif

  // FIXME(bz, bug 1344914) We should really GetContext here and make skipFixup
  // work there.
  return NS_NewStyleContext(aParentContext, mPresContext, aPseudoTag,
                            CSSPseudoElementType::InheritingAnonBox,
                            computedValues.forget(), skipFixup);
}

already_AddRefed<nsStyleContext>
ServoStyleSet::ResolveNonInheritingAnonymousBoxStyle(nsIAtom* aPseudoTag)
{
  MOZ_ASSERT(nsCSSAnonBoxes::IsAnonBox(aPseudoTag) &&
             nsCSSAnonBoxes::IsNonInheritingAnonBox(aPseudoTag));
  MOZ_ASSERT(aPseudoTag != nsCSSAnonBoxes::pageContent,
             "If nsCSSAnonBoxes::pageContent ends up non-inheriting, check "
             "whether we need to do anything to move the "
             "@page handling from ResolveInheritingAnonymousBoxStyle to "
             "ResolveNonInheritingAnonymousBoxStyle");

  nsCSSAnonBoxes::NonInheriting type =
    nsCSSAnonBoxes::NonInheritingTypeForPseudoTag(aPseudoTag);
  RefPtr<nsStyleContext>& cache = mNonInheritingStyleContexts[type];
  if (cache) {
    RefPtr<nsStyleContext> retval = cache;
    return retval.forget();
  }

  // We always want to skip parent-based display fixup here.  It never makes
  // sense for non-inheriting anonymous boxes.
  MOZ_ASSERT(!nsCSSAnonBoxes::IsNonInheritingAnonBox(nsCSSAnonBoxes::viewport),
             "viewport needs fixup to handle blockifying it");
  RefPtr<ServoComputedValues> computedValues =
    Servo_ComputedValues_GetForAnonymousBox(nullptr, aPseudoTag, true,
                                            mRawSet.get()).Consume();
#ifdef DEBUG
  if (!computedValues) {
    nsString pseudo;
    aPseudoTag->ToString(pseudo);
    NS_ERROR(nsPrintfCString("stylo: could not get anon-box: %s",
             NS_ConvertUTF16toUTF8(pseudo).get()).get());
    MOZ_CRASH();
  }
#endif

  RefPtr<nsStyleContext> retval =
    GetContext(computedValues.forget(), nullptr, aPseudoTag,
               CSSPseudoElementType::NonInheritingAnonBox, nullptr);
  cache = retval;
  return retval.forget();
}

// manage the set of style sheets in the style set
nsresult
ServoStyleSet::AppendStyleSheet(SheetType aType,
                                ServoStyleSheet* aSheet)
{
  MOZ_ASSERT(aSheet);
  MOZ_ASSERT(aSheet->IsApplicable());
  MOZ_ASSERT(nsStyleSet::IsCSSSheetType(aType));

  MOZ_ASSERT(aSheet->RawSheet(), "Raw sheet should be in place before insertion.");
  mSheets[aType].RemoveElement(aSheet);
  mSheets[aType].AppendElement(aSheet);

  if (mRawSet) {
    // Maintain a mirrored list of sheets on the servo side.
    Servo_StyleSet_AppendStyleSheet(mRawSet.get(), aSheet->RawSheet(), !mBatching);
  }

  return NS_OK;
}

nsresult
ServoStyleSet::PrependStyleSheet(SheetType aType,
                                 ServoStyleSheet* aSheet)
{
  MOZ_ASSERT(aSheet);
  MOZ_ASSERT(aSheet->IsApplicable());
  MOZ_ASSERT(nsStyleSet::IsCSSSheetType(aType));

  MOZ_ASSERT(aSheet->RawSheet(), "Raw sheet should be in place before insertion.");
  mSheets[aType].RemoveElement(aSheet);
  mSheets[aType].InsertElementAt(0, aSheet);

  if (mRawSet) {
    // Maintain a mirrored list of sheets on the servo side.
    Servo_StyleSet_PrependStyleSheet(mRawSet.get(), aSheet->RawSheet(), !mBatching);
  }

  return NS_OK;
}

nsresult
ServoStyleSet::RemoveStyleSheet(SheetType aType,
                                ServoStyleSheet* aSheet)
{
  MOZ_ASSERT(aSheet);
  MOZ_ASSERT(nsStyleSet::IsCSSSheetType(aType));

  mSheets[aType].RemoveElement(aSheet);

  if (mRawSet) {
    // Maintain a mirrored list of sheets on the servo side.
    Servo_StyleSet_RemoveStyleSheet(mRawSet.get(), aSheet->RawSheet(), !mBatching);
  }

  return NS_OK;
}

nsresult
ServoStyleSet::ReplaceSheets(SheetType aType,
                             const nsTArray<RefPtr<ServoStyleSheet>>& aNewSheets)
{
  // Gecko uses a two-dimensional array keyed by sheet type, whereas Servo
  // stores a flattened list. This makes ReplaceSheets a pretty clunky thing
  // to express. If the need ever arises, we can easily make this more efficent,
  // probably by aligning the representations better between engines.

  if (mRawSet) {
    for (ServoStyleSheet* sheet : mSheets[aType]) {
      Servo_StyleSet_RemoveStyleSheet(mRawSet.get(), sheet->RawSheet(), false);
    }
  }

  mSheets[aType].Clear();
  mSheets[aType].AppendElements(aNewSheets);

  if (mRawSet) {
    for (ServoStyleSheet* sheet : mSheets[aType]) {
      MOZ_ASSERT(sheet->RawSheet(), "Raw sheet should be in place before replacement.");
      Servo_StyleSet_AppendStyleSheet(mRawSet.get(), sheet->RawSheet(), false);
    }
  }

  if (!mBatching) {
    Servo_StyleSet_FlushStyleSheets(mRawSet.get());
  }

  return NS_OK;
}

nsresult
ServoStyleSet::InsertStyleSheetBefore(SheetType aType,
                                      ServoStyleSheet* aNewSheet,
                                      ServoStyleSheet* aReferenceSheet)
{
  MOZ_ASSERT(aNewSheet);
  MOZ_ASSERT(aReferenceSheet);
  MOZ_ASSERT(aNewSheet->IsApplicable());

  mSheets[aType].RemoveElement(aNewSheet);
  size_t idx = mSheets[aType].IndexOf(aReferenceSheet);
  if (idx == mSheets[aType].NoIndex) {
    return NS_ERROR_INVALID_ARG;
  }
  MOZ_ASSERT(aReferenceSheet->RawSheet(), "Reference sheet should have a raw sheet.");

  MOZ_ASSERT(aNewSheet->RawSheet(), "Raw sheet should be in place before insertion.");
  mSheets[aType].InsertElementAt(idx, aNewSheet);

  if (mRawSet) {
    // Maintain a mirrored list of sheets on the servo side.
    Servo_StyleSet_InsertStyleSheetBefore(mRawSet.get(), aNewSheet->RawSheet(),
                                          aReferenceSheet->RawSheet(), !mBatching);
  }

  return NS_OK;
}

int32_t
ServoStyleSet::SheetCount(SheetType aType) const
{
  MOZ_ASSERT(nsStyleSet::IsCSSSheetType(aType));
  return mSheets[aType].Length();
}

ServoStyleSheet*
ServoStyleSet::StyleSheetAt(SheetType aType,
                            int32_t aIndex) const
{
  MOZ_ASSERT(nsStyleSet::IsCSSSheetType(aType));
  return mSheets[aType][aIndex];
}

nsresult
ServoStyleSet::RemoveDocStyleSheet(ServoStyleSheet* aSheet)
{
  return RemoveStyleSheet(SheetType::Doc, aSheet);
}

nsresult
ServoStyleSet::AddDocStyleSheet(ServoStyleSheet* aSheet,
                                nsIDocument* aDocument)
{
  MOZ_ASSERT(aSheet->IsApplicable());
  MOZ_ASSERT(aSheet->RawSheet(), "Raw sheet should be in place by this point.");

  RefPtr<StyleSheet> strong(aSheet);

  nsTArray<RefPtr<ServoStyleSheet>>& sheetsArray = mSheets[SheetType::Doc];

  sheetsArray.RemoveElement(aSheet);

  size_t index =
    aDocument->FindDocStyleSheetInsertionPoint(sheetsArray, aSheet);
  sheetsArray.InsertElementAt(index, aSheet);

  if (mRawSet) {
    // Maintain a mirrored list of sheets on the servo side.
    ServoStyleSheet* followingSheet = sheetsArray.SafeElementAt(index + 1);
    if (followingSheet) {
      MOZ_ASSERT(followingSheet->RawSheet(), "Every mSheets element should have a raw sheet");
      Servo_StyleSet_InsertStyleSheetBefore(mRawSet.get(), aSheet->RawSheet(),
                                            followingSheet->RawSheet(), !mBatching);
    } else {
      Servo_StyleSet_AppendStyleSheet(mRawSet.get(), aSheet->RawSheet(), !mBatching);
    }
  }

  return NS_OK;
}

already_AddRefed<nsStyleContext>
ServoStyleSet::ProbePseudoElementStyle(Element* aOriginatingElement,
                                       CSSPseudoElementType aType,
                                       nsStyleContext* aParentContext)
{
  // NB: We ignore aParentContext, on the assumption that pseudo element styles
  // should just inherit from aOriginatingElement's primary style, which Servo
  // already knows.
  MOZ_ASSERT(aType < CSSPseudoElementType::Count);
  nsIAtom* pseudoTag = nsCSSPseudoElements::GetPseudoAtom(aType);

  RefPtr<ServoComputedValues> computedValues =
    Servo_ResolvePseudoStyle(aOriginatingElement, pseudoTag,
                             /* is_probe = */ true, mRawSet.get()).Consume();
  if (!computedValues) {
    return nullptr;
  }

  // For :before and :after pseudo-elements, having display: none or no
  // 'content' property is equivalent to not having the pseudo-element
  // at all.
  bool isBeforeOrAfter = pseudoTag == nsCSSPseudoElements::before ||
                         pseudoTag == nsCSSPseudoElements::after;
  if (isBeforeOrAfter) {
    const nsStyleDisplay *display = Servo_GetStyleDisplay(computedValues);
    const nsStyleContent *content = Servo_GetStyleContent(computedValues);
    // XXXldb What is contentCount for |content: ""|?
    if (display->mDisplay == StyleDisplay::None ||
        content->ContentCount() == 0) {
      return nullptr;
    }
  }

  return GetContext(computedValues.forget(), aParentContext, pseudoTag, aType,
                    isBeforeOrAfter ? aOriginatingElement : nullptr);
}

already_AddRefed<nsStyleContext>
ServoStyleSet::ProbePseudoElementStyle(Element* aOriginatingElement,
                                       CSSPseudoElementType aType,
                                       nsStyleContext* aParentContext,
                                       TreeMatchContext& aTreeMatchContext,
                                       Element* aPseudoElement)
{
  if (aPseudoElement) {
    NS_ERROR("stylo: We don't support CSS_PSEUDO_ELEMENT_SUPPORTS_USER_ACTION_STATE yet");
  }
  return ProbePseudoElementStyle(aOriginatingElement, aType, aParentContext);
}

nsRestyleHint
ServoStyleSet::HasStateDependentStyle(dom::Element* aElement,
                                      EventStates aStateMask)
{
  NS_WARNING("stylo: HasStateDependentStyle always returns zero!");
  return nsRestyleHint(0);
}

nsRestyleHint
ServoStyleSet::HasStateDependentStyle(dom::Element* aElement,
                                      CSSPseudoElementType aPseudoType,
                                     dom::Element* aPseudoElement,
                                     EventStates aStateMask)
{
  NS_WARNING("stylo: HasStateDependentStyle always returns zero!");
  return nsRestyleHint(0);
}

bool
ServoStyleSet::StyleDocument()
{
  PreTraverse();

  // Restyle the document from the root element and each of the document level
  // NAC subtree roots.
  bool postTraversalRequired = false;
  DocumentStyleRootIterator iter(mPresContext->Document());
  while (Element* root = iter.GetNextStyleRoot()) {
    if (PrepareAndTraverseSubtree(root, TraversalRootBehavior::Normal)) {
      postTraversalRequired = true;
    }
  }
  return postTraversalRequired;
}

void
ServoStyleSet::StyleNewSubtree(Element* aRoot)
{
  MOZ_ASSERT(!aRoot->HasServoData());

  PreTraverse();

  DebugOnly<bool> postTraversalRequired =
    PrepareAndTraverseSubtree(aRoot, TraversalRootBehavior::Normal);
  MOZ_ASSERT(!postTraversalRequired);
}

void
ServoStyleSet::StyleNewChildren(Element* aParent)
{
  PreTraverse();

  PrepareAndTraverseSubtree(aParent, TraversalRootBehavior::UnstyledChildrenOnly);
  // We can't assert that Servo_TraverseSubtree returns false, since aParent
  // or some of its other children might have pending restyles.
}

void
ServoStyleSet::NoteStyleSheetsChanged()
{
  Servo_StyleSet_NoteStyleSheetsChanged(mRawSet.get());
}

#ifdef DEBUG
void
ServoStyleSet::AssertTreeIsClean()
{
  DocumentStyleRootIterator iter(mPresContext->Document());
  while (Element* root = iter.GetNextStyleRoot()) {
    Servo_AssertTreeIsClean(root);
  }
}
#endif

bool
ServoStyleSet::FillKeyframesForName(const nsString& aName,
                                    const nsTimingFunction& aTimingFunction,
                                    const ServoComputedValues* aComputedValues,
                                    nsTArray<Keyframe>& aKeyframes)
{
  NS_ConvertUTF16toUTF8 name(aName);
  return Servo_StyleSet_FillKeyframesForName(mRawSet.get(),
                                             &name,
                                             &aTimingFunction,
                                             aComputedValues,
                                             &aKeyframes);
}

nsTArray<ComputedKeyframeValues>
ServoStyleSet::GetComputedKeyframeValuesFor(
  const nsTArray<Keyframe>& aKeyframes,
  dom::Element* aElement,
  const ServoComputedValuesWithParent& aServoValues)
{
  nsTArray<ComputedKeyframeValues> result(aKeyframes.Length());

  // Construct each nsTArray<PropertyStyleAnimationValuePair> here.
  result.AppendElements(aKeyframes.Length());

  Servo_GetComputedKeyframeValues(&aKeyframes,
                                  aServoValues.mCurrentStyle,
                                  aServoValues.mParentStyle,
                                  mRawSet.get(),
                                  &result);
  return result;
}

void
ServoStyleSet::RebuildData()
{
  ClearNonInheritingStyleContexts();
  Servo_StyleSet_RebuildData(mRawSet.get());
}

already_AddRefed<ServoComputedValues>
ServoStyleSet::ResolveServoStyle(Element* aElement)
{
  return Servo_ResolveStyle(aElement, mRawSet.get()).Consume();
}

void
ServoStyleSet::ClearNonInheritingStyleContexts()
{
  for (RefPtr<nsStyleContext>& ptr : mNonInheritingStyleContexts) {
    ptr = nullptr;
  }
}

already_AddRefed<ServoComputedValues>
ServoStyleSet::ResolveStyleLazily(Element* aElement, nsIAtom* aPseudoTag)
{
  mPresContext->EffectCompositor()->PreTraverse(aElement, aPseudoTag);

  MOZ_ASSERT(!sInServoTraversal);
  sInServoTraversal = true;
  RefPtr<ServoComputedValues> computedValues =
    Servo_ResolveStyleLazily(aElement, aPseudoTag, mRawSet.get()).Consume();

  if (mPresContext->EffectCompositor()->PreTraverse(aElement, aPseudoTag)) {
    computedValues =
      Servo_ResolveStyleLazily(aElement, aPseudoTag, mRawSet.get()).Consume();
  }
  sInServoTraversal = false;

  return computedValues.forget();
}

bool ServoStyleSet::sInServoTraversal = false;
