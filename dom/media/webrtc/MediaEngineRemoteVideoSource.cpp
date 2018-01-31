/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MediaEngineRemoteVideoSource.h"

#include "AllocationHandle.h"
#include "CamerasChild.h"
#include "MediaManager.h"
#include "MediaTrackConstraints.h"
#include "mozilla/RefPtr.h"
#include "nsIPrefService.h"
#include "VideoFrameUtils.h"
#include "VideoUtils.h"
#include "webrtc/api/video/i420_buffer.h"
#include "webrtc/common_video/libyuv/include/webrtc_libyuv.h"

mozilla::LogModule* GetMediaManagerLog();
#define LOG(msg) MOZ_LOG(GetMediaManagerLog(), mozilla::LogLevel::Debug, msg)
#define LOGFRAME(msg) MOZ_LOG(GetMediaManagerLog(), mozilla::LogLevel::Verbose, msg)

namespace mozilla {

using dom::ConstrainLongRange;
using dom::MediaSourceEnum;
using dom::MediaTrackConstraints;
using dom::MediaTrackConstraintSet;
using dom::MediaTrackSettings;
using dom::VideoFacingModeEnum;

MediaEngineRemoteVideoSource::MediaEngineRemoteVideoSource(
    int aIndex,
    camera::CaptureEngine aCapEngine,
    MediaSourceEnum aMediaSource,
    bool aScary)
  : mCaptureIndex(aIndex)
  , mMediaSource(aMediaSource)
  , mCapEngine(aCapEngine)
  , mScary(aScary)
  , mMutex("MediaEngineRemoteVideoSource::mMutex")
  , mSettings(MakeAndAddRef<media::Refcountable<MediaTrackSettings>>())
{
  MOZ_ASSERT(aMediaSource != MediaSourceEnum::Other);
  Init();
}

void
MediaEngineRemoteVideoSource::Init()
{
  LOG((__PRETTY_FUNCTION__));
  AssertIsOnOwningThread();

  char deviceName[kMaxDeviceNameLength];
  char uniqueId[kMaxUniqueIdLength];
  if (camera::GetChildAndCall(&camera::CamerasChild::GetCaptureDevice,
                              mCapEngine, mCaptureIndex,
                              deviceName, kMaxDeviceNameLength,
                              uniqueId, kMaxUniqueIdLength, nullptr)) {
    LOG(("Error initializing RemoteVideoSource (GetCaptureDevice)"));
    return;
  }

  SetName(NS_ConvertUTF8toUTF16(deviceName));
  SetUUID(uniqueId);

  mInitDone = true;
}

void
MediaEngineRemoteVideoSource::Shutdown()
{
  LOG((__PRETTY_FUNCTION__));
  AssertIsOnOwningThread();

  if (!mInitDone) {
    // Already shut down
    return;
  }

  // Allocate always returns a null AllocationHandle.
  // We can safely pass nullptr here.
  if (mState == kStarted) {
    Stop(nullptr);
  }
  if (mState == kAllocated || mState == kStopped) {
    Deallocate(nullptr);
  }
  MOZ_ASSERT(mState == kReleased);

  mInitDone = false;
}

void
MediaEngineRemoteVideoSource::SetName(nsString aName)
{
  LOG((__PRETTY_FUNCTION__));
  AssertIsOnOwningThread();

  mDeviceName = Move(aName);
  bool hasFacingMode = false;
  VideoFacingModeEnum facingMode = VideoFacingModeEnum::User;

  // Set facing mode based on device name.
#if defined(ANDROID)
  // Names are generated. Example: "Camera 0, Facing back, Orientation 90"
  //
  // See media/webrtc/trunk/webrtc/modules/video_capture/android/java/src/org/
  // webrtc/videoengine/VideoCaptureDeviceInfoAndroid.java

  if (aName.Find(NS_LITERAL_STRING("Facing back")) != kNotFound) {
    hasFacingMode = true;
    facingMode = VideoFacingModeEnum::Environment;
  } else if (aName.Find(NS_LITERAL_STRING("Facing front")) != kNotFound) {
    hasFacingMode = true;
    facingMode = VideoFacingModeEnum::User;
  }
#endif // ANDROID
#ifdef XP_MACOSX
  // Kludge to test user-facing cameras on OSX.
  if (aName.Find(NS_LITERAL_STRING("Face")) != -1) {
    hasFacingMode = true;
    facingMode = VideoFacingModeEnum::User;
  }
#endif
#ifdef XP_WIN
  // The cameras' name of Surface book are "Microsoft Camera Front" and
  // "Microsoft Camera Rear" respectively.

  if (aName.Find(NS_LITERAL_STRING("Front")) != kNotFound) {
    hasFacingMode = true;
    facingMode = VideoFacingModeEnum::User;
  } else if (aName.Find(NS_LITERAL_STRING("Rear")) != kNotFound) {
    hasFacingMode = true;
    facingMode = VideoFacingModeEnum::Environment;
  }
#endif // WINDOWS
  if (hasFacingMode) {
    mFacingMode.Assign(NS_ConvertUTF8toUTF16(
        dom::VideoFacingModeEnumValues::strings[uint32_t(facingMode)].value));
  } else {
    mFacingMode.Truncate();
  }
}

nsString
MediaEngineRemoteVideoSource::GetName() const
{
  AssertIsOnOwningThread();

  return mDeviceName;
}

void
MediaEngineRemoteVideoSource::SetUUID(const char* aUUID)
{
  AssertIsOnOwningThread();

  mUniqueId.Assign(aUUID);
}

nsCString
MediaEngineRemoteVideoSource::GetUUID() const
{
  AssertIsOnOwningThread();

  return mUniqueId;
}

nsresult
MediaEngineRemoteVideoSource::Allocate(
    const MediaTrackConstraints& aConstraints,
    const MediaEnginePrefs& aPrefs,
    const nsString& aDeviceId,
    const mozilla::ipc::PrincipalInfo& aPrincipalInfo,
    AllocationHandle** aOutHandle,
    const char** aOutBadConstraint)
{
  LOG((__PRETTY_FUNCTION__));
  AssertIsOnOwningThread();

  MOZ_ASSERT(mInitDone);
  MOZ_ASSERT(mState == kReleased);

  NormalizedConstraints constraints(aConstraints);
  LOG(("ChooseCapability(kFitness) for mTargetCapability and mCapability (Allocate) ++"));
  if (!ChooseCapability(constraints, aPrefs, aDeviceId, mCapability, kFitness)) {
    *aOutBadConstraint =
      MediaConstraintsHelper::FindBadConstraint(constraints, this, aDeviceId);
    return NS_ERROR_FAILURE;
  }
  LOG(("ChooseCapability(kFitness) for mTargetCapability and mCapability (Allocate) --"));

  if (camera::GetChildAndCall(&camera::CamerasChild::AllocateCaptureDevice,
                              mCapEngine, mUniqueId.get(),
                              kMaxUniqueIdLength, mCaptureIndex,
                              aPrincipalInfo)) {
    return NS_ERROR_FAILURE;
  }

  *aOutHandle = nullptr;

  {
    MutexAutoLock lock(mMutex);
    mState = kAllocated;
  }

  LOG(("Video device %d allocated", mCaptureIndex));
  return NS_OK;
}

nsresult
MediaEngineRemoteVideoSource::Deallocate(const RefPtr<const AllocationHandle>& aHandle)
{
  LOG((__PRETTY_FUNCTION__));
  AssertIsOnOwningThread();

  MOZ_ASSERT(mState == kStopped || mState == kAllocated);
  MOZ_ASSERT(mStream);
  MOZ_ASSERT(IsTrackIDExplicit(mTrackID));

  mStream->EndTrack(mTrackID);

  {
    MutexAutoLock lock(mMutex);

    mStream = nullptr;
    mTrackID = TRACK_NONE;
    mPrincipal = PRINCIPAL_HANDLE_NONE;
    mState = kReleased;
  }

  // Stop() has stopped capture synchronously on the media thread before we get
  // here, so there are no longer any callbacks on an IPC thread accessing
  // mImageContainer.
  mImageContainer = nullptr;

  LOG(("Video device %d deallocated", mCaptureIndex));

  if (camera::GetChildAndCall(&camera::CamerasChild::ReleaseCaptureDevice,
                              mCapEngine, mCaptureIndex)) {
    MOZ_ASSERT_UNREACHABLE("Couldn't release allocated device");
  }
  return NS_OK;
}

nsresult
MediaEngineRemoteVideoSource::SetTrack(const RefPtr<const AllocationHandle>& aHandle,
                                       const RefPtr<SourceMediaStream>& aStream,
                                       TrackID aTrackID,
                                       const PrincipalHandle& aPrincipal)
{
  LOG((__PRETTY_FUNCTION__));
  AssertIsOnOwningThread();

  MOZ_ASSERT(mState == kAllocated);
  MOZ_ASSERT(!mStream);
  MOZ_ASSERT(mTrackID == TRACK_NONE);
  MOZ_ASSERT(aStream);
  MOZ_ASSERT(IsTrackIDExplicit(aTrackID));

  if (!mImageContainer) {
    mImageContainer = layers::LayerManager::CreateImageContainer(
                          layers::ImageContainer::ASYNCHRONOUS);
  }

  {
    MutexAutoLock lock(mMutex);
    mStream = aStream;
    mTrackID = aTrackID;
    mPrincipal = aPrincipal;
  }
  aStream->AddTrack(aTrackID, 0, new VideoSegment(),
                    SourceMediaStream::ADDTRACK_QUEUED);
  return NS_OK;
}

nsresult
MediaEngineRemoteVideoSource::Start(const RefPtr<const AllocationHandle>& aHandle)
{
  LOG((__PRETTY_FUNCTION__));
  AssertIsOnOwningThread();

  MOZ_ASSERT(mInitDone);
  MOZ_ASSERT(mState == kAllocated || mState == kStopped);
  MOZ_ASSERT(mStream);
  MOZ_ASSERT(IsTrackIDExplicit(mTrackID));

  {
    MutexAutoLock lock(mMutex);
    mState = kStarted;
  }

  if (camera::GetChildAndCall(&camera::CamerasChild::StartCapture,
                              mCapEngine, mCaptureIndex, mCapability, this)) {
    LOG(("StartCapture failed"));
    MutexAutoLock lock(mMutex);
    mState = kStopped;
    return NS_ERROR_FAILURE;
  }


  return NS_OK;
}

nsresult
MediaEngineRemoteVideoSource::Stop(const RefPtr<const AllocationHandle>& aHandle)
{
  LOG((__PRETTY_FUNCTION__));
  AssertIsOnOwningThread();

  MOZ_ASSERT(mState == kStarted);

  if (camera::GetChildAndCall(&camera::CamerasChild::StopCapture,
                              mCapEngine, mCaptureIndex)) {
    MOZ_DIAGNOSTIC_ASSERT(false, "Stopping a started capture failed");
  }

  {
    MutexAutoLock lock(mMutex);
    mState = kStopped;

    // Drop any cached image so we don't start with a stale image on next
    // usage.  Also, gfx gets very upset if these are held until this object
    // is gc'd in final-cc during shutdown (bug 1374164)
    mImage = nullptr;
  }

  return NS_OK;
}

nsresult
MediaEngineRemoteVideoSource::Reconfigure(const RefPtr<AllocationHandle>& aHandle,
                                          const MediaTrackConstraints& aConstraints,
                                          const MediaEnginePrefs& aPrefs,
                                          const nsString& aDeviceId,
                                          const char** aOutBadConstraint)
{
  LOG((__PRETTY_FUNCTION__));
  AssertIsOnOwningThread();

  MOZ_ASSERT(mInitDone);

  NormalizedConstraints constraints(aConstraints);
  webrtc::CaptureCapability newCapability;
  LOG(("ChooseCapability(kFitness) for mTargetCapability (Reconfigure) ++"));
  if (!ChooseCapability(constraints, aPrefs, aDeviceId, newCapability, kFitness)) {
    *aOutBadConstraint =
      MediaConstraintsHelper::FindBadConstraint(constraints, this, aDeviceId);
    return NS_ERROR_FAILURE;
  }
  LOG(("ChooseCapability(kFitness) for mTargetCapability (Reconfigure) --"));

  if (mCapability == newCapability) {
    return NS_OK;
  }

  // Start() applies mCapability on the device.
  mCapability = newCapability;


  if (mState == kStarted) {
    // Allocate always returns a null AllocationHandle.
    // We can safely pass nullptr below.
    nsresult rv = Stop(nullptr);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }

    rv = Start(nullptr);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }
  }

  return NS_OK;
}

