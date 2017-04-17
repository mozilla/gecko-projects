/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WebRenderLayerManager.h"

#include "apz/src/AsyncPanZoomController.h"
#include "gfxPrefs.h"
#include "LayersLogging.h"
#include "mozilla/dom/TabChild.h"
#include "mozilla/layers/APZCTreeManager.h"
#include "mozilla/layers/AsyncCompositionManager.h"
#include "mozilla/layers/CompositorBridgeChild.h"
#include "mozilla/layers/TextureClient.h"
#include "mozilla/layers/WebRenderBridgeChild.h"
#include "mozilla/widget/PlatformWidgetTypes.h"
#include "nsThreadUtils.h"
#include "TreeTraversal.h"
#include "WebRenderCanvasLayer.h"
#include "WebRenderColorLayer.h"
#include "WebRenderContainerLayer.h"
#include "WebRenderImageLayer.h"
#include "WebRenderPaintedLayer.h"
#include "WebRenderTextLayer.h"
#include "WebRenderDisplayItemLayer.h"

namespace mozilla {

using namespace gfx;

namespace layers {

WebRenderLayerManager*
WebRenderLayer::WrManager()
{
  return static_cast<WebRenderLayerManager*>(GetLayer()->Manager());
}

WebRenderBridgeChild*
WebRenderLayer::WrBridge()
{
  return WrManager()->WrBridge();
}

Rect
WebRenderLayer::RelativeToVisible(Rect aRect)
{
  IntRect bounds = GetLayer()->GetVisibleRegion().GetBounds().ToUnknownRect();
  aRect.MoveBy(-bounds.x, -bounds.y);
  return aRect;
}

Rect
WebRenderLayer::RelativeToTransformedVisible(Rect aRect)
{
  IntRect bounds = GetLayer()->GetVisibleRegion().GetBounds().ToUnknownRect();
  Rect transformed = GetLayer()->GetTransform().TransformBounds(IntRectToRect(bounds));
  aRect.MoveBy(-transformed.x, -transformed.y);
  return aRect;
}

Rect
WebRenderLayer::ParentStackingContextBounds()
{
  // Walk up to find the parent stacking context. This will be created either
  // by the nearest scrollable metrics, or by the parent layer which must be a
  // ContainerLayer.
  Layer* layer = GetLayer();
  if (layer->GetParent()) {
    return IntRectToRect(layer->GetParent()->GetVisibleRegion().GetBounds().ToUnknownRect());
  }
  return Rect();
}

Rect
WebRenderLayer::RelativeToParent(Rect aRect)
{
  Rect parentBounds = ParentStackingContextBounds();
  aRect.MoveBy(-parentBounds.x, -parentBounds.y);
  return aRect;
}

Point
WebRenderLayer::GetOffsetToParent()
{
  Rect parentBounds = ParentStackingContextBounds();
  return parentBounds.TopLeft();
}

Rect
WebRenderLayer::VisibleBoundsRelativeToParent()
{
  return RelativeToParent(IntRectToRect(GetLayer()->GetVisibleRegion().GetBounds().ToUnknownRect()));
}

Rect
WebRenderLayer::TransformedVisibleBoundsRelativeToParent()
{
  IntRect bounds = GetLayer()->GetVisibleRegion().GetBounds().ToUnknownRect();
  Rect transformed = GetLayer()->GetTransform().TransformBounds(IntRectToRect(bounds));
  return RelativeToParent(transformed);
}

Maybe<WrImageMask>
WebRenderLayer::BuildWrMaskLayer(bool aUnapplyLayerTransform)
{
  if (GetLayer()->GetMaskLayer()) {
    WebRenderLayer* maskLayer = ToWebRenderLayer(GetLayer()->GetMaskLayer());
    // The size of mask layer is transformed, and we may set the layer transform to wr stacking context.
    // So we should apply inverse transform for mask layer.
    gfx::Matrix4x4 transform;
    if (aUnapplyLayerTransform) {
      transform = GetWrBoundTransform().Inverse();
    }
    return maskLayer->RenderMaskLayer(transform);
  }

  return Nothing();
}

gfx::Rect
WebRenderLayer::GetWrBoundsRect()
{
  LayerIntRect bounds = GetLayer()->GetVisibleRegion().GetBounds();
  return Rect(0, 0, bounds.width, bounds.height);
}

gfx::Rect
WebRenderLayer::GetWrClipRect(gfx::Rect& aRect)
{
  gfx::Rect clip;
  Layer* layer = GetLayer();
  Matrix4x4 transform = layer->GetTransform();
  if (layer->GetClipRect().isSome()) {
    clip = RelativeToVisible(transform.Inverse().TransformBounds(
             IntRectToRect(layer->GetClipRect().ref().ToUnknownRect()))
           );
  } else {
    clip = aRect;
  }

  return clip;
}

gfx::Matrix4x4
WebRenderLayer::GetWrBoundTransform()
{
  gfx::Matrix4x4 transform = GetLayer()->GetTransform();
  transform._41 = 0.0f;
  transform._42 = 0.0f;
  transform._43 = 0.0f;
  return transform;
}

gfx::Rect
WebRenderLayer::GetWrRelBounds()
{
  gfx::Rect bounds = IntRectToRect(GetLayer()->GetVisibleRegion().GetBounds().ToUnknownRect());
  gfx::Matrix4x4 transform = GetWrBoundTransform();
  if (!transform.IsIdentity()) {
    // WR will only apply the 'translate' of the transform, so we need to do the scale/rotation manually.
    bounds.MoveTo(transform.TransformPoint(bounds.TopLeft()));
  }

  return RelativeToParent(bounds);
}

void
WebRenderLayer::DumpLayerInfo(const char* aLayerType, gfx::Rect& aRect)
{
  if (!gfxPrefs::LayersDump()) {
    return;
  }

  Matrix4x4 transform = GetLayer()->GetTransform();
  Rect clip = GetWrClipRect(aRect);
  Rect relBounds = GetWrRelBounds();
  Rect overflow(0, 0, relBounds.width, relBounds.height);
  WrMixBlendMode mixBlendMode = wr::ToWrMixBlendMode(GetLayer()->GetMixBlendMode());

  printf_stderr("%s %p using bounds=%s, overflow=%s, transform=%s, rect=%s, clip=%s, mix-blend-mode=%s\n",
                aLayerType,
                GetLayer(),
                Stringify(relBounds).c_str(),
                Stringify(overflow).c_str(),
                Stringify(transform).c_str(),
                Stringify(aRect).c_str(),
                Stringify(clip).c_str(),
                Stringify(mixBlendMode).c_str());
}

WebRenderLayerManager::WebRenderLayerManager(nsIWidget* aWidget)
  : mWidget(aWidget)
  , mLatestTransactionId(0)
  , mNeedsComposite(false)
  , mTarget(nullptr)
{
  MOZ_COUNT_CTOR(WebRenderLayerManager);
}

KnowsCompositor*
WebRenderLayerManager::AsKnowsCompositor()
{
  return mWrChild;
}

void
WebRenderLayerManager::Initialize(PCompositorBridgeChild* aCBChild,
                                  wr::PipelineId aLayersId,
                                  TextureFactoryIdentifier* aTextureFactoryIdentifier)
{
  MOZ_ASSERT(mWrChild == nullptr);
  MOZ_ASSERT(aTextureFactoryIdentifier);

  LayoutDeviceIntSize size = mWidget->GetClientSize();
  TextureFactoryIdentifier textureFactoryIdentifier;
  uint32_t id_namespace;
  PWebRenderBridgeChild* bridge = aCBChild->SendPWebRenderBridgeConstructor(aLayersId,
                                                                            size,
                                                                            &textureFactoryIdentifier,
                                                                            &id_namespace);
  MOZ_ASSERT(bridge);
  mWrChild = static_cast<WebRenderBridgeChild*>(bridge);
  WrBridge()->SendCreate(size.ToUnknownSize());
  WrBridge()->IdentifyTextureHost(textureFactoryIdentifier);
  WrBridge()->SetNamespace(id_namespace);
  *aTextureFactoryIdentifier = textureFactoryIdentifier;
}

void
WebRenderLayerManager::Destroy()
{
  if (IsDestroyed()) {
    return;
  }

  LayerManager::Destroy();
  DiscardImages();
  WrBridge()->Destroy();

  if (mTransactionIdAllocator) {
    // Make sure to notify the refresh driver just in case it's waiting on a
    // pending transaction. Do this at the top of the event loop so we don't
    // cause a paint to occur during compositor shutdown.
    RefPtr<TransactionIdAllocator> allocator = mTransactionIdAllocator;
    uint64_t id = mLatestTransactionId;

    RefPtr<Runnable> task = NS_NewRunnableFunction([allocator, id] () -> void {
      allocator->NotifyTransactionCompleted(id);
    });
    NS_DispatchToMainThread(task.forget());
  }
}

WebRenderLayerManager::~WebRenderLayerManager()
{
  Destroy();
  MOZ_COUNT_DTOR(WebRenderLayerManager);
}

CompositorBridgeChild*
WebRenderLayerManager::GetCompositorBridgeChild()
{
  return mWidget ? mWidget->GetRemoteRenderer() : nullptr;
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
  return true;
}

bool
WebRenderLayerManager::EndEmptyTransaction(EndTransactionFlags aFlags)
{
  return false;
}

void
WebRenderLayerManager::EndTransaction(DrawPaintedLayerCallback aCallback,
                                      void* aCallbackData,
                                      EndTransactionFlags aFlags)
{
  DiscardImages();
  WrBridge()->RemoveExpiredFontKeys();

  mPaintedLayerCallback = aCallback;
  mPaintedLayerCallbackData = aCallbackData;

  if (gfxPrefs::LayersDump()) {
    this->Dump();
  }

  // Since we don't do repeat transactions right now, just set the time
  mAnimationReadyTime = TimeStamp::Now();

  LayoutDeviceIntSize size = mWidget->GetClientSize();
  if (!WrBridge()->DPBegin(size.ToUnknownSize())) {
    return;
  }

  wr::DisplayListBuilder builder(WrBridge()->GetPipeline());
  WebRenderLayer::ToWebRenderLayer(mRoot)->RenderLayer(builder);

  bool sync = mTarget != nullptr;
  mLatestTransactionId = mTransactionIdAllocator->GetTransactionId();

  WrBridge()->DPEnd(builder, size.ToUnknownSize(), sync, mLatestTransactionId);

  MakeSnapshotIfRequired(size);
  mNeedsComposite = false;

  ClearDisplayItemLayers();

  // this may result in Layers being deleted, which results in
  // PLayer::Send__delete__() and DeallocShmem()
  mKeepAlive.Clear();
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
  if (!WrBridge()->SendDPGetSnapshot(texture->GetIPDLActor())) {
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

  Rect dst(bounds.x, bounds.y, bounds.width, bounds.height);
  Rect src(0, 0, bounds.width, bounds.height);

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
  mImageKeys.push_back(key);
}

void
WebRenderLayerManager::DiscardImages()
{
  for (auto key : mImageKeys) {
      WrBridge()->SendDeleteImage(key);
  }
  mImageKeys.clear();
}

void
WebRenderLayerManager::Hold(Layer* aLayer)
{
  mKeepAlive.AppendElement(aLayer);
}

void
WebRenderLayerManager::SetLayerObserverEpoch(uint64_t aLayerObserverEpoch)
{
  WrBridge()->SendSetLayerObserverEpoch(aLayerObserverEpoch);
}

void
WebRenderLayerManager::DidComposite(uint64_t aTransactionId,
                                    const mozilla::TimeStamp& aCompositeStart,
                                    const mozilla::TimeStamp& aCompositeEnd)
{
  MOZ_ASSERT(mWidget);

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
  for (Layer* child = aLayer->GetFirstChild(); child;
       child = child->GetNextSibling()) {
    ClearLayer(child);
  }
}

void
WebRenderLayerManager::ClearCachedResources(Layer* aSubtree)
{
  WrBridge()->SendClearCachedResources();
  if (aSubtree) {
    ClearLayer(aSubtree);
  } else if (mRoot) {
    ClearLayer(mRoot);
  }
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
  CompositorBridgeChild* bridge = GetCompositorBridgeChild();
  if (bridge) {
    bridge->SendFlushRendering();
  }
}

void
WebRenderLayerManager::SendInvalidRegion(const nsIntRegion& aRegion)
{
  // XXX Webrender does not support invalid region yet.
}

void
WebRenderLayerManager::Composite()
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
  return MakeAndAddRef<WebRenderPaintedLayer>(this);
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

} // namespace layers
} // namespace mozilla
