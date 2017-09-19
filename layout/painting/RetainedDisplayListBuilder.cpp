/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: set ts=2 sw=2 et tw=78:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "RetainedDisplayListBuilder.h"
#include "nsSubDocumentFrame.h"

using namespace mozilla;

void MarkFramesWithItemsAndImagesModified(nsDisplayList* aList)
{
  for (nsDisplayItem* i = aList->GetBottom(); i != nullptr; i = i->GetAbove()) {
    if (!i->HasDeletedFrame() && i->CanBeReused() && !i->Frame()->IsFrameModified()) {
      // If we have existing cached geometry for this item, then check that for
      // whether we need to invalidate for a sync decode. If we don't, then
      // use the item's flags.
      DisplayItemData* data = FrameLayerBuilder::GetOldDataFor(i);
      bool invalidate = false;
      if (data &&
          data->GetGeometry()) {
        invalidate = data->GetGeometry()->InvalidateForSyncDecodeImages();
      } else if (!(i->GetFlags() & TYPE_RENDERS_NO_IMAGES)) {
        invalidate = true;
      }

      if (invalidate) {
        i->Frame()->MarkNeedsDisplayItemRebuild();
      }
    }
    if (i->GetChildren()) {
      MarkFramesWithItemsAndImagesModified(i->GetChildren());
    }
  }
}

bool IsAnyAncestorModified(nsIFrame* aFrame)
{
  nsIFrame* f = aFrame;
  while (f) {
    if (f->IsFrameModified()) {
      return true;
    }
    f = nsLayoutUtils::GetCrossDocParentFrame(f);
  }
  return false;
}

// Removes any display items that belonged to a frame that was deleted,
// and mark frames that belong to a different AGR so that get their
// items built again.
// TODO: We currently descend into all children even if we don't have an AGR
// to mark, as child stacking contexts might. It would be nice if we could
// jump into those immediately rather than walking the entire thing.
void
RetainedDisplayListBuilder::PreProcessDisplayList(nsDisplayList* aList,
                                                  AnimatedGeometryRoot* aAGR)
{
  nsDisplayList saved;
  while (nsDisplayItem* i = aList->RemoveBottom()) {
    if (i->HasDeletedFrame() || !i->CanBeReused()) {
      i->Destroy(&mBuilder);
      continue;
    }

    if (i->GetChildren()) {
      AnimatedGeometryRoot *childAGR = aAGR;
      if (i->Frame()->IsStackingContext()) {
        if (i->Frame()->HasOverrideDirtyRegion()) {
          nsDisplayListBuilder::DisplayListBuildingData* data =
            i->Frame()->GetProperty(nsDisplayListBuilder::DisplayListBuildingRect());
          if (data) {
            childAGR = data->mModifiedAGR;
          }
        } else {
          childAGR = nullptr;
        }
      }
      PreProcessDisplayList(i->GetChildren(), childAGR);
    }

    // TODO: We should be able to check the clipped bounds relative
    // to the common AGR (of both the existing item and the invalidated
    // frame) and determine if they can ever intersect.
    if (aAGR && i->GetAnimatedGeometryRoot()->GetAsyncAGR() != aAGR) {
      mBuilder.MarkFrameForDisplayIfVisible(i->Frame());
    }

    // TODO: This is here because we sometimes reuse the previous display list
    // completely. For optimization, we could only restore the state for reused
    // display items.
    i->RestoreState();

    saved.AppendToTop(i);
  }
  aList->AppendToTop(&saved);
  aList->RestoreState();
}

bool IsSameItem(nsDisplayItem* aFirst, nsDisplayItem* aSecond)
{
  return aFirst->Frame() == aSecond->Frame() &&
         aFirst->GetPerFrameKey() == aSecond->GetPerFrameKey();
}

struct DisplayItemKey {

  bool operator ==(const DisplayItemKey& aOther) const {
    return mFrame == aOther.mFrame &&
           mKey == aOther.mKey;
  }