size_t
MediaEngineRemoteVideoSource::NumCapabilities() const
{
  AssertIsOnOwningThread();

  mHardcodedCapabilities.Clear();
  int num = camera::GetChildAndCall(&camera::CamerasChild::NumberOfCapabilities,
                                    mCapEngine, mUniqueId.get());

  if (num >= 1) {
    return num;
  }

  // The default for devices that don't return discrete capabilities: treat
  // them as supporting all capabilities orthogonally. E.g. screensharing.
  // CaptureCapability defaults key values to 0, which means accept any value.
  mHardcodedCapabilities.AppendElement(webrtc::CaptureCapability());
  return mHardcodedCapabilities.Length(); // 1
}

void
MediaEngineRemoteVideoSource::GetCapability(size_t aIndex,
                                            webrtc::CaptureCapability& aOut) const
{
  AssertIsOnOwningThread();
  if (!mHardcodedCapabilities.IsEmpty()) {
    MOZ_ASSERT(aIndex < mHardcodedCapabilities.Length());
    aOut = mHardcodedCapabilities.SafeElementAt(aIndex, webrtc::CaptureCapability());
  }
  camera::GetChildAndCall(&camera::CamerasChild::GetCaptureCapability,
                          mCapEngine, mUniqueId.get(), aIndex, aOut);
}

