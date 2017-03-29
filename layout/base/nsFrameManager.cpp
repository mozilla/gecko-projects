/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim:cindent:ts=2:et:sw=2:
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * This Original Code has been modified by IBM Corporation. Modifications made by IBM 
 * described herein are Copyright (c) International Business Machines Corporation, 2000.
 * Modifications to Mozilla code or documentation identified per MPL Section 3.3
 *
 * Date             Modified by     Description of modification
 * 04/20/2000       IBM Corp.      OS/2 VisualAge build.
 */

/* storage of the frame tree and information about it */

#include "nscore.h"
#include "nsIPresShell.h"
#include "nsStyleContext.h"
#include "nsCOMPtr.h"
#include "plhash.h"
#include "nsPlaceholderFrame.h"
#include "nsGkAtoms.h"
#include "nsILayoutHistoryState.h"
#include "nsPresState.h"
#include "mozilla/dom/Element.h"
#include "nsIDocument.h"

#include "nsContentUtils.h"
#include "nsError.h"
#include "nsAutoPtr.h"
#include "nsAbsoluteContainingBlock.h"
#include "ChildIterator.h"

#include "nsFrameManager.h"
#include "GeckoProfiler.h"
#include "nsIStatefulFrame.h"
#include "nsContainerFrame.h"

// #define DEBUG_UNDISPLAYED_MAP
// #define DEBUG_DISPLAY_CONTENTS_MAP

using namespace mozilla;
using namespace mozilla::dom;

//----------------------------------------------------------------------

struct PlaceholderMapEntry : public PLDHashEntryHdr {
  // key (the out of flow frame) can be obtained through placeholder frame
  nsPlaceholderFrame *placeholderFrame;
};

static bool
PlaceholderMapMatchEntry(const PLDHashEntryHdr *hdr, const void *key)
{
  const PlaceholderMapEntry *entry =
    static_cast<const PlaceholderMapEntry*>(hdr);
  NS_ASSERTION(entry->placeholderFrame->GetOutOfFlowFrame() !=
               (void*)0xdddddddd,
               "Dead placeholder in placeholder map");
  return entry->placeholderFrame->GetOutOfFlowFrame() == key;
}

static const PLDHashTableOps PlaceholderMapOps = {
  PLDHashTable::HashVoidPtrKeyStub,
  PlaceholderMapMatchEntry,
  PLDHashTable::MoveEntryStub,
  PLDHashTable::ClearEntryStub,
  nullptr
};

nsFrameManagerBase::nsFrameManagerBase()
  : mPresShell(nullptr)
  , mRootFrame(nullptr)
  , mPlaceholderMap(&PlaceholderMapOps, sizeof(PlaceholderMapEntry))
  , mUndisplayedMap(nullptr)
  , mDisplayContentsMap(nullptr)
  , mIsDestroyingFrames(false)
{
}

//----------------------------------------------------------------------

/**
 * The undisplayed map is a class that maps a parent content node to the
 * undisplayed content children, and their style contexts.
 *
 * The linked list of nodes holds strong references to the style contexts and
 * the content.
 */
class nsFrameManagerBase::UndisplayedMap :
  private nsClassHashtable<nsPtrHashKey<nsIContent>,
                           LinkedList<UndisplayedNode>>
{
  typedef nsClassHashtable<nsPtrHashKey<nsIContent>, LinkedList<UndisplayedNode>> base_type;

public:
  UndisplayedMap();
  ~UndisplayedMap();

  UndisplayedNode* GetFirstNode(nsIContent* aParentContent);

  void AddNodeFor(nsIContent* aParentContent,
                  nsIContent* aChild,
                  nsStyleContext* aStyle);

  void RemoveNodeFor(nsIContent* aParentContent, UndisplayedNode* aNode);

  void RemoveNodesFor(nsIContent* aParentContent);

  nsAutoPtr<LinkedList<UndisplayedNode>>
    UnlinkNodesFor(nsIContent* aParentContent);

  // Removes all entries from the hash table
  void  Clear();

protected:
  LinkedList<UndisplayedNode>* GetListFor(nsIContent** aParentContent);
  LinkedList<UndisplayedNode>* GetOrCreateListFor(nsIContent** aParentContent);
  void AppendNodeFor(UndisplayedNode* aNode, nsIContent* aParentContent);
  /**
   * Get the applicable parent for the map lookup. This is almost always the
   * provided argument, except if it's a <xbl:children> element, in which case
   * it's the parent of the children element.
   */
  nsIContent* GetApplicableParent(nsIContent* aParent);
};

