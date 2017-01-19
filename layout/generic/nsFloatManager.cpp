/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* class that manages rules for positioning floats */

#include "nsFloatManager.h"

#include <algorithm>

#include "mozilla/ReflowInput.h"
#include "mozilla/ShapeUtils.h"
#include "nsBlockFrame.h"
#include "nsError.h"
#include "nsIPresShell.h"
#include "nsMemory.h"

using namespace mozilla;

int32_t nsFloatManager::sCachedFloatManagerCount = 0;
void* nsFloatManager::sCachedFloatManagers[NS_FLOAT_MANAGER_CACHE_SIZE];

/////////////////////////////////////////////////////////////////////////////

// PresShell Arena allocate callback (for nsIntervalSet use below)
static void*
PSArenaAllocCB(size_t aSize, void* aClosure)
{
  return static_cast<nsIPresShell*>(aClosure)->AllocateMisc(aSize);
}

// PresShell Arena free callback (for nsIntervalSet use below)
static void
PSArenaFreeCB(size_t aSize, void* aPtr, void* aClosure)
{
  static_cast<nsIPresShell*>(aClosure)->FreeMisc(aSize, aPtr);
}

/////////////////////////////////////////////////////////////////////////////
// nsFloatManager

nsFloatManager::nsFloatManager(nsIPresShell* aPresShell,
                               mozilla::WritingMode aWM)
  :
#ifdef DEBUG
    mWritingMode(aWM),
#endif
    mLineLeft(0), mBlockStart(0),
    mFloatDamage(PSArenaAllocCB, PSArenaFreeCB, aPresShell),
    mPushedLeftFloatPastBreak(false),
    mPushedRightFloatPastBreak(false),
    mSplitLeftFloatAcrossBreak(false),
    mSplitRightFloatAcrossBreak(false)
{
  MOZ_COUNT_CTOR(nsFloatManager);
}

nsFloatManager::~nsFloatManager()
{
  MOZ_COUNT_DTOR(nsFloatManager);
}

// static
void* nsFloatManager::operator new(size_t aSize) CPP_THROW_NEW
{
  if (sCachedFloatManagerCount > 0) {
    // We have cached unused instances of this class, return a cached
    // instance in stead of always creating a new one.
    return sCachedFloatManagers[--sCachedFloatManagerCount];
  }

  // The cache is empty, this means we have to create a new instance using
  // the global |operator new|.
  return moz_xmalloc(aSize);
}

void
nsFloatManager::operator delete(void* aPtr, size_t aSize)
{
  if (!aPtr)
    return;
  // This float manager is no longer used, if there's still room in
  // the cache we'll cache this float manager, unless the layout
  // module was already shut down.

  if (sCachedFloatManagerCount < NS_FLOAT_MANAGER_CACHE_SIZE &&
      sCachedFloatManagerCount >= 0) {
    // There's still space in the cache for more instances, put this
    // instance in the cache in stead of deleting it.

    sCachedFloatManagers[sCachedFloatManagerCount++] = aPtr;
    return;
  }

  // The cache is full, or the layout module has been shut down,
  // delete this float manager.
  free(aPtr);
}


/* static */
void nsFloatManager::Shutdown()
{
  // The layout module is being shut down, clean up the cache and
  // disable further caching.

  int32_t i;

  for (i = 0; i < sCachedFloatManagerCount; i++) {
    void* floatManager = sCachedFloatManagers[i];
    if (floatManager)
      free(floatManager);
  }

  // Disable further caching.
  sCachedFloatManagerCount = -1;
}

#define CHECK_BLOCK_AND_LINE_DIR(aWM) \
  NS_ASSERTION((aWM).GetBlockDir() == mWritingMode.GetBlockDir() &&     \
               (aWM).IsLineInverted() == mWritingMode.IsLineInverted(), \
               "incompatible writing modes")