void
MediaEngineRemoteVideoSource::Pull(const RefPtr<const AllocationHandle>& aHandle,
                                   const RefPtr<SourceMediaStream>& aStream,
                                   TrackID aTrackID,
                                   StreamTime aDesiredTime,
                                   const PrincipalHandle& aPrincipalHandle)
{
  MutexAutoLock lock(mMutex);
  if (mState == kReleased) {
    // We end the track before deallocating, so this is safe.
    return;
  }

  MOZ_ASSERT(mState == kStarted || mState == kStopped);

  StreamTime delta = aDesiredTime - aStream->GetEndOfAppendedData(aTrackID);
  if (delta <= 0) {
    return;
  }

  VideoSegment segment;
  RefPtr<layers::Image> image = mImage;
  if (mState == kStarted) {
    MOZ_ASSERT(!image || mImageSize == image->GetSize());
    segment.AppendFrame(image.forget(), delta, mImageSize, aPrincipalHandle);
  } else {
    // nullptr images are allowed, but we force it to black and retain the size.
    segment.AppendFrame(image.forget(), delta, mImageSize, aPrincipalHandle, true);
  }

  // This is safe from any thread, and is safe if the track is Finished
  // or Destroyed.
  // This can fail if either a) we haven't added the track yet, or b)
  // we've removed or finished the track.
  aStream->AppendToTrack(aTrackID, &segment);
}

