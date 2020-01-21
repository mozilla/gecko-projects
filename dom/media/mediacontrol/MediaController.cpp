/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MediaController.h"

#include "MediaControlService.h"
#include "MediaControlUtils.h"
#include "mozilla/dom/BrowsingContext.h"
#include "mozilla/dom/CanonicalBrowsingContext.h"

// avoid redefined macro in unified build
#undef LOG
#define LOG(msg, ...)                                                    \
  MOZ_LOG(gMediaControlLog, LogLevel::Debug,                             \
          ("MediaController=%p, Id=%" PRId64 ", " msg, this, this->Id(), \
           ##__VA_ARGS__))

namespace mozilla {
namespace dom {

MediaController::MediaController(uint64_t aContextId)
    : mBrowsingContextId(aContextId) {
  MOZ_DIAGNOSTIC_ASSERT(XRE_IsParentProcess(),
                        "MediaController only runs on Chrome process!");
  LOG("Create controller %" PRId64, Id());
}

MediaController::~MediaController() {
  LOG("Destroy controller %" PRId64, Id());
  MOZ_DIAGNOSTIC_ASSERT(!mIsRegisteredToService);
};

void MediaController::Play() {
  LOG("Play");
  SetPlayState(PlaybackState::ePlaying);
  UpdateMediaControlKeysEventToContentMediaIfNeeded(
      MediaControlKeysEvent::ePlay);
}

void MediaController::Pause() {
  LOG("Pause");
  SetPlayState(PlaybackState::ePaused);
  UpdateMediaControlKeysEventToContentMediaIfNeeded(
      MediaControlKeysEvent::ePause);
}

void MediaController::Stop() {
  LOG("Stop");
  SetPlayState(PlaybackState::eStopped);
  UpdateMediaControlKeysEventToContentMediaIfNeeded(
      MediaControlKeysEvent::eStop);
}

void MediaController::UpdateMediaControlKeysEventToContentMediaIfNeeded(
    MediaControlKeysEvent aEvent) {
  // There is no controlled media existing, we have no need to update media
  // action to the content process.
  if (!ControlledMediaNum()) {
    return;
  }
  RefPtr<BrowsingContext> context = BrowsingContext::Get(mBrowsingContextId);
  if (context) {
    context->Canonical()->UpdateMediaControlKeysEvent(aEvent);
  }
}

void MediaController::Shutdown() {
  SetPlayState(PlaybackState::eStopped);
  // The media controller would be removed from the service when we receive a
  // notification from the content process about all controlled media has been
  // stoppped. However, if controlled media is stopped after detaching
  // browsing context, then sending the notification from the content process
  // would fail so that we are not able to notify the chrome process to remove
  // the corresponding controller. Therefore, we should manually remove the
  // controller from the service.
  Deactivate();
  mControlledMediaNum = 0;
  mPlayingControlledMediaNum = 0;
}

void MediaController::NotifyMediaStateChanged(ControlledMediaState aState) {
  if (aState == ControlledMediaState::eStarted) {
    IncreaseControlledMediaNum();
  } else if (aState == ControlledMediaState::eStopped) {
    DecreaseControlledMediaNum();
  } else if (aState == ControlledMediaState::ePlayed) {
    IncreasePlayingControlledMediaNum();
  } else if (aState == ControlledMediaState::ePaused) {
    DecreasePlayingControlledMediaNum();
  }
}

void MediaController::NotifyMediaAudibleChanged(bool aAudible) {
  mAudible = aAudible;
  RefPtr<MediaControlService> service = MediaControlService::GetService();
  MOZ_ASSERT(service);
  if (mAudible) {
    service->GetAudioFocusManager().RequestAudioFocus(this);
  } else {
    service->GetAudioFocusManager().RevokeAudioFocus(this);
  }
}

void MediaController::IncreaseControlledMediaNum() {
  MOZ_DIAGNOSTIC_ASSERT(mControlledMediaNum >= 0);
  mControlledMediaNum++;
  LOG("Increase controlled media num to %" PRId64, mControlledMediaNum);
  if (mControlledMediaNum == 1) {
    Activate();
  }
}

void MediaController::DecreaseControlledMediaNum() {
  MOZ_DIAGNOSTIC_ASSERT(mControlledMediaNum >= 1);
  mControlledMediaNum--;
  LOG("Decrease controlled media num to %" PRId64, mControlledMediaNum);
  if (mControlledMediaNum == 0) {
    Deactivate();
  }
}

void MediaController::IncreasePlayingControlledMediaNum() {
  MOZ_ASSERT(mPlayingControlledMediaNum >= 0);
  mPlayingControlledMediaNum++;
  LOG("Increase playing controlled media num to %" PRId64,
      mPlayingControlledMediaNum);
  MOZ_ASSERT(mPlayingControlledMediaNum <= mControlledMediaNum,
             "The number of playing media should not exceed the number of "
             "controlled media!");
  if (mPlayingControlledMediaNum == 1) {
    SetPlayState(PlaybackState::ePlaying);
  }
}

void MediaController::DecreasePlayingControlledMediaNum() {
  mPlayingControlledMediaNum--;
  LOG("Decrease playing controlled media num to %" PRId64,
      mPlayingControlledMediaNum);
  MOZ_ASSERT(mPlayingControlledMediaNum >= 0);
  if (mPlayingControlledMediaNum == 0) {
    SetPlayState(PlaybackState::ePaused);
  }
}

// TODO : Use watchable to moniter mControlledMediaNum
void MediaController::Activate() {
  RefPtr<MediaControlService> service = MediaControlService::GetService();
  if (service && !mIsRegisteredToService) {
    mIsRegisteredToService = service->RegisterActiveMediaController(this);
    MOZ_ASSERT(mIsRegisteredToService, "Fail to register controller!");
  }
}

void MediaController::Deactivate() {
  RefPtr<MediaControlService> service = MediaControlService::GetService();
  if (service) {
    service->GetAudioFocusManager().RevokeAudioFocus(this);
    if (mIsRegisteredToService) {
      mIsRegisteredToService = !service->UnregisterActiveMediaController(this);
      MOZ_ASSERT(!mIsRegisteredToService, "Fail to unregister controller!");
    }
  }
}

void MediaController::SetPlayState(PlaybackState aState) {
  if (mState == aState) {
    return;
  }
  LOG("SetPlayState : '%s'", ToPlaybackStateEventStr(aState));
  mState = aState;
  mPlaybackStateChangedEvent.Notify(mState);
}

PlaybackState MediaController::GetState() const { return mState; }

uint64_t MediaController::Id() const { return mBrowsingContextId; }

bool MediaController::IsAudible() const {
  return mState == PlaybackState::ePlaying && mAudible;
}

uint64_t MediaController::ControlledMediaNum() const {
  return mControlledMediaNum;
}

}  // namespace dom
}  // namespace mozilla
