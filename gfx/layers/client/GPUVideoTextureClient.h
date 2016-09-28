/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_GPUVIDEOTEXTURECLIENT_H
#define MOZILLA_GFX_GPUVIDEOTEXTURECLIENT_H

#include "mozilla/layers/TextureClient.h"

namespace mozilla {
namespace layers {

class GPUVideoTextureData : public TextureData
{
public:
  GPUVideoTextureData(const SurfaceDescriptorGPUVideo& aSD,
                      const gfx::IntSize& aSize)
    : mSD(aSD)
    , mSize(aSize)
  {}

  ~GPUVideoTextureData() {};

  virtual void FillInfo(TextureData::Info& aInfo) const override;

  virtual bool Lock(OpenMode, FenceHandle*) override { return true; };

  virtual void Unlock() override {};

  virtual bool Serialize(SurfaceDescriptor& aOutDescriptor) override;

  virtual void Deallocate(LayersIPCChannel* aAllocator) override;

  virtual void Forget(LayersIPCChannel* aAllocator) override;

protected:
  SurfaceDescriptorGPUVideo mSD;
  gfx::IntSize mSize;
};

} // namespace layers
} // namespace mozilla

#endif // MOZILLA_GFX_GPUVIDEOTEXTURECLIENT_H