int
MediaEngineRemoteVideoSource::DeliverFrame(uint8_t* aBuffer,
                                           const camera::VideoFrameProperties& aProps)
{
  // Cameras IPC thread - take great care with accessing members!

  int32_t req_max_width;
  int32_t req_max_height;
  int32_t req_ideal_width;
  int32_t req_ideal_height;
  {
    MutexAutoLock lock(mMutex);
    MOZ_ASSERT(mState == kStarted);
    req_max_width = mCapability.width & 0xffff;
    req_max_height = mCapability.height & 0xffff;
    req_ideal_width = (mCapability.width >> 16) & 0xffff;
    req_ideal_height = (mCapability.height >> 16) & 0xffff;
  }

  int32_t dest_max_width = std::min(req_max_width, aProps.width());
  int32_t dest_max_height = std::min(req_max_height, aProps.height());
  // This logic works for both camera and screen sharing case.
  // for camera case, req_ideal_width and req_ideal_height is 0.
  // The following snippet will set dst_width to dest_max_width and dst_height to dest_max_height
  int32_t dst_width = std::min(req_ideal_width > 0 ? req_ideal_width : aProps.width(), dest_max_width);
  int32_t dst_height = std::min(req_ideal_height > 0 ? req_ideal_height : aProps.height(), dest_max_height);

  int dst_stride_y = dst_width;
  int dst_stride_uv = (dst_width + 1) / 2;

  camera::VideoFrameProperties properties;
  uint8_t* frame;
  bool needReScale = (dst_width != aProps.width() ||
                      dst_height != aProps.height()) &&
                     dst_width <= aProps.width() &&
                     dst_height <= aProps.height();

  if (!needReScale) {
    dst_width = aProps.width();
    dst_height = aProps.height();
    frame = aBuffer;
  } else {
    rtc::scoped_refptr<webrtc::I420Buffer> i420Buffer;
    i420Buffer = webrtc::I420Buffer::Create(aProps.width(),
                                            aProps.height(),
                                            aProps.width(),
                                            (aProps.width() + 1) / 2,
                                            (aProps.width() + 1) / 2);

    const int conversionResult = webrtc::ConvertToI420(webrtc::kI420,
                                                       aBuffer,
                                                       0, 0,  // No cropping
                                                       aProps.width(), aProps.height(),
                                                       aProps.width() * aProps.height() * 3 / 2,
                                                       webrtc::kVideoRotation_0,
                                                       i420Buffer.get());

    webrtc::VideoFrame captureFrame(i420Buffer, 0, 0, webrtc::kVideoRotation_0);
    if (conversionResult < 0) {
      return 0;
    }

    rtc::scoped_refptr<webrtc::I420Buffer> scaledBuffer;
    scaledBuffer = webrtc::I420Buffer::Create(dst_width, dst_height, dst_stride_y,
                                              dst_stride_uv, dst_stride_uv);

    scaledBuffer->CropAndScaleFrom(*captureFrame.video_frame_buffer().get());
    webrtc::VideoFrame scaledFrame(scaledBuffer, 0, 0, webrtc::kVideoRotation_0);

    VideoFrameUtils::InitFrameBufferProperties(scaledFrame, properties);
    frame = new unsigned char[properties.bufferSize()];

    if (!frame) {
      return 0;
    }

    VideoFrameUtils::CopyVideoFrameBuffers(frame,
                                           properties.bufferSize(), scaledFrame);
  }

  // Create a video frame and append it to the track.
  RefPtr<layers::PlanarYCbCrImage> image =
    mImageContainer->CreatePlanarYCbCrImage();

  const uint8_t lumaBpp = 8;
  const uint8_t chromaBpp = 4;

  layers::PlanarYCbCrData data;

  // Take lots of care to round up!
  data.mYChannel = frame;
  data.mYSize = IntSize(dst_width, dst_height);
  data.mYStride = (dst_width * lumaBpp + 7) / 8;
  data.mCbCrStride = (dst_width * chromaBpp + 7) / 8;
  data.mCbChannel = frame + dst_height * data.mYStride;
  data.mCrChannel = data.mCbChannel + ((dst_height + 1) / 2) * data.mCbCrStride;
  data.mCbCrSize = IntSize((dst_width + 1) / 2, (dst_height + 1) / 2);
  data.mPicX = 0;
  data.mPicY = 0;
  data.mPicSize = IntSize(dst_width, dst_height);
  data.mStereoMode = StereoMode::MONO;

  if (!image->CopyData(data)) {
    MOZ_ASSERT(false);
    return 0;
  }

  if (needReScale && frame) {
    delete frame;
    frame = nullptr;
  }

#ifdef DEBUG
  static uint32_t frame_num = 0;
  LOGFRAME(("frame %d (%dx%d)->(%dx%d); timeStamp %u, ntpTimeMs %" PRIu64 ", renderTimeMs %" PRIu64,
            frame_num++, aProps.width(), aProps.height(), dst_width, dst_height,
            aProps.timeStamp(), aProps.ntpTimeMs(), aProps.renderTimeMs()));
#endif

  bool sizeChanged = false;
  {
    MutexAutoLock lock(mMutex);
    // implicitly releases last image
    sizeChanged = mImage && image && mImage->GetSize() != image->GetSize();
    mImage = image.forget();
    mImageSize = mImage->GetSize();
  }

  if (sizeChanged) {
    NS_DispatchToMainThread(NS_NewRunnableFunction(
        "MediaEngineRemoteVideoSource::FrameSizeChange",
        [settings = mSettings, dst_width, dst_height]() mutable {
      settings->mWidth.Value() = dst_width;
      settings->mHeight.Value() = dst_height;
    }));
  }

  // We'll push the frame into the MSG on the next Pull. This will avoid
  // swamping the MSG with frames should it be taking longer than normal to run
  // an iteration.

  return 0;
}