  nsIFrame* mFrame;
  uint32_t mKey;
};

class DisplayItemHashEntry : public PLDHashEntryHdr
{
public:
  typedef DisplayItemKey KeyType;
  typedef const DisplayItemKey* KeyTypePointer;

  explicit DisplayItemHashEntry(KeyTypePointer aKey)
    : mKey(*aKey) {}
  explicit DisplayItemHashEntry(const DisplayItemHashEntry& aCopy)=default;

  ~DisplayItemHashEntry() = default;

  KeyType GetKey() const { return mKey; }
  bool KeyEquals(KeyTypePointer aKey) const
  {
    return mKey == *aKey;
  }

  static KeyTypePointer KeyToPointer(KeyType& aKey) { return &aKey; }
  static PLDHashNumber HashKey(KeyTypePointer aKey)
  {
    if (!aKey)
      return 0;

    return mozilla::HashGeneric(aKey->mFrame, aKey->mKey);
  }
  enum { ALLOW_MEMMOVE = true };

  DisplayItemKey mKey;
};

template<typename T>
void SwapAndRemove(nsTArray<T>& aArray, uint32_t aIndex)
{
  if (aIndex != (aArray.Length() - 1)) {
    T last = aArray.LastElement();
    aArray.LastElement() = aArray[aIndex];
    aArray[aIndex] = last;
  }

  aArray.RemoveElementAt(aArray.Length() - 1);
}

void MergeFrameRects(nsDisplayLayerEventRegions* aOldItem,
                     nsDisplayLayerEventRegions* aNewItem,
                     nsDisplayLayerEventRegions::FrameRects nsDisplayLayerEventRegions::*aRectList,
                     bool aUpdateOld,
                     nsTArray<nsIFrame*>& aAddedFrames)
{
  // Go through the old item's rect list and remove any rectangles
  // belonging to invalidated frames (deleted frames should
  // already be gone at this point)
  nsDisplayLayerEventRegions::FrameRects& oldRects = aOldItem->*aRectList;
  uint32_t i = 0;
  while (i < oldRects.mFrames.Length()) {
    // TODO: As mentioned in nsDisplayLayerEventRegions, this
    // operation might perform really poorly on a vector.
    nsIFrame* f = oldRects.mFrames[i];
    if (IsAnyAncestorModified(f)) {
      MOZ_ASSERT(f != aOldItem->Frame());
      f->RealDisplayItemData().RemoveElement(aOldItem);
      SwapAndRemove(oldRects.mFrames, i);
      SwapAndRemove(oldRects.mBoxes, i);
    } else {
      i++;
    }
  }
  if (!aNewItem) {
    return;
  }

  // Copy items from the source list to the dest list, but
  // only if the dest doesn't already include them.
  nsDisplayItem* destItem;
  nsDisplayLayerEventRegions::FrameRects* destRects;
  nsDisplayLayerEventRegions::FrameRects* srcRects;
  if (aUpdateOld) {
    destItem = aOldItem;
    destRects = &(aOldItem->*aRectList);
    srcRects = &(aNewItem->*aRectList);
  } else {
    destItem = aNewItem;
    destRects = &(aNewItem->*aRectList);
    srcRects = &(aOldItem->*aRectList);
  }

  for (uint32_t i = 0; i < srcRects->mFrames.Length(); i++) {
    nsIFrame* f = srcRects->mFrames[i];
    if (!f->RealDisplayItemData().Contains(destItem)) {
      // If this frame isn't already in the destination item,
      // then add it!
      destRects->Add(f, srcRects->mBoxes[i]);

      // We also need to update RealDisplayItemData for 'f',
      // but that'll mess up this check for the following
      // FrameRects lists, so defer that until the end.
      aAddedFrames.AppendElement(f);
      MOZ_ASSERT(f != aOldItem->Frame());
    }

  }
}