//----------------------------------------------------------------------

nsFrameManager::~nsFrameManager()
{
  NS_ASSERTION(!mPresShell, "nsFrameManager::Destroy never called");
}

void
nsFrameManager::Destroy()
{
  NS_ASSERTION(mPresShell, "Frame manager already shut down.");

  // Destroy the frame hierarchy.
  mPresShell->SetIgnoreFrameDestruction(true);

  // Unregister all placeholders before tearing down the frame tree
  nsFrameManager::ClearPlaceholderFrameMap();

  if (mRootFrame) {
    mRootFrame->Destroy();
    mRootFrame = nullptr;
  }

  delete mUndisplayedMap;
  mUndisplayedMap = nullptr;
  delete mDisplayContentsMap;
  mDisplayContentsMap = nullptr;

  mPresShell = nullptr;
}

//----------------------------------------------------------------------

// Placeholder frame functions
nsPlaceholderFrame*
nsFrameManager::GetPlaceholderFrameFor(const nsIFrame* aFrame)
{
  NS_PRECONDITION(aFrame, "null param unexpected");

  auto entry = static_cast<PlaceholderMapEntry*>
    (const_cast<PLDHashTable*>(&mPlaceholderMap)->Search(aFrame));
  if (entry) {
    return entry->placeholderFrame;
  }

  return nullptr;
}

void
nsFrameManager::RegisterPlaceholderFrame(nsPlaceholderFrame* aPlaceholderFrame)
{
  MOZ_ASSERT(aPlaceholderFrame, "null param unexpected");
  MOZ_ASSERT(nsGkAtoms::placeholderFrame == aPlaceholderFrame->GetType(),
             "unexpected frame type");
  auto entry = static_cast<PlaceholderMapEntry*>
    (mPlaceholderMap.Add(aPlaceholderFrame->GetOutOfFlowFrame()));
  MOZ_ASSERT(!entry->placeholderFrame,
             "Registering a placeholder for a frame that already has a placeholder!");
  entry->placeholderFrame = aPlaceholderFrame;
}

void
nsFrameManager::UnregisterPlaceholderFrame(nsPlaceholderFrame* aPlaceholderFrame)
{
  NS_PRECONDITION(aPlaceholderFrame, "null param unexpected");
  NS_PRECONDITION(nsGkAtoms::placeholderFrame == aPlaceholderFrame->GetType(),
                  "unexpected frame type");

  mPlaceholderMap.Remove(aPlaceholderFrame->GetOutOfFlowFrame());
}

void
nsFrameManager::ClearPlaceholderFrameMap()
{
  for (auto iter = mPlaceholderMap.Iter(); !iter.Done(); iter.Next()) {
    auto entry = static_cast<PlaceholderMapEntry*>(iter.Get());
    entry->placeholderFrame->SetOutOfFlowFrame(nullptr);
  }
  mPlaceholderMap.Clear();
}

//----------------------------------------------------------------------

static nsIContent*
ParentForUndisplayedMap(const nsIContent* aContent)
{
  MOZ_ASSERT(aContent);

  nsIContent* parent = aContent->GetParentElementCrossingShadowRoot();
  MOZ_ASSERT(parent || !aContent->GetParent(), "no non-elements");

  return parent;
}

/* static */ nsStyleContext*
nsFrameManager::GetStyleContextInMap(UndisplayedMap* aMap,
                                     const nsIContent* aContent)
{
  UndisplayedNode* node = GetUndisplayedNodeInMapFor(aMap, aContent);
  return node ? node->mStyle.get() : nullptr;
}