uint32_t
MediaEngineRemoteVideoSource::GetDistance(
    const webrtc::CaptureCapability& aCandidate,
    const NormalizedConstraintSet &aConstraints,
    const nsString& aDeviceId,
    const DistanceCalculation aCalculate) const
{
  if (aCalculate == kFeasibility) {
    return GetFeasibilityDistance(aCandidate, aConstraints, aDeviceId);
  }
  return GetFitnessDistance(aCandidate, aConstraints, aDeviceId);
}

uint32_t
MediaEngineRemoteVideoSource::GetFitnessDistance(
    const webrtc::CaptureCapability& aCandidate,
    const NormalizedConstraintSet& aConstraints,
    const nsString& aDeviceId) const
{
  AssertIsOnOwningThread();

  // Treat width|height|frameRate == 0 on capability as "can do any".
  // This allows for orthogonal capabilities that are not in discrete steps.

  typedef MediaConstraintsHelper H;
  uint64_t distance =
    uint64_t(H::FitnessDistance(aDeviceId, aConstraints.mDeviceId)) +
    uint64_t(H::FitnessDistance(mFacingMode, aConstraints.mFacingMode)) +
    uint64_t(aCandidate.width ? H::FitnessDistance(int32_t(aCandidate.width),
                                                  aConstraints.mWidth) : 0) +
    uint64_t(aCandidate.height ? H::FitnessDistance(int32_t(aCandidate.height),
                                                    aConstraints.mHeight) : 0) +
    uint64_t(aCandidate.maxFPS ? H::FitnessDistance(double(aCandidate.maxFPS),
                                                    aConstraints.mFrameRate) : 0);
  return uint32_t(std::min(distance, uint64_t(UINT32_MAX)));
}

