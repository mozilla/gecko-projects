/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef include_ipc_VideoDecoderManagerParent_h
#define include_ipc_VideoDecoderManagerParent_h

#include "mozilla/PVideoDecoderManagerParent.h"

namespace mozilla {

class VideoDecoderManagerThreadHolder;

class VideoDecoderManagerParent final : public PVideoDecoderManagerParent {
  friend class PVideoDecoderManagerParent;

 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(VideoDecoderManagerParent)

  static bool CreateForContent(
      Endpoint<PVideoDecoderManagerParent>&& aEndpoint);

  // Can be called from any thread
  SurfaceDescriptorGPUVideo StoreImage(layers::Image* aImage,
                                       layers::TextureClient* aTexture);

  static void StartupThreads();
  static void ShutdownThreads();

  static void ShutdownVideoBridge();

  bool OnManagerThread();

 protected:
  PVideoDecoderParent* AllocPVideoDecoderParent(
      const VideoInfo& aVideoInfo, const float& aFramerate,
      const CreateDecoderParams::OptionSet& aOptions,
      const layers::TextureFactoryIdentifier& aIdentifier, bool* aSuccess,
      nsCString* aBlacklistedD3D11Driver, nsCString* aBlacklistedD3D9Driver,
      nsCString* aErrorDescription);
  bool DeallocPVideoDecoderParent(PVideoDecoderParent* actor);

  mozilla::ipc::IPCResult RecvReadback(const SurfaceDescriptorGPUVideo& aSD,
                                       SurfaceDescriptor* aResult);
  mozilla::ipc::IPCResult RecvDeallocateSurfaceDescriptorGPUVideo(
      const SurfaceDescriptorGPUVideo& aSD);

  void ActorDestroy(mozilla::ipc::IProtocol::ActorDestroyReason) override;

  void DeallocPVideoDecoderManagerParent() override;

 private:
  explicit VideoDecoderManagerParent(
      VideoDecoderManagerThreadHolder* aThreadHolder);
  ~VideoDecoderManagerParent();

  void Open(Endpoint<PVideoDecoderManagerParent>&& aEndpoint);

  std::map<uint64_t, RefPtr<layers::Image>> mImageMap;
  std::map<uint64_t, RefPtr<layers::TextureClient>> mTextureMap;

  RefPtr<VideoDecoderManagerThreadHolder> mThreadHolder;
};

}  // namespace mozilla

#endif  // include_dom_ipc_VideoDecoderManagerParent_h