void MergeLayerEventRegions(nsDisplayItem* aOldItem,
                            nsDisplayItem* aNewItem,
                            bool aUpdateOld)
{
  nsDisplayLayerEventRegions* oldItem =
    static_cast<nsDisplayLayerEventRegions*>(aOldItem);
  nsDisplayLayerEventRegions* newItem =
    static_cast<nsDisplayLayerEventRegions*>(aNewItem);

  nsTArray<nsIFrame*> addedFrames;

  MergeFrameRects(oldItem, newItem, &nsDisplayLayerEventRegions::mHitRegion, aUpdateOld, addedFrames);
  MergeFrameRects(oldItem, newItem, &nsDisplayLayerEventRegions::mMaybeHitRegion, aUpdateOld, addedFrames);
  MergeFrameRects(oldItem, newItem, &nsDisplayLayerEventRegions::mDispatchToContentHitRegion, aUpdateOld, addedFrames);
  MergeFrameRects(oldItem, newItem, &nsDisplayLayerEventRegions::mNoActionRegion, aUpdateOld, addedFrames);
  MergeFrameRects(oldItem, newItem, &nsDisplayLayerEventRegions::mHorizontalPanRegion, aUpdateOld, addedFrames);
  MergeFrameRects(oldItem, newItem, &nsDisplayLayerEventRegions::mVerticalPanRegion, aUpdateOld, addedFrames);

  // MergeFrameRects deferred updating the display item data list during
  // processing so that earlier calls didn't change the result of later
  // ones. Fix that up now.
  nsDisplayItem* dest = aUpdateOld ? aOldItem : aNewItem;
  for (nsIFrame* f : addedFrames) {
    if (!f->RealDisplayItemData().Contains(dest)) {
      f->RealDisplayItemData().AppendElement(dest);
    }
  }
}

void
RetainedDisplayListBuilder::IncrementSubDocPresShellPaintCount(nsDisplayItem* aItem)
{
  MOZ_ASSERT(aItem->GetType() == DisplayItemType::TYPE_SUBDOCUMENT);

  nsSubDocumentFrame* subDocFrame =
    static_cast<nsDisplaySubDocument*>(aItem)->SubDocumentFrame();
  MOZ_ASSERT(subDocFrame);

  nsIPresShell* presShell = subDocFrame->GetSubdocumentPresShellForPainting(0);
  MOZ_ASSERT(presShell);

  mBuilder.IncrementPresShellPaintCount(presShell);
}

