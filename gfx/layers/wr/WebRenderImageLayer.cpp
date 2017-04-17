/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WebRenderImageLayer.h"

#include "gfxPrefs.h"
#include "LayersLogging.h"
#include "mozilla/layers/ImageClient.h"
#include "mozilla/layers/TextureClientRecycleAllocator.h"
#include "mozilla/layers/TextureWrapperImage.h"
#include "mozilla/layers/WebRenderBridgeChild.h"
#include "mozilla/webrender/WebRenderTypes.h"

namespace mozilla {
namespace layers {

using namespace gfx;

WebRenderImageLayer::WebRenderImageLayer(WebRenderLayerManager* aLayerManager)
  : ImageLayer(aLayerManager, static_cast<WebRenderLayer*>(this))
  , mExternalImageId(0)
  , mImageClientTypeContainer(CompositableType::UNKNOWN)
{
  MOZ_COUNT_CTOR(WebRenderImageLayer);
}

WebRenderImageLayer::~WebRenderImageLayer()
{
  MOZ_COUNT_DTOR(WebRenderImageLayer);
  if (mExternalImageId) {
    WrBridge()->DeallocExternalImageId(mExternalImageId);
  }
}

CompositableType
WebRenderImageLayer::GetImageClientType()
{
  if (mImageClientTypeContainer != CompositableType::UNKNOWN) {
    return mImageClientTypeContainer;
  }

  if (mContainer->IsAsync()) {
    mImageClientTypeContainer = CompositableType::IMAGE_BRIDGE;
    return mImageClientTypeContainer;
  }

  AutoLockImage autoLock(mContainer);

  mImageClientTypeContainer = autoLock.HasImage()
    ? CompositableType::IMAGE : CompositableType::UNKNOWN;
  return mImageClientTypeContainer;
}

already_AddRefed<gfx::SourceSurface>
WebRenderImageLayer::GetAsSourceSurface()
{
  if (!mContainer) {
    return nullptr;
  }
  AutoLockImage autoLock(mContainer);
  Image *image = autoLock.GetImage();
  if (!image) {
    return nullptr;
  }
  RefPtr<gfx::SourceSurface> surface = image->GetAsSourceSurface();
  if (!surface || !surface->IsValid()) {
    return nullptr;
  }
  return surface.forget();
}

void
WebRenderImageLayer::ClearCachedResources()
{
  if (mImageClient) {
    mImageClient->ClearCachedResources();
  }
}

void
WebRenderImageLayer::RenderLayer(wr::DisplayListBuilder& aBuilder)
{
  if (!mContainer) {
     return;
  }

  CompositableType type = GetImageClientType();
  if (type == CompositableType::UNKNOWN) {
    return;
  }

  MOZ_ASSERT(GetImageClientType() != CompositableType::UNKNOWN);

  if (GetImageClientType() == CompositableType::IMAGE && !mImageClient) {
    mImageClient = ImageClient::CreateImageClient(CompositableType::IMAGE,
                                                  WrBridge(),
                                                  TextureFlags::DEFAULT);
    if (!mImageClient) {
      return;
    }
    mImageClient->Connect();
  }

  if (!mExternalImageId) {
    if (GetImageClientType() == CompositableType::IMAGE_BRIDGE) {
      MOZ_ASSERT(!mImageClient);
      mExternalImageId = WrBridge()->AllocExternalImageId(mContainer->GetAsyncContainerHandle());
    } else {
      // Handle CompositableType::IMAGE case
      MOZ_ASSERT(mImageClient);
      mExternalImageId = WrBridge()->AllocExternalImageIdForCompositable(mImageClient);
    }
  }
  MOZ_ASSERT(mExternalImageId);

  // XXX Not good for async ImageContainer case.
  AutoLockImage autoLock(mContainer);
  Image* image = autoLock.GetImage();
  if (!image) {
    return;
  }
  gfx::IntSize size = image->GetSize();

  if (mImageClient && !mImageClient->UpdateImage(mContainer, /* unused */0)) {
    return;
  }

  gfx::Matrix4x4 transform = GetTransform();
  gfx::Rect relBounds = GetWrRelBounds();

  gfx::Rect rect = gfx::Rect(0, 0, size.width, size.height);
  if (mScaleMode != ScaleMode::SCALE_NONE) {
    NS_ASSERTION(mScaleMode == ScaleMode::STRETCH,
                 "No other scalemodes than stretch and none supported yet.");
    rect = gfx::Rect(0, 0, mScaleToSize.width, mScaleToSize.height);
  }
  rect = RelativeToVisible(rect);

  gfx::Rect clipRect = GetWrClipRect(rect);
  Maybe<WrImageMask> mask = BuildWrMaskLayer(true);
  WrClipRegion clip = aBuilder.BuildClipRegion(wr::ToWrRect(clipRect), mask.ptrOr(nullptr));

  wr::ImageRendering filter = wr::ToImageRendering(mSamplingFilter);
  wr::MixBlendMode mixBlendMode = wr::ToWrMixBlendMode(GetMixBlendMode());

  DumpLayerInfo("Image Layer", rect);
  if (gfxPrefs::LayersDump()) {
    printf_stderr("ImageLayer %p texture-filter=%s \n",
                  GetLayer(),
                  Stringify(filter).c_str());
  }

  WrImageKey key;
  key.mNamespace = WrBridge()->GetNamespace();
  key.mHandle = WrBridge()->GetNextResourceId();
  WrBridge()->AddWebRenderParentCommand(OpAddExternalImage(mExternalImageId, key));
  Manager()->AddImageKeyForDiscard(key);

  aBuilder.PushStackingContext(wr::ToWrRect(relBounds),
                            1.0f,
                            //GetAnimations(),
                            transform,
                            mixBlendMode);
  aBuilder.PushImage(wr::ToWrRect(rect), clip, filter, key);
  aBuilder.PopStackingContext();
}

Maybe<WrImageMask>
WebRenderImageLayer::RenderMaskLayer(const gfx::Matrix4x4& aTransform)
{
  if (!mContainer) {
     return Nothing();
  }

  CompositableType type = GetImageClientType();
  if (type == CompositableType::UNKNOWN) {
    return Nothing();
  }

  MOZ_ASSERT(GetImageClientType() == CompositableType::IMAGE);
  if (GetImageClientType() != CompositableType::IMAGE) {
    return Nothing();
  }

  if (!mImageClient) {
    mImageClient = ImageClient::CreateImageClient(CompositableType::IMAGE,
                                                  WrBridge(),
                                                  TextureFlags::DEFAULT);
    if (!mImageClient) {
      return Nothing();
    }
    mImageClient->Connect();
  }

  if (!mExternalImageId) {
    mExternalImageId = WrBridge()->AllocExternalImageIdForCompositable(mImageClient);
  }
  MOZ_ASSERT(mExternalImageId);

  AutoLockImage autoLock(mContainer);
  Image* image = autoLock.GetImage();
  if (!image) {
    return Nothing();
  }
  if (!mImageClient->UpdateImage(mContainer, /* unused */0)) {
    return Nothing();
  }

  WrImageKey key;
  key.mNamespace = WrBridge()->GetNamespace();
  key.mHandle = WrBridge()->GetNextResourceId();
  WrBridge()->AddWebRenderParentCommand(OpAddExternalImage(mExternalImageId, key));
  Manager()->AddImageKeyForDiscard(key);

  gfx::IntSize size = image->GetSize();
  WrImageMask imageMask;
  imageMask.image = key;
  Rect maskRect = aTransform.TransformBounds(Rect(0, 0, size.width, size.height));
  imageMask.rect = wr::ToWrRect(maskRect);
  imageMask.repeat = false;
  return Some(imageMask);
}

} // namespace layers
} // namespace mozilla
