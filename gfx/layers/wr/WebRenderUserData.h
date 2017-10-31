/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_WEBRENDERUSERDATA_H
#define GFX_WEBRENDERUSERDATA_H

#include "BasicLayers.h"                // for BasicLayerManager
#include "mozilla/layers/StackingContextHelper.h"
#include "mozilla/webrender/WebRenderAPI.h"
#include "mozilla/layers/AnimationInfo.h"

class nsDisplayItemGeometry;

namespace mozilla {
namespace wr {
class IpcResourceUpdateQueue;
}

namespace layers {
class CanvasLayer;
class ImageClient;
class ImageContainer;
class WebRenderBridgeChild;
class WebRenderCanvasData;
class WebRenderCanvasRendererAsync;
class WebRenderImageData;
class WebRenderFallbackData;
class WebRenderLayerManager;

class WebRenderUserData
{
public:
  typedef nsTHashtable<nsRefPtrHashKey<WebRenderUserData> > WebRenderUserDataRefTable;

  NS_INLINE_DECL_REFCOUNTING(WebRenderUserData)

  WebRenderUserData(WebRenderLayerManager* aWRManager, nsDisplayItem* aItem);

  virtual WebRenderImageData* AsImageData() { return nullptr; }
  virtual WebRenderFallbackData* AsFallbackData() { return nullptr; }
  virtual WebRenderCanvasData* AsCanvasData() { return nullptr; }

  enum class UserDataType {
    eImage,
    eFallback,
    eAnimation,
    eCanvas,
  };

  virtual UserDataType GetType() = 0;
  bool IsDataValid(WebRenderLayerManager* aManager);
  bool IsUsed() { return mUsed; }
  void SetUsed(bool aUsed) { mUsed = aUsed; }
  nsIFrame* GetFrame() { return mFrame; }
  uint32_t GetDisplayItemKey() { return mDisplayItemKey; }
  void RemoveFromTable();
  virtual void ClearCachedResources() {};
protected:
  virtual ~WebRenderUserData();

  WebRenderBridgeChild* WrBridge() const;

  RefPtr<WebRenderLayerManager> mWRManager;
  nsIFrame* mFrame;
  uint32_t mDisplayItemKey;
  WebRenderUserDataRefTable* mTable;
  bool mUsed;
};

class WebRenderImageData : public WebRenderUserData
{
public:
  explicit WebRenderImageData(WebRenderLayerManager* aWRManager, nsDisplayItem* aItem);
  virtual ~WebRenderImageData();

  virtual WebRenderImageData* AsImageData() override { return this; }
  virtual UserDataType GetType() override { return UserDataType::eImage; }
  static UserDataType Type() { return UserDataType::eImage; }
  Maybe<wr::ImageKey> GetKey() { return mKey; }
  void SetKey(const wr::ImageKey& aKey) { mKey = Some(aKey); }
  already_AddRefed<ImageClient> GetImageClient();

  Maybe<wr::ImageKey> UpdateImageKey(ImageContainer* aContainer,
                                     wr::IpcResourceUpdateQueue& aResources,
                                     bool aForceUpdate = false);

  void CreateAsyncImageWebRenderCommands(mozilla::wr::DisplayListBuilder& aBuilder,
                                         ImageContainer* aContainer,
                                         const StackingContextHelper& aSc,
                                         const LayoutDeviceRect& aBounds,
                                         const LayoutDeviceRect& aSCBounds,
                                         const gfx::Matrix4x4& aSCTransform,
                                         const gfx::MaybeIntSize& aScaleToSize,
                                         const wr::ImageRendering& aFilter,
                                         const wr::MixBlendMode& aMixBlendMode,
                                         bool aIsBackfaceVisible);

  void CreateImageClientIfNeeded();
  void ClearCachedResources() override;
protected:
  void CreateExternalImageIfNeeded();

  wr::MaybeExternalImageId mExternalImageId;
  Maybe<wr::ImageKey> mKey;
  RefPtr<ImageClient> mImageClient;
  Maybe<wr::PipelineId> mPipelineId;
  RefPtr<ImageContainer> mContainer;
  uint32_t mGeneration;
};

class WebRenderFallbackData : public WebRenderImageData
{
public:
  explicit WebRenderFallbackData(WebRenderLayerManager* aWRManager, nsDisplayItem* aItem);
  virtual ~WebRenderFallbackData();

  virtual WebRenderFallbackData* AsFallbackData() override { return this; }
  virtual UserDataType GetType() override { return UserDataType::eFallback; }
  static UserDataType Type() { return UserDataType::eFallback; }
  nsAutoPtr<nsDisplayItemGeometry> GetGeometry();
  void SetGeometry(nsAutoPtr<nsDisplayItemGeometry> aGeometry);
  nsRect GetBounds() { return mBounds; }
  void SetBounds(const nsRect& aRect) { mBounds = aRect; }
  void SetInvalid(bool aInvalid) { mInvalid = aInvalid; }
  void SetScale(gfx::Size aScale) { mScale = aScale; }
  gfx::Size GetScale() { return mScale; }
  bool IsInvalid() { return mInvalid; }

  RefPtr<BasicLayerManager> mBasicLayerManager;
protected:
  nsAutoPtr<nsDisplayItemGeometry> mGeometry;
  nsRect mBounds;
  bool mInvalid;
  gfx::Size mScale;
};

class WebRenderAnimationData : public WebRenderUserData
{
public:
  explicit WebRenderAnimationData(WebRenderLayerManager* aWRManager, nsDisplayItem* aItem);
  virtual ~WebRenderAnimationData();

  virtual UserDataType GetType() override { return UserDataType::eAnimation; }
  static UserDataType Type() { return UserDataType::eAnimation; }
  AnimationInfo& GetAnimationInfo() { return mAnimationInfo; }

protected:
  AnimationInfo mAnimationInfo;
};

class WebRenderCanvasData : public WebRenderUserData
{
public:
  explicit WebRenderCanvasData(WebRenderLayerManager* aWRManager, nsDisplayItem* aItem);
  virtual ~WebRenderCanvasData();

  virtual WebRenderCanvasData* AsCanvasData() override { return this; }
  virtual UserDataType GetType() override { return UserDataType::eCanvas; }
  static UserDataType Type() { return UserDataType::eCanvas; }

  void ClearCanvasRenderer();
  WebRenderCanvasRendererAsync* GetCanvasRenderer();
  WebRenderCanvasRendererAsync* CreateCanvasRenderer();
  void ClearCachedResources() override;
protected:
  UniquePtr<WebRenderCanvasRendererAsync> mCanvasRenderer;
};

} // namespace layers
} // namespace mozilla

#endif /* GFX_WEBRENDERUSERDATA_H */