nsFlowAreaRect
nsFloatManager::GetFlowArea(WritingMode aWM, nscoord aBCoord, nscoord aBSize,
                            BandInfoType aBandInfoType, ShapeType aShapeType,
                            LogicalRect aContentArea, SavedState* aState,
                            const nsSize& aContainerSize) const
{
  CHECK_BLOCK_AND_LINE_DIR(aWM);
  NS_ASSERTION(aBSize >= 0, "unexpected max block size");
  NS_ASSERTION(aContentArea.ISize(aWM) >= 0,
               "unexpected content area inline size");

  nscoord blockStart = aBCoord + mBlockStart;
  if (blockStart < nscoord_MIN) {
    NS_WARNING("bad value");
    blockStart = nscoord_MIN;
  }

  // Determine the last float that we should consider.
  uint32_t floatCount;
  if (aState) {
    // Use the provided state.
    floatCount = aState->mFloatInfoCount;
    MOZ_ASSERT(floatCount <= mFloats.Length(), "bad state");
  } else {
    // Use our current state.
    floatCount = mFloats.Length();
  }

  // If there are no floats at all, or we're below the last one, return
  // quickly.
  if (floatCount == 0 ||
      (mFloats[floatCount-1].mLeftBEnd <= blockStart &&
       mFloats[floatCount-1].mRightBEnd <= blockStart)) {
    return nsFlowAreaRect(aWM, aContentArea.IStart(aWM), aBCoord,
                          aContentArea.ISize(aWM), aBSize, false);
  }

  nscoord blockEnd;
  if (aBSize == nscoord_MAX) {
    // This warning (and the two below) are possible to hit on pages
    // with really large objects.
    NS_WARNING_ASSERTION(aBandInfoType == BandInfoType::BandFromPoint, "bad height");
    blockEnd = nscoord_MAX;
  } else {
    blockEnd = blockStart + aBSize;
    if (blockEnd < blockStart || blockEnd > nscoord_MAX) {
      NS_WARNING("bad value");
      blockEnd = nscoord_MAX;
    }
  }
  nscoord lineLeft = mLineLeft + aContentArea.LineLeft(aWM, aContainerSize);
  nscoord lineRight = mLineLeft + aContentArea.LineRight(aWM, aContainerSize);
  if (lineRight < lineLeft) {
    NS_WARNING("bad value");
    lineRight = lineLeft;
  }

  // Walk backwards through the floats until we either hit the front of
  // the list or we're above |blockStart|.
  bool haveFloats = false;
  for (uint32_t i = floatCount; i > 0; --i) {
    const FloatInfo &fi = mFloats[i-1];
    if (fi.mLeftBEnd <= blockStart && fi.mRightBEnd <= blockStart) {
      // There aren't any more floats that could intersect this band.
      break;
    }
    if (fi.IsEmpty(aShapeType)) {
      // For compatibility, ignore floats with empty rects, even though it
      // disagrees with the spec.  (We might want to fix this in the
      // future, though.)
      continue;
    }

    nscoord floatBStart = fi.BStart(aShapeType);
    nscoord floatBEnd = fi.BEnd(aShapeType);
    if (blockStart < floatBStart && aBandInfoType == BandInfoType::BandFromPoint) {
      // This float is below our band.  Shrink our band's height if needed.
      if (floatBStart < blockEnd) {
        blockEnd = floatBStart;
      }
    }
    // If blockStart == blockEnd (which happens only with WidthWithinHeight),
    // we include floats that begin at our 0-height vertical area.  We
    // need to do this to satisfy the invariant that a
    // WidthWithinHeight call is at least as narrow on both sides as a
    // BandFromPoint call beginning at its blockStart.
    else if (blockStart < floatBEnd &&
             (floatBStart < blockEnd ||
              (floatBStart == blockEnd && blockStart == blockEnd))) {
      // This float is in our band.

      // Shrink our band's width if needed.
      StyleFloat floatStyle = fi.mFrame->StyleDisplay()->PhysicalFloats(aWM);

      // When aBandInfoType is BandFromPoint, we're only intended to
      // consider a point along the y axis rather than a band.
      const nscoord bandBlockEnd =
        aBandInfoType == BandInfoType::BandFromPoint ? blockStart : blockEnd;
      if (floatStyle == StyleFloat::Left) {
        // A left float
        nscoord lineRightEdge =
          fi.LineRight(aWM, aShapeType, blockStart, bandBlockEnd);
        if (lineRightEdge > lineLeft) {
          lineLeft = lineRightEdge;
          // Only set haveFloats to true if the float is inside our
          // containing block.  This matches the spec for what some
          // callers want and disagrees for other callers, so we should
          // probably provide better information at some point.
          haveFloats = true;
        }
      } else {
        // A right float
        nscoord lineLeftEdge =
          fi.LineLeft(aWM, aShapeType, blockStart, bandBlockEnd);
        if (lineLeftEdge < lineRight) {
          lineRight = lineLeftEdge;
          // See above.
          haveFloats = true;
        }
      }

      // Shrink our band's height if needed.
      if (floatBEnd < blockEnd && aBandInfoType == BandInfoType::BandFromPoint) {
        blockEnd = floatBEnd;
      }
    }
  }

  nscoord blockSize = (blockEnd == nscoord_MAX) ?
                       nscoord_MAX : (blockEnd - blockStart);
  // convert back from LineLeft/Right to IStart
  nscoord inlineStart = aWM.IsBidiLTR()
                        ? lineLeft - mLineLeft
                        : mLineLeft - lineRight +
                          LogicalSize(aWM, aContainerSize).ISize(aWM);

  return nsFlowAreaRect(aWM, inlineStart, blockStart - mBlockStart,
                        lineRight - lineLeft, blockSize, haveFloats);
}