/* static */ UndisplayedNode*
nsFrameManager::GetUndisplayedNodeInMapFor(UndisplayedMap* aMap,
                                           const nsIContent* aContent)
{
  if (!aContent) {
    return nullptr;
  }
  nsIContent* parent = ParentForUndisplayedMap(aContent);
  for (UndisplayedNode* node = aMap->GetFirstNode(parent);
       node; node = node->getNext()) {
    if (node->mContent == aContent)
      return node;
  }

  return nullptr;
}


/* static */ UndisplayedNode*
nsFrameManager::GetAllUndisplayedNodesInMapFor(UndisplayedMap* aMap,
                                               nsIContent* aParentContent)
{
  return aMap ? aMap->GetFirstNode(aParentContent) : nullptr;
}

UndisplayedNode*
nsFrameManager::GetAllUndisplayedContentIn(nsIContent* aParentContent)
{
  return GetAllUndisplayedNodesInMapFor(mUndisplayedMap, aParentContent);
}

/* static */ void
nsFrameManager::SetStyleContextInMap(UndisplayedMap* aMap,
                                     nsIContent* aContent,
                                     nsStyleContext* aStyleContext)
{
  MOZ_ASSERT(!aStyleContext->GetPseudo(),
             "Should only have actual elements here");

#if defined(DEBUG_UNDISPLAYED_MAP) || defined(DEBUG_DISPLAY_BOX_CONTENTS_MAP)
  static int i = 0;
  printf("SetStyleContextInMap(%d): p=%p \n", i++, (void *)aContent);
#endif

  MOZ_ASSERT(!GetStyleContextInMap(aMap, aContent),
             "Already have an entry for aContent");

  nsIContent* parent = ParentForUndisplayedMap(aContent);
#ifdef DEBUG
  nsIPresShell* shell = aStyleContext->PresContext()->PresShell();
  NS_ASSERTION(parent || (shell && shell->GetDocument() &&
                          shell->GetDocument()->GetRootElement() == aContent),
               "undisplayed content must have a parent, unless it's the root "
               "element");
#endif
  aMap->AddNodeFor(parent, aContent, aStyleContext);
}

void
nsFrameManager::SetUndisplayedContent(nsIContent* aContent,
                                      nsStyleContext* aStyleContext)
{
  if (!mUndisplayedMap) {
    mUndisplayedMap = new UndisplayedMap;
  }
  SetStyleContextInMap(mUndisplayedMap, aContent, aStyleContext);
}

/* static */ void
nsFrameManager::ChangeStyleContextInMap(UndisplayedMap* aMap,
                                        nsIContent* aContent,
                                        nsStyleContext* aStyleContext)
{
  MOZ_ASSERT(aMap, "expecting a map");

#if defined(DEBUG_UNDISPLAYED_MAP) || defined(DEBUG_DISPLAY_BOX_CONTENTS_MAP)
   static int i = 0;
   printf("ChangeStyleContextInMap(%d): p=%p \n", i++, (void *)aContent);
#endif

  for (UndisplayedNode* node = aMap->GetFirstNode(aContent->GetParent());
       node; node = node->getNext()) {
    if (node->mContent == aContent) {
      node->mStyle = aStyleContext;
      return;
    }
  }

  MOZ_CRASH("couldn't find the entry to change");
}

void
nsFrameManager::ClearUndisplayedContentIn(nsIContent* aContent,
                                          nsIContent* aParentContent)
{
#ifdef DEBUG_UNDISPLAYED_MAP
  static int i = 0;
  printf("ClearUndisplayedContent(%d): content=%p parent=%p --> ", i++, (void *)aContent, (void*)aParentContent);
#endif

  if (!mUndisplayedMap) {
    return;
  }

  for (UndisplayedNode* node = mUndisplayedMap->GetFirstNode(aParentContent);
       node; node = node->getNext()) {
    if (node->mContent == aContent) {
      mUndisplayedMap->RemoveNodeFor(aParentContent, node);

#ifdef DEBUG_UNDISPLAYED_MAP
      printf( "REMOVED!\n");
#endif
      // make sure that there are no more entries for the same content
      MOZ_ASSERT(!GetUndisplayedContent(aContent),
                 "Found more undisplayed content data after removal");
      return;
    }
  }

#ifdef DEBUG_UNDISPLAYED_MAP
  printf( "not found.\n");
#endif
}

