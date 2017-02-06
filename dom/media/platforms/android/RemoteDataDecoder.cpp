/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "AndroidDecoderModule.h"
#include "AndroidBridge.h"
#include "AndroidSurfaceTexture.h"
#include "FennecJNINatives.h"
#include "GLImages.h"

#include "MediaData.h"
#include "MediaInfo.h"
#include "VideoUtils.h"
#include "VPXDecoder.h"

#include "nsThreadUtils.h"
#include "nsPromiseFlatString.h"
#include "nsIGfxInfo.h"

#include "prlog.h"

#include <deque>
#include <jni.h>


#undef LOG
#define LOG(arg, ...) MOZ_LOG(sAndroidDecoderModuleLog, \
    mozilla::LogLevel::Debug, ("RemoteDataDecoder(%p)::%s: " arg, \
      this, __func__, ##__VA_ARGS__))

using namespace mozilla;
using namespace mozilla::gl;
using namespace mozilla::java;
using namespace mozilla::java::sdk;
using media::TimeUnit;

namespace mozilla {

class JavaCallbacksSupport
  : public CodecProxy::NativeCallbacks::Natives<JavaCallbacksSupport>
{
public:
  typedef CodecProxy::NativeCallbacks::Natives<JavaCallbacksSupport> Base;
  using Base::AttachNative;

  JavaCallbacksSupport() : mCanceled(false) { }

  virtual ~JavaCallbacksSupport() { }

  virtual void HandleInputExhausted() = 0;

  void OnInputExhausted()
  {
    if (!mCanceled) {
      HandleInputExhausted();
    }
  }

  virtual void HandleOutput(Sample::Param aSample) = 0;

  void OnOutput(jni::Object::Param aSample)
  {
    if (!mCanceled) {
      HandleOutput(Sample::Ref::From(aSample));
    }
  }

  virtual void HandleOutputFormatChanged(MediaFormat::Param aFormat) { };

  void OnOutputFormatChanged(jni::Object::Param aFormat)
  {
    if (!mCanceled) {
      HandleOutputFormatChanged(MediaFormat::Ref::From(aFormat));
    }
  }

  virtual void HandleError(const MediaResult& aError) = 0;

  void OnError(bool aIsFatal)
  {
    if (!mCanceled) {
      HandleError(
        aIsFatal ? MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR, __func__)
                 : MediaResult(NS_ERROR_DOM_MEDIA_DECODE_ERR, __func__));
    }
  }

  void DisposeNative()
  {
    // TODO
  }

  void Cancel()
  {
    mCanceled = true;
  }

private:
  Atomic<bool> mCanceled;
};

class RemoteVideoDecoder : public RemoteDataDecoder
{
public:
  // Hold an output buffer and render it to the surface when the frame is sent
  // to compositor, or release it if not presented.
  class RenderOrReleaseOutput : public VideoData::Listener
  {
  public:
    RenderOrReleaseOutput(java::CodecProxy::Param aCodec,
                          java::Sample::Param aSample)
      : mCodec(aCodec)
      , mSample(aSample)
    {
    }

    ~RenderOrReleaseOutput()
    {
      ReleaseOutput(false);
    }

    void OnSentToCompositor() override
    {
      ReleaseOutput(true);
      mCodec = nullptr;
      mSample = nullptr;
    }

  private:
    void ReleaseOutput(bool aToRender)
    {
      if (mCodec && mSample) {
        mCodec->ReleaseOutput(mSample, aToRender);
      }
    }

    java::CodecProxy::GlobalRef mCodec;
    java::Sample::GlobalRef mSample;
  };

  class CallbacksSupport final : public JavaCallbacksSupport
  {
  public:
    CallbacksSupport(RemoteVideoDecoder* aDecoder) : mDecoder(aDecoder) { }

    void HandleInputExhausted() override
    {
      mDecoder->InputExhausted();
    }

    void HandleOutput(Sample::Param aSample) override
    {
      Maybe<int64_t> durationUs = mDecoder->mInputDurations.Get();
      if (!durationUs) {
        return;
      }

      BufferInfo::LocalRef info = aSample->Info();

      int32_t flags;
      bool ok = NS_SUCCEEDED(info->Flags(&flags));

      int32_t offset;
      ok &= NS_SUCCEEDED(info->Offset(&offset));

      int64_t presentationTimeUs;
      ok &= NS_SUCCEEDED(info->PresentationTimeUs(&presentationTimeUs));

      int32_t size;
      ok &= NS_SUCCEEDED(info->Size(&size));

      if (!ok) {
        HandleError(MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                                RESULT_DETAIL("VideoCallBack::HandleOutput")));
        return;
      }

      if (size > 0) {
        RefPtr<layers::Image> img = new SurfaceTextureImage(
          mDecoder->mSurfaceTexture.get(), mDecoder->mConfig.mDisplay,
          gl::OriginPos::BottomLeft);

        RefPtr<VideoData> v = VideoData::CreateFromImage(
          mDecoder->mConfig, offset, presentationTimeUs, durationUs.value(),
          img, !!(flags & MediaCodec::BUFFER_FLAG_SYNC_FRAME),
          presentationTimeUs,
          gfx::IntRect(0, 0, mDecoder->mConfig.mDisplay.width,
                       mDecoder->mConfig.mDisplay.height));

        UniquePtr<VideoData::Listener> listener(
          new RenderOrReleaseOutput(mDecoder->mJavaDecoder, aSample));
        v->SetListener(Move(listener));

        mDecoder->Output(v);
      }

      if ((flags & MediaCodec::BUFFER_FLAG_END_OF_STREAM) != 0) {
        mDecoder->DrainComplete();
      }
    }

    void HandleError(const MediaResult& aError) override
    {
      mDecoder->Error(aError);
    }

    friend class RemoteDataDecoder;

  private:
    RemoteVideoDecoder* mDecoder;
  };

  RemoteVideoDecoder(const VideoInfo& aConfig,
                     MediaFormat::Param aFormat,
                     layers::ImageContainer* aImageContainer,
                     const nsString& aDrmStubId, TaskQueue* aTaskQueue)
    : RemoteDataDecoder(MediaData::Type::VIDEO_DATA, aConfig.mMimeType,
                        aFormat, aDrmStubId, aTaskQueue)
    , mImageContainer(aImageContainer)
    , mConfig(aConfig)
  {
  }

  RefPtr<InitPromise> Init() override
  {
    mSurfaceTexture = AndroidSurfaceTexture::Create();
    if (!mSurfaceTexture) {
      NS_WARNING("Failed to create SurfaceTexture for video decode\n");
      return InitPromise::CreateAndReject(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                                          __func__);
    }

    if (!jni::IsFennec()) {
      NS_WARNING("Remote decoding not supported in non-Fennec environment\n");
      return InitPromise::CreateAndReject(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                                          __func__);
    }

    // Register native methods.
    JavaCallbacksSupport::Init();

    mJavaCallbacks = CodecProxy::NativeCallbacks::New();
    JavaCallbacksSupport::AttachNative(
      mJavaCallbacks, mozilla::MakeUnique<CallbacksSupport>(this));

    mJavaDecoder = CodecProxy::Create(mFormat,
                                      mSurfaceTexture->JavaSurface(),
                                      mJavaCallbacks,
                                      mDrmStubId);
    if (mJavaDecoder == nullptr) {
      return InitPromise::CreateAndReject(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                                          __func__);
    }
    mIsCodecSupportAdaptivePlayback =
      mJavaDecoder->IsAdaptivePlaybackSupported();

    return InitPromise::CreateAndResolve(TrackInfo::kVideoTrack, __func__);
  }

  RefPtr<MediaDataDecoder::FlushPromise> Flush() override
  {
    mInputDurations.Clear();
    return RemoteDataDecoder::Flush();
  }

  RefPtr<MediaDataDecoder::DecodePromise> Drain() override
  {
    mInputDurations.Put(0);
    return RemoteDataDecoder::Drain();
  }

  RefPtr<MediaDataDecoder::DecodePromise> Decode(MediaRawData* aSample) override
  {
    mInputDurations.Put(aSample->mDuration);
    return RemoteDataDecoder::Decode(aSample);
  }

  bool SupportDecoderRecycling() const override
  {
    return mIsCodecSupportAdaptivePlayback;
  }

private:
  class DurationQueue
  {
  public:

    DurationQueue() : mMutex("Video duration queue") { }

    void Clear()
    {
      MutexAutoLock lock(mMutex);
      mValues.clear();
    }

    void Put(int64_t aDurationUs)
    {
      MutexAutoLock lock(mMutex);
      mValues.emplace_back(aDurationUs);
    }

    Maybe<int64_t> Get()
    {
      MutexAutoLock lock(mMutex);
      if (mValues.empty()) {
        return Nothing();
      }

      auto value = Some(mValues.front());
      mValues.pop_front();

      return value;
    }

  private:
    Mutex mMutex; // To protect mValues.
    std::deque<int64_t> mValues;
  };

  layers::ImageContainer* mImageContainer;
  const VideoInfo mConfig;
  RefPtr<AndroidSurfaceTexture> mSurfaceTexture;
  DurationQueue mInputDurations;
  bool mIsCodecSupportAdaptivePlayback = false;
};