void
nsFloatManager::AddFloat(nsIFrame* aFloatFrame, const LogicalRect& aMarginRect,
                         WritingMode aWM, const nsSize& aContainerSize)
{
  CHECK_BLOCK_AND_LINE_DIR(aWM);
  NS_ASSERTION(aMarginRect.ISize(aWM) >= 0, "negative inline size!");
  NS_ASSERTION(aMarginRect.BSize(aWM) >= 0, "negative block size!");

  FloatInfo info(aFloatFrame, mLineLeft, mBlockStart, aMarginRect, aWM,
                 aContainerSize);

  // Set mLeftBEnd and mRightBEnd.
  if (HasAnyFloats()) {
    FloatInfo &tail = mFloats[mFloats.Length() - 1];
    info.mLeftBEnd = tail.mLeftBEnd;
    info.mRightBEnd = tail.mRightBEnd;
  } else {
    info.mLeftBEnd = nscoord_MIN;
    info.mRightBEnd = nscoord_MIN;
  }
  StyleFloat floatStyle = aFloatFrame->StyleDisplay()->PhysicalFloats(aWM);
  MOZ_ASSERT(floatStyle == StyleFloat::Left || floatStyle == StyleFloat::Right,
             "Unexpected float style!");
  nscoord& sideBEnd =
    floatStyle == StyleFloat::Left ? info.mLeftBEnd : info.mRightBEnd;
  nscoord thisBEnd = info.BEnd();
  if (thisBEnd > sideBEnd)
    sideBEnd = thisBEnd;

  mFloats.AppendElement(Move(info));
}

// static
LogicalRect
nsFloatManager::CalculateRegionFor(WritingMode          aWM,
                                   nsIFrame*            aFloat,
                                   const LogicalMargin& aMargin,
                                   const nsSize&        aContainerSize)
{
  // We consider relatively positioned frames at their original position.
  LogicalRect region(aWM, nsRect(aFloat->GetNormalPosition(),
                                 aFloat->GetSize()),
                     aContainerSize);

  // Float region includes its margin
  region.Inflate(aWM, aMargin);

  // Don't store rectangles with negative margin-box width or height in
  // the float manager; it can't deal with them.
  if (region.ISize(aWM) < 0) {
    // Preserve the right margin-edge for left floats and the left
    // margin-edge for right floats
    const nsStyleDisplay* display = aFloat->StyleDisplay();
    StyleFloat floatStyle = display->PhysicalFloats(aWM);
    if ((StyleFloat::Left == floatStyle) == aWM.IsBidiLTR()) {
      region.IStart(aWM) = region.IEnd(aWM);
    }
    region.ISize(aWM) = 0;
  }
  if (region.BSize(aWM) < 0) {
    region.BSize(aWM) = 0;
  }
  return region;
}

NS_DECLARE_FRAME_PROPERTY_DELETABLE(FloatRegionProperty, nsMargin)

LogicalRect
nsFloatManager::GetRegionFor(WritingMode aWM, nsIFrame* aFloat,
                             const nsSize& aContainerSize)
{
  LogicalRect region = aFloat->GetLogicalRect(aWM, aContainerSize);
  void* storedRegion = aFloat->Properties().Get(FloatRegionProperty());
  if (storedRegion) {
    nsMargin margin = *static_cast<nsMargin*>(storedRegion);
    region.Inflate(aWM, LogicalMargin(aWM, margin));
  }
  return region;
}

void
nsFloatManager::StoreRegionFor(WritingMode aWM, nsIFrame* aFloat,
                               const LogicalRect& aRegion,
                               const nsSize& aContainerSize)
{
  nsRect region = aRegion.GetPhysicalRect(aWM, aContainerSize);
  nsRect rect = aFloat->GetRect();
  FrameProperties props = aFloat->Properties();
  if (region.IsEqualEdges(rect)) {
    props.Delete(FloatRegionProperty());
  }
  else {
    nsMargin* storedMargin = props.Get(FloatRegionProperty());
    if (!storedMargin) {
      storedMargin = new nsMargin();
      props.Set(FloatRegionProperty(), storedMargin);
    }
    *storedMargin = region - rect;
  }
}

