/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_PAINTEDLAYERMLGPU_H
#define MOZILLA_GFX_PAINTEDLAYERMLGPU_H

#include "LayerManagerMLGPU.h"
#include "mozilla/layers/ContentHost.h"
#include "MLGDeviceTypes.h"
#include "nsRegionFwd.h"
#include <functional>

namespace mozilla {
namespace layers {

class PaintedLayerMLGPU final
  : public PaintedLayer,
    public LayerMLGPU
{
public:
  explicit PaintedLayerMLGPU(LayerManagerMLGPU* aManager);
  ~PaintedLayerMLGPU() override;

  // Layer
  HostLayer* AsHostLayer() override { return this; }
  PaintedLayerMLGPU* AsPaintedLayerMLGPU() override { return this; }
  virtual Layer* GetLayer() override { return this; }
  bool SetCompositableHost(CompositableHost*) override;
  CompositableHost* GetCompositableHost() override;
  void Disconnect() override;
  bool IsContentOpaque() override;

  // PaintedLayer
  void InvalidateRegion(const nsIntRegion& aRegion) override { 
    MOZ_CRASH("PaintedLayerMLGPU can't fill invalidated regions");
  }

  bool HasComponentAlpha() const {
    return !!mTextureOnWhite;
  }
  TextureSource* GetTexture() const {
    return mTexture;
  }
  TextureSource* GetTextureOnWhite() const {
    MOZ_ASSERT(HasComponentAlpha());
    return mTextureOnWhite;
  }
  ContentHostTexture* GetContentHost() const {
    return mHost;
  }
  SamplerMode GetSamplerMode() {
    // Note that when resamping, we must break the texture coordinates into
    // no-repeat rects. When we have simple integer translations we can
    // simply wrap around the edge of the buffer texture.
    return MayResample()
           ? SamplerMode::LinearClamp
           : SamplerMode::LinearRepeat;
  }

  // This can return a different region than GetShadowVisibleRegion or
  // GetLocalVisibleRegion, since we make sure to clamp it to the
  // texture size and account for resampling.
  nsIntRegion GetRenderRegion();

  MOZ_LAYER_DECL_NAME("PaintedLayerMLGPU", TYPE_PAINTED)

  void CleanupCachedResources();

protected:
  void PrintInfo(std::stringstream& aStream, const char* aPrefix) override;
  bool OnPrepareToRender(FrameBuilder* aBuilder) override;

  void CleanupResources();

private:
  RefPtr<ContentHostTexture> mHost;
  RefPtr<TextureSource> mTexture;
  RefPtr<TextureSource> mTextureOnWhite;
  gfx::IntRegion mLocalDrawRegion;
  gfx::IntRegion mTextureRegion;
};

} // namespace layers
} // namespace mozilla

#endif