void
nsFrameManager::ClearAllUndisplayedContentIn(nsIContent* aParentContent)
{
#ifdef DEBUG_UNDISPLAYED_MAP
  static int i = 0;
  printf("ClearAllUndisplayedContentIn(%d): parent=%p \n", i++, (void*)aParentContent);
#endif

  if (mUndisplayedMap) {
    mUndisplayedMap->RemoveNodesFor(aParentContent);
  }

  // Need to look at aParentContent's content list due to XBL insertions.
  // Nodes in aParentContent's content list do not have aParentContent as a
  // parent, but are treated as children of aParentContent. We iterate over
  // the flattened content list and just ignore any nodes we don't care about.
  FlattenedChildIterator iter(aParentContent);
  for (nsIContent* child = iter.GetNextChild(); child; child = iter.GetNextChild()) {
    if (child->GetParent() != aParentContent) {
      ClearUndisplayedContentIn(child, child->GetParent());
    }
  }
}

//----------------------------------------------------------------------

void
nsFrameManager::SetDisplayContents(nsIContent* aContent,
                                   nsStyleContext* aStyleContext)
{
  if (!mDisplayContentsMap) {
    mDisplayContentsMap = new UndisplayedMap;
  }
  SetStyleContextInMap(mDisplayContentsMap, aContent, aStyleContext);
}

UndisplayedNode*
nsFrameManager::GetAllDisplayContentsIn(nsIContent* aParentContent)
{
  return GetAllUndisplayedNodesInMapFor(mDisplayContentsMap, aParentContent);
}

void
nsFrameManager::ClearDisplayContentsIn(nsIContent* aContent,
                                       nsIContent* aParentContent)
{
#ifdef DEBUG_DISPLAY_CONTENTS_MAP
  static int i = 0;
  printf("ClearDisplayContents(%d): content=%p parent=%p --> ", i++, (void *)aContent, (void*)aParentContent);
#endif

  if (!mDisplayContentsMap) {
    return;
  }

  for (UndisplayedNode* node = mDisplayContentsMap->GetFirstNode(aParentContent);
       node; node = node->getNext()) {
    if (node->mContent == aContent) {
      mDisplayContentsMap->RemoveNodeFor(aParentContent, node);

#ifdef DEBUG_DISPLAY_CONTENTS_MAP
      printf( "REMOVED!\n");
#endif
      // make sure that there are no more entries for the same content
      MOZ_ASSERT(!GetDisplayContentsStyleFor(aContent),
                 "Found more entries for aContent after removal");
      ClearAllDisplayContentsIn(aContent);
      ClearAllUndisplayedContentIn(aContent);
      return;
    }
  }
#ifdef DEBUG_DISPLAY_CONTENTS_MAP
  printf( "not found.\n");
#endif
}

void
nsFrameManager::ClearAllDisplayContentsIn(nsIContent* aParentContent)
{
#ifdef DEBUG_DISPLAY_CONTENTS_MAP
  static int i = 0;
  printf("ClearAllDisplayContentsIn(%d): parent=%p \n", i++, (void*)aParentContent);
#endif

  if (mDisplayContentsMap) {
    nsAutoPtr<LinkedList<UndisplayedNode>> list =
      mDisplayContentsMap->UnlinkNodesFor(aParentContent);
    if (list) {
      while (UndisplayedNode* node = list->popFirst()) {
        ClearAllDisplayContentsIn(node->mContent);
        ClearAllUndisplayedContentIn(node->mContent);
        delete node;
      }
    }
  }

  // Need to look at aParentContent's content list due to XBL insertions.
  // Nodes in aParentContent's content list do not have aParentContent as a
  // parent, but are treated as children of aParentContent. We iterate over
  // the flattened content list and just ignore any nodes we don't care about.
  FlattenedChildIterator iter(aParentContent);
  for (nsIContent* child = iter.GetNextChild(); child; child = iter.GetNextChild()) {
    if (child->GetParent() != aParentContent) {
      ClearDisplayContentsIn(child, child->GetParent());
      ClearUndisplayedContentIn(child, child->GetParent());
    }
  }
}

