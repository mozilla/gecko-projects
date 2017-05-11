/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/ServoStyleSet.h"

#include "gfxPlatformFontList.h"
#include "mozilla/DocumentStyleRootIterator.h"
#include "mozilla/ServoRestyleManager.h"
#include "mozilla/dom/AnonymousContent.h"
#include "mozilla/dom/ChildIterator.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/ElementInlines.h"
#include "mozilla/RestyleManagerInlines.h"
#include "mozilla/ServoComputedValuesWithParent.h"
#include "nsCSSAnonBoxes.h"
#include "nsCSSPseudoElements.h"
#include "nsCSSRuleProcessor.h"
#include "nsDeviceContext.h"
#include "nsHTMLStyleSheet.h"
#include "nsIDocumentInlines.h"
#include "nsPrintfCString.h"
#include "nsSMILAnimationController.h"
#include "nsStyleContext.h"
#include "nsStyleSet.h"

using namespace mozilla;
using namespace mozilla::dom;

ServoStyleSet::ServoStyleSet()
  : mPresContext(nullptr)
  , mUniqueIDCounter(0)
  , mAllowResolveStaleStyles(false)
  , mAuthorStyleDisabled(false)
  , mStylistMayNeedRebuild(false)
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

  mPresContext->DeviceContext()->InitFontCache();
  gfxPlatformFontList::PlatformFontList()->InitLangService();

  // Now that we have an mRawSet, go ahead and notify about whatever stylesheets
  // we have so far.
  for (auto& entryArray : mEntries) {
    for (auto& entry : entryArray) {
      // There's no guarantee this will create a list on the servo side whose
      // ordering matches the list that would have been created had all those
      // sheets been appended/prepended/etc after we had mRawSet. That's okay
      // because Servo only needs to maintain relative ordering within a sheet
      // type, which this preserves.

      // Set the uniqueIDs as we go.
      entry.uniqueID = ++mUniqueIDCounter;

      MOZ_ASSERT(entry.sheet->RawSheet(), "We should only append non-null raw sheets.");
      Servo_StyleSet_AppendStyleSheet(mRawSet.get(),
                                      entry.sheet->RawSheet(),
                                      entry.uniqueID);
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

size_t
ServoStyleSet::SizeOfIncludingThis(MallocSizeOf aMallocSizeOf) const
{
  size_t n = aMallocSizeOf(this);

  // Measurement of the following members may be added later if DMD finds it is
  // worthwhile:
  // - mRawSet
  // - mSheets
  // - mNonInheritingStyleContexts
  //
  // The following members are not measured:
  // - mPresContext, because it a non-owning pointer

  return n;
}

bool
ServoStyleSet::GetAuthorStyleDisabled() const
{
  return mAuthorStyleDisabled;
}

nsresult
ServoStyleSet::SetAuthorStyleDisabled(bool aStyleDisabled)
{
  if (mAuthorStyleDisabled == aStyleDisabled) {
    return NS_OK;
  }

  mAuthorStyleDisabled = aStyleDisabled;

  // If we've just disabled, we have to note the stylesheets have changed and
  // call flush directly, since the PresShell won't.
  if (mAuthorStyleDisabled) {
    NoteStyleSheetsChanged();
  }
  // If we've just enabled, then PresShell will trigger the notification and
  // later flush when the stylesheet objects are enabled in JS.
  //
  // TODO(emilio): Users can have JS disabled, can't they? Will that affect that
  // notification on content documents?

  return NS_OK;
}

void
ServoStyleSet::BeginUpdate()
{
}

nsresult
ServoStyleSet::EndUpdate()
{
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

  RefPtr<ServoComputedValues> computedValues;
  if (aMayCompute == LazyComputeBehavior::Allow) {
    PreTraverseSync();
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

  RefPtr<nsStyleContext> result = NS_NewStyleContext(aParentContext, mPresContext, aPseudoTag,
                                                     aPseudoType, Move(aComputedValues));

  // Set the body color on the pres context. See nsStyleSet::GetContext
  if (aElementForAnimation &&
      aElementForAnimation->IsHTMLElement(nsGkAtoms::body) &&
      aPseudoType == CSSPseudoElementType::NotPseudo &&
      mPresContext->CompatibilityMode() == eCompatibility_NavQuirks) {
    nsIDocument* doc = aElementForAnimation->GetUncomposedDoc();
    if (doc && doc->GetBodyElement() == aElementForAnimation) {
      // Update the prescontext's body color
      mPresContext->SetBodyTextColor(result->StyleColor()->mColor);
    }
  }
  return result.forget();
}

const ServoElementSnapshotTable&
ServoStyleSet::Snapshots()
{
  return mPresContext->RestyleManager()->AsServo()->Snapshots();
}

void
ServoStyleSet::ResolveMappedAttrDeclarationBlocks()
{
  if (nsHTMLStyleSheet* sheet = mPresContext->Document()->GetAttributeStyleSheet()) {
    sheet->CalculateMappedServoDeclarations(mPresContext);
  }

  mPresContext->Document()->ResolveScheduledSVGPresAttrs();
}

void
ServoStyleSet::PreTraverseSync()
{
  MaybeRebuildStylist();

  ResolveMappedAttrDeclarationBlocks();

  nsCSSRuleProcessor::InitSystemMetrics();

  // This is lazily computed and pseudo matching needs to access
  // it so force computation early.
  mPresContext->Document()->GetDocumentState();

  // Ensure that the @font-face data is not stale
  mPresContext->Document()->GetUserFontSet();
}

void
ServoStyleSet::PreTraverse(Element* aRoot)
{
  PreTraverseSync();

  // Process animation stuff that we should avoid doing during the parallel
  // traversal.
  nsSMILAnimationController* smilController =
    mPresContext->Document()->GetAnimationController();
  if (aRoot) {
    mPresContext->EffectCompositor()->PreTraverseInSubtree(aRoot);
    if (smilController) {
      smilController->PreTraverseInSubtree(aRoot);
    }
  } else {
    mPresContext->EffectCompositor()->PreTraverse();
    if (smilController) {
      smilController->PreTraverse();
    }
  }
}

bool
ServoStyleSet::PrepareAndTraverseSubtree(
  RawGeckoElementBorrowed aRoot,
  TraversalRootBehavior aRootBehavior,
  TraversalRestyleBehavior aRestyleBehavior)
{
  // Get the Document's root element to ensure that the cache is valid before
  // calling into the (potentially-parallel) Servo traversal, where a cache hit
  // is necessary to avoid a data race when updating the cache.
  mozilla::Unused << aRoot->OwnerDoc()->GetRootElement();

  MOZ_ASSERT(!mStylistMayNeedRebuild);
  AutoSetInServoTraversal guard(this);

  const SnapshotTable& snapshots = Snapshots();

  bool isInitial = !aRoot->HasServoData();
  bool forReconstruct =
    aRestyleBehavior == TraversalRestyleBehavior::ForReconstruct;
  bool postTraversalRequired = Servo_TraverseSubtree(
    aRoot, mRawSet.get(), &snapshots, aRootBehavior, aRestyleBehavior);
  MOZ_ASSERT_IF(isInitial || forReconstruct, !postTraversalRequired);

  auto root = const_cast<Element*>(aRoot);

  // If there are still animation restyles needed, trigger a second traversal to
  // update CSS animations or transitions' styles.
  //
  // We don't need to do this for SMIL since SMIL only updates its animation
  // values once at the begin of a tick. As a result, even if the previous
  // traversal caused, for example, the font-size to change, the SMIL style
  // won't be updated until the next tick anyway.
  EffectCompositor* compositor = mPresContext->EffectCompositor();
  if (forReconstruct ? compositor->PreTraverseInSubtree(root)
                     : compositor->PreTraverse()) {
    if (Servo_TraverseSubtree(
          aRoot, mRawSet.get(), &snapshots, aRootBehavior, aRestyleBehavior)) {
      MOZ_ASSERT(!forReconstruct);
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
        ServoRestyleManager::ClearRestyleStateFromSubtree(root);
      } else {
        postTraversalRequired = true;
      }
    }
  }

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
    Servo_ComputedValues_Inherit(mRawSet.get(),
                                 parentComputedValues,
                                 InheritTarget::Text).Consume();

  return GetContext(computedValues.forget(), aParentContext,
                    nsCSSAnonBoxes::mozText,
                    CSSPseudoElementType::InheritingAnonBox,
                    nullptr);
}

already_AddRefed<nsStyleContext>
ServoStyleSet::ResolveStyleForFirstLetterContinuation(nsStyleContext* aParentContext)
{
  const ServoComputedValues* parent =
    aParentContext->StyleSource().AsServoComputedValues();
  RefPtr<ServoComputedValues> computedValues =
    Servo_ComputedValues_Inherit(mRawSet.get(),
                                 parent,
                                 InheritTarget::FirstLetterContinuation)
                                 .Consume();
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
    Servo_ComputedValues_Inherit(mRawSet.get(),
                                 nullptr,
                                 InheritTarget::PlaceholderFrame)
                                 .Consume();
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

  MaybeRebuildStylist();

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
ServoStyleSet::ResolveTransientStyle(Element* aElement,
                                     nsIAtom* aPseudoTag,
                                     CSSPseudoElementType aPseudoType)
{
  RefPtr<ServoComputedValues> computedValues =
    ResolveTransientServoStyle(aElement, aPseudoTag);

  return GetContext(computedValues.forget(),
                    nullptr,
                    aPseudoTag,
                    aPseudoType, nullptr);
}

already_AddRefed<ServoComputedValues>
ServoStyleSet::ResolveTransientServoStyle(Element* aElement,
                                          nsIAtom* aPseudoTag)
{
  PreTraverseSync();
  return ResolveStyleLazily(aElement, aPseudoTag);
}

already_AddRefed<nsStyleContext>
ServoStyleSet::ResolveInheritingAnonymousBoxStyle(nsIAtom* aPseudoTag,
                                                  nsStyleContext* aParentContext)
{
  MOZ_ASSERT(nsCSSAnonBoxes::IsAnonBox(aPseudoTag) &&
             !nsCSSAnonBoxes::IsNonInheritingAnonBox(aPseudoTag));

  MaybeRebuildStylist();

  bool skipFixup =
    nsCSSAnonBoxes::AnonBoxSkipsParentDisplayBasedStyleFixup(aPseudoTag);

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

  return GetContext(computedValues.forget(), aParentContext, aPseudoTag,
                    CSSPseudoElementType::InheritingAnonBox, nullptr);
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

  MaybeRebuildStylist();

  // We always want to skip parent-based display fixup here.  It never makes
  // sense for non-inheriting anonymous boxes.  (Static assertions in
  // nsCSSAnonBoxes.cpp ensure that all non-inheriting non-anonymous boxes
  // are indeed annotated as skipping this fixup.)
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

already_AddRefed<RawServoRuleNode>
ServoStyleSet::ResolveRuleNode(dom::Element *aElement, nsIAtom *aPseudoTag)
{
  MOZ_ASSERT(aElement);
  return Servo_ResolveRuleNode(aElement, aPseudoTag, mRawSet.get()).Consume();
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

  // If we were already tracking aSheet, the newUniqueID will be the same
  // as the oldUniqueID. In that case, Servo will remove aSheet from its
  // original position as part of the call to Servo_StyleSet_AppendStyleSheet.
  uint32_t oldUniqueID = RemoveSheetOfType(aType, aSheet);
  uint32_t newUniqueID = AppendSheetOfType(aType, aSheet, oldUniqueID);

  if (mRawSet) {
    // Maintain a mirrored list of sheets on the servo side.
    Servo_StyleSet_AppendStyleSheet(mRawSet.get(),
                                    aSheet->RawSheet(),
                                    newUniqueID);
    mStylistMayNeedRebuild = true;
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

  // If we were already tracking aSheet, the newUniqueID will be the same
  // as the oldUniqueID. In that case, Servo will remove aSheet from its
  // original position as part of the call to Servo_StyleSet_PrependStyleSheet.
  uint32_t oldUniqueID = RemoveSheetOfType(aType, aSheet);
  uint32_t newUniqueID = PrependSheetOfType(aType, aSheet, oldUniqueID);

  if (mRawSet) {
    // Maintain a mirrored list of sheets on the servo side.
    Servo_StyleSet_PrependStyleSheet(mRawSet.get(),
                                     aSheet->RawSheet(),
                                     newUniqueID);
    mStylistMayNeedRebuild = true;
  }

  return NS_OK;
}

nsresult
ServoStyleSet::RemoveStyleSheet(SheetType aType,
                                ServoStyleSheet* aSheet)
{
  MOZ_ASSERT(aSheet);
  MOZ_ASSERT(nsStyleSet::IsCSSSheetType(aType));

  uint32_t uniqueID = RemoveSheetOfType(aType, aSheet);
  if (mRawSet && uniqueID) {
    // Maintain a mirrored list of sheets on the servo side.
    Servo_StyleSet_RemoveStyleSheet(mRawSet.get(), uniqueID);
    mStylistMayNeedRebuild = true;
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

  mStylistMayNeedRebuild = true;

  // Remove all the existing sheets first.
  if (mRawSet) {
    for (const Entry& entry : mEntries[aType]) {
      Servo_StyleSet_RemoveStyleSheet(mRawSet.get(), entry.uniqueID);
    }
  }
  mEntries[aType].Clear();

  // Add in all the new sheets.
  for (auto& sheet : aNewSheets) {
    uint32_t uniqueID = AppendSheetOfType(aType, sheet);
    if (mRawSet) {
      MOZ_ASSERT(sheet->RawSheet(), "Raw sheet should be in place before replacement.");
      Servo_StyleSet_AppendStyleSheet(mRawSet.get(),
                                      sheet->RawSheet(),
                                      uniqueID);
    }
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
  MOZ_ASSERT(aNewSheet != aReferenceSheet, "Can't place sheet before itself.");
  MOZ_ASSERT(aNewSheet->RawSheet(), "Raw sheet should be in place before insertion.");
  MOZ_ASSERT(aReferenceSheet->RawSheet(), "Reference sheet should have a raw sheet.");

  uint32_t beforeUniqueID = FindSheetOfType(aType, aReferenceSheet);
  if (beforeUniqueID == 0) {
    return NS_ERROR_INVALID_ARG;
  }

  // If we were already tracking aNewSheet, the newUniqueID will be the same
  // as the oldUniqueID. In that case, Servo will remove aNewSheet from its
  // original position as part of the call to Servo_StyleSet_InsertStyleSheetBefore.
  uint32_t oldUniqueID = RemoveSheetOfType(aType, aNewSheet);
  uint32_t newUniqueID = InsertSheetOfType(aType,
                                           aNewSheet,
                                           beforeUniqueID,
                                           oldUniqueID);

  if (mRawSet) {
    // Maintain a mirrored list of sheets on the servo side.
    Servo_StyleSet_InsertStyleSheetBefore(mRawSet.get(),
                                          aNewSheet->RawSheet(),
                                          newUniqueID,
                                          beforeUniqueID);
    mStylistMayNeedRebuild = true;
  }

  return NS_OK;
}

int32_t
ServoStyleSet::SheetCount(SheetType aType) const
{
  MOZ_ASSERT(nsStyleSet::IsCSSSheetType(aType));
  return mEntries[aType].Length();
}

ServoStyleSheet*
ServoStyleSet::StyleSheetAt(SheetType aType,
                            int32_t aIndex) const
{
  MOZ_ASSERT(nsStyleSet::IsCSSSheetType(aType));
  return mEntries[aType][aIndex].sheet;
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

  uint32_t oldUniqueID = RemoveSheetOfType(SheetType::Doc, aSheet);

  size_t index =
    aDocument->FindDocStyleSheetInsertionPoint(mEntries[SheetType::Doc], aSheet);

  if (index < mEntries[SheetType::Doc].Length()) {
    // This case is insert before.
    uint32_t beforeUniqueID = mEntries[SheetType::Doc][index].uniqueID;
    uint32_t newUniqueID = InsertSheetOfType(SheetType::Doc,
                                             aSheet,
                                             beforeUniqueID,
                                             oldUniqueID);

    if (mRawSet) {
      // Maintain a mirrored list of sheets on the servo side.
      Servo_StyleSet_InsertStyleSheetBefore(mRawSet.get(),
                                            aSheet->RawSheet(),
                                            newUniqueID,
                                            beforeUniqueID);
      mStylistMayNeedRebuild = true;
    }
  } else {
    // This case is append.
    uint32_t newUniqueID = AppendSheetOfType(SheetType::Doc,
                                             aSheet,
                                             oldUniqueID);

    if (mRawSet) {
      // Maintain a mirrored list of sheets on the servo side.
      Servo_StyleSet_AppendStyleSheet(mRawSet.get(),
                                      aSheet->RawSheet(),
                                      newUniqueID);
      mStylistMayNeedRebuild = true;
    }
  }

  return NS_OK;
}

already_AddRefed<nsStyleContext>
ServoStyleSet::ProbePseudoElementStyle(Element* aOriginatingElement,
                                       CSSPseudoElementType aType,
                                       nsStyleContext* aParentContext)
{
  MaybeRebuildStylist();

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
    if (PrepareAndTraverseSubtree(root,
                                  TraversalRootBehavior::Normal,
                                  TraversalRestyleBehavior::Normal)) {
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
    PrepareAndTraverseSubtree(aRoot,
                              TraversalRootBehavior::Normal,
                              TraversalRestyleBehavior::Normal);
  MOZ_ASSERT(!postTraversalRequired);
}

void
ServoStyleSet::StyleNewChildren(Element* aParent)
{
  PreTraverse();

  PrepareAndTraverseSubtree(aParent,
                            TraversalRootBehavior::UnstyledChildrenOnly,
                            TraversalRestyleBehavior::Normal);
  // We can't assert that Servo_TraverseSubtree returns false, since aParent
  // or some of its other children might have pending restyles.
}

void
ServoStyleSet::StyleSubtreeForReconstruct(Element* aRoot)
{
  PreTraverse(aRoot);

  DebugOnly<bool> postTraversalRequired =
    PrepareAndTraverseSubtree(aRoot,
                              TraversalRootBehavior::Normal,
                              TraversalRestyleBehavior::ForReconstruct);
  MOZ_ASSERT(!postTraversalRequired);
}

void
ServoStyleSet::NoteStyleSheetsChanged()
{
  mStylistMayNeedRebuild = true;
  Servo_StyleSet_NoteStyleSheetsChanged(mRawSet.get(), mAuthorStyleDisabled);
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
  MaybeRebuildStylist();

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

already_AddRefed<ServoComputedValues>
ServoStyleSet::GetBaseComputedValuesForElement(Element* aElement,
                                               nsIAtom* aPseudoTag)
{
  return Servo_StyleSet_GetBaseComputedValuesForElement(mRawSet.get(),
                                                        aElement,
                                                        &Snapshots(),
                                                        aPseudoTag).Consume();
}

already_AddRefed<RawServoAnimationValue>
ServoStyleSet::ComputeAnimationValue(
  RawServoDeclarationBlock* aDeclarations,
  const ServoComputedValuesWithParent& aComputedValues)
{
  return Servo_AnimationValue_Compute(aDeclarations,
                                      aComputedValues.mCurrentStyle,
                                      aComputedValues.mParentStyle,
                                      mRawSet.get()).Consume();
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
  MaybeRebuildStylist();
  return Servo_ResolveStyle(aElement, mRawSet.get(),
                            mAllowResolveStaleStyles).Consume();
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
  MOZ_ASSERT(!mStylistMayNeedRebuild);

  AutoSetInServoTraversal guard(this);

  /**
   * NB: This is needed because we process animations and transitions on the
   * pseudo-elements themselves, not on the parent's EagerPseudoStyles.
   *
   * That means that that style doesn't account for animations, and we can't do
   * that easily from the traversal without doing wasted work.
   *
   * As such, we just lie here a bit, which is the entrypoint of
   * getComputedStyle, the only API where this can be observed, to look at the
   * style of the pseudo-element if it exists instead.
   */
  Element* elementForStyleResolution = aElement;
  nsIAtom* pseudoTagForStyleResolution = aPseudoTag;
  if (aPseudoTag == nsCSSPseudoElements::before) {
    if (Element* pseudo = nsLayoutUtils::GetBeforePseudo(aElement)) {
      elementForStyleResolution = pseudo;
      pseudoTagForStyleResolution = nullptr;
    }
  } else if (aPseudoTag == nsCSSPseudoElements::after) {
    if (Element* pseudo = nsLayoutUtils::GetAfterPseudo(aElement)) {
      elementForStyleResolution = pseudo;
      pseudoTagForStyleResolution = nullptr;
    }
  }

  RefPtr<ServoComputedValues> computedValues =
    Servo_ResolveStyleLazily(elementForStyleResolution,
                             pseudoTagForStyleResolution,
                             &Snapshots(),
                             mRawSet.get()).Consume();

  if (mPresContext->EffectCompositor()->PreTraverse(aElement, aPseudoTag)) {
    computedValues =
      Servo_ResolveStyleLazily(elementForStyleResolution,
                               pseudoTagForStyleResolution,
                               &Snapshots(),
                               mRawSet.get()).Consume();
  }

  return computedValues.forget();
}

bool
ServoStyleSet::AppendFontFaceRules(nsTArray<nsFontFaceRuleContainer>& aArray)
{
  MaybeRebuildStylist();
  Servo_StyleSet_GetFontFaceRules(mRawSet.get(), &aArray);
  return true;
}

already_AddRefed<ServoComputedValues>
ServoStyleSet::ResolveForDeclarations(
  ServoComputedValuesBorrowedOrNull aParentOrNull,
  RawServoDeclarationBlockBorrowed aDeclarations)
{
  MaybeRebuildStylist();
  return Servo_StyleSet_ResolveForDeclarations(mRawSet.get(),
                                               aParentOrNull,
                                               aDeclarations).Consume();
}

void
ServoStyleSet::RebuildStylist()
{
  MOZ_ASSERT(mStylistMayNeedRebuild);
  Servo_StyleSet_FlushStyleSheets(mRawSet.get());
  mStylistMayNeedRebuild = false;
}

uint32_t
ServoStyleSet::FindSheetOfType(SheetType aType,
                               ServoStyleSheet* aSheet)
{
  for (const auto& entry : mEntries[aType]) {
    if (entry.sheet == aSheet) {
      return entry.uniqueID;
    }
  }
  return 0;
}

uint32_t
ServoStyleSet::PrependSheetOfType(SheetType aType,
                                  ServoStyleSheet* aSheet,
                                  uint32_t aReuseUniqueID)
{
  Entry* entry = mEntries[aType].InsertElementAt(0);
  entry->uniqueID = aReuseUniqueID ? aReuseUniqueID : ++mUniqueIDCounter;
  entry->sheet = aSheet;
  return entry->uniqueID;
}

uint32_t
ServoStyleSet::AppendSheetOfType(SheetType aType,
                                 ServoStyleSheet* aSheet,
                                 uint32_t aReuseUniqueID)
{
  Entry* entry = mEntries[aType].AppendElement();
  entry->uniqueID = aReuseUniqueID ? aReuseUniqueID : ++mUniqueIDCounter;
  entry->sheet = aSheet;
  return entry->uniqueID;
}

uint32_t
ServoStyleSet::InsertSheetOfType(SheetType aType,
                                 ServoStyleSheet* aSheet,
                                 uint32_t aBeforeUniqueID,
                                 uint32_t aReuseUniqueID)
{
  for (uint32_t i = 0; i < mEntries[aType].Length(); ++i) {
    if (mEntries[aType][i].uniqueID == aBeforeUniqueID) {
      Entry* entry = mEntries[aType].InsertElementAt(i);
      entry->uniqueID = aReuseUniqueID ? aReuseUniqueID : ++mUniqueIDCounter;
      entry->sheet = aSheet;
      return entry->uniqueID;
    }
  }
  return 0;
}

uint32_t
ServoStyleSet::RemoveSheetOfType(SheetType aType,
                                 ServoStyleSheet* aSheet)
{
  for (uint32_t i = 0; i < mEntries[aType].Length(); ++i) {
    if (mEntries[aType][i].sheet == aSheet) {
      uint32_t uniqueID = mEntries[aType][i].uniqueID;
      mEntries[aType].RemoveElementAt(i);
      return uniqueID;
    }
  }
  return 0;
}

void
ServoStyleSet::RunPostTraversalTasks()
{
  MOZ_ASSERT(!IsInServoTraversal());

  if (mPostTraversalTasks.IsEmpty()) {
    return;
  }

  nsTArray<PostTraversalTask> tasks;
  tasks.SwapElements(mPostTraversalTasks);

  for (auto& task : tasks) {
    task.Run();
  }
}

ServoStyleSet* ServoStyleSet::sInServoTraversal = nullptr;