class RemoteAudioDecoder : public RemoteDataDecoder
{
public:
  RemoteAudioDecoder(const AudioInfo& aConfig,
                     MediaFormat::Param aFormat,
                     const nsString& aDrmStubId, TaskQueue* aTaskQueue)
    : RemoteDataDecoder(MediaData::Type::AUDIO_DATA, aConfig.mMimeType,
                        aFormat, aDrmStubId, aTaskQueue)
    , mConfig(aConfig)
  {
    JNIEnv* const env = jni::GetEnvForThread();

    bool formatHasCSD = false;
    NS_ENSURE_SUCCESS_VOID(
      aFormat->ContainsKey(NS_LITERAL_STRING("csd-0"), &formatHasCSD));

    if (!formatHasCSD && aConfig.mCodecSpecificConfig->Length() >= 2) {
      jni::ByteBuffer::LocalRef buffer(env);
      buffer = jni::ByteBuffer::New(aConfig.mCodecSpecificConfig->Elements(),
          aConfig.mCodecSpecificConfig->Length());
      NS_ENSURE_SUCCESS_VOID(
        aFormat->SetByteBuffer(NS_LITERAL_STRING("csd-0"), buffer));
    }
  }

  RefPtr<InitPromise> Init() override
  {
    // Register native methods.
    JavaCallbacksSupport::Init();

    mJavaCallbacks = CodecProxy::NativeCallbacks::New();
    JavaCallbacksSupport::AttachNative(
      mJavaCallbacks, mozilla::MakeUnique<CallbacksSupport>(this));

    mJavaDecoder =
      CodecProxy::Create(mFormat, nullptr, mJavaCallbacks, mDrmStubId);
    if (mJavaDecoder == nullptr) {
      return InitPromise::CreateAndReject(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                                          __func__);
    }

    return InitPromise::CreateAndResolve(TrackInfo::kAudioTrack, __func__);
  }

private:
  class CallbacksSupport final : public JavaCallbacksSupport
  {
  public:
    CallbacksSupport(RemoteAudioDecoder* aDecoder) : mDecoder(aDecoder) { }