uint32_t
MediaEngineRemoteVideoSource::GetFeasibilityDistance(
    const webrtc::CaptureCapability& aCandidate,
    const NormalizedConstraintSet& aConstraints,
    const nsString& aDeviceId) const
{
  AssertIsOnOwningThread();

  // Treat width|height|frameRate == 0 on capability as "can do any".
  // This allows for orthogonal capabilities that are not in discrete steps.

  typedef MediaConstraintsHelper H;
  uint64_t distance =
    uint64_t(H::FitnessDistance(aDeviceId, aConstraints.mDeviceId)) +
    uint64_t(H::FitnessDistance(mFacingMode, aConstraints.mFacingMode)) +
    uint64_t(aCandidate.width ? H::FeasibilityDistance(int32_t(aCandidate.width),
                                                       aConstraints.mWidth) : 0) +
    uint64_t(aCandidate.height ? H::FeasibilityDistance(int32_t(aCandidate.height),
                                                        aConstraints.mHeight) : 0) +
    uint64_t(aCandidate.maxFPS ? H::FeasibilityDistance(double(aCandidate.maxFPS),
                                                        aConstraints.mFrameRate) : 0);
  return uint32_t(std::min(distance, uint64_t(UINT32_MAX)));
}

// Find best capability by removing inferiors. May leave >1 of equal distance

/* static */ void
MediaEngineRemoteVideoSource::TrimLessFitCandidates(nsTArray<CapabilityCandidate>& set)
{
  uint32_t best = UINT32_MAX;
  for (auto& candidate : set) {
    if (best > candidate.mDistance) {
      best = candidate.mDistance;
    }
  }
  for (size_t i = 0; i < set.Length();) {
    if (set[i].mDistance > best) {
      set.RemoveElementAt(i);
    } else {
      ++i;
    }
  }
  MOZ_ASSERT(set.Length());
}

uint32_t
MediaEngineRemoteVideoSource::GetBestFitnessDistance(
    const nsTArray<const NormalizedConstraintSet*>& aConstraintSets,
    const nsString& aDeviceId) const
{
  AssertIsOnOwningThread();

  size_t num = NumCapabilities();

  nsTArray<CapabilityCandidate> candidateSet;
  for (size_t i = 0; i < num; i++) {
    candidateSet.AppendElement(i);
  }

  bool first = true;
  for (const NormalizedConstraintSet* ns : aConstraintSets) {
    for (size_t i = 0; i < candidateSet.Length();  ) {
      auto& candidate = candidateSet[i];
      webrtc::CaptureCapability cap;
      GetCapability(candidate.mIndex, cap);
      uint32_t distance = GetFitnessDistance(cap, *ns, aDeviceId);
      if (distance == UINT32_MAX) {
        candidateSet.RemoveElementAt(i);
      } else {
        ++i;
        if (first) {
          candidate.mDistance = distance;
        }
      }
    }
    first = false;
  }
  if (!candidateSet.Length()) {
    return UINT32_MAX;
  }
  TrimLessFitCandidates(candidateSet);
  return candidateSet[0].mDistance;
}

