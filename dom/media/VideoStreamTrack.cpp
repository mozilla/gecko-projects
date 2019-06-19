/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-*/
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "VideoStreamTrack.h"

#include "MediaStreamGraph.h"
#include "MediaStreamListener.h"
#include "nsContentUtils.h"
#include "nsGlobalWindowInner.h"
#include "VideoOutput.h"

namespace mozilla {
namespace dom {

VideoStreamTrack::VideoStreamTrack(DOMMediaStream* aStream, TrackID aTrackID,
                                   TrackID aInputTrackID,
                                   MediaStreamTrackSource* aSource,
                                   const MediaTrackConstraints& aConstraints)
    : MediaStreamTrack(aStream, aTrackID, aInputTrackID, aSource,
                       aConstraints) {}

void VideoStreamTrack::Destroy() {
  mVideoOutputs.Clear();
  MediaStreamTrack::Destroy();
}

void VideoStreamTrack::AddVideoOutput(VideoFrameContainer* aSink) {
  auto output = MakeRefPtr<VideoOutput>(
      aSink, nsGlobalWindowInner::Cast(GetParentObject())
                 ->AbstractMainThreadFor(TaskCategory::Other));
  AddVideoOutput(output);
}

void VideoStreamTrack::AddVideoOutput(VideoOutput* aOutput) {
  for (const auto& output : mVideoOutputs) {
    if (output == aOutput) {
      MOZ_ASSERT_UNREACHABLE("A VideoOutput was already added");
      return;
    }
  }
  mVideoOutputs.AppendElement(aOutput);
  AddDirectListener(aOutput);
  AddListener(aOutput);
}

void VideoStreamTrack::RemoveVideoOutput(VideoFrameContainer* aSink) {
  for (const auto& output : nsTArray<RefPtr<VideoOutput>>(mVideoOutputs)) {
    if (output->mVideoFrameContainer == aSink) {
      mVideoOutputs.RemoveElement(output);
      RemoveDirectListener(output);
      RemoveListener(output);
    }
  }
}

void VideoStreamTrack::RemoveVideoOutput(VideoOutput* aOutput) {
  for (const auto& output : nsTArray<RefPtr<VideoOutput>>(mVideoOutputs)) {
    if (output == aOutput) {
      mVideoOutputs.RemoveElement(aOutput);
      RemoveDirectListener(aOutput);
      RemoveListener(aOutput);
    }
  }
}

void VideoStreamTrack::GetLabel(nsAString& aLabel, CallerType aCallerType) {
  if (nsContentUtils::ResistFingerprinting(aCallerType)) {
    aLabel.AssignLiteral("Internal Camera");
    return;
  }
  MediaStreamTrack::GetLabel(aLabel, aCallerType);
}

}  // namespace dom
}  // namespace mozilla