    void HandleInputExhausted() override
    {
      mDecoder->InputExhausted();
    }

    void HandleOutput(Sample::Param aSample) override
    {
      BufferInfo::LocalRef info = aSample->Info();

      int32_t flags;
      bool ok = NS_SUCCEEDED(info->Flags(&flags));

      int32_t offset;
      ok &= NS_SUCCEEDED(info->Offset(&offset));

      int64_t presentationTimeUs;
      ok &= NS_SUCCEEDED(info->PresentationTimeUs(&presentationTimeUs));

      int32_t size;
      ok &= NS_SUCCEEDED(info->Size(&size));

      if (!ok) {
        HandleError(MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                                RESULT_DETAIL("AudioCallBack::HandleOutput")));
        return;
      }

      if (size > 0) {
#ifdef MOZ_SAMPLE_TYPE_S16
        const int32_t numSamples = size / 2;
#else
#error We only support 16-bit integer PCM
#endif

        const int32_t numFrames = numSamples / mOutputChannels;
        AlignedAudioBuffer audio(numSamples);
        if (!audio) {
          mDecoder->Error(MediaResult(NS_ERROR_OUT_OF_MEMORY, __func__));
          return;
        }

        jni::ByteBuffer::LocalRef dest =
          jni::ByteBuffer::New(audio.get(), size);
        aSample->WriteToByteBuffer(dest);

        RefPtr<AudioData> data = new AudioData(
          0, presentationTimeUs,
          FramesToUsecs(numFrames, mOutputSampleRate).value(), numFrames,
          Move(audio), mOutputChannels, mOutputSampleRate);

        mDecoder->Output(data);
      }