nsresult
nsFloatManager::RemoveTrailingRegions(nsIFrame* aFrameList)
{
  if (!aFrameList) {
    return NS_OK;
  }
  // This could be a good bit simpler if we could guarantee that the
  // floats given were at the end of our list, so we could just search
  // for the head of aFrameList.  (But we can't;
  // layout/reftests/bugs/421710-1.html crashes.)
  nsTHashtable<nsPtrHashKey<nsIFrame> > frameSet(1);

  for (nsIFrame* f = aFrameList; f; f = f->GetNextSibling()) {
    frameSet.PutEntry(f);
  }

  uint32_t newLength = mFloats.Length();
  while (newLength > 0) {
    if (!frameSet.Contains(mFloats[newLength - 1].mFrame)) {
      break;
    }
    --newLength;
  }
  mFloats.TruncateLength(newLength);

#ifdef DEBUG
  for (uint32_t i = 0; i < mFloats.Length(); ++i) {
    NS_ASSERTION(!frameSet.Contains(mFloats[i].mFrame),
                 "Frame region deletion was requested but we couldn't delete it");
  }
#endif

  return NS_OK;
}

void
nsFloatManager::PushState(SavedState* aState)
{
  NS_PRECONDITION(aState, "Need a place to save state");

  // This is a cheap push implementation, which
  // only saves the (x,y) and last frame in the mFrameInfoMap
  // which is enough info to get us back to where we should be
  // when pop is called.
  //
  // This push/pop mechanism is used to undo any
  // floats that were added during the unconstrained reflow
  // in nsBlockReflowContext::DoReflowBlock(). (See bug 96736)
  //
  // It should also be noted that the state for mFloatDamage is
  // intentionally not saved or restored in PushState() and PopState(),
  // since that could lead to bugs where damage is missed/dropped when
  // we move from position A to B (during the intermediate incremental
  // reflow mentioned above) and then from B to C during the subsequent
  // reflow. In the typical case A and C will be the same, but not always.
  // Allowing mFloatDamage to accumulate the damage incurred during both
  // reflows ensures that nothing gets missed.
  aState->mLineLeft = mLineLeft;
  aState->mBlockStart = mBlockStart;
  aState->mPushedLeftFloatPastBreak = mPushedLeftFloatPastBreak;
  aState->mPushedRightFloatPastBreak = mPushedRightFloatPastBreak;
  aState->mSplitLeftFloatAcrossBreak = mSplitLeftFloatAcrossBreak;
  aState->mSplitRightFloatAcrossBreak = mSplitRightFloatAcrossBreak;
  aState->mFloatInfoCount = mFloats.Length();
}

void
nsFloatManager::PopState(SavedState* aState)
{
  NS_PRECONDITION(aState, "No state to restore?");

  mLineLeft = aState->mLineLeft;
  mBlockStart = aState->mBlockStart;
  mPushedLeftFloatPastBreak = aState->mPushedLeftFloatPastBreak;
  mPushedRightFloatPastBreak = aState->mPushedRightFloatPastBreak;
  mSplitLeftFloatAcrossBreak = aState->mSplitLeftFloatAcrossBreak;
  mSplitRightFloatAcrossBreak = aState->mSplitRightFloatAcrossBreak;

  NS_ASSERTION(aState->mFloatInfoCount <= mFloats.Length(),
               "somebody misused PushState/PopState");
  mFloats.TruncateLength(aState->mFloatInfoCount);
}

nscoord
nsFloatManager::GetLowestFloatTop() const
{
  if (mPushedLeftFloatPastBreak || mPushedRightFloatPastBreak) {
    return nscoord_MAX;
  }
  if (!HasAnyFloats()) {
    return nscoord_MIN;
  }
  return mFloats[mFloats.Length() -1].BStart() - mBlockStart;
}

#ifdef DEBUG_FRAME_DUMP
void
DebugListFloatManager(const nsFloatManager *aFloatManager)
{
  aFloatManager->List(stdout);
}

nsresult
nsFloatManager::List(FILE* out) const
{
  if (!HasAnyFloats())
    return NS_OK;

  for (uint32_t i = 0; i < mFloats.Length(); ++i) {
    const FloatInfo &fi = mFloats[i];
    fprintf_stderr(out, "Float %u: frame=%p rect={%d,%d,%d,%d} BEnd={l:%d, r:%d}\n",
                   i, static_cast<void*>(fi.mFrame),
                   fi.LineLeft(), fi.BStart(), fi.ISize(), fi.BSize(),
                   fi.mLeftBEnd, fi.mRightBEnd);
  }
  return NS_OK;
}
#endif