void
RetainedDisplayListBuilder::MergeDisplayLists(nsDisplayList* aNewList,
                                              nsDisplayList* aOldList,
                                              nsDisplayList* aOutList)
{
  nsDisplayList merged;
  nsDisplayItem* old;

  const auto ReuseItem = [&](nsDisplayItem* aItem) {
    merged.AppendToTop(aItem);
    aItem->SetReused(true);

    if (aItem->GetType() == DisplayItemType::TYPE_SUBDOCUMENT) {
      IncrementSubDocPresShellPaintCount(aItem);
    }
  };

  nsDataHashtable<DisplayItemHashEntry, nsDisplayItem*> oldListLookup(aOldList->Count());

  for (nsDisplayItem* i = aOldList->GetBottom(); i != nullptr; i = i->GetAbove()) {
    i->SetReused(false);

    if (!aNewList->IsEmpty()) {
      oldListLookup.Put({ i->Frame(), i->GetPerFrameKey() }, i);
    }
  }

#ifdef DEBUG
  nsDataHashtable<DisplayItemHashEntry, nsDisplayItem*> newListLookup(aNewList->Count());
  for (nsDisplayItem* i = aNewList->GetBottom(); i != nullptr; i = i->GetAbove()) {
    if (newListLookup.Get({ i->Frame(), i->GetPerFrameKey() }, nullptr)) {
       MOZ_CRASH_UNSAFE_PRINTF("Duplicate display items detected!: %s(0x%p) type=%d key=%d",
                                i->Name(), i->Frame(),
                                static_cast<int>(i->GetType()), i->GetPerFrameKey());
    }
    newListLookup.Put({ i->Frame(), i->GetPerFrameKey() }, i);
  }
#endif

  while (nsDisplayItem* i = aNewList->RemoveBottom()) {
    // If the new item has a matching counterpart in the old list, copy all items
    // up to that one into the merged list, but discard the repeat.
    if (nsDisplayItem* oldItem = oldListLookup.Get({ i->Frame(), i->GetPerFrameKey() })) {
      if (oldItem->IsReused()) {
        // If we've already put the old item into the merged list (we might have iterated over it earlier)
        // then stick with that one. Merge any child lists, and then delete the new item.

        if (oldItem->GetChildren()) {
          MOZ_ASSERT(i->GetChildren());
          MergeDisplayLists(i->GetChildren(), oldItem->GetChildren(), oldItem->GetChildren());
          oldItem->UpdateBounds(&mBuilder);
        }
        if (oldItem->GetType() == DisplayItemType::TYPE_LAYER_EVENT_REGIONS) {
          MergeLayerEventRegions(oldItem, i, true);
        }
        i->Destroy(&mBuilder);
      } else {
        while ((old = aOldList->RemoveBottom()) && !IsSameItem(i, old)) {
          if (!IsAnyAncestorModified(old->FrameForInvalidation())) {
            ReuseItem(old);
          } else {
            // TODO: Is it going to be safe to call the dtor on a display item that belongs
            // to a deleted frame? Can we ensure that it is? Or do we need to make sure we
            // destroy display items during frame deletion.
            oldListLookup.Remove({ old->Frame(), old->GetPerFrameKey() });
            old->Destroy(&mBuilder);
          }
        }
        // Recursively merge any child lists.
        // TODO: We may need to call UpdateBounds on any non-flattenable nsDisplayWrapLists
        // here. Is there any other cached state that we need to update?
        MOZ_ASSERT(old && IsSameItem(i, old));

        if (old->GetType() == DisplayItemType::TYPE_LAYER_EVENT_REGIONS &&
            !IsAnyAncestorModified(old->FrameForInvalidation())) {
          // Event regions items don't have anything interesting other than
          // the lists of regions and frames, so we have no need to use the
          // newer item. Always use the old item instead since we assume it's
          // likely to have the bigger lists and merging will be quicker.
          MergeLayerEventRegions(old, i, true);
          ReuseItem(old);
          i->Destroy(&mBuilder);
        } else {
          if (!IsAnyAncestorModified(old->FrameForInvalidation()) &&
              old->GetChildren()) {
            MOZ_ASSERT(i->GetChildren());
            MergeDisplayLists(i->GetChildren(), old->GetChildren(), i->GetChildren());
            i->UpdateBounds(&mBuilder);
          }

          old->Destroy(&mBuilder);
          merged.AppendToTop(i);
        }

      }
    } else {
      merged.AppendToTop(i);
    }
  }

  MOZ_ASSERT(aNewList->IsEmpty());

  // Reuse the remaining items from the old display list.
  while ((old = aOldList->RemoveBottom())) {
    if (!IsAnyAncestorModified(old->FrameForInvalidation())) {
      ReuseItem(old);

      if (old->GetChildren()) {
        // We are calling MergeDisplayLists() to ensure that the display items
        // with modified or deleted children will be correctly handled.
        // Passing an empty new display list as an argument skips the merging
        // loop above and jumps back here.
        nsDisplayList empty;

        MergeDisplayLists(&empty, old->GetChildren(), old->GetChildren());
        old->UpdateBounds(&mBuilder);
      }
      if (old->GetType() == DisplayItemType::TYPE_LAYER_EVENT_REGIONS) {
        MergeLayerEventRegions(old, nullptr, false);
      }
    } else {
      old->Destroy(&mBuilder);
    }
  }

  aOutList->AppendToTop(&merged);
}