      if ((flags & MediaCodec::BUFFER_FLAG_END_OF_STREAM) != 0) {
        mDecoder->DrainComplete();
      }
    }

    void HandleOutputFormatChanged(MediaFormat::Param aFormat) override
    {
      aFormat->GetInteger(NS_LITERAL_STRING("channel-count"), &mOutputChannels);
      AudioConfig::ChannelLayout layout(mOutputChannels);
      if (!layout.IsValid()) {
        mDecoder->Error(MediaResult(
          NS_ERROR_DOM_MEDIA_FATAL_ERR,
          RESULT_DETAIL("Invalid channel layout:%d", mOutputChannels)));
        return;
      }
      aFormat->GetInteger(NS_LITERAL_STRING("sample-rate"), &mOutputSampleRate);
      LOG("Audio output format changed: channels:%d sample rate:%d",
          mOutputChannels, mOutputSampleRate);
    }

    void HandleError(const MediaResult& aError) override
    {
      mDecoder->Error(aError);
    }

  private:
    RemoteAudioDecoder* mDecoder;
    int32_t mOutputChannels;
    int32_t mOutputSampleRate;
  };

  const AudioInfo mConfig;
};

already_AddRefed<MediaDataDecoder>
RemoteDataDecoder::CreateAudioDecoder(const AudioInfo& aConfig,
                                      MediaFormat::Param aFormat,
                                      const nsString& aDrmStubId,
                                      CDMProxy* aProxy, TaskQueue* aTaskQueue)
{
  RefPtr<MediaDataDecoder> decoder;
  if (!aProxy) {
    decoder = new RemoteAudioDecoder(aConfig, aFormat, aDrmStubId, aTaskQueue);
  } else {
    // TODO in bug 1334061.
  }
  return decoder.forget();
}

already_AddRefed<MediaDataDecoder>
RemoteDataDecoder::CreateVideoDecoder(const VideoInfo& aConfig,
                                      MediaFormat::Param aFormat,
                                      layers::ImageContainer* aImageContainer,
                                      const nsString& aDrmStubId,
                                      CDMProxy* aProxy, TaskQueue* aTaskQueue)
{
  RefPtr<MediaDataDecoder> decoder;
  if (!aProxy) {
    decoder = new RemoteVideoDecoder(aConfig, aFormat, aImageContainer,
                                     aDrmStubId, aTaskQueue);
  } else {
    // TODO in bug 1334061.
  }
  return decoder.forget();
}

RemoteDataDecoder::RemoteDataDecoder(MediaData::Type aType,
                                     const nsACString& aMimeType,
                                     MediaFormat::Param aFormat,
                                     const nsString& aDrmStubId,
                                     TaskQueue* aTaskQueue)
  : mType(aType)
  , mMimeType(aMimeType)
  , mFormat(aFormat)
  , mDrmStubId(aDrmStubId)
  , mTaskQueue(aTaskQueue)
{
}

RefPtr<MediaDataDecoder::FlushPromise>
RemoteDataDecoder::Flush()
{
  RefPtr<RemoteDataDecoder> self = this;
  return InvokeAsync(mTaskQueue, __func__, [self, this]() {
    mDecodePromise.RejectIfExists(NS_ERROR_DOM_MEDIA_CANCELED, __func__);
    mDrainPromise.RejectIfExists(NS_ERROR_DOM_MEDIA_CANCELED, __func__);
    mJavaDecoder->Flush();
    return FlushPromise::CreateAndResolve(true, __func__);
  });
}

RefPtr<MediaDataDecoder::DecodePromise>
RemoteDataDecoder::Drain()
{
  RefPtr<RemoteDataDecoder> self = this;
  return InvokeAsync(mTaskQueue, __func__, [self, this]() {
    BufferInfo::LocalRef bufferInfo;
    nsresult rv = BufferInfo::New(&bufferInfo);
    if (NS_FAILED(rv)) {
      return DecodePromise::CreateAndReject(NS_ERROR_OUT_OF_MEMORY, __func__);
    }
    bufferInfo->Set(0, 0, -1, MediaCodec::BUFFER_FLAG_END_OF_STREAM);

    RefPtr<DecodePromise> p = mDrainPromise.Ensure(__func__);
    mJavaDecoder->Input(nullptr, bufferInfo, nullptr);
    return p;
  });
}

