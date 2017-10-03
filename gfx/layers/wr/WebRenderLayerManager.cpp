/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WebRenderLayerManager.h"

#include "BasicLayers.h"
#include "gfxPrefs.h"
#include "GeckoProfiler.h"
#include "LayersLogging.h"
#include "mozilla/gfx/DrawEventRecorder.h"
#include "mozilla/layers/CompositorBridgeChild.h"
#include "mozilla/layers/IpcResourceUpdateQueue.h"
#include "mozilla/layers/StackingContextHelper.h"
#include "mozilla/layers/TextureClient.h"
#include "mozilla/layers/WebRenderBridgeChild.h"
#include "mozilla/layers/UpdateImageHelper.h"
#include "nsDisplayList.h"
#include "WebRenderCanvasLayer.h"
#include "WebRenderCanvasRenderer.h"
#include "WebRenderColorLayer.h"
#include "WebRenderContainerLayer.h"
#include "WebRenderImageLayer.h"
#include "WebRenderPaintedLayer.h"
#include "WebRenderPaintedLayerBlob.h"
#include "WebRenderTextLayer.h"
#include "WebRenderDisplayItemLayer.h"

namespace mozilla {

using namespace gfx;

namespace layers {

WebRenderLayerManager::WebRenderLayerManager(nsIWidget* aWidget)
  : mWidget(aWidget)
  , mLatestTransactionId(0)
  , mLastAsr(nullptr)
  , mNeedsComposite(false)
  , mIsFirstPaint(false)
  , mEndTransactionWithoutLayers(false)
  , mTarget(nullptr)
  , mPaintSequenceNumber(0)
  , mShouldNotifyInvalidation(false)
{
  MOZ_COUNT_CTOR(WebRenderLayerManager);
}

KnowsCompositor*
WebRenderLayerManager::AsKnowsCompositor()
{
  return mWrChild;
}

bool
WebRenderLayerManager::Initialize(PCompositorBridgeChild* aCBChild,
                                  wr::PipelineId aLayersId,
                                  TextureFactoryIdentifier* aTextureFactoryIdentifier)
{
  MOZ_ASSERT(mWrChild == nullptr);
  MOZ_ASSERT(aTextureFactoryIdentifier);

  LayoutDeviceIntSize size = mWidget->GetClientSize();
  TextureFactoryIdentifier textureFactoryIdentifier;
  wr::IdNamespace id_namespace;
  PWebRenderBridgeChild* bridge = aCBChild->SendPWebRenderBridgeConstructor(aLayersId,
                                                                            size,
                                                                            &textureFactoryIdentifier,
                                                                            &id_namespace);
  if (!bridge) {
    // This should only fail if we attempt to access a layer we don't have
    // permission for, or more likely, the GPU process crashed again during
    // reinitialization. We can expect to be notified again to reinitialize
    // (which may or may not be using WebRender).
    gfxCriticalNote << "Failed to create WebRenderBridgeChild.";
    return false;
  }

  mWrChild = static_cast<WebRenderBridgeChild*>(bridge);
  WrBridge()->SendCreate(size.ToUnknownSize());
  WrBridge()->IdentifyTextureHost(textureFactoryIdentifier);
  WrBridge()->SetNamespace(id_namespace);
  *aTextureFactoryIdentifier = textureFactoryIdentifier;
  return true;
}

void
WebRenderLayerManager::Destroy()
{
  DoDestroy(/* aIsSync */ false);
}

void
WebRenderLayerManager::DoDestroy(bool aIsSync)
{
  if (IsDestroyed()) {
    return;
  }

  LayerManager::Destroy();

  if (WrBridge()) {
    // Just clear ImageKeys, they are deleted during WebRenderAPI destruction.
    mImageKeysToDeleteLater.Clear();
    mImageKeysToDelete.Clear();
    // CompositorAnimations are cleared by WebRenderBridgeParent.
    mDiscardedCompositorAnimationsIds.Clear();
    WrBridge()->Destroy(aIsSync);
  }

  // Clear this before calling RemoveUnusedAndResetWebRenderUserData(),
  // otherwise that function might destroy some WebRenderAnimationData instances
  // which will put stuff back into mDiscardedCompositorAnimationsIds. If
  // mActiveCompositorAnimationIds is empty that won't happen.
  mActiveCompositorAnimationIds.clear();

  mLastCanvasDatas.Clear();
  RemoveUnusedAndResetWebRenderUserData();

  if (mTransactionIdAllocator) {
    // Make sure to notify the refresh driver just in case it's waiting on a
    // pending transaction. Do this at the top of the event loop so we don't
    // cause a paint to occur during compositor shutdown.
    RefPtr<TransactionIdAllocator> allocator = mTransactionIdAllocator;
    uint64_t id = mLatestTransactionId;

    RefPtr<Runnable> task = NS_NewRunnableFunction(
      "TransactionIdAllocator::NotifyTransactionCompleted",
      [allocator, id] () -> void {
      allocator->NotifyTransactionCompleted(id);
    });
    NS_DispatchToMainThread(task.forget());
  }

  // Forget the widget pointer in case we outlive our owning widget.
  mWidget = nullptr;
}

WebRenderLayerManager::~WebRenderLayerManager()
{
  Destroy();
  MOZ_COUNT_DTOR(WebRenderLayerManager);
}

CompositorBridgeChild*
WebRenderLayerManager::GetCompositorBridgeChild()
{
  return WrBridge()->GetCompositorBridgeChild();
}

int32_t
WebRenderLayerManager::GetMaxTextureSize() const
{
  return WrBridge()->GetMaxTextureSize();
}

bool
WebRenderLayerManager::BeginTransactionWithTarget(gfxContext* aTarget)
{
  mTarget = aTarget;
  return BeginTransaction();
}

bool
WebRenderLayerManager::BeginTransaction()
{
  if (!WrBridge()->IPCOpen()) {
    gfxCriticalNote << "IPC Channel is already torn down unexpectedly\n";
    return false;
  }

  // Increment the paint sequence number even if test logging isn't
  // enabled in this process; it may be enabled in the parent process,
  // and the parent process expects unique sequence numbers.
  ++mPaintSequenceNumber;
  if (gfxPrefs::APZTestLoggingEnabled()) {
    mApzTestData.StartNewPaint(mPaintSequenceNumber);
  }
  return true;
}

bool
WebRenderLayerManager::EndEmptyTransaction(EndTransactionFlags aFlags)
{
  if (!mRoot) {
    // With the WebRenderLayerManager we reject attempts to set most kind of
    // "pending data" for empty transactions. Any place that attempts to update
    // transforms or scroll offset, for example, will get failure return values
    // back, and will fall back to a full transaction. Therefore the only piece
    // of "pending" information we need to send in an empty transaction is the
    // APZ focus state.
    WrBridge()->SendSetFocusTarget(mFocusTarget);
    return true;
  }

  // We might used painted layer images so don't delete them yet.
  return EndTransactionInternal(nullptr, nullptr, aFlags);
}

/*static*/ int32_t
PopulateScrollData(WebRenderScrollData& aTarget, Layer* aLayer)
{
  MOZ_ASSERT(aLayer);

  // We want to allocate a WebRenderLayerScrollData object for this layer,
  // but don't keep a pointer to it since it might get memmove'd during the
  // recursion below. Instead keep the index and get the pointer later.
  size_t index = aTarget.AddNewLayerData();

  int32_t descendants = 0;
  for (Layer* child = aLayer->GetLastChild(); child; child = child->GetPrevSibling()) {
    descendants += PopulateScrollData(aTarget, child);
  }
  aTarget.GetLayerDataMutable(index)->Initialize(aTarget, aLayer, descendants);
  return descendants + 1;
}

void
WebRenderLayerManager::CreateWebRenderCommandsFromDisplayList(nsDisplayList* aDisplayList,
                                                              nsDisplayListBuilder* aDisplayListBuilder,
                                                              const StackingContextHelper& aSc,
                                                              wr::DisplayListBuilder& aBuilder,
                                                              wr::IpcResourceUpdateQueue& aResources)
{
  bool apzEnabled = AsyncPanZoomEnabled();
  EventRegions eventRegions;

  nsDisplayList savedItems;
  nsDisplayItem* item;
  while ((item = aDisplayList->RemoveBottom()) != nullptr) {
    DisplayItemType itemType = item->GetType();

    // If the item is a event regions item, but is empty (has no regions in it)
    // then we should just throw it out
    if (itemType == DisplayItemType::TYPE_LAYER_EVENT_REGIONS) {
      nsDisplayLayerEventRegions* eventRegions =
        static_cast<nsDisplayLayerEventRegions*>(item);
      if (eventRegions->IsEmpty()) {
        item->Destroy(aDisplayListBuilder);
        continue;
      }
    }

    // Peek ahead to the next item and try merging with it or swapping with it
    // if necessary.
    AutoTArray<nsDisplayItem*, 1> mergedItems;
    mergedItems.AppendElement(item);
    for (nsDisplayItem* peek = item->GetAbove(); peek; peek = peek->GetAbove()) {
      if (!item->CanMerge(peek)) {
        break;
      }

      mergedItems.AppendElement(peek);

      // Move the iterator forward since we will merge this item.
      item = peek;
    }

    if (mergedItems.Length() > 1) {
      item = aDisplayListBuilder->MergeItems(mergedItems);
      MOZ_ASSERT(item && itemType == item->GetType());
    }

    nsDisplayList* itemSameCoordinateSystemChildren
      = item->GetSameCoordinateSystemChildren();
    if (item->ShouldFlattenAway(aDisplayListBuilder)) {
      aDisplayList->AppendToBottom(itemSameCoordinateSystemChildren);
      item->Destroy(aDisplayListBuilder);
      continue;
    }

    savedItems.AppendToTop(item);

    bool forceNewLayerData = false;
    size_t layerCountBeforeRecursing = mLayerScrollData.size();
    if (apzEnabled) {
      // For some types of display items we want to force a new
      // WebRenderLayerScrollData object, to ensure we preserve the APZ-relevant
      // data that is in the display item.
      forceNewLayerData = item->UpdateScrollData(nullptr, nullptr);

      // Anytime the ASR changes we also want to force a new layer data because
      // the stack of scroll metadata is going to be different for this
      // display item than previously, so we can't squash the display items
      // into the same "layer".
      const ActiveScrolledRoot* asr = item->GetActiveScrolledRoot();
      if (asr != mLastAsr) {
        mLastAsr = asr;
        forceNewLayerData = true;
      }

      // If we're creating a new layer data then flush whatever event regions
      // we've collected onto the old layer.
      if (forceNewLayerData && !eventRegions.IsEmpty()) {
        // If eventRegions is non-empty then we must have a layer data already,
        // because we (below) force one if we encounter an event regions item
        // with an empty layer data list. Additionally, the most recently
        // created layer data must have been created from an item whose ASR
        // is the same as the ASR on the event region items that were collapsed
        // into |eventRegions|. This is because any ASR change causes us to force
        // a new layer data which flushes the eventRegions.
        MOZ_ASSERT(!mLayerScrollData.empty());
        mLayerScrollData.back().AddEventRegions(eventRegions);
        eventRegions.SetEmpty();
      }

      // Collapse event region data into |eventRegions|, which will either be
      // empty, or filled with stuff from previous display items with the same
      // ASR.
      if (itemType == DisplayItemType::TYPE_LAYER_EVENT_REGIONS) {
        nsDisplayLayerEventRegions* regionsItem =
            static_cast<nsDisplayLayerEventRegions*>(item);
        int32_t auPerDevPixel = item->Frame()->PresContext()->AppUnitsPerDevPixel();
        EventRegions regions(
            regionsItem->HitRegion().ScaleToOutsidePixels(1.0f, 1.0f, auPerDevPixel),
            regionsItem->MaybeHitRegion().ScaleToOutsidePixels(1.0f, 1.0f, auPerDevPixel),
            regionsItem->DispatchToContentHitRegion().ScaleToOutsidePixels(1.0f, 1.0f, auPerDevPixel),
            regionsItem->NoActionRegion().ScaleToOutsidePixels(1.0f, 1.0f, auPerDevPixel),
            regionsItem->HorizontalPanRegion().ScaleToOutsidePixels(1.0f, 1.0f, auPerDevPixel),
            regionsItem->VerticalPanRegion().ScaleToOutsidePixels(1.0f, 1.0f, auPerDevPixel));

        eventRegions.OrWith(regions);
        if (mLayerScrollData.empty()) {
          // If we don't have a layer data yet then create one because we will
          // need it to store this event region information.
          forceNewLayerData = true;
        }
      }

      // If we're going to create a new layer data for this item, stash the
      // ASR so that if we recurse into a sublist they will know where to stop
      // walking up their ASR chain when building scroll metadata.
      if (forceNewLayerData) {
        mAsrStack.push_back(asr);
      }
    }

    // If there is any invalid item, we should notify nsPresContext after EndTransaction.
    if (!mShouldNotifyInvalidation) {
      nsRect invalid;
      if (item->IsInvalid(invalid)) {
        mShouldNotifyInvalidation = true;
      }
    }

    { // scope the ScrollingLayersHelper
      ScrollingLayersHelper clip(item, aBuilder, aSc, mClipIdCache, AsyncPanZoomEnabled());

      // Note: this call to CreateWebRenderCommands can recurse back into
      // this function if the |item| is a wrapper for a sublist.
      if (!item->CreateWebRenderCommands(aBuilder, aResources, aSc, this,
                                         aDisplayListBuilder)) {
        PushItemAsImage(item, aBuilder, aResources, aSc, aDisplayListBuilder);
      }
    }

    if (apzEnabled && forceNewLayerData) {
      // Pop the thing we pushed before the recursion, so the topmost item on
      // the stack is enclosing display item's ASR (or the stack is empty)
      mAsrStack.pop_back();
      const ActiveScrolledRoot* stopAtAsr =
          mAsrStack.empty() ? nullptr : mAsrStack.back();

      int32_t descendants = mLayerScrollData.size() - layerCountBeforeRecursing;

      mLayerScrollData.emplace_back();
      mLayerScrollData.back().Initialize(mScrollData, item, descendants, stopAtAsr);
    }
  }
  aDisplayList->AppendToTop(&savedItems);

  // If we have any event region info left over we need to flush it before we
  // return. Again, at this point the layer data list must be non-empty, and
  // the most recently created layer data will have been created by an item
  // with matching ASRs.
  if (!eventRegions.IsEmpty()) {
    MOZ_ASSERT(apzEnabled);
    MOZ_ASSERT(!mLayerScrollData.empty());
    mLayerScrollData.back().AddEventRegions(eventRegions);
  }
}

void
WebRenderLayerManager::EndTransactionWithoutLayer(nsDisplayList* aDisplayList,
                                                  nsDisplayListBuilder* aDisplayListBuilder)
{
  MOZ_ASSERT(aDisplayList && aDisplayListBuilder);
  mEndTransactionWithoutLayers = true;
  WrBridge()->RemoveExpiredFontKeys();
  EndTransactionInternal(nullptr,
                         nullptr,
                         EndTransactionFlags::END_DEFAULT,
                         aDisplayList,
                         aDisplayListBuilder);
}

Maybe<wr::ImageKey>
WebRenderLayerManager::CreateImageKey(nsDisplayItem* aItem,
                                      ImageContainer* aContainer,
                                      mozilla::wr::DisplayListBuilder& aBuilder,
                                      mozilla::wr::IpcResourceUpdateQueue& aResources,
                                      const StackingContextHelper& aSc,
                                      gfx::IntSize& aSize)
{
  RefPtr<WebRenderImageData> imageData = CreateOrRecycleWebRenderUserData<WebRenderImageData>(aItem);
  MOZ_ASSERT(imageData);

  if (aContainer->IsAsync()) {
    bool snap;
    nsRect bounds = aItem->GetBounds(nullptr, &snap);
    int32_t appUnitsPerDevPixel = aItem->Frame()->PresContext()->AppUnitsPerDevPixel();
    LayerRect rect = ViewAs<LayerPixel>(
      LayoutDeviceRect::FromAppUnits(bounds, appUnitsPerDevPixel),
      PixelCastJustification::WebRenderHasUnitResolution);
    LayerRect scBounds(0, 0, rect.width, rect.Height());
    MaybeIntSize scaleToSize;
    if (!aContainer->GetScaleHint().IsEmpty()) {
      scaleToSize = Some(aContainer->GetScaleHint());
    }
    // TODO!
    // We appear to be using the image bridge for a lot (most/all?) of
    // layers-free image handling and that breaks frame consistency.
    imageData->CreateAsyncImageWebRenderCommands(aBuilder,
                                                 aContainer,
                                                 aSc,
                                                 rect,
                                                 scBounds,
                                                 gfx::Matrix4x4(),
                                                 scaleToSize,
                                                 wr::ImageRendering::Auto,
                                                 wr::MixBlendMode::Normal,
                                                 !aItem->BackfaceIsHidden());
    return Nothing();
  }

  AutoLockImage autoLock(aContainer);
  if (!autoLock.HasImage()) {
    return Nothing();
  }
  mozilla::layers::Image* image = autoLock.GetImage();
  aSize = image->GetSize();

  return imageData->UpdateImageKey(aContainer, aResources);
}

bool
WebRenderLayerManager::PushImage(nsDisplayItem* aItem,
                                 ImageContainer* aContainer,
                                 mozilla::wr::DisplayListBuilder& aBuilder,
                                 mozilla::wr::IpcResourceUpdateQueue& aResources,
                                 const StackingContextHelper& aSc,
                                 const LayerRect& aRect)
{
  gfx::IntSize size;
  Maybe<wr::ImageKey> key = CreateImageKey(aItem, aContainer,
                                          aBuilder, aResources,
                                          aSc, size);
  if (aContainer->IsAsync()) {
    // Async ImageContainer does not create ImageKey, instead it uses Pipeline.
    MOZ_ASSERT(key.isNothing());
    return true;
  }
  if (!key) {
    return false;
  }

  auto r = aSc.ToRelativeLayoutRect(aRect);
  SamplingFilter sampleFilter = nsLayoutUtils::GetSamplingFilterForFrame(aItem->Frame());
  aBuilder.PushImage(r, r, !aItem->BackfaceIsHidden(), wr::ToImageRendering(sampleFilter), key.value());

  return true;
}

static void
PaintItemByDrawTarget(nsDisplayItem* aItem,
                      DrawTarget* aDT,
                      const LayerRect& aImageRect,
                      const LayerPoint& aOffset,
                      nsDisplayListBuilder* aDisplayListBuilder,
                      RefPtr<BasicLayerManager>& aManager,
                      WebRenderLayerManager* aWrManager,
                      const gfx::Size& aScale)
{
  MOZ_ASSERT(aDT);

  aDT->ClearRect(aImageRect.ToUnknownRect());
  RefPtr<gfxContext> context = gfxContext::CreateOrNull(aDT);
  MOZ_ASSERT(context);

  context->SetMatrix(context->CurrentMatrix().PreScale(aScale.width, aScale.height).PreTranslate(-aOffset.x, -aOffset.y));

  switch (aItem->GetType()) {
  case DisplayItemType::TYPE_MASK:
    static_cast<nsDisplayMask*>(aItem)->PaintMask(aDisplayListBuilder, context);
    break;
  case DisplayItemType::TYPE_FILTER:
    {
      if (aManager == nullptr) {
        aManager = new BasicLayerManager(BasicLayerManager::BLM_INACTIVE);
      }

      FrameLayerBuilder* layerBuilder = new FrameLayerBuilder();
      layerBuilder->Init(aDisplayListBuilder, aManager);
      layerBuilder->DidBeginRetainedLayerTransaction(aManager);

      aManager->BeginTransactionWithTarget(context);

      ContainerLayerParameters param;
      RefPtr<Layer> layer =
        static_cast<nsDisplayFilter*>(aItem)->BuildLayer(aDisplayListBuilder,
                                                         aManager, param);

      if (layer) {
        UniquePtr<LayerProperties> props;
        props = Move(LayerProperties::CloneFrom(aManager->GetRoot()));

        aManager->SetRoot(layer);
        layerBuilder->WillEndTransaction();

        nsIntRegion invalid;
        props->ComputeDifferences(layer, invalid, nullptr);

        static_cast<nsDisplayFilter*>(aItem)->PaintAsLayer(aDisplayListBuilder,
                                                           context, aManager);

        if (!invalid.IsEmpty()) {
          aWrManager->SetNotifyInvalidation(true);
        }
      }

      if (aManager->InTransaction()) {
        aManager->AbortTransaction();
      }
      aManager->SetTarget(nullptr);
      break;
    }
  default:
    aItem->Paint(aDisplayListBuilder, context);
    break;
  }

  if (gfxPrefs::WebRenderHighlightPaintedLayers()) {
    aDT->SetTransform(Matrix());
    aDT->FillRect(Rect(0, 0, aImageRect.Width(), aImageRect.Height()), ColorPattern(Color(1.0, 0.0, 0.0, 0.5)));
  }
  if (aItem->Frame()->PresContext()->GetPaintFlashing()) {
    aDT->SetTransform(Matrix());
    float r = float(rand()) / RAND_MAX;
    float g = float(rand()) / RAND_MAX;
    float b = float(rand()) / RAND_MAX;
    aDT->FillRect(Rect(0, 0, aImageRect.Width(), aImageRect.Height()), ColorPattern(Color(r, g, b, 0.5)));
  }
}

already_AddRefed<WebRenderFallbackData>
WebRenderLayerManager::GenerateFallbackData(nsDisplayItem* aItem,
                                            wr::DisplayListBuilder& aBuilder,
                                            wr::IpcResourceUpdateQueue& aResources,
                                            const StackingContextHelper& aSc,
                                            nsDisplayListBuilder* aDisplayListBuilder,
                                            LayerRect& aImageRect)
{
  RefPtr<WebRenderFallbackData> fallbackData = CreateOrRecycleWebRenderUserData<WebRenderFallbackData>(aItem);

  bool snap;
  nsRect itemBounds = aItem->GetBounds(aDisplayListBuilder, &snap);
  nsRect clippedBounds = itemBounds;

  const DisplayItemClip& clip = aItem->GetClip();
  // Blob images will only draw the visible area of the blob so we don't need to clip
  // them here and can just rely on the webrender clipping.
  if (clip.HasClip() && !gfxPrefs::WebRenderBlobImages()) {
    clippedBounds = itemBounds.Intersect(clip.GetClipRect());
  }

  // nsDisplayItem::Paint() may refer the variables that come from ComputeVisibility().
  // So we should call ComputeVisibility() before painting. e.g.: nsDisplayBoxShadowInner
  // uses mVisibleRegion in Paint() and mVisibleRegion is computed in
  // nsDisplayBoxShadowInner::ComputeVisibility().
  nsRegion visibleRegion(clippedBounds);
  aItem->ComputeVisibility(aDisplayListBuilder, &visibleRegion);

  const int32_t appUnitsPerDevPixel = aItem->Frame()->PresContext()->AppUnitsPerDevPixel();
  LayerRect bounds = ViewAs<LayerPixel>(
      LayoutDeviceRect::FromAppUnits(clippedBounds, appUnitsPerDevPixel),
      PixelCastJustification::WebRenderHasUnitResolution);

  gfx::Size scale = aSc.GetInheritedScale();
  LayerIntSize paintSize = RoundedToInt(LayerSize(bounds.width * scale.width, bounds.height * scale.height));
  if (paintSize.width == 0 || paintSize.height == 0) {
    return nullptr;
  }

  bool needPaint = true;
  LayerIntPoint offset = RoundedToInt(bounds.TopLeft());
  aImageRect = LayerRect(offset, LayerSize(RoundedToInt(bounds.Size())));
  LayerRect paintRect = LayerRect(LayerPoint(0, 0), LayerSize(paintSize));
  nsAutoPtr<nsDisplayItemGeometry> geometry = fallbackData->GetGeometry();

  // nsDisplayFilter is rendered via BasicLayerManager which means the invalidate
  // region is unknown until we traverse the displaylist contained by it.
  if (geometry && !fallbackData->IsInvalid() &&
      aItem->GetType() != DisplayItemType::TYPE_FILTER) {
    nsRect invalid;
    nsRegion invalidRegion;

    if (aItem->IsInvalid(invalid)) {
      invalidRegion.OrWith(clippedBounds);
    } else {
      nsPoint shift = itemBounds.TopLeft() - geometry->mBounds.TopLeft();
      geometry->MoveBy(shift);
      aItem->ComputeInvalidationRegion(aDisplayListBuilder, geometry, &invalidRegion);

      nsRect lastBounds = fallbackData->GetBounds();
      lastBounds.MoveBy(shift);

      if (!lastBounds.IsEqualInterior(clippedBounds)) {
        invalidRegion.OrWith(lastBounds);
        invalidRegion.OrWith(clippedBounds);
      }
    }
    needPaint = !invalidRegion.IsEmpty();
  }

  if (needPaint) {
    gfx::SurfaceFormat format = aItem->GetType() == DisplayItemType::TYPE_MASK ?
                                                      gfx::SurfaceFormat::A8 : gfx::SurfaceFormat::B8G8R8A8;
    if (gfxPrefs::WebRenderBlobImages()) {
      bool snapped;
      bool isOpaque = aItem->GetOpaqueRegion(aDisplayListBuilder, &snapped).Contains(clippedBounds);

      RefPtr<gfx::DrawEventRecorderMemory> recorder = MakeAndAddRef<gfx::DrawEventRecorderMemory>();
      RefPtr<gfx::DrawTarget> dummyDt =
        gfx::Factory::CreateDrawTarget(gfx::BackendType::SKIA, gfx::IntSize(1, 1), format);
      RefPtr<gfx::DrawTarget> dt = gfx::Factory::CreateRecordingDrawTarget(recorder, dummyDt, paintSize.ToUnknownSize());
      PaintItemByDrawTarget(aItem, dt, paintRect, offset, aDisplayListBuilder,
                            fallbackData->mBasicLayerManager, this, scale);
      recorder->Finish();

      Range<uint8_t> bytes((uint8_t*)recorder->mOutputStream.mData, recorder->mOutputStream.mLength);
      wr::ImageKey key = WrBridge()->GetNextImageKey();
      wr::ImageDescriptor descriptor(paintSize.ToUnknownSize(), 0, dt->GetFormat(), isOpaque);
      aResources.AddBlobImage(key, descriptor, bytes);
      fallbackData->SetKey(key);
    } else {
      fallbackData->CreateImageClientIfNeeded();
      RefPtr<ImageClient> imageClient = fallbackData->GetImageClient();
      RefPtr<ImageContainer> imageContainer = LayerManager::CreateImageContainer();

      {
        UpdateImageHelper helper(imageContainer, imageClient, paintSize.ToUnknownSize(), format);
        {
          RefPtr<gfx::DrawTarget> dt = helper.GetDrawTarget();
          if (!dt) {
            return nullptr;
          }
          PaintItemByDrawTarget(aItem, dt, paintRect, offset,
                                aDisplayListBuilder,
                                fallbackData->mBasicLayerManager, this, scale);
        }
        if (!helper.UpdateImage()) {
          return nullptr;
        }
      }

      // Force update the key in fallback data since we repaint the image in this path.
      // If not force update, fallbackData may reuse the original key because it
      // doesn't know UpdateImageHelper already updated the image container.
      if (!fallbackData->UpdateImageKey(imageContainer, aResources, true)) {
        return nullptr;
      }
    }

    geometry = aItem->AllocateGeometry(aDisplayListBuilder);
    fallbackData->SetInvalid(false);
  }

  // Update current bounds to fallback data
  fallbackData->SetGeometry(Move(geometry));
  fallbackData->SetBounds(clippedBounds);

  MOZ_ASSERT(fallbackData->GetKey());

  return fallbackData.forget();
}

Maybe<wr::WrImageMask>
WebRenderLayerManager::BuildWrMaskImage(nsDisplayItem* aItem,
                                        wr::DisplayListBuilder& aBuilder,
                                        wr::IpcResourceUpdateQueue& aResources,
                                        const StackingContextHelper& aSc,
                                        nsDisplayListBuilder* aDisplayListBuilder,
                                        const LayerRect& aBounds)
{
  LayerRect imageRect;
  RefPtr<WebRenderFallbackData> fallbackData = GenerateFallbackData(aItem, aBuilder, aResources,
                                                                    aSc, aDisplayListBuilder,
                                                                    imageRect);
  if (!fallbackData) {
    return Nothing();
  }

  wr::WrImageMask imageMask;
  imageMask.image = fallbackData->GetKey().value();
  imageMask.rect = aSc.ToRelativeLayoutRect(aBounds);
  imageMask.repeat = false;
  return Some(imageMask);
}

bool
WebRenderLayerManager::PushItemAsImage(nsDisplayItem* aItem,
                                       wr::DisplayListBuilder& aBuilder,
                                       wr::IpcResourceUpdateQueue& aResources,
                                       const StackingContextHelper& aSc,
                                       nsDisplayListBuilder* aDisplayListBuilder)
{
  LayerRect imageRect;
  RefPtr<WebRenderFallbackData> fallbackData = GenerateFallbackData(aItem, aBuilder, aResources,
                                                                    aSc, aDisplayListBuilder,
                                                                    imageRect);
  if (!fallbackData) {
    return false;
  }

  wr::LayoutRect dest = aSc.ToRelativeLayoutRect(imageRect);
  SamplingFilter sampleFilter = nsLayoutUtils::GetSamplingFilterForFrame(aItem->Frame());
  aBuilder.PushImage(dest,
                     dest,
                     !aItem->BackfaceIsHidden(),
                     wr::ToImageRendering(sampleFilter),
                     fallbackData->GetKey().value());
  return true;
}

void
WebRenderLayerManager::EndTransaction(DrawPaintedLayerCallback aCallback,
                                      void* aCallbackData,
                                      EndTransactionFlags aFlags)
{
  mEndTransactionWithoutLayers = false;
  WrBridge()->RemoveExpiredFontKeys();
  EndTransactionInternal(aCallback, aCallbackData, aFlags);
}

bool
WebRenderLayerManager::EndTransactionInternal(DrawPaintedLayerCallback aCallback,
                                              void* aCallbackData,
                                              EndTransactionFlags aFlags,
                                              nsDisplayList* aDisplayList,
                                              nsDisplayListBuilder* aDisplayListBuilder)
{
  AutoProfilerTracing tracing("Paint", "RenderLayers");
  mPaintedLayerCallback = aCallback;
  mPaintedLayerCallbackData = aCallbackData;
  mTransactionIncomplete = false;

  if (gfxPrefs::LayersDump()) {
    this->Dump();
  }

  // Since we don't do repeat transactions right now, just set the time
  mAnimationReadyTime = TimeStamp::Now();

  LayoutDeviceIntSize size = mWidget->GetClientSize();
  if (!WrBridge()->BeginTransaction(size.ToUnknownSize())) {
    return false;
  }
  DiscardCompositorAnimations();

  wr::LayoutSize contentSize { (float)size.width, (float)size.height };
  wr::DisplayListBuilder builder(WrBridge()->GetPipeline(), contentSize);
  wr::IpcResourceUpdateQueue resourceUpdates(WrBridge()->GetShmemAllocator());

  if (mEndTransactionWithoutLayers) {
    // Reset the notification flag at the begin of the EndTransaction.
    mShouldNotifyInvalidation = false;

    // aDisplayList being null here means this is an empty transaction following a layers-free
    // transaction, so we reuse the previously built displaylist and scroll
    // metadata information
    if (aDisplayList && aDisplayListBuilder) {
      StackingContextHelper sc;
      mParentCommands.Clear();
      mScrollData = WebRenderScrollData();
      MOZ_ASSERT(mLayerScrollData.empty());
      mLastCanvasDatas.Clear();
      mLastAsr = nullptr;

      CreateWebRenderCommandsFromDisplayList(aDisplayList, aDisplayListBuilder, sc, builder, resourceUpdates);

      builder.Finalize(contentSize, mBuiltDisplayList);

      // Make a "root" layer data that has everything else as descendants
      mLayerScrollData.emplace_back();
      mLayerScrollData.back().InitializeRoot(mLayerScrollData.size() - 1);
      if (aDisplayListBuilder->IsBuildingLayerEventRegions()) {
        nsIPresShell* shell = aDisplayListBuilder->RootReferenceFrame()->PresContext()->PresShell();
        if (nsLayoutUtils::HasDocumentLevelListenersForApzAwareEvents(shell)) {
          mLayerScrollData.back().SetEventRegionsOverride(EventRegionsOverride::ForceDispatchToContent);
        }
      }
      RefPtr<WebRenderLayerManager> self(this);
      auto callback = [self](FrameMetrics::ViewID aScrollId) -> bool {
        return self->mScrollData.HasMetadataFor(aScrollId);
      };
      if (Maybe<ScrollMetadata> rootMetadata = nsLayoutUtils::GetRootMetadata(
            aDisplayListBuilder, nullptr, ContainerLayerParameters(), callback)) {
        mLayerScrollData.back().AppendScrollMetadata(mScrollData, rootMetadata.ref());
      }
      // Append the WebRenderLayerScrollData items into WebRenderScrollData
      // in reverse order, from topmost to bottommost. This is in keeping with
      // the semantics of WebRenderScrollData.
      for (auto i = mLayerScrollData.crbegin(); i != mLayerScrollData.crend(); i++) {
        mScrollData.AddLayerData(*i);
      }
      mLayerScrollData.clear();
      mClipIdCache.clear();

      // Remove the user data those are not displayed on the screen and
      // also reset the data to unused for next transaction.
      RemoveUnusedAndResetWebRenderUserData();
    } else {
      for (auto iter = mLastCanvasDatas.Iter(); !iter.Done(); iter.Next()) {
        RefPtr<WebRenderCanvasData> canvasData = iter.Get()->GetKey();
        WebRenderCanvasRendererAsync* canvas = canvasData->GetCanvasRenderer();
        if (canvas->IsDirty()) {
          mShouldNotifyInvalidation = true;
        }
        canvas->UpdateCompositableClient();
      }
    }

    builder.PushBuiltDisplayList(mBuiltDisplayList);
    WrBridge()->AddWebRenderParentCommands(mParentCommands);
  } else {
    mScrollData = WebRenderScrollData();

    mRoot->StartPendingAnimations(mAnimationReadyTime);
    StackingContextHelper sc;

    WebRenderLayer::ToWebRenderLayer(mRoot)->RenderLayer(builder, resourceUpdates, sc);

    // Need to do this after RenderLayer because the compositor animation IDs
    // get populated during RenderLayer and we need those.
    PopulateScrollData(mScrollData, mRoot.get());
  }

  mWidget->AddWindowOverlayWebRenderCommands(WrBridge(), builder, resourceUpdates);
  WrBridge()->ClearReadLocks();

  // We can't finish this transaction so return. This usually
  // happens in an empty transaction where we can't repaint a painted layer.
  // In this case, leave the transaction open and let a full transaction happen.
  if (mTransactionIncomplete) {
    DiscardLocalImages();
    WrBridge()->ProcessWebRenderParentCommands();
    return false;
  }

  if (AsyncPanZoomEnabled()) {
    mScrollData.SetFocusTarget(mFocusTarget);
    mFocusTarget = FocusTarget();

    if (mIsFirstPaint) {
      mScrollData.SetIsFirstPaint();
      mIsFirstPaint = false;
    }
    mScrollData.SetPaintSequenceNumber(mPaintSequenceNumber);
  }

  bool sync = mTarget != nullptr;
  mLatestTransactionId = mTransactionIdAllocator->GetTransactionId(/*aThrottle*/ true);
  TimeStamp transactionStart = mTransactionIdAllocator->GetTransactionStart();

  for (const auto& key : mImageKeysToDelete) {
    resourceUpdates.DeleteImage(key);
  }
  mImageKeysToDelete.Clear();
  mImageKeysToDelete.SwapElements(mImageKeysToDeleteLater);

  // Skip the synchronization for buffer since we also skip the painting during
  // device-reset status.
  if (!gfxPlatform::GetPlatform()->DidRenderingDeviceReset()) {
    if (WrBridge()->GetSyncObject() &&
        WrBridge()->GetSyncObject()->IsSyncObjectValid()) {
      WrBridge()->GetSyncObject()->Synchronize();
    }
  }
  {
    AutoProfilerTracing
      tracing("Paint", sync ? "ForwardDPTransactionSync":"ForwardDPTransaction");
    WrBridge()->EndTransaction(builder, resourceUpdates, size.ToUnknownSize(), sync,
                               mLatestTransactionId, mScrollData, transactionStart);
  }

  MakeSnapshotIfRequired(size);
  mNeedsComposite = false;

  ClearDisplayItemLayers();

  // this may result in Layers being deleted, which results in
  // PLayer::Send__delete__() and DeallocShmem()
  mKeepAlive.Clear();
  ClearMutatedLayers();

  return true;
}

void
WebRenderLayerManager::SetFocusTarget(const FocusTarget& aFocusTarget)
{
  mFocusTarget = aFocusTarget;
}

bool
WebRenderLayerManager::AsyncPanZoomEnabled() const
{
  return mWidget->AsyncPanZoomEnabled();
}

void
WebRenderLayerManager::MakeSnapshotIfRequired(LayoutDeviceIntSize aSize)
{
  if (!mTarget || aSize.IsEmpty()) {
    return;
  }

  // XXX Add other TextureData supports.
  // Only BufferTexture is supported now.

  // TODO: fixup for proper surface format.
  RefPtr<TextureClient> texture =
    TextureClient::CreateForRawBufferAccess(WrBridge(),
                                            SurfaceFormat::B8G8R8A8,
                                            aSize.ToUnknownSize(),
                                            BackendType::SKIA,
                                            TextureFlags::SNAPSHOT);
  if (!texture) {
    return;
  }

  texture->InitIPDLActor(WrBridge());
  if (!texture->GetIPDLActor()) {
    return;
  }

  IntRect bounds = ToOutsideIntRect(mTarget->GetClipExtents());
  if (!WrBridge()->SendGetSnapshot(texture->GetIPDLActor())) {
    return;
  }

  TextureClientAutoLock autoLock(texture, OpenMode::OPEN_READ_ONLY);
  if (!autoLock.Succeeded()) {
    return;
  }
  RefPtr<DrawTarget> drawTarget = texture->BorrowDrawTarget();
  if (!drawTarget || !drawTarget->IsValid()) {
    return;
  }
  RefPtr<SourceSurface> snapshot = drawTarget->Snapshot();
/*
  static int count = 0;
  char filename[100];
  snprintf(filename, 100, "output%d.png", count++);
  printf_stderr("Writing to :%s\n", filename);
  gfxUtils::WriteAsPNG(snapshot, filename);
  */

  Rect dst(bounds.x, bounds.y, bounds.Width(), bounds.Height());
  Rect src(0, 0, bounds.Width(), bounds.Height());

  // The data we get from webrender is upside down. So flip and translate up so the image is rightside up.
  // Webrender always does a full screen readback.
  SurfacePattern pattern(snapshot, ExtendMode::CLAMP,
                         Matrix::Scaling(1.0, -1.0).PostTranslate(0.0, aSize.height));
  DrawTarget* dt = mTarget->GetDrawTarget();
  MOZ_RELEASE_ASSERT(dt);
  dt->FillRect(dst, pattern);

  mTarget = nullptr;
}

void
WebRenderLayerManager::AddImageKeyForDiscard(wr::ImageKey key)
{
  mImageKeysToDeleteLater.AppendElement(key);
}

void
WebRenderLayerManager::DiscardImages()
{
  wr::IpcResourceUpdateQueue resources(WrBridge()->GetShmemAllocator());
  for (const auto& key : mImageKeysToDeleteLater) {
    resources.DeleteImage(key);
  }
  for (const auto& key : mImageKeysToDelete) {
    resources.DeleteImage(key);
  }
  mImageKeysToDeleteLater.Clear();
  mImageKeysToDelete.Clear();
  WrBridge()->UpdateResources(resources);
}

void
WebRenderLayerManager::AddActiveCompositorAnimationId(uint64_t aId)
{
  // In layers-free mode we track the active compositor animation ids on the
  // client side so that we don't try to discard the same animation id multiple
  // times. We could just ignore the multiple-discard on the parent side, but
  // checking on the content side reduces IPC traffic.
  MOZ_ASSERT(IsLayersFreeTransaction());
  mActiveCompositorAnimationIds.insert(aId);
}

void
WebRenderLayerManager::AddCompositorAnimationsIdForDiscard(uint64_t aId)
{
  if (!IsLayersFreeTransaction()) {
    // For layers-full we don't track the active animation id in
    // mActiveCompositorAnimationIds, we just call this on layer destruction and
    // don't need to worry about discarding the same id multiple times.
    mDiscardedCompositorAnimationsIds.AppendElement(aId);
  } else if (mActiveCompositorAnimationIds.erase(aId)) {
    // For layers-free ensure we don't try to discard an animation id that wasn't
    // active. We also remove it from mActiveCompositorAnimationIds so we don't
    // discard it again unless it gets re-activated.
    mDiscardedCompositorAnimationsIds.AppendElement(aId);
  }
}

void
WebRenderLayerManager::DiscardCompositorAnimations()
{
  if (WrBridge()->IPCOpen() &&
      !mDiscardedCompositorAnimationsIds.IsEmpty()) {
    WrBridge()->
      SendDeleteCompositorAnimations(mDiscardedCompositorAnimationsIds);
  }
  mDiscardedCompositorAnimationsIds.Clear();
}

void
WebRenderLayerManager::DiscardLocalImages()
{
  // Removes images but doesn't tell the parent side about them
  // This is useful in empty / failed transactions where we created
  // image keys but didn't tell the parent about them yet.
  mImageKeysToDeleteLater.Clear();
  mImageKeysToDelete.Clear();
}

void
WebRenderLayerManager::Mutated(Layer* aLayer)
{
  LayerManager::Mutated(aLayer);
  AddMutatedLayer(aLayer);
}

void
WebRenderLayerManager::MutatedSimple(Layer* aLayer)
{
  LayerManager::Mutated(aLayer);
  AddMutatedLayer(aLayer);
}

void
WebRenderLayerManager::AddMutatedLayer(Layer* aLayer)
{
  mMutatedLayers.AppendElement(aLayer);
}

void
WebRenderLayerManager::ClearMutatedLayers()
{
  mMutatedLayers.Clear();
}

bool
WebRenderLayerManager::IsMutatedLayer(Layer* aLayer)
{
  return mMutatedLayers.Contains(aLayer);
}

void
WebRenderLayerManager::Hold(Layer* aLayer)
{
  mKeepAlive.AppendElement(aLayer);
}

void
WebRenderLayerManager::SetLayerObserverEpoch(uint64_t aLayerObserverEpoch)
{
  if (WrBridge()->IPCOpen()) {
    WrBridge()->SendSetLayerObserverEpoch(aLayerObserverEpoch);
  }
}

void
WebRenderLayerManager::DidComposite(uint64_t aTransactionId,
                                    const mozilla::TimeStamp& aCompositeStart,
                                    const mozilla::TimeStamp& aCompositeEnd)
{
  MOZ_ASSERT(mWidget);

  // Notifying the observers may tick the refresh driver which can cause
  // a lot of different things to happen that may affect the lifetime of
  // this layer manager. So let's make sure this object stays alive until
  // the end of the method invocation.
  RefPtr<WebRenderLayerManager> selfRef = this;

  // |aTransactionId| will be > 0 if the compositor is acknowledging a shadow
  // layers transaction.
  if (aTransactionId) {
    nsIWidgetListener *listener = mWidget->GetWidgetListener();
    if (listener) {
      listener->DidCompositeWindow(aTransactionId, aCompositeStart, aCompositeEnd);
    }
    listener = mWidget->GetAttachedWidgetListener();
    if (listener) {
      listener->DidCompositeWindow(aTransactionId, aCompositeStart, aCompositeEnd);
    }
    mTransactionIdAllocator->NotifyTransactionCompleted(aTransactionId);
  }

  // These observers fire whether or not we were in a transaction.
  for (size_t i = 0; i < mDidCompositeObservers.Length(); i++) {
    mDidCompositeObservers[i]->DidComposite();
  }
}

void
WebRenderLayerManager::ClearLayer(Layer* aLayer)
{
  aLayer->ClearCachedResources();
  if (aLayer->GetMaskLayer()) {
    aLayer->GetMaskLayer()->ClearCachedResources();
  }
  for (size_t i = 0; i < aLayer->GetAncestorMaskLayerCount(); i++) {
    aLayer->GetAncestorMaskLayerAt(i)->ClearCachedResources();
  }
  for (Layer* child = aLayer->GetFirstChild(); child;
       child = child->GetNextSibling()) {
    ClearLayer(child);
  }
}

void
WebRenderLayerManager::ClearCachedResources(Layer* aSubtree)
{
  WrBridge()->BeginClearCachedResources();
  if (aSubtree) {
    ClearLayer(aSubtree);
  } else if (mRoot) {
    ClearLayer(mRoot);
  }
  DiscardImages();
  WrBridge()->EndClearCachedResources();
}

void
WebRenderLayerManager::UpdateTextureFactoryIdentifier(const TextureFactoryIdentifier& aNewIdentifier,
                                                      uint64_t aDeviceResetSeqNo)
{
  WrBridge()->IdentifyTextureHost(aNewIdentifier);
}

TextureFactoryIdentifier
WebRenderLayerManager::GetTextureFactoryIdentifier()
{
  return WrBridge()->GetTextureFactoryIdentifier();
}

void
WebRenderLayerManager::AddDidCompositeObserver(DidCompositeObserver* aObserver)
{
  if (!mDidCompositeObservers.Contains(aObserver)) {
    mDidCompositeObservers.AppendElement(aObserver);
  }
}

void
WebRenderLayerManager::RemoveDidCompositeObserver(DidCompositeObserver* aObserver)
{
  mDidCompositeObservers.RemoveElement(aObserver);
}

void
WebRenderLayerManager::FlushRendering()
{
  CompositorBridgeChild* cBridge = GetCompositorBridgeChild();
  if (!cBridge) {
    return;
  }
  MOZ_ASSERT(mWidget);

  if (mWidget->SynchronouslyRepaintOnResize() || gfxPrefs::LayersForceSynchronousResize()) {
    cBridge->SendFlushRendering();
  } else {
    cBridge->SendFlushRenderingAsync();
  }
}

void
WebRenderLayerManager::WaitOnTransactionProcessed()
{
  CompositorBridgeChild* bridge = GetCompositorBridgeChild();
  if (bridge) {
    bridge->SendWaitOnTransactionProcessed();
  }
}

void
WebRenderLayerManager::SendInvalidRegion(const nsIntRegion& aRegion)
{
  // XXX Webrender does not support invalid region yet.
}

void
WebRenderLayerManager::ScheduleComposite()
{
  WrBridge()->SendForceComposite();
}

void
WebRenderLayerManager::SetRoot(Layer* aLayer)
{
  mRoot = aLayer;
}

already_AddRefed<PaintedLayer>
WebRenderLayerManager::CreatePaintedLayer()
{
  if (gfxPrefs::WebRenderBlobImages()) {
    return MakeAndAddRef<WebRenderPaintedLayerBlob>(this);
  } else {
    return MakeAndAddRef<WebRenderPaintedLayer>(this);
  }
}

already_AddRefed<ContainerLayer>
WebRenderLayerManager::CreateContainerLayer()
{
  return MakeAndAddRef<WebRenderContainerLayer>(this);
}

already_AddRefed<ImageLayer>
WebRenderLayerManager::CreateImageLayer()
{
  return MakeAndAddRef<WebRenderImageLayer>(this);
}

already_AddRefed<CanvasLayer>
WebRenderLayerManager::CreateCanvasLayer()
{
  return MakeAndAddRef<WebRenderCanvasLayer>(this);
}

already_AddRefed<ReadbackLayer>
WebRenderLayerManager::CreateReadbackLayer()
{
  return nullptr;
}

already_AddRefed<ColorLayer>
WebRenderLayerManager::CreateColorLayer()
{
  return MakeAndAddRef<WebRenderColorLayer>(this);
}

already_AddRefed<RefLayer>
WebRenderLayerManager::CreateRefLayer()
{
  return MakeAndAddRef<WebRenderRefLayer>(this);
}

already_AddRefed<TextLayer>
WebRenderLayerManager::CreateTextLayer()
{
  return MakeAndAddRef<WebRenderTextLayer>(this);
}

already_AddRefed<BorderLayer>
WebRenderLayerManager::CreateBorderLayer()
{
  return nullptr;
}

already_AddRefed<DisplayItemLayer>
WebRenderLayerManager::CreateDisplayItemLayer()
{
  return MakeAndAddRef<WebRenderDisplayItemLayer>(this);
}

bool
WebRenderLayerManager::SetPendingScrollUpdateForNextTransaction(FrameMetrics::ViewID aScrollId,
                                                                const ScrollUpdateInfo& aUpdateInfo)
{
  // If we ever support changing the scroll position in an "empty transactions"
  // properly in WR we can fill this in. Covered by bug 1382259.
  return false;
}

} // namespace layers
} // namespace mozilla