static void
AddModifiedFramesFromRootFrame(std::vector<WeakFrame>& aFrames,
                               nsIFrame* aRootFrame)
{
  MOZ_ASSERT(aRootFrame);

  std::vector<WeakFrame>* frames =
    aRootFrame->GetProperty(nsIFrame::ModifiedFrameList());

  if (frames) {
    for (WeakFrame& frame : *frames) {
      aFrames.push_back(Move(frame));
    }

    frames->clear();
  }
}

static bool
SubDocEnumCb(nsIDocument* aDocument, void* aData)
{
  MOZ_ASSERT(aDocument);
  MOZ_ASSERT(aData);

  std::vector<WeakFrame>* modifiedFrames =
    static_cast<std::vector<WeakFrame>*>(aData);

  nsIPresShell* presShell = aDocument->GetShell();
  nsIFrame* rootFrame = presShell ? presShell->GetRootFrame() : nullptr;

  if (rootFrame) {
    AddModifiedFramesFromRootFrame(*modifiedFrames, rootFrame);
  }

  aDocument->EnumerateSubDocuments(SubDocEnumCb, aData);
  return true;
}

static std::vector<WeakFrame>
GetModifiedFrames(nsIFrame* aDisplayRootFrame)
{
  MOZ_ASSERT(aDisplayRootFrame);

  std::vector<WeakFrame> modifiedFrames;
  AddModifiedFramesFromRootFrame(modifiedFrames, aDisplayRootFrame);

  nsIDocument *rootdoc = aDisplayRootFrame->PresContext()->Document();

  if (rootdoc) {
    rootdoc->EnumerateSubDocuments(SubDocEnumCb, &modifiedFrames);
  }

  return modifiedFrames;
}

// ComputeRebuildRegion  debugging
// #define CRR_DEBUG 0
#if CRR_DEBUG
#  define CRR_LOG(...) printf_stderr(__VA_ARGS__)
#else
#  define CRR_LOG(...)
#endif