RefPtr<ShutdownPromise>
RemoteDataDecoder::Shutdown()
{
  LOG("");
  RefPtr<RemoteDataDecoder> self = this;
  return InvokeAsync(mTaskQueue, this, __func__,
                     &RemoteDataDecoder::ProcessShutdown);
}

RefPtr<ShutdownPromise>
RemoteDataDecoder::ProcessShutdown()
{
  AssertOnTaskQueue();
  mShutdown = true;
  if (mJavaDecoder) {
    mJavaDecoder->Release();
    mJavaDecoder = nullptr;
  }

  if (mJavaCallbacks) {
    JavaCallbacksSupport::GetNative(mJavaCallbacks)->Cancel();
    mJavaCallbacks = nullptr;
  }

  mFormat = nullptr;

  return ShutdownPromise::CreateAndResolve(true, __func__);
}

RefPtr<MediaDataDecoder::DecodePromise>
RemoteDataDecoder::Decode(MediaRawData* aSample)
{
  MOZ_ASSERT(aSample != nullptr);

  RefPtr<RemoteDataDecoder> self = this;
  RefPtr<MediaRawData> sample = aSample;
  return InvokeAsync(mTaskQueue, __func__, [self, sample, this]() {
    jni::ByteBuffer::LocalRef bytes = jni::ByteBuffer::New(
      const_cast<uint8_t*>(sample->Data()), sample->Size());

    BufferInfo::LocalRef bufferInfo;
    nsresult rv = BufferInfo::New(&bufferInfo);
    if (NS_FAILED(rv)) {
      return DecodePromise::CreateAndReject(
        MediaResult(NS_ERROR_OUT_OF_MEMORY, __func__), __func__);
    }
    bufferInfo->Set(0, sample->Size(), sample->mTime, 0);

    RefPtr<DecodePromise> p = mDecodePromise.Ensure(__func__);
    mJavaDecoder->Input(bytes, bufferInfo, GetCryptoInfoFromSample(sample));
    return p;
  });
}

void
RemoteDataDecoder::Output(MediaData* aSample)
{
  if (!mTaskQueue->IsCurrentThreadIn()) {
    mTaskQueue->Dispatch(
      NewRunnableMethod<MediaData*>(this, &RemoteDataDecoder::Output, aSample));
    return;
  }
  AssertOnTaskQueue();
  if (mShutdown) {
    return;
  }
  mDecodedData.AppendElement(aSample);
}

void
RemoteDataDecoder::InputExhausted()
{
  if (!mTaskQueue->IsCurrentThreadIn()) {
    mTaskQueue->Dispatch(
      NewRunnableMethod(this, &RemoteDataDecoder::InputExhausted));
    return;
  }
  AssertOnTaskQueue();
  if (mShutdown) {
    return;
  }
  mDecodePromise.ResolveIfExists(mDecodedData, __func__);
  mDecodedData.Clear();
}

void
RemoteDataDecoder::DrainComplete()
{
  if (!mTaskQueue->IsCurrentThreadIn()) {
    mTaskQueue->Dispatch(
      NewRunnableMethod(this, &RemoteDataDecoder::DrainComplete));
    return;
  }
  AssertOnTaskQueue();
  if (mShutdown) {
    return;
  }
  mDrainPromise.ResolveIfExists(mDecodedData, __func__);
  mDecodedData.Clear();
}

void
RemoteDataDecoder::Error(const MediaResult& aError)
{
  if (!mTaskQueue->IsCurrentThreadIn()) {
    mTaskQueue->Dispatch(
      NewRunnableMethod<MediaResult>(this, &RemoteDataDecoder::Error, aError));
    return;
  }
  AssertOnTaskQueue();
  if (mShutdown) {
    return;
  }
  mDecodePromise.RejectIfExists(aError, __func__);
  mDrainPromise.RejectIfExists(aError, __func__);
  mDecodedData.Clear();
}

} // mozilla