//----------------------------------------------------------------------
void
nsFrameManager::AppendFrames(nsContainerFrame* aParentFrame,
                             ChildListID       aListID,
                             nsFrameList&      aFrameList)
{
  if (aParentFrame->IsAbsoluteContainer() &&
      aListID == aParentFrame->GetAbsoluteListID()) {
    aParentFrame->GetAbsoluteContainingBlock()->
      AppendFrames(aParentFrame, aListID, aFrameList);
  } else {
    aParentFrame->AppendFrames(aListID, aFrameList);
  }
}

void
nsFrameManager::InsertFrames(nsContainerFrame* aParentFrame,
                             ChildListID       aListID,
                             nsIFrame*         aPrevFrame,
                             nsFrameList&      aFrameList)
{
  NS_PRECONDITION(!aPrevFrame || (!aPrevFrame->GetNextContinuation()
                  || (((aPrevFrame->GetNextContinuation()->GetStateBits() & NS_FRAME_IS_OVERFLOW_CONTAINER))
                  && !(aPrevFrame->GetStateBits() & NS_FRAME_IS_OVERFLOW_CONTAINER))),
                  "aPrevFrame must be the last continuation in its chain!");

  if (aParentFrame->IsAbsoluteContainer() &&
      aListID == aParentFrame->GetAbsoluteListID()) {
    aParentFrame->GetAbsoluteContainingBlock()->
      InsertFrames(aParentFrame, aListID, aPrevFrame, aFrameList);
  } else {
    aParentFrame->InsertFrames(aListID, aPrevFrame, aFrameList);
  }
}

void
nsFrameManager::RemoveFrame(ChildListID     aListID,
                            nsIFrame*       aOldFrame)
{
  bool wasDestroyingFrames = mIsDestroyingFrames;
  mIsDestroyingFrames = true;

  // In case the reflow doesn't invalidate anything since it just leaves
  // a gap where the old frame was, we invalidate it here.  (This is
  // reasonably likely to happen when removing a last child in a way
  // that doesn't change the size of the parent.)
  // This has to sure to invalidate the entire overflow rect; this
  // is important in the presence of absolute positioning
  aOldFrame->InvalidateFrameForRemoval();

  NS_ASSERTION(!aOldFrame->GetPrevContinuation() ||
               // exception for nsCSSFrameConstructor::RemoveFloatingFirstLetterFrames
               aOldFrame->GetType() == nsGkAtoms::textFrame,
               "Must remove first continuation.");
  NS_ASSERTION(!(aOldFrame->GetStateBits() & NS_FRAME_OUT_OF_FLOW &&
                 GetPlaceholderFrameFor(aOldFrame)),
               "Must call RemoveFrame on placeholder for out-of-flows.");
  nsContainerFrame* parentFrame = aOldFrame->GetParent();
  if (parentFrame->IsAbsoluteContainer() &&
      aListID == parentFrame->GetAbsoluteListID()) {
    parentFrame->GetAbsoluteContainingBlock()->
      RemoveFrame(parentFrame, aListID, aOldFrame);
  } else {
    parentFrame->RemoveFrame(aListID, aOldFrame);
  }

  mIsDestroyingFrames = wasDestroyingFrames;
}

//----------------------------------------------------------------------

void
nsFrameManager::NotifyDestroyingFrame(nsIFrame* aFrame)
{
  nsIContent* content = aFrame->GetContent();
  if (content && content->GetPrimaryFrame() == aFrame) {
    ClearAllUndisplayedContentIn(content);
    ClearAllDisplayContentsIn(content);
  }
}