nscoord
nsFloatManager::ClearFloats(nscoord aBCoord, StyleClear aBreakType,
                            uint32_t aFlags) const
{
  if (!(aFlags & DONT_CLEAR_PUSHED_FLOATS) && ClearContinues(aBreakType)) {
    return nscoord_MAX;
  }
  if (!HasAnyFloats()) {
    return aBCoord;
  }

  nscoord blockEnd = aBCoord + mBlockStart;

  const FloatInfo &tail = mFloats[mFloats.Length() - 1];
  switch (aBreakType) {
    case StyleClear::Both:
      blockEnd = std::max(blockEnd, tail.mLeftBEnd);
      blockEnd = std::max(blockEnd, tail.mRightBEnd);
      break;
    case StyleClear::Left:
      blockEnd = std::max(blockEnd, tail.mLeftBEnd);
      break;
    case StyleClear::Right:
      blockEnd = std::max(blockEnd, tail.mRightBEnd);
      break;
    default:
      // Do nothing
      break;
  }

  blockEnd -= mBlockStart;

  return blockEnd;
}

bool
nsFloatManager::ClearContinues(StyleClear aBreakType) const
{
  return ((mPushedLeftFloatPastBreak || mSplitLeftFloatAcrossBreak) &&
          (aBreakType == StyleClear::Both ||
           aBreakType == StyleClear::Left)) ||
         ((mPushedRightFloatPastBreak || mSplitRightFloatAcrossBreak) &&
          (aBreakType == StyleClear::Both ||
           aBreakType == StyleClear::Right));
}

/////////////////////////////////////////////////////////////////////////////
// BoxShapeInfo

nscoord
nsFloatManager::BoxShapeInfo::LineLeft(WritingMode aWM,
                                       const nscoord aBStart,
                                       const nscoord aBEnd) const
{
  nscoord radii[8];
  bool hasRadii = mFrame->GetShapeBoxBorderRadii(radii);

  if (!hasRadii) {
    return mShapeBoxRect.x;
  }

  // Get the physical side for line-left since border-radii are in
  // the physical axis.
  mozilla::Side lineLeftSide =
    aWM.PhysicalSide(aWM.LogicalSideForLineRelativeDir(eLineRelativeDirLeft));
  nscoord blockStartCornerRadiusL =
    radii[SideToHalfCorner(lineLeftSide, true, false)];
  nscoord blockStartCornerRadiusB =
    radii[SideToHalfCorner(lineLeftSide, true, true)];
  nscoord blockEndCornerRadiusL =
    radii[SideToHalfCorner(lineLeftSide, false, false)];
  nscoord blockEndCornerRadiusB =
    radii[SideToHalfCorner(lineLeftSide, false, true)];

  if (aWM.IsLineInverted()) {
    // This happens only when aWM is vertical-lr. Need to swap blockStart
    // and blockEnd corners.
    std::swap(blockStartCornerRadiusL, blockEndCornerRadiusL);
    std::swap(blockStartCornerRadiusB, blockEndCornerRadiusB);
  }

  nscoord lineLeftDiff =
    ComputeEllipseLineInterceptDiff(
      mShapeBoxRect.y, mShapeBoxRect.YMost(),
      blockStartCornerRadiusL, blockStartCornerRadiusB,
      blockEndCornerRadiusL, blockEndCornerRadiusB,
      aBStart, aBEnd);
  return mShapeBoxRect.x + lineLeftDiff;
}

nscoord
nsFloatManager::BoxShapeInfo::LineRight(WritingMode aWM,
                                        const nscoord aBStart,
                                        const nscoord aBEnd) const
{
  nscoord radii[8];
  bool hasRadii = mFrame->GetShapeBoxBorderRadii(radii);

  if (!hasRadii) {
    return mShapeBoxRect.XMost();
  }

  // Get the physical side for line-right since border-radii are in
  // the physical axis.
  mozilla::Side lineRightSide =
    aWM.PhysicalSide(aWM.LogicalSideForLineRelativeDir(eLineRelativeDirRight));
  nscoord blockStartCornerRadiusL =
    radii[SideToHalfCorner(lineRightSide, false, false)];
  nscoord blockStartCornerRadiusB =
    radii[SideToHalfCorner(lineRightSide, false, true)];
  nscoord blockEndCornerRadiusL =
    radii[SideToHalfCorner(lineRightSide, true, false)];
  nscoord blockEndCornerRadiusB =
    radii[SideToHalfCorner(lineRightSide, true, true)];

  if (aWM.IsLineInverted()) {
    // This happens only when aWM is vertical-lr. Need to swap blockStart
    // and blockEnd corners.
    std::swap(blockStartCornerRadiusL, blockEndCornerRadiusL);
    std::swap(blockStartCornerRadiusB, blockEndCornerRadiusB);
  }

  nscoord lineRightDiff =
    ComputeEllipseLineInterceptDiff(
      mShapeBoxRect.y, mShapeBoxRect.YMost(),
      blockStartCornerRadiusL, blockStartCornerRadiusB,
      blockEndCornerRadiusL, blockEndCornerRadiusB,
      aBStart, aBEnd);
  return mShapeBoxRect.XMost() - lineRightDiff;
}