bool
RetainedDisplayListBuilder::ComputeRebuildRegion(std::vector<WeakFrame>& aModifiedFrames,
                                                 nsIFrame* aDisplayRootFrame,
                                                 nsRect* aOutDirty,
                                                 AnimatedGeometryRoot** aOutModifiedAGR,
                                                 nsTArray<nsIFrame*>* aOutFramesWithProps)
{
  CRR_LOG("Computing rebuild regions for %d frames:\n", aModifiedFrames.size());
  for (nsIFrame* f : aModifiedFrames) {
    if (!f) {
      continue;
    }

    if (f->HasOverrideDirtyRegion()) {
      aOutFramesWithProps->AppendElement(f);
    }

    // TODO: There is almost certainly a faster way of doing this, probably can be combined with the ancestor
    // walk for TransformFrameRectToAncestor.
    AnimatedGeometryRoot* agr = mBuilder.FindAnimatedGeometryRootFor(f)->GetAsyncAGR();

    CRR_LOG("Processing frame %p with agr %p\n", f, agr->mFrame);


    // Convert the frame's overflow rect into the coordinate space
    // of the nearest stacking context that has an existing display item.
    // We store the overflow rect on that stacking context so that we build
    // all items that intersect that changed frame within the stacking context,
    // and then we use MarkFrameForDisplayIfVisible to make sure the stacking
    // context itself gets built. We don't need to build items that intersect outside
    // of the stacking context, since we know the stacking context item exists in
    // the old list, so we can trivially merge without needing other items.
    nsRect overflow = f->GetVisualOverflowRectRelativeToSelf();
    nsIFrame* currentFrame = f;

    while (currentFrame != aDisplayRootFrame) {
      overflow = nsLayoutUtils::TransformFrameRectToAncestor(currentFrame, overflow, aDisplayRootFrame,
                                                             nullptr, nullptr,
                                                             /* aStopAtStackingContextAndDisplayPort = */ true,
                                                             &currentFrame);
      MOZ_ASSERT(currentFrame);

      if (nsLayoutUtils::FrameHasDisplayPort(currentFrame)) {
        CRR_LOG("Frame belongs to displayport frame %p\n", currentFrame);
        nsIScrollableFrame* sf = do_QueryFrame(currentFrame);
        MOZ_ASSERT(sf);
        nsRect displayPort;
        DebugOnly<bool> hasDisplayPort =
          nsLayoutUtils::GetDisplayPort(currentFrame->GetContent(), &displayPort, RelativeTo::ScrollPort);
        MOZ_ASSERT(hasDisplayPort);
        // get it relative to the scrollport (from the scrollframe)
        nsRect r = overflow - sf->GetScrollPortRect().TopLeft();
        r.IntersectRect(r, displayPort);
        if (!r.IsEmpty()) {
          nsRect* rect =
            currentFrame->GetProperty(nsDisplayListBuilder::DisplayListBuildingDisplayPortRect());
          if (!rect) {
            rect = new nsRect();
            currentFrame->SetProperty(nsDisplayListBuilder::DisplayListBuildingDisplayPortRect(), rect);
            currentFrame->SetHasOverrideDirtyRegion(true);
          }
          rect->UnionRect(*rect, r);
          aOutFramesWithProps->AppendElement(currentFrame);
          CRR_LOG("Adding area to displayport draw area: %d %d %d %d\n", r.x, r.y, r.width, r.height);

          // TODO: Can we just use MarkFrameForDisplayIfVisible, plus MarkFramesForDifferentAGR to
          // ensure that this displayport, plus any items that move relative to it get rebuilt,
          // and then not contribute to the root dirty area?
          overflow = sf->GetScrollPortRect();
        } else {
          // Don't contribute to the root dirty area at all.
          overflow.SetEmpty();
          break;
        }
      }

      if (currentFrame->IsStackingContext()) {
        CRR_LOG("Frame belongs to stacking context frame %p\n", currentFrame);
        // If we found an intermediate stacking context with an existing display item
        // then we can store the dirty rect there and stop.
        if (currentFrame != aDisplayRootFrame &&
            currentFrame->RealDisplayItemData().Length() != 0) {
          mBuilder.MarkFrameForDisplayIfVisible(currentFrame);

          // Store the stacking context relative dirty area such
          // that display list building will pick it up when it
          // gets to it.
          nsDisplayListBuilder::DisplayListBuildingData* data =
            currentFrame->GetProperty(nsDisplayListBuilder::DisplayListBuildingRect());
          if (!data) {
            data = new nsDisplayListBuilder::DisplayListBuildingData;
            currentFrame->SetProperty(nsDisplayListBuilder::DisplayListBuildingRect(), data);
            currentFrame->SetHasOverrideDirtyRegion(true);
            aOutFramesWithProps->AppendElement(currentFrame);
          }
          data->mDirtyRect.UnionRect(data->mDirtyRect, overflow);
          CRR_LOG("Adding area to stacking context draw area: %d %d %d %d\n", overflow.x, overflow.y, overflow.width, overflow.height);
          if (!data->mModifiedAGR) {
            data->mModifiedAGR = agr;
          } else if (data->mModifiedAGR != agr) {
            data->mDirtyRect = currentFrame->GetVisualOverflowRectRelativeToSelf();
            CRR_LOG("Found multiple modified AGRs within this stacking context, giving up\n");
          }

          // Don't contribute to the root dirty area at all.
          agr = nullptr;
          overflow.SetEmpty();
          break;
        }
      }
    }
    aOutDirty->UnionRect(*aOutDirty, overflow);
    CRR_LOG("Adding area to root draw area: %d %d %d %d\n", overflow.x, overflow.y, overflow.width, overflow.height);

    // If we get changed frames from multiple AGRS, then just give up as it gets really complex to
    // track which items would need to be marked in MarkFramesForDifferentAGR.
    // TODO: We should store the modifiedAGR on the per-stacking context data and only do the
    // marking within the scope of the current stacking context.
    if (!*aOutModifiedAGR) {
      *aOutModifiedAGR = agr;
    } else if (agr && *aOutModifiedAGR != agr) {
      CRR_LOG("Found multiple AGRs in root stacking context, giving up\n");
      return false;
    }
  }

  return true;
}