// Capture state for a given frame.
// Accept a content id here, in some cases we may not have content (scroll position)
void
nsFrameManager::CaptureFrameStateFor(nsIFrame* aFrame,
                                     nsILayoutHistoryState* aState)
{
  if (!aFrame || !aState) {
    NS_WARNING("null frame, or state");
    return;
  }

  // Only capture state for stateful frames
  nsIStatefulFrame* statefulFrame = do_QueryFrame(aFrame);
  if (!statefulFrame) {
    return;
  }

  // Capture the state, exit early if we get null (nothing to save)
  nsAutoPtr<nsPresState> frameState;
  nsresult rv = statefulFrame->SaveState(getter_Transfers(frameState));
  if (!frameState) {
    return;
  }

  // Generate the hash key to store the state under
  // Exit early if we get empty key
  nsAutoCString stateKey;
  nsIContent* content = aFrame->GetContent();
  nsIDocument* doc = content ? content->GetUncomposedDoc() : nullptr;
  rv = statefulFrame->GenerateStateKey(content, doc, stateKey);
  if(NS_FAILED(rv) || stateKey.IsEmpty()) {
    return;
  }

  // Store the state. aState owns frameState now.
  aState->AddState(stateKey, frameState.forget());
}

void
nsFrameManager::CaptureFrameState(nsIFrame* aFrame,
                                  nsILayoutHistoryState* aState)
{
  NS_PRECONDITION(nullptr != aFrame && nullptr != aState, "null parameters passed in");

  CaptureFrameStateFor(aFrame, aState);

  // Now capture state recursively for the frame hierarchy rooted at aFrame
  nsIFrame::ChildListIterator lists(aFrame);
  for (; !lists.IsDone(); lists.Next()) {
    nsFrameList::Enumerator childFrames(lists.CurrentList());
    for (; !childFrames.AtEnd(); childFrames.Next()) {
      nsIFrame* child = childFrames.get();
      if (child->GetStateBits() & NS_FRAME_OUT_OF_FLOW) {
        // We'll pick it up when we get to its placeholder
        continue;
      }
      // Make sure to walk through placeholders as needed, so that we
      // save state for out-of-flows which may not be our descendants
      // themselves but whose placeholders are our descendants.
      CaptureFrameState(nsPlaceholderFrame::GetRealFrameFor(child), aState);
    }
  }
}

// Restore state for a given frame.
// Accept a content id here, in some cases we may not have content (scroll position)
void
nsFrameManager::RestoreFrameStateFor(nsIFrame* aFrame,
                                     nsILayoutHistoryState* aState)
{
  if (!aFrame || !aState) {
    NS_WARNING("null frame or state");
    return;
  }

  // Only restore state for stateful frames
  nsIStatefulFrame* statefulFrame = do_QueryFrame(aFrame);
  if (!statefulFrame) {
    return;
  }

  // Generate the hash key the state was stored under
  // Exit early if we get empty key
  nsIContent* content = aFrame->GetContent();
  // If we don't have content, we can't generate a hash
  // key and there's probably no state information for us.
  if (!content) {
    return;
  }

  nsAutoCString stateKey;
  nsIDocument* doc = content->GetUncomposedDoc();
  nsresult rv = statefulFrame->GenerateStateKey(content, doc, stateKey);
  if (NS_FAILED(rv) || stateKey.IsEmpty()) {
    return;
  }

  // Get the state from the hash
  nsPresState* frameState = aState->GetState(stateKey);
  if (!frameState) {
    return;
  }

  // Restore it
  rv = statefulFrame->RestoreState(frameState);
  if (NS_FAILED(rv)) {
    return;
  }

  // If we restore ok, remove the state from the state table
  aState->RemoveState(stateKey);
}

void
nsFrameManager::RestoreFrameState(nsIFrame* aFrame,
                                  nsILayoutHistoryState* aState)
{
  NS_PRECONDITION(nullptr != aFrame && nullptr != aState, "null parameters passed in");
  
  RestoreFrameStateFor(aFrame, aState);

  // Now restore state recursively for the frame hierarchy rooted at aFrame
  nsIFrame::ChildListIterator lists(aFrame);
  for (; !lists.IsDone(); lists.Next()) {
    nsFrameList::Enumerator childFrames(lists.CurrentList());
    for (; !childFrames.AtEnd(); childFrames.Next()) {
      RestoreFrameState(childFrames.get(), aState);
    }
  }
}

//----------------------------------------------------------------------