/////////////////////////////////////////////////////////////////////////////
// CircleShapeInfo

nsFloatManager::CircleShapeInfo::CircleShapeInfo(
  StyleBasicShape* const aBasicShape,
  nscoord aLineLeft,
  nscoord aBlockStart,
  const LogicalRect& aShapeBoxRect,
  WritingMode aWM,
  const nsSize& aContainerSize)
{
  // Use physical coordinates to compute the center of the circle() since
  // the <position> keywords such as 'left', 'top', etc. are physical.
  // https://drafts.csswg.org/css-shapes-1/#funcdef-circle
  nsRect physicalShapeBoxRect =
    aShapeBoxRect.GetPhysicalRect(aWM, aContainerSize);
  nsPoint physicalCenter =
    ShapeUtils::ComputeCircleOrEllipseCenter(aBasicShape, physicalShapeBoxRect);
  mRadius =
    ShapeUtils::ComputeCircleRadius(aBasicShape, physicalCenter,
                                    physicalShapeBoxRect);

  // Convert the coordinate space back to the same as FloatInfo::mRect.
  // mCenter.x is in the line-axis of the frame manager and mCenter.y are in
  // the frame manager's real block-axis.
  LogicalPoint logicalCenter(aWM, physicalCenter, aContainerSize);
  mCenter = nsPoint(logicalCenter.LineRelative(aWM, aContainerSize) + aLineLeft,
                    logicalCenter.B(aWM) + aBlockStart);
}

nscoord
nsFloatManager::CircleShapeInfo::LineLeft(WritingMode aWM,
                                          const nscoord aBStart,
                                          const nscoord aBEnd) const
{
  nscoord lineLeftDiff =
    ComputeEllipseLineInterceptDiff(BStart(), BEnd(),
                                    mRadius, mRadius, mRadius, mRadius,
                                    aBStart, aBEnd);
  return mCenter.x - mRadius + lineLeftDiff;
}

nscoord
nsFloatManager::CircleShapeInfo::LineRight(WritingMode aWM,
                                           const nscoord aBStart,
                                           const nscoord aBEnd) const
{
  nscoord lineRightDiff =
    ComputeEllipseLineInterceptDiff(BStart(), BEnd(),
                                    mRadius, mRadius, mRadius, mRadius,
                                    aBStart, aBEnd);
  return mCenter.x + mRadius - lineRightDiff;
}

/////////////////////////////////////////////////////////////////////////////
// FloatInfo

nsFloatManager::FloatInfo::FloatInfo(nsIFrame* aFrame,
                                     nscoord aLineLeft, nscoord aBlockStart,
                                     const LogicalRect& aMarginRect,
                                     WritingMode aWM,
                                     const nsSize& aContainerSize)
  : mFrame(aFrame)
  , mRect(aMarginRect.LineLeft(aWM, aContainerSize) + aLineLeft,
          aMarginRect.BStart(aWM) + aBlockStart,
          aMarginRect.ISize(aWM),
          aMarginRect.BSize(aWM))
{
  MOZ_COUNT_CTOR(nsFloatManager::FloatInfo);

  const StyleShapeOutside& shapeOutside = mFrame->StyleDisplay()->mShapeOutside;

  if (shapeOutside.GetType() == StyleShapeSourceType::None) {
    return;
  }

  if (shapeOutside.GetType() == StyleShapeSourceType::URL) {
    // Bug 1265343: Implement 'shape-image-threshold'. Early return
    // here because shape-outside with url() value doesn't have a
    // reference box, and GetReferenceBox() asserts that.
    return;
  }

  // Initialize <shape-box>'s reference rect.
  LogicalRect rect = aMarginRect;

  switch (shapeOutside.GetReferenceBox()) {
    case StyleShapeOutsideShapeBox::Content:
      rect.Deflate(aWM, mFrame->GetLogicalUsedPadding(aWM));
      MOZ_FALLTHROUGH;
    case StyleShapeOutsideShapeBox::Padding:
      rect.Deflate(aWM, mFrame->GetLogicalUsedBorder(aWM));
      MOZ_FALLTHROUGH;
    case StyleShapeOutsideShapeBox::Border:
      rect.Deflate(aWM, mFrame->GetLogicalUsedMargin(aWM));
      break;
    case StyleShapeOutsideShapeBox::Margin:
      // Do nothing. rect is already a margin rect.
      break;
    case StyleShapeOutsideShapeBox::NoBox:
      MOZ_ASSERT(shapeOutside.GetType() != StyleShapeSourceType::Box,
                 "Box source type must have <shape-box> specified!");
      break;
  }

  if (shapeOutside.GetType() == StyleShapeSourceType::Box) {
    nsRect shapeBoxRect(rect.LineLeft(aWM, aContainerSize) + aLineLeft,
                        rect.BStart(aWM) + aBlockStart,
                        rect.ISize(aWM), rect.BSize(aWM));
    mShapeInfo = MakeUnique<BoxShapeInfo>(shapeBoxRect, mFrame);
  } else if (shapeOutside.GetType() == StyleShapeSourceType::Shape) {
    StyleBasicShape* const basicShape = shapeOutside.GetBasicShape();

    if (basicShape->GetShapeType() == StyleBasicShapeType::Circle) {
      mShapeInfo = MakeUnique<CircleShapeInfo>(basicShape, aLineLeft, aBlockStart,
                                               rect, aWM, aContainerSize);
    }
  } else {
    MOZ_ASSERT_UNREACHABLE("Unknown StyleShapeSourceType!");
  }
}