static void
LogConstraints(const NormalizedConstraintSet& aConstraints)
{
  auto& c = aConstraints;
  if (c.mWidth.mIdeal.isSome()) {
    LOG(("Constraints: width: { min: %d, max: %d, ideal: %d }",
         c.mWidth.mMin, c.mWidth.mMax,
         c.mWidth.mIdeal.valueOr(0)));
  } else {
    LOG(("Constraints: width: { min: %d, max: %d }",
         c.mWidth.mMin, c.mWidth.mMax));
  }
  if (c.mHeight.mIdeal.isSome()) {
    LOG(("             height: { min: %d, max: %d, ideal: %d }",
         c.mHeight.mMin, c.mHeight.mMax,
         c.mHeight.mIdeal.valueOr(0)));
  } else {
    LOG(("             height: { min: %d, max: %d }",
         c.mHeight.mMin, c.mHeight.mMax));
  }
  if (c.mFrameRate.mIdeal.isSome()) {
    LOG(("             frameRate: { min: %f, max: %f, ideal: %f }",
         c.mFrameRate.mMin, c.mFrameRate.mMax,
         c.mFrameRate.mIdeal.valueOr(0)));
  } else {
    LOG(("             frameRate: { min: %f, max: %f }",
         c.mFrameRate.mMin, c.mFrameRate.mMax));
  }
}

static void
LogCapability(const char* aHeader,
              const webrtc::CaptureCapability &aCapability,
              uint32_t aDistance)
{
  // RawVideoType and VideoCodecType media/webrtc/trunk/webrtc/common_types.h
  static const char* const types[] = {
    "I420",
    "YV12",
    "YUY2",
    "UYVY",
    "IYUV",
    "ARGB",
    "RGB24",
    "RGB565",
    "ARGB4444",
    "ARGB1555",
    "MJPEG",
    "NV12",
    "NV21",
    "BGRA",
    "Unknown type"
  };

  static const char* const codec[] = {
    "VP8",
    "VP9",
    "H264",
    "I420",
    "RED",
    "ULPFEC",
    "Generic codec",
    "Unknown codec"
  };

  LOG(("%s: %4u x %4u x %2u maxFps, %s, %s. Distance = %" PRIu32,
       aHeader, aCapability.width, aCapability.height, aCapability.maxFPS,
       types[std::min(std::max(uint32_t(0), uint32_t(aCapability.rawType)),
                      uint32_t(sizeof(types) / sizeof(*types) - 1))],
       codec[std::min(std::max(uint32_t(0), uint32_t(aCapability.codecType)),
                      uint32_t(sizeof(codec) / sizeof(*codec) - 1))],
       aDistance));
}

