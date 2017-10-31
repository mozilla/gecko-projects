/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WebRenderUserData.h"

#include "mozilla/layers/ImageClient.h"
#include "mozilla/layers/WebRenderBridgeChild.h"
#include "mozilla/layers/WebRenderLayerManager.h"
#include "mozilla/layers/WebRenderMessages.h"
#include "mozilla/layers/IpcResourceUpdateQueue.h"
#include "mozilla/layers/SharedSurfacesChild.h"
#include "nsDisplayListInvalidation.h"
#include "WebRenderCanvasRenderer.h"

namespace mozilla {
namespace layers {

WebRenderUserData::WebRenderUserData(WebRenderLayerManager* aWRManager, nsDisplayItem* aItem)
  : mWRManager(aWRManager)
  , mFrame(aItem->Frame())
  , mDisplayItemKey(aItem->GetPerFrameKey())
  , mTable(aWRManager->GetWebRenderUserDataTable())
  , mUsed(false)
{
}

WebRenderUserData::~WebRenderUserData()
{
}

bool
WebRenderUserData::IsDataValid(WebRenderLayerManager* aManager)
{
  return aManager == mWRManager;
}

void
WebRenderUserData::RemoveFromTable()
{
  mTable->RemoveEntry(this);
}

WebRenderBridgeChild*
WebRenderUserData::WrBridge() const
{
  return mWRManager->WrBridge();
}

WebRenderImageData::WebRenderImageData(WebRenderLayerManager* aWRManager, nsDisplayItem* aItem)
  : WebRenderUserData(aWRManager, aItem)
  , mGeneration(0)
{
}

WebRenderImageData::~WebRenderImageData()
{
  ClearCachedResources();
}

void
WebRenderImageData::ClearCachedResources()
{
  if (mKey) {
    mWRManager->AddImageKeyForDiscard(mKey.value());
    mKey.reset();
  }

  if (mExternalImageId) {
    WrBridge()->DeallocExternalImageId(mExternalImageId.ref());
    mExternalImageId.reset();
  }

  if (mPipelineId) {
    WrBridge()->RemovePipelineIdForCompositable(mPipelineId.ref());
    mPipelineId.reset();
  }
}

Maybe<wr::ImageKey>
WebRenderImageData::UpdateImageKey(ImageContainer* aContainer,
                                   wr::IpcResourceUpdateQueue& aResources,
                                   bool aForceUpdate)
{
  MOZ_ASSERT(aContainer);

  if (mContainer != aContainer) {
    mContainer = aContainer;
  }

  wr::ExternalImageId externalId;
  uint32_t generation;
  nsresult rv = SharedSurfacesChild::Share(aContainer, externalId, generation);
  if (NS_SUCCEEDED(rv)) {
    if (mExternalImageId.isSome() && mExternalImageId.ref() == externalId) {
      // The image container has the same surface as before, we can reuse the
      // key if the generation matches and the caller allows us.
      if (mKey && mGeneration == generation && !aForceUpdate) {
        return mKey;
      }
    } else {
      // The image container has a new surface, generate a new image key.
      mExternalImageId = Some(externalId);
    }

    mGeneration = generation;
  } else if (rv == NS_ERROR_NOT_IMPLEMENTED) {
    CreateImageClientIfNeeded();
    CreateExternalImageIfNeeded();

    if (!mImageClient || !mExternalImageId) {
      return Nothing();
    }

    MOZ_ASSERT(mImageClient->AsImageClientSingle());

    ImageClientSingle* imageClient = mImageClient->AsImageClientSingle();
    uint32_t oldCounter = imageClient->GetLastUpdateGenerationCounter();

    bool ret = imageClient->UpdateImage(aContainer, /* unused */0);
    if (!ret || imageClient->IsEmpty()) {
      // Delete old key
      if (mKey) {
        mWRManager->AddImageKeyForDiscard(mKey.value());
        mKey = Nothing();
      }
      return Nothing();
    }

    // Reuse old key if generation is not updated.
    if (!aForceUpdate && oldCounter == imageClient->GetLastUpdateGenerationCounter() && mKey) {
      return mKey;
    }
  }

  // Delete old key, we are generating a new key.
  // TODO(nical): noooo... we need to reuse image keys.
  if (mKey) {
    mWRManager->AddImageKeyForDiscard(mKey.value());
  }

  wr::WrImageKey key = WrBridge()->GetNextImageKey();
  aResources.AddExternalImage(mExternalImageId.value(), key);
  mKey = Some(key);

  return mKey;
}

already_AddRefed<ImageClient>
WebRenderImageData::GetImageClient()
{
  RefPtr<ImageClient> imageClient = mImageClient;
  return imageClient.forget();
}

void
WebRenderImageData::CreateAsyncImageWebRenderCommands(mozilla::wr::DisplayListBuilder& aBuilder,
                                                      ImageContainer* aContainer,
                                                      const StackingContextHelper& aSc,
                                                      const LayoutDeviceRect& aBounds,
                                                      const LayoutDeviceRect& aSCBounds,
                                                      const gfx::Matrix4x4& aSCTransform,
                                                      const gfx::MaybeIntSize& aScaleToSize,
                                                      const wr::ImageRendering& aFilter,
                                                      const wr::MixBlendMode& aMixBlendMode,
                                                      bool aIsBackfaceVisible)
{
  MOZ_ASSERT(aContainer->IsAsync());
  if (!mPipelineId) {
    // Alloc async image pipeline id.
    mPipelineId = Some(WrBridge()->GetCompositorBridgeChild()->GetNextPipelineId());
    WrBridge()->AddPipelineIdForAsyncCompositable(mPipelineId.ref(),
                                                  aContainer->GetAsyncContainerHandle());
  }
  MOZ_ASSERT(!mImageClient);
  MOZ_ASSERT(!mExternalImageId);

  // Push IFrame for async image pipeline.
  //
  // We don't push a stacking context for this async image pipeline here.
  // Instead, we do it inside the iframe that hosts the image. As a result,
  // a bunch of the calculations normally done as part of that stacking
  // context need to be done manually and pushed over to the parent side,
  // where it will be done when we build the display list for the iframe.
  // That happens in AsyncImagePipelineManager.
  wr::LayoutRect r = aSc.ToRelativeLayoutRect(aBounds);
  aBuilder.PushIFrame(r, aIsBackfaceVisible, mPipelineId.ref());

  WrBridge()->AddWebRenderParentCommand(OpUpdateAsyncImagePipeline(mPipelineId.value(),
                                                                   aSCBounds,
                                                                   aSCTransform,
                                                                   aScaleToSize,
                                                                   aFilter,
                                                                   aMixBlendMode));
}

void
WebRenderImageData::CreateImageClientIfNeeded()
{
  if (!mImageClient) {
    mImageClient = ImageClient::CreateImageClient(CompositableType::IMAGE,
                                                  WrBridge(),
                                                  TextureFlags::DEFAULT);
    if (!mImageClient) {
      return;
    }

    mImageClient->Connect();
  }
}

void
WebRenderImageData::CreateExternalImageIfNeeded()
{
  if (!mExternalImageId)  {
    mExternalImageId = Some(WrBridge()->AllocExternalImageIdForCompositable(mImageClient));
  }
}

WebRenderFallbackData::WebRenderFallbackData(WebRenderLayerManager* aWRManager, nsDisplayItem* aItem)
  : WebRenderImageData(aWRManager, aItem)
  , mInvalid(false)
{
}

WebRenderFallbackData::~WebRenderFallbackData()
{
}

nsAutoPtr<nsDisplayItemGeometry>
WebRenderFallbackData::GetGeometry()
{
  return mGeometry;
}

void
WebRenderFallbackData::SetGeometry(nsAutoPtr<nsDisplayItemGeometry> aGeometry)
{
  mGeometry = aGeometry;
}

WebRenderAnimationData::WebRenderAnimationData(WebRenderLayerManager* aWRManager, nsDisplayItem* aItem)
  : WebRenderUserData(aWRManager, aItem)
  , mAnimationInfo(aWRManager)
{
}

WebRenderAnimationData::~WebRenderAnimationData()
{
  // It may be the case that nsDisplayItem that created this WebRenderUserData
  // gets destroyed without getting a chance to discard the compositor animation
  // id, so we should do it as part of cleanup here.
  uint64_t animationId = mAnimationInfo.GetCompositorAnimationsId();
  // animationId might be 0 if mAnimationInfo never held any active animations.
  if (animationId) {
    mWRManager->AddCompositorAnimationsIdForDiscard(animationId);
  }
}

WebRenderCanvasData::WebRenderCanvasData(WebRenderLayerManager* aWRManager, nsDisplayItem* aItem)
  : WebRenderUserData(aWRManager, aItem)
{
}

WebRenderCanvasData::~WebRenderCanvasData()
{
  ClearCachedResources();
}

void
WebRenderCanvasData::ClearCachedResources()
{
  if (mCanvasRenderer) {
    mCanvasRenderer->ClearCachedResources();
  }
}

void
WebRenderCanvasData::ClearCanvasRenderer()
{
  mCanvasRenderer = nullptr;
}

WebRenderCanvasRendererAsync*
WebRenderCanvasData::GetCanvasRenderer()
{
  return mCanvasRenderer.get();
}

WebRenderCanvasRendererAsync*
WebRenderCanvasData::CreateCanvasRenderer()
{
  mCanvasRenderer = MakeUnique<WebRenderCanvasRendererAsync>(mWRManager);
  return mCanvasRenderer.get();
}

} // namespace layers
} // namespace mozilla
