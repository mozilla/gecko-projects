/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_WEBRENDERLAYER_H
#define GFX_WEBRENDERLAYER_H

#include "Layers.h"
#include "mozilla/webrender/WebRenderTypes.h"
#include "mozilla/webrender/WebRenderAPI.h"

namespace mozilla {
namespace layers {

class ImageClientSingle;
class WebRenderBridgeChild;
class WebRenderLayerManager;

typedef gfx::Matrix4x4Typed<LayerPixel, LayerPixel> BoundsTransformMatrix;

class WebRenderLayer
{
public:
  virtual Layer* GetLayer() = 0;
  virtual void RenderLayer(wr::DisplayListBuilder& aBuilder) = 0;
  virtual Maybe<WrImageMask> RenderMaskLayer(const gfx::Matrix4x4& aTransform)
  {
    MOZ_ASSERT(false);
    return Nothing();
  }

  virtual already_AddRefed<gfx::SourceSurface> GetAsSourceSurface() { return nullptr; }
  static inline WebRenderLayer*
  ToWebRenderLayer(Layer* aLayer)
  {
    return static_cast<WebRenderLayer*>(aLayer->ImplData());
  }

  Maybe<wr::ImageKey> UpdateImageKey(ImageClientSingle* aImageClient,
                                     ImageContainer* aContainer,
                                     Maybe<wr::ImageKey>& aOldKey,
                                     wr::ExternalImageId& aExternalImageId);

  WebRenderLayerManager* WrManager();
  WebRenderBridgeChild* WrBridge();
  WrImageKey GetImageKey();

  LayerRect RelativeToParent(const LayerRect& aRect);
  LayerRect RelativeToParent(const LayoutDeviceRect& aRect);
  LayerPoint GetOffsetToParent();

  LayerRect Bounds();
  LayerRect BoundsForStackingContext();
protected:
  BoundsTransformMatrix BoundsTransform();
  LayerRect ParentBounds();
  Maybe<LayerRect> ClipRect();

  gfx::Rect TransformedVisibleBoundsRelativeToParent();

  void DumpLayerInfo(const char* aLayerType, const LayerRect& aRect);
  Maybe<WrImageMask> BuildWrMaskLayer(bool aUnapplyLayerTransform);
};

} // namespace layers
} // namespace mozilla

#endif /* GFX_WEBRENDERLAYER_H */