bool
MediaEngineRemoteVideoSource::ChooseCapability(
    const NormalizedConstraints& aConstraints,
    const MediaEnginePrefs& aPrefs,
    const nsString& aDeviceId,
    webrtc::CaptureCapability& aCapability,
    const DistanceCalculation aCalculate)
{
  LOG((__PRETTY_FUNCTION__));
  AssertIsOnOwningThread();

  if (MOZ_LOG_TEST(GetMediaManagerLog(), LogLevel::Debug)) {
    LOG(("ChooseCapability: prefs: %dx%d @%dfps",
         aPrefs.GetWidth(), aPrefs.GetHeight(),
         aPrefs.mFPS));
    LogConstraints(aConstraints);
    if (!aConstraints.mAdvanced.empty()) {
      LOG(("Advanced array[%zu]:", aConstraints.mAdvanced.size()));
      for (auto& advanced : aConstraints.mAdvanced) {
        LogConstraints(advanced);
      }
    }
  }

  switch (mMediaSource) {
    case MediaSourceEnum::Screen:
    case MediaSourceEnum::Window:
    case MediaSourceEnum::Application: {
      FlattenedConstraints c(aConstraints);
      // The actual resolution to constrain around is not easy to find ahead of
      // time (and may in fact change over time), so as a hack, we push ideal
      // and max constraints down to desktop_capture_impl.cc and finish the
      // algorithm there.
      aCapability.width =
        (c.mWidth.mIdeal.valueOr(0) & 0xffff) << 16 | (c.mWidth.mMax & 0xffff);
      aCapability.height =
        (c.mHeight.mIdeal.valueOr(0) & 0xffff) << 16 | (c.mHeight.mMax & 0xffff);
      aCapability.maxFPS =
        c.mFrameRate.Clamp(c.mFrameRate.mIdeal.valueOr(aPrefs.mFPS));
      return true;
    }
    default:
      break;
  }

  size_t num = NumCapabilities();

  nsTArray<CapabilityCandidate> candidateSet;
  for (size_t i = 0; i < num; i++) {
    candidateSet.AppendElement(i);
  }

  // First, filter capabilities by required constraints (min, max, exact).

  for (size_t i = 0; i < candidateSet.Length();) {
    auto& candidate = candidateSet[i];
    webrtc::CaptureCapability cap;
    GetCapability(candidate.mIndex, cap);
    candidate.mDistance = GetDistance(cap, aConstraints, aDeviceId, aCalculate);
    LogCapability("Capability", cap, candidate.mDistance);
    if (candidate.mDistance == UINT32_MAX) {
      candidateSet.RemoveElementAt(i);
    } else {
      ++i;
    }
  }

  if (!candidateSet.Length()) {
    LOG(("failed to find capability match from %zu choices",num));
    return false;
  }

  // Filter further with all advanced constraints (that don't overconstrain).

  for (const auto &cs : aConstraints.mAdvanced) {
    nsTArray<CapabilityCandidate> rejects;
    for (size_t i = 0; i < candidateSet.Length();) {
      auto& candidate = candidateSet[i];
      webrtc::CaptureCapability cap;
      GetCapability(candidate.mIndex, cap);
      if (GetDistance(cap, cs, aDeviceId, aCalculate) == UINT32_MAX) {
        rejects.AppendElement(candidate);
        candidateSet.RemoveElementAt(i);
      } else {
        ++i;
      }
    }
    if (!candidateSet.Length()) {
      candidateSet.AppendElements(Move(rejects));
    }
  }
  MOZ_ASSERT(candidateSet.Length(),
             "advanced constraints filtering step can't reduce candidates to zero");

  // Remaining algorithm is up to the UA.

  TrimLessFitCandidates(candidateSet);

  // Any remaining multiples all have the same distance. A common case of this
  // occurs when no ideal is specified. Lean toward defaults.
  uint32_t sameDistance = candidateSet[0].mDistance;
  {
    MediaTrackConstraintSet prefs;
    prefs.mWidth.SetAsLong() = aPrefs.GetWidth();
    prefs.mHeight.SetAsLong() = aPrefs.GetHeight();
    prefs.mFrameRate.SetAsDouble() = aPrefs.mFPS;
    NormalizedConstraintSet normPrefs(prefs, false);

    for (auto& candidate : candidateSet) {
      webrtc::CaptureCapability cap;
      GetCapability(candidate.mIndex, cap);
      candidate.mDistance = GetDistance(cap, normPrefs, aDeviceId, aCalculate);
    }
    TrimLessFitCandidates(candidateSet);
  }

  // Any remaining multiples all have the same distance, but may vary on
  // format. Some formats are more desirable for certain use like WebRTC.
  // E.g. I420 over RGB24 can remove a needless format conversion.

  bool found = false;
  for (auto& candidate : candidateSet) {
    webrtc::CaptureCapability cap;
    GetCapability(candidate.mIndex, cap);
    if (cap.rawType == webrtc::RawVideoType::kVideoI420 ||
        cap.rawType == webrtc::RawVideoType::kVideoYUY2 ||
        cap.rawType == webrtc::RawVideoType::kVideoYV12) {
      aCapability = cap;
      found = true;
      break;
    }
  }
  if (!found) {
    GetCapability(candidateSet[0].mIndex, aCapability);
  }

  LogCapability("Chosen capability", aCapability, sameDistance);
  return true;
}

void
MediaEngineRemoteVideoSource::GetSettings(MediaTrackSettings& aOutSettings) const
{
  aOutSettings = *mSettings;
}

void
MediaEngineRemoteVideoSource::Refresh(int aIndex)
{
  LOG((__PRETTY_FUNCTION__));
  AssertIsOnOwningThread();

  // NOTE: mCaptureIndex might have changed when allocated!
  // Use aIndex to update information, but don't change mCaptureIndex!!
  // Caller looked up this source by uniqueId, so it shouldn't change
  char deviceName[kMaxDeviceNameLength];
  char uniqueId[kMaxUniqueIdLength];

  if (camera::GetChildAndCall(&camera::CamerasChild::GetCaptureDevice,
                              mCapEngine, aIndex, deviceName,
                              sizeof(deviceName), uniqueId, sizeof(uniqueId),
                              nullptr)) {
    return;
  }

  SetName(NS_ConvertUTF8toUTF16(deviceName));
  MOZ_ASSERT(mUniqueId.Equals(uniqueId));
}

} // namespace mozilla
