/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "NextFrameSeekTask.h"
#include "MediaDecoderReaderWrapper.h"
#include "mozilla/AbstractThread.h"
#include "mozilla/Assertions.h"
#include "nsPrintfCString.h"

namespace mozilla {

extern LazyLogModule gMediaSampleLog;

#define SAMPLE_LOG(x, ...) MOZ_LOG(gMediaSampleLog, LogLevel::Debug, \
  ("[NextFrameSeekTask] Decoder=%p " x, mDecoderID, ##__VA_ARGS__))

namespace media {

NextFrameSeekTask::NextFrameSeekTask(const void* aDecoderID,
                                     AbstractThread* aThread,
                                     MediaDecoderReaderWrapper* aReader,
                                     const SeekTarget& aTarget,
                                     const MediaInfo& aInfo,
                                     const media::TimeUnit& aDuration,
                                     int64_t aCurrentTime,
                                     MediaQueue<MediaData>& aAudioQueue,
                                     MediaQueue<MediaData>& aVideoQueue)
  : SeekTask(aDecoderID, aThread, aReader, aTarget)
  , mAudioQueue(aAudioQueue)
  , mVideoQueue(aVideoQueue)
  , mCurrentTime(aCurrentTime)
  , mDuration(aDuration)
{
  AssertOwnerThread();
  MOZ_ASSERT(aInfo.HasVideo());
}

NextFrameSeekTask::~NextFrameSeekTask()
{
  AssertOwnerThread();
  MOZ_ASSERT(mIsDiscarded);
}

void
NextFrameSeekTask::Discard()
{
  AssertOwnerThread();

  // Disconnect MDSM.
  RejectIfExist(NS_ERROR_DOM_MEDIA_CANCELED, __func__);

  mIsDiscarded = true;
}

int64_t
NextFrameSeekTask::CalculateNewCurrentTime() const
{
  AssertOwnerThread();

  // The HTMLMediaElement.currentTime should be updated to the seek target
  // which has been updated to the next frame's time.
  return mTarget.GetTime().ToMicroseconds();
}

void
NextFrameSeekTask::HandleAudioDecoded(MediaData* aAudio)
{
  AssertOwnerThread();
  MOZ_ASSERT(aAudio);
  MOZ_ASSERT(!mSeekTaskPromise.IsEmpty(), "Seek shouldn't be finished");

  // The MDSM::mDecodedAudioEndTime will be updated once the whole SeekTask is
  // resolved.

  SAMPLE_LOG("OnAudioDecoded [%lld,%lld]", aAudio->mTime, aAudio->GetEndTime());

  // We accept any audio data here.
  mSeekedAudioData = aAudio;

  MaybeFinishSeek();
}

void
NextFrameSeekTask::HandleVideoDecoded(MediaData* aVideo, TimeStamp aDecodeStart)
{
  AssertOwnerThread();
  MOZ_ASSERT(aVideo);
  MOZ_ASSERT(!mSeekTaskPromise.IsEmpty(), "Seek shouldn't be finished");

  // The MDSM::mDecodedVideoEndTime will be updated once the whole SeekTask is
  // resolved.

  SAMPLE_LOG("OnVideoDecoded [%lld,%lld]", aVideo->mTime, aVideo->GetEndTime());

  if (aVideo->mTime > mCurrentTime) {
    mSeekedVideoData = aVideo;
  }

  if (NeedMoreVideo()) {
    RequestVideoData();
    return;
  }

  MaybeFinishSeek();
}

void
NextFrameSeekTask::HandleNotDecoded(MediaData::Type aType, const MediaResult& aError)
{
  AssertOwnerThread();
  switch (aType) {
  case MediaData::AUDIO_DATA:
  {
    MOZ_ASSERT(!mSeekTaskPromise.IsEmpty(), "Seek shouldn't be finished");

    SAMPLE_LOG("OnAudioNotDecoded (aError=%u)", aError.Code());

    // We don't really handle audio deocde error here. Let MDSM to trigger further
    // audio decoding tasks if it needs to play audio, and MDSM will then receive
    // the decoding state from MediaDecoderReader.

    MaybeFinishSeek();
    break;
  }
  case MediaData::VIDEO_DATA:
  {
    MOZ_ASSERT(!mSeekTaskPromise.IsEmpty(), "Seek shouldn't be finished");

    SAMPLE_LOG("OnVideoNotDecoded (aError=%u)", aError.Code());

    if (aError == NS_ERROR_DOM_MEDIA_END_OF_STREAM) {
      mIsVideoQueueFinished = true;
    }

    // Video seek not finished.
    if (NeedMoreVideo()) {
      switch (aError.Code()) {
        case NS_ERROR_DOM_MEDIA_WAITING_FOR_DATA:
          mReader->WaitForData(MediaData::VIDEO_DATA);
          break;
        case NS_ERROR_DOM_MEDIA_CANCELED:
          RequestVideoData();
          break;
        case NS_ERROR_DOM_MEDIA_END_OF_STREAM:
          MOZ_ASSERT(false, "Shouldn't want more data for ended video.");
          break;
        default:
          // Reject the promise since we can't finish video seek anyway.
          RejectIfExist(aError, __func__);
          break;
      }
      return;
    }

    MaybeFinishSeek();
    break;
  }
  default:
    MOZ_ASSERT_UNREACHABLE("We cannot handle RAW_DATA or NULL_DATA here.");
  }
}

void
NextFrameSeekTask::HandleAudioWaited(MediaData::Type aType)
{
  AssertOwnerThread();

  // We don't make an audio decode request here, instead, let MDSM to
  // trigger further audio decode tasks if MDSM itself needs to play audio.
  MaybeFinishSeek();
}

void
NextFrameSeekTask::HandleVideoWaited(MediaData::Type aType)
{
  AssertOwnerThread();

  if (NeedMoreVideo()) {
    RequestVideoData();
    return;
  }
  MaybeFinishSeek();
}

void
NextFrameSeekTask::HandleNotWaited(const WaitForDataRejectValue& aRejection)
{
  AssertOwnerThread();

  switch(aRejection.mType) {
  case MediaData::AUDIO_DATA:
  {
    // We don't make an audio decode request here, instead, let MDSM to
    // trigger further audio decode tasks if MDSM itself needs to play audio.
    MaybeFinishSeek();
    break;
  }
  case MediaData::VIDEO_DATA:
  {
    if (NeedMoreVideo()) {
      // Reject if we can't finish video seeking.
      RejectIfExist(NS_ERROR_DOM_MEDIA_CANCELED, __func__);
      return;
    }
    MaybeFinishSeek();
    break;
  }
  default:
    MOZ_ASSERT_UNREACHABLE("We cannot handle RAW_DATA or NULL_DATA here.");
  }
}

/*
 * Remove samples from the queue until aCompare() returns false.
 * aCompare A function object with the signature bool(int64_t) which returns
 *          true for samples that should be removed.
 */
template <typename Function> static void
DiscardFrames(MediaQueue<MediaData>& aQueue, const Function& aCompare)
{
  while(aQueue.GetSize() > 0) {
    if (aCompare(aQueue.PeekFront()->mTime)) {
      RefPtr<MediaData> releaseMe = aQueue.PopFront();
      continue;
    }
    break;
  }
}

RefPtr<NextFrameSeekTask::SeekTaskPromise>
NextFrameSeekTask::Seek(const media::TimeUnit&)
{
  AssertOwnerThread();

  auto currentTime = mCurrentTime;
  DiscardFrames(mVideoQueue, [currentTime] (int64_t aSampleTime) {
    return aSampleTime <= currentTime;
  });

  RefPtr<SeekTaskPromise> promise = mSeekTaskPromise.Ensure(__func__);
  if (!IsVideoRequestPending() && NeedMoreVideo()) {
    RequestVideoData();
  }
  MaybeFinishSeek(); // Might resolve mSeekTaskPromise and modify audio queue.
  return promise;
}

void
NextFrameSeekTask::RequestVideoData()
{
  AssertOwnerThread();
  mReader->RequestVideoData(false, media::TimeUnit());
}

bool
NextFrameSeekTask::NeedMoreVideo() const
{
  AssertOwnerThread();
  // Need to request video when we have none and video queue is not finished.
  return mVideoQueue.GetSize() == 0 &&
         !mSeekedVideoData &&
         !mVideoQueue.IsFinished() &&
         !mIsVideoQueueFinished;
}

bool
NextFrameSeekTask::IsVideoRequestPending() const
{
  AssertOwnerThread();
  return mReader->IsRequestingVideoData() || mReader->IsWaitingVideoData();
}

bool
NextFrameSeekTask::IsAudioSeekComplete() const
{
  AssertOwnerThread();
  // Don't finish seek until there are no pending requests. Otherwise, we might
  // lose audio samples for the promise is resolved asynchronously.
  return !mReader->IsRequestingAudioData() && !mReader->IsWaitingAudioData();
}

bool
NextFrameSeekTask::IsVideoSeekComplete() const
{
  AssertOwnerThread();
  // Don't finish seek until there are no pending requests. Otherwise, we might
  // lose video samples for the promise is resolved asynchronously.
  return !IsVideoRequestPending() && !NeedMoreVideo();
}

void
NextFrameSeekTask::MaybeFinishSeek()
{
  AssertOwnerThread();
  if (IsAudioSeekComplete() && IsVideoSeekComplete()) {
    UpdateSeekTargetTime();

    auto time = mTarget.GetTime().ToMicroseconds();
    DiscardFrames(mAudioQueue, [time] (int64_t aSampleTime) {
      return aSampleTime < time;
    });

    Resolve(__func__); // Call to MDSM::SeekCompleted();
  }
}

void
NextFrameSeekTask::UpdateSeekTargetTime()
{
  AssertOwnerThread();

  RefPtr<MediaData> data = mVideoQueue.PeekFront();
  if (data) {
    mTarget.SetTime(TimeUnit::FromMicroseconds(data->mTime));
  } else if (mSeekedVideoData) {
    mTarget.SetTime(TimeUnit::FromMicroseconds(mSeekedVideoData->mTime));
  } else if (mIsVideoQueueFinished || mVideoQueue.AtEndOfStream()) {
    mTarget.SetTime(mDuration);
  } else {
    MOZ_ASSERT(false, "No data!");
  }
}
} // namespace media
} // namespace mozilla
