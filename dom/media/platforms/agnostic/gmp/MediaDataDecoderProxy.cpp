/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MediaDataDecoderProxy.h"
#include "MediaData.h"
#include "mozilla/SyncRunnable.h"

namespace mozilla {

RefPtr<MediaDataDecoder::InitPromise>
MediaDataDecoderProxy::Init()
{
  MOZ_ASSERT(!mIsShutdown);

  RefPtr<MediaDataDecoderProxy> self = this;
  return InvokeAsync(mProxyThread, __func__,
                     [self, this]() { return mProxyDecoder->Init(); });
}

RefPtr<MediaDataDecoder::DecodePromise>
MediaDataDecoderProxy::Decode(MediaRawData* aSample)
{
  MOZ_ASSERT(!IsOnProxyThread());
  MOZ_ASSERT(!mIsShutdown);

  RefPtr<MediaDataDecoderProxy> self = this;
  RefPtr<MediaRawData> sample = aSample;
  return InvokeAsync(mProxyThread, __func__, [self, this, sample]() {
    return mProxyDecoder->Decode(sample);
  });
}

RefPtr<MediaDataDecoder::FlushPromise>
MediaDataDecoderProxy::Flush()
{
  MOZ_ASSERT(!IsOnProxyThread());
  MOZ_ASSERT(!mIsShutdown);

  RefPtr<MediaDataDecoderProxy> self = this;
  return InvokeAsync(mProxyThread, __func__,
                     [self, this]() { return mProxyDecoder->Flush(); });
}

RefPtr<MediaDataDecoder::DecodePromise>
MediaDataDecoderProxy::Drain()
{
  MOZ_ASSERT(!IsOnProxyThread());
  MOZ_ASSERT(!mIsShutdown);

  RefPtr<MediaDataDecoderProxy> self = this;
  return InvokeAsync(mProxyThread, __func__,
                     [self, this]() { return mProxyDecoder->Drain(); });
}

RefPtr<ShutdownPromise>
MediaDataDecoderProxy::Shutdown()
{
  MOZ_ASSERT(!IsOnProxyThread());
  // Note that this *may* be called from the proxy thread also.
  MOZ_ASSERT(!mIsShutdown);
#if defined(DEBUG)
  mIsShutdown = true;
#endif

  RefPtr<MediaDataDecoderProxy> self = this;
  return InvokeAsync(mProxyThread, __func__,
                     [self, this]() { return mProxyDecoder->Shutdown(); });
}

} // namespace mozilla