#ifdef NS_BUILD_REFCNT_LOGGING
nsFloatManager::FloatInfo::FloatInfo(FloatInfo&& aOther)
  : mFrame(Move(aOther.mFrame))
  , mLeftBEnd(Move(aOther.mLeftBEnd))
  , mRightBEnd(Move(aOther.mRightBEnd))
  , mRect(Move(aOther.mRect))
  , mShapeInfo(Move(aOther.mShapeInfo))
{
  MOZ_COUNT_CTOR(nsFloatManager::FloatInfo);
}

nsFloatManager::FloatInfo::~FloatInfo()
{
  MOZ_COUNT_DTOR(nsFloatManager::FloatInfo);
}
#endif

nscoord
nsFloatManager::FloatInfo::LineLeft(WritingMode aWM,
                                    ShapeType aShapeType,
                                    const nscoord aBStart,
                                    const nscoord aBEnd) const
{
  if (aShapeType == ShapeType::Margin) {
    return LineLeft();
  }

  MOZ_ASSERT(aShapeType == ShapeType::ShapeOutside);
  if (!mShapeInfo) {
    return LineLeft();
  }
  // Clip the flow area to the margin-box because
  // https://drafts.csswg.org/css-shapes-1/#relation-to-box-model-and-float-behavior
  // says "When a shape is used to define a float area, the shape is clipped
  // to the float’s margin box."
  return std::max(LineLeft(), mShapeInfo->LineLeft(aWM, aBStart, aBEnd));
}

nscoord
nsFloatManager::FloatInfo::LineRight(WritingMode aWM,
                                     ShapeType aShapeType,
                                     const nscoord aBStart,
                                     const nscoord aBEnd) const
{
  if (aShapeType == ShapeType::Margin) {
    return LineRight();
  }

  MOZ_ASSERT(aShapeType == ShapeType::ShapeOutside);
  if (!mShapeInfo) {
    return LineRight();
  }
  // Clip the flow area to the margin-box. See LineLeft().
  return std::min(LineRight(), mShapeInfo->LineRight(aWM, aBStart, aBEnd));
}

nscoord
nsFloatManager::FloatInfo::BStart(ShapeType aShapeType) const
{
  if (aShapeType == ShapeType::Margin) {
    return BStart();
  }

  MOZ_ASSERT(aShapeType == ShapeType::ShapeOutside);
  if (!mShapeInfo) {
    return BStart();
  }
  // Clip the flow area to the margin-box. See LineLeft().
  return std::max(BStart(), mShapeInfo->BStart());
}

nscoord
nsFloatManager::FloatInfo::BEnd(ShapeType aShapeType) const
{
  if (aShapeType == ShapeType::Margin) {
    return BEnd();
  }

  MOZ_ASSERT(aShapeType == ShapeType::ShapeOutside);
  if (!mShapeInfo) {
    return BEnd();
  }
  // Clip the flow area to the margin-box. See LineLeft().
  return std::min(BEnd(), mShapeInfo->BEnd());
}

bool
nsFloatManager::FloatInfo::IsEmpty(ShapeType aShapeType) const
{
  if (aShapeType == ShapeType::Margin) {
    return IsEmpty();
  }

  MOZ_ASSERT(aShapeType == ShapeType::ShapeOutside);
  if (!mShapeInfo) {
    return IsEmpty();
  }
  return mShapeInfo->IsEmpty();
}

