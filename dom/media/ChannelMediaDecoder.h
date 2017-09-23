/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef ChannelMediaDecoder_h_
#define ChannelMediaDecoder_h_

#include "MediaDecoder.h"
#include "MediaResourceCallback.h"

class nsIChannel;
class nsIStreamListener;

namespace mozilla {

class BaseMediaResource;

class ChannelMediaDecoder : public MediaDecoder
{
  // Used to register with MediaResource to receive notifications which will
  // be forwarded to MediaDecoder.
  class ResourceCallback : public MediaResourceCallback
  {
    // Throttle calls to MediaDecoder::NotifyDataArrived()
    // to be at most once per 500ms.
    static const uint32_t sDelay = 500;

  public:
    explicit ResourceCallback(AbstractThread* aMainThread);
    // Start to receive notifications from ResourceCallback.
    void Connect(ChannelMediaDecoder* aDecoder);
    // Called upon shutdown to stop receiving notifications.
    void Disconnect();

  private:
    /* MediaResourceCallback functions */
    AbstractThread* AbstractMainThread() const override;
    MediaDecoderOwner* GetMediaOwner() const override;
    void NotifyNetworkError() override;
    void NotifyDataArrived() override;
    void NotifyDataEnded(nsresult aStatus) override;
    void NotifyPrincipalChanged() override;
    void NotifySuspendedStatusChanged(bool aSuspendedByCache) override;
    void NotifyBytesConsumed(int64_t aBytes, int64_t aOffset) override;

    static void TimerCallback(nsITimer* aTimer, void* aClosure);

    // The decoder to send notifications. Main-thread only.
    ChannelMediaDecoder* mDecoder = nullptr;
    nsCOMPtr<nsITimer> mTimer;
    bool mTimerArmed = false;
    const RefPtr<AbstractThread> mAbstractMainThread;
  };

protected:
  void OnPlaybackEvent(MediaEventType aEvent) override;
  void DurationChanged() override;
  void DownloadProgressed() override;
  void MetadataLoaded(UniquePtr<MediaInfo> aInfo,
                      UniquePtr<MetadataTags> aTags,
                      MediaDecoderEventVisibility aEventVisibility) override;

  RefPtr<ResourceCallback> mResourceCallback;
  RefPtr<BaseMediaResource> mResource;

public:
  explicit ChannelMediaDecoder(MediaDecoderInit& aInit);

  void Shutdown() override;

  bool CanClone();

  // Create a new decoder of the same type as this one.
  already_AddRefed<ChannelMediaDecoder> Clone(MediaDecoderInit& aInit);

  nsresult Load(nsIChannel* aChannel,
                bool aIsPrivateBrowsing,
                nsIStreamListener** aStreamListener);

  void AddSizeOfResources(ResourceSizes* aSizes) override;
  already_AddRefed<nsIPrincipal> GetCurrentPrincipal() override;
  bool IsTransportSeekable() override;
  void SetLoadInBackground(bool aLoadInBackground) override;
  void Suspend() override;
  void Resume() override;

private:
  void PinForSeek() override;
  void UnpinForSeek() override;

  // Create a new state machine to run this decoder.
  MediaDecoderStateMachine* CreateStateMachine();

  nsresult Load(BaseMediaResource* aOriginal);

  // Called by MediaResource when the download has ended.
  // Called on the main thread only. aStatus is the result from OnStopRequest.
  void NotifyDownloadEnded(nsresult aStatus);

  // Called by the MediaResource to keep track of the number of bytes read
  // from the resource. Called on the main by an event runner dispatched
  // by the MediaResource read functions.
  void NotifyBytesConsumed(int64_t aBytes, int64_t aOffset);

  void SeekingChanged();

  bool CanPlayThroughImpl() override final;

  bool IsLiveStream() override final;

  // The actual playback rate computation.
  void ComputePlaybackRate();

  // Something has changed that could affect the computed playback rate,
  // so recompute it.
  void UpdatePlaybackRate();

  // Return statistics. This is used for progress events and other things.
  // This can be called from any thread. It's only a snapshot of the
  // current state, since other threads might be changing the state
  // at any time.
  MediaStatistics GetStatistics();

  bool ShouldThrottleDownload();

  WatchManager<ChannelMediaDecoder> mWatchManager;

  // True when seeking or otherwise moving the play position around in
  // such a manner that progress event data is inaccurate. This is set
  // during seek and duration operations to prevent the progress indicator
  // from jumping around. Read/Write on the main thread only.
  bool mIgnoreProgressData = false;

  // Data needed to estimate playback data rate. The timeline used for
  // this estimate is "decode time" (where the "current time" is the
  // time of the last decoded video frame).
  MediaChannelStatistics mPlaybackStatistics;

  // Estimate of the current playback rate (bytes/second).
  double mPlaybackBytesPerSecond = 0;

  // True if mPlaybackBytesPerSecond is a reliable estimate.
  bool mPlaybackRateReliable = true;

  // True when our media stream has been pinned. We pin the stream
  // while seeking.
  bool mPinnedForSeek = false;
};

} // namespace mozilla

#endif // ChannelMediaDecoder_h_
