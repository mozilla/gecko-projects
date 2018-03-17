/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ParentRecovery.h"

using namespace mozilla::recordreplay::channel;

namespace mozilla {
namespace recordreplay {
namespace recovery {

// The last snapshot which the child process reached.
static size_t gLastSnapshot;

// Messages sent before the last snapshot was reached which will need to be
// resent to a recovering process.
static StaticInfallibleVector<Message*> gOldMessages;

// Messages sent since the last snapshot which affect the replaying process'
// behavior. These also need to be resent to a recovering process.
static StaticInfallibleVector<Message*> gSnapshotMessages;

// The current stage of recovery we are in.
enum class RecoveryStage {
  None,
  ReachingSnapshot,
  PlayingSnapshotMessages
};
static RecoveryStage gRecoveryStage = RecoveryStage::None;

// When in the PlayingSnapshotMessages stage, how much of gSnapshotMessages has
// been sent to the recovering process.
static size_t gNumRecoveredSnapshotMessages;

bool
IsRecovering()
{
  return gRecoveryStage != RecoveryStage::None;
}

void
BeginRecovery()
{
  MOZ_RELEASE_ASSERT(!IsRecovering());
  MOZ_RELEASE_ASSERT(!gSnapshotMessages.empty());
  gRecoveryStage = RecoveryStage::ReachingSnapshot;
}

void
NoteOutgoingMessage(const Message& aMsg)
{
  MOZ_RELEASE_ASSERT(!IsRecovering());
  switch (aMsg.mType) {
  case MessageType::SetBreakpoint:
  case MessageType::DebuggerRequest:
  case MessageType::Resume:
    gSnapshotMessages.emplaceBack(aMsg.Clone());
    break;
  default:
    return;
  }
}

bool
NoteIncomingMessage(const Message& aMsg)
{
  // Check if we are done recovering.
  if (gRecoveryStage == RecoveryStage::PlayingSnapshotMessages &&
      gNumRecoveredSnapshotMessages == gSnapshotMessages.length())
  {
    switch (aMsg.mType) {
    case MessageType::HitSnapshot:
    case MessageType::HitBreakpoint:
    case MessageType::DebuggerResponse:
      gRecoveryStage = RecoveryStage::None;
      SendMessage(SetAllowIntentionalCrashesMessage(true));
      break;
    default: break;
    }
  }

  if (IsRecovering()) {
    switch (aMsg.mType) {
    case MessageType::HitSnapshot: {
      const HitSnapshotMessage& nmsg = static_cast<const HitSnapshotMessage&>(aMsg);
      if (nmsg.mSnapshotId == 0) {
        SendMessage(SetAllowIntentionalCrashesMessage(false));
      }
      if (nmsg.mInterim) {
        MOZ_RELEASE_ASSERT(gRecoveryStage == RecoveryStage::PlayingSnapshotMessages);
        MOZ_RELEASE_ASSERT(nmsg.mSnapshotId < gLastSnapshot);
      } else if (nmsg.mSnapshotId < gLastSnapshot) {
        MOZ_RELEASE_ASSERT(gRecoveryStage == RecoveryStage::ReachingSnapshot);
        SendMessage(ResumeMessage(/* forward = */ true, /* hitOtherBreakpoints = */ false));
      } else {
        MOZ_RELEASE_ASSERT(gRecoveryStage == RecoveryStage::ReachingSnapshot);
        MOZ_RELEASE_ASSERT(nmsg.mSnapshotId == gLastSnapshot);

        // Send messages to set all breakpoints which existed when this
        // snapshot was originally reached.
        for (const Message* oldMessage : gOldMessages) {
          SendMessage(*oldMessage);
        }

        gRecoveryStage = RecoveryStage::PlayingSnapshotMessages;
        SendMessage(*gSnapshotMessages[0]);
        gNumRecoveredSnapshotMessages = 1;
      }
      return false;
    }
    case MessageType::HitBreakpoint:
    case MessageType::DebuggerResponse:
      MOZ_RELEASE_ASSERT(gRecoveryStage == RecoveryStage::PlayingSnapshotMessages);
      Message* msg;
      do {
        msg = gSnapshotMessages[gNumRecoveredSnapshotMessages++];
        SendMessage(*msg);
      } while (msg->mType == MessageType::SetBreakpoint);
      return false;
    case MessageType::FatalError:
      return true;
    default:
      return false;
    }
  }

  switch (aMsg.mType) {
  case MessageType::HitSnapshot: {
    const HitSnapshotMessage& nmsg = static_cast<const HitSnapshotMessage&>(aMsg);

    // Ignore interim snapshot messages.
    if (nmsg.mInterim) {
      break;
    }

    gLastSnapshot = nmsg.mSnapshotId;

    // All messages sent since the last snapshot are now obsolete, except
    // SetBreakpoint messages.
    for (Message* msg : gSnapshotMessages) {
      if (msg->mType == MessageType::SetBreakpoint) {
        // Look for an older SetBreakpoint on the same ID to overwrite.
        bool found = false;
        for (Message*& older : gOldMessages) {
          if (older->mType == MessageType::SetBreakpoint &&
              static_cast<SetBreakpointMessage*>(msg)->mId ==
              static_cast<SetBreakpointMessage*>(older)->mId) {
            free(older);
            older = msg;
            found = true;
          }
        }
        if (!found) {
          gOldMessages.emplaceBack(msg);
        }
      } else {
        free(msg);
      }
    }
    gSnapshotMessages.clear();
    break;
  }
  default:
    break;
  }
  return true;
}

} // namespace recovery
} // namespace recordreplay
} // namespace mozilla
