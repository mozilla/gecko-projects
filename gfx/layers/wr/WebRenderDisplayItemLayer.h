/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_WEBRENDERDISPLAYITEMLAYER_H
#define GFX_WEBRENDERDISPLAYITEMLAYER_H

#include "Layers.h"
#include "WebRenderLayerManager.h"
#include "mozilla/layers/ImageClient.h"
#include "mozilla/layers/PWebRenderBridgeChild.h"

namespace mozilla {
namespace layers {

class WebRenderDisplayItemLayer : public WebRenderLayer,
                                  public DisplayItemLayer {
public:
  explicit WebRenderDisplayItemLayer(WebRenderLayerManager* aLayerManager)
    : DisplayItemLayer(aLayerManager, static_cast<WebRenderLayer*>(this))
    , mExternalImageId(0)
  {
    MOZ_COUNT_CTOR(WebRenderDisplayItemLayer);
  }

  uint64_t SendImageContainer(ImageContainer* aContainer);

protected:
  virtual ~WebRenderDisplayItemLayer()
  {
    mCommands.Clear();
    MOZ_COUNT_DTOR(WebRenderDisplayItemLayer);
  }

public:
  Layer* GetLayer() override { return this; }
  void RenderLayer() override;

private:
  nsTArray<WebRenderCommand> mCommands;
  nsTArray<WebRenderParentCommand> mParentCommands;
  RefPtr<ImageClient> mImageClient;
  RefPtr<ImageContainer> mImageContainer;
  uint64_t mExternalImageId;

};

} // namespace layers
} // namespace mozilla

#endif // GFX_WEBRENDERDisplayItemLayer_H