nsFrameManagerBase::UndisplayedMap::UndisplayedMap()
{
  MOZ_COUNT_CTOR(nsFrameManagerBase::UndisplayedMap);
}

nsFrameManagerBase::UndisplayedMap::~UndisplayedMap(void)
{
  MOZ_COUNT_DTOR(nsFrameManagerBase::UndisplayedMap);
  Clear();
}

void
nsFrameManagerBase::UndisplayedMap::Clear()
{
  for (auto iter = Iter(); !iter.Done(); iter.Next()) {
    auto* list = iter.UserData();
    while (auto* node = list->popFirst()) {
      delete node;
    }
    iter.Remove();
  }
}


nsIContent*
nsFrameManagerBase::UndisplayedMap::GetApplicableParent(nsIContent* aParent)
{
  // In the case of XBL default content, <xbl:children> elements do not get a
  // frame causing a mismatch between the content tree and the frame tree.
  // |GetEntryFor| is sometimes called with the content tree parent (which may
  // be a <xbl:children> element) but the parent in the frame tree would be the
  // insertion parent (parent of the <xbl:children> element). Here the children
  // elements are normalized to the insertion parent to correct for the mismatch.
  if (aParent && nsContentUtils::IsContentInsertionPoint(aParent)) {
    return aParent->GetParent();
  }

  return aParent;
}

LinkedList<UndisplayedNode>*
nsFrameManagerBase::UndisplayedMap::GetListFor(nsIContent** aParent)
{
  *aParent = GetApplicableParent(*aParent);

  LinkedList<UndisplayedNode>* list;
  if (Get(*aParent, &list)) {
    return list;
  }

  return nullptr;
}

LinkedList<UndisplayedNode>*
nsFrameManagerBase::UndisplayedMap::GetOrCreateListFor(nsIContent** aParent)
{
  *aParent = GetApplicableParent(*aParent);
  return LookupOrAdd(*aParent);
}


UndisplayedNode*
nsFrameManagerBase::UndisplayedMap::GetFirstNode(nsIContent* aParentContent)
{
  auto* list = GetListFor(&aParentContent);
  return list ? list->getFirst() : nullptr;
}


void
nsFrameManagerBase::UndisplayedMap::AppendNodeFor(UndisplayedNode* aNode,
                                                  nsIContent* aParentContent)
{
  LinkedList<UndisplayedNode>* list = GetOrCreateListFor(&aParentContent);

#ifdef DEBUG
  for (UndisplayedNode* node = list->getFirst(); node; node = node->getNext()) {
    // NOTE: In the original code there was a work around for this case, I want
    // to check it still happens before hacking around it the same way.
    MOZ_ASSERT(node->mContent != aNode->mContent,
               "Duplicated content in undisplayed list!");
  }
#endif

  list->insertBack(aNode);
}

void
nsFrameManagerBase::UndisplayedMap::AddNodeFor(nsIContent* aParentContent,
                                               nsIContent* aChild,
                                               nsStyleContext* aStyle)
{
  UndisplayedNode*  node = new UndisplayedNode(aChild, aStyle);
  AppendNodeFor(node, aParentContent);
}

void
nsFrameManagerBase::UndisplayedMap::RemoveNodeFor(nsIContent* aParentContent,
                                                  UndisplayedNode* aNode)
{
#ifdef DEBUG
  auto list = GetListFor(&aParentContent);
  MOZ_ASSERT(list, "content not in map");
  aNode->removeFrom(*list);
#else
  aNode->remove();
#endif
  delete aNode;
}


nsAutoPtr<LinkedList<UndisplayedNode>>
nsFrameManagerBase::UndisplayedMap::UnlinkNodesFor(nsIContent* aParentContent)
{
  nsAutoPtr<LinkedList<UndisplayedNode>> list;
  RemoveAndForget(GetApplicableParent(aParentContent), list);
  return list;
}

void
nsFrameManagerBase::UndisplayedMap::RemoveNodesFor(nsIContent* aParentContent)
{
  nsAutoPtr<LinkedList<UndisplayedNode>> list = UnlinkNodesFor(aParentContent);
  if (list) {
    while (auto* node = list->popFirst()) {
      delete node;
    }
  }
}