bool
RetainedDisplayListBuilder::AttemptPartialUpdate(nsDisplayList* aList,
                                                 nsIFrame* aFrame,
                                                 nscolor aBackstop)
{
  mBuilder.RemoveModifiedWindowDraggingRegion();
  if (mBuilder.ShouldSyncDecodeImages()) {
    MarkFramesWithItemsAndImagesModified(&mList);
  }

  std::vector<WeakFrame> modifiedFrames = GetModifiedFrames(aFrame);

  if (mPreviousCaret != mBuilder.GetCaretFrame()) {
    if (mPreviousCaret) {
      mBuilder.MarkFrameModifiedDuringBuilding(mPreviousCaret);
    }

    if (mBuilder.GetCaretFrame()) {
      mBuilder.MarkFrameModifiedDuringBuilding(mBuilder.GetCaretFrame());
    }

    mPreviousCaret = mBuilder.GetCaretFrame();
  }

  nsRect modifiedDirty;
  AnimatedGeometryRoot* modifiedAGR = nullptr;
  nsTArray<nsIFrame*> framesWithProps;
  bool merged = false;
  if (!aList->IsEmpty() &&
      ComputeRebuildRegion(modifiedFrames, aFrame, &modifiedDirty, &modifiedAGR, &framesWithProps)) {
    modifiedDirty.IntersectRect(modifiedDirty, aFrame->GetVisualOverflowRectRelativeToSelf());

    PreProcessDisplayList(aList, modifiedAGR);

    nsDisplayList modifiedDL;
    if (!modifiedDirty.IsEmpty() || !framesWithProps.IsEmpty()) {
      mBuilder.SetDirtyRect(modifiedDirty);
      mBuilder.SetPartialUpdate(true);
      aFrame->BuildDisplayListForStackingContext(&mBuilder, &modifiedDL);
      nsLayoutUtils::AddExtraBackgroundItems(mBuilder, modifiedDL, aFrame,
                                             nsRect(nsPoint(0, 0), aFrame->GetSize()),
                                             aFrame->GetVisualOverflowRectRelativeToSelf(),
                                             aBackstop);
      mBuilder.SetPartialUpdate(false);

      //printf_stderr("Painting --- Modified list (dirty %d,%d,%d,%d):\n",
      //      modifiedDirty.x, modifiedDirty.y, modifiedDirty.width, modifiedDirty.height);
      //nsFrame::PrintDisplayList(&builder, modifiedDL);

      mBuilder.LeavePresShell(aFrame, &modifiedDL);
      mBuilder.EnterPresShell(aFrame);
    } else {
      // TODO: We can also skip layer building and painting if
      // PreProcessDisplayList didn't end up changing anything
      // Invariant: display items should have their original state here.
      // printf_stderr("Skipping display list building since nothing needed to be done\n");
    }

    // |modifiedDL| can sometimes be empty here. We still perform the
    // display list merging to prune unused items (for example, items that
    // are not visible anymore) from the old list.
    // TODO: Optimization opportunity. In this case, MergeDisplayLists()
    // unnecessarily creates a hashtable of the old items.
    MergeDisplayLists(&modifiedDL, aList, aList);

    //printf_stderr("Painting --- Merged list:\n");
    //nsFrame::PrintDisplayList(&builder, list);

    merged = true;
  }

  // TODO: Do we mark frames as modified during displaylist building? If
  // we do this isn't gonna work.
  for (nsIFrame* f : modifiedFrames) {
    if (f) {
      f->SetFrameIsModified(false);
    }
  }
  modifiedFrames.clear();

  for (nsIFrame* f: framesWithProps) {
    f->SetHasOverrideDirtyRegion(false);
    f->DeleteProperty(nsDisplayListBuilder::DisplayListBuildingRect());
    f->DeleteProperty(nsDisplayListBuilder::DisplayListBuildingDisplayPortRect());
  }

  return merged;
}