/* static */ nscoord
nsFloatManager::ShapeInfo::ComputeEllipseLineInterceptDiff(
  const nscoord aShapeBoxBStart, const nscoord aShapeBoxBEnd,
  const nscoord aBStartCornerRadiusL, const nscoord aBStartCornerRadiusB,
  const nscoord aBEndCornerRadiusL, const nscoord aBEndCornerRadiusB,
  const nscoord aBandBStart, const nscoord aBandBEnd)
{
  // An example for the band intersecting with the top right corner of an
  // ellipse with writing-mode horizontal-tb.
  //
  //                             lineIntercept lineDiff
  //                                    |       |
  //  +---------------------------------|-------|-+---- aShapeBoxBStart
  //  |                ##########^      |       | |
  //  |            ##############|####  |       | |
  //  +---------#################|######|-------|-+---- aBandBStart
  //  |       ###################|######|##     | |
  //  |     aBStartCornerRadiusB |######|###    | |
  //  |    ######################|######|#####  | |
  //  +---#######################|<-----------><->^---- aBandBEnd
  //  |  ########################|##############  |
  //  |  ########################|##############  |---- b
  //  | #########################|############### |
  //  | ######################## v<-------------->v
  //  |###################### aBStartCornerRadiusL|
  //  |###########################################|
  //  |###########################################|
  //  |###########################################|
  //  |###########################################|
  //  | ######################################### |
  //  | ######################################### |
  //  |  #######################################  |
  //  |  #######################################  |
  //  |   #####################################   |
  //  |    ###################################    |
  //  |      ###############################      |
  //  |       #############################       |
  //  |         #########################         |
  //  |            ###################            |
  //  |                ###########                |
  //  +-------------------------------------------+----- aShapeBoxBEnd

  NS_ASSERTION(aShapeBoxBStart <= aShapeBoxBEnd, "Bad shape box coordinates!");
  NS_ASSERTION(aBandBStart <= aBandBEnd, "Bad band coordinates!");

  nscoord lineDiff = 0;

  // If the band intersects both the block-start and block-end corners, we
  // don't need to enter either branch because the correct lineDiff is 0.
  if (aBStartCornerRadiusB > 0 &&
      aBandBEnd >= aShapeBoxBStart &&
      aBandBEnd <= aShapeBoxBStart + aBStartCornerRadiusB) {
    // The band intersects only the block-start corner.
    nscoord b = aBStartCornerRadiusB - (aBandBEnd - aShapeBoxBStart);
    nscoord lineIntercept =
      XInterceptAtY(b, aBStartCornerRadiusL, aBStartCornerRadiusB);
    lineDiff = aBStartCornerRadiusL - lineIntercept;
  } else if (aBEndCornerRadiusB > 0 &&
             aBandBStart >= aShapeBoxBEnd - aBEndCornerRadiusB &&
             aBandBStart <= aShapeBoxBEnd) {
    // The band intersects only the block-end corner.
    nscoord b = aBEndCornerRadiusB - (aShapeBoxBEnd - aBandBStart);
    nscoord lineIntercept =
      XInterceptAtY(b, aBEndCornerRadiusL, aBEndCornerRadiusB);
    lineDiff = aBEndCornerRadiusL - lineIntercept;
  }

  return lineDiff;
}

/* static */ nscoord
nsFloatManager::ShapeInfo::XInterceptAtY(const nscoord aY,
                                         const nscoord aRadiusX,
                                         const nscoord aRadiusY)
{
  // Solve for x in the ellipse equation (x/radiusX)^2 + (y/radiusY)^2 = 1.
  MOZ_ASSERT(aRadiusY > 0);
  return aRadiusX * std::sqrt(1 - (aY * aY) / double(aRadiusY * aRadiusY));
}

//----------------------------------------------------------------------

nsAutoFloatManager::~nsAutoFloatManager()
{
  // Restore the old float manager in the reflow input if necessary.
  if (mNew) {
#ifdef DEBUG
    if (nsBlockFrame::gNoisyFloatManager) {
      printf("restoring old float manager %p\n", mOld);
    }
#endif

    mReflowInput.mFloatManager = mOld;

#ifdef DEBUG
    if (nsBlockFrame::gNoisyFloatManager) {
      if (mOld) {
        mReflowInput.mFrame->ListTag(stdout);
        printf(": float manager %p after reflow\n", mOld);
        mOld->List(stdout);
      }
    }
#endif

    delete mNew;
  }
}

void
nsAutoFloatManager::CreateFloatManager(nsPresContext *aPresContext)
{
  // Create a new float manager and install it in the reflow
  // input. `Remember' the old float manager so we can restore it
  // later.
  mNew = new nsFloatManager(aPresContext->PresShell(),
                            mReflowInput.GetWritingMode());

#ifdef DEBUG
  if (nsBlockFrame::gNoisyFloatManager) {
    printf("constructed new float manager %p (replacing %p)\n",
           mNew, mReflowInput.mFloatManager);
  }
#endif

  // Set the float manager in the existing reflow input.
  mOld = mReflowInput.mFloatManager;
  mReflowInput.mFloatManager = mNew;
}
