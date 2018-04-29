/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

// This file has the logic which the middleman process uses to communicate with
// the parent process and with the replayed process.

#include "ParentInternal.h"

#include "base/task.h"
#include "ipc/Channel.h"
#include "js/Proxy.h"
#include "mozilla/dom/ContentChild.h"
#include "mozilla/dom/ProcessGlobal.h"
#include "mozilla/dom/ScriptSettings.h"
#include "mozilla/layers/CompositorBridgeChild.h"
#include "mozilla/ipc/IOThreadChild.h"
#include "InfallibleVector.h"
#include "Monitor.h"
#include "ProcessRecordReplay.h"
#include "ProcessRedirect.h"

#include <algorithm>

using std::min;

namespace mozilla {
namespace recordreplay {
namespace parent {

///////////////////////////////////////////////////////////////////////////////
// Preferences
///////////////////////////////////////////////////////////////////////////////

static bool gPreferencesLoaded;
static bool gRewindingEnabled;

static void
PreferencesLoaded()
{
  MOZ_RELEASE_ASSERT(NS_IsMainThread());

  MOZ_RELEASE_ASSERT(!gPreferencesLoaded);
  gPreferencesLoaded = true;

  gRewindingEnabled = Preferences::GetBool("devtools.recordreplay.enableRewinding");

  // Force-disable rewinding and saving checkpoints with an env var for testing.
  if (getenv("NO_REWIND")) {
    gRewindingEnabled = false;
  }
}

bool
CanRewind()
{
  MOZ_RELEASE_ASSERT(gPreferencesLoaded);
  return gRewindingEnabled;
}

///////////////////////////////////////////////////////////////////////////////
// Child Roles
///////////////////////////////////////////////////////////////////////////////

static const double FlushSeconds = .5;
static const double MajorCheckpointSeconds = 2;

// This section describes the strategy used for managing child processes. When
// recording, there is a single recording process and two replaying processes.
// When replaying, there are two replaying processes. The main advantage of
// using two replaying processes is to provide a smooth experience when
// rewinding.
//
// At any time there is one active child: the process which the user is
// interacting with. This may be any of the two or three children in existence,
// depending on the user's behavior. Below are some scenarios showing the state
// we attempt to keep the children in, and ways in which the active process
// switches from one to another.
//
// When the recording process is actively recording, flushes are issued to it
// every FlushSeconds to keep the recording reasonably current. Additionally,
// one replaying process saves a checkpoint every MajorCheckpointSeconds
// with the process saving the checkpoint alternating back and forth so that
// individual processes save checkpoints every MajorCheckpointSeconds*2. These
// are the major checkpoints for each replaying process.
//
// Active  Recording:    -----------------------
// Standby Replaying #1: *---------*---------*
// Standby Replaying #2: -----*---------*-----
//
// When the recording process is explicitly paused (via the debugger UI) at a
// checkpoint or breakpoint, it is flushed and the replaying processes will
// navigate around the recording to ensure all checkpoints going back at least
// MajorCheckpointSeconds have been saved. These are the intermediate
// checkpoints. No replaying process needs to rewind past its last major
// checkpoint, and a given intermediate checkpoint will only ever be saved by
// the replaying process with the most recent major checkpoint.
//
// Active  Recording:    -----------------------
// Standby Replaying #1: *---------*---------***
// Standby Replaying #2: -----*---------*****
//
// If the user starts rewinding, the replaying process with the most recent
// major checkpoint (and which has been saving the most recent intermediate
// checkpoints) becomes the active child.
//
// Inert   Recording:    -----------------------
// Active  Replaying #1: *---------*---------**
// Standby Replaying #2: -----*---------*****
//
// As the user continues rewinding, the replaying process stays active until it
// goes past its most recent major checkpoint. At that time the other replaying
// process (which has been saving checkpoints prior to that point) becomes the
// active child and allows continuous rewinding. The first replaying process
// rewinds to its last major checkpoint and begins saving older intermediate
// checkpoints, attempting to maintain the invariant that we have saved (or are
// saving) all checkpoints going back MajorCheckpointSeconds.
//
// Inert   Recording:    -----------------------
// Standby Replaying #1: *---------*****
// Active  Replaying #2: -----*---------**
//
// Rewinding continues in this manner, alternating back and forth between the
// replaying process as the user continues going back in time.
//
// Inert   Recording:    -----------------------
// Active  Replaying #1: *---------**
// Standby Replaying #2: -----*****
//
// If the user starts navigating forward, the replaying processes both run
// forward and save checkpoints at the same major checkpoints as earlier.
// Note that this is how all forward execution works when there is no recording
// process (i.e. we started from a saved recording).
//
// Inert   Recording:    -----------------------
// Active  Replaying #1: *---------**------
// Standby Replaying #2: -----*****-----*--
//
// If the user pauses at a checkpoint or breakpoint in the replay, we again
// want to fill in all the checkpoints going back MajorCheckpointSeconds to
// allow smooth rewinding. This cannot be done simultaneously -- as it was when
// the recording process was active -- since we need to keep one of the
// replaying processes at an up to date point and be the active one. This falls
// on the one whose most recent major checkpoint is oldest, as the other is
// responsible for saving the most recent intermediate checkpoints.
//
// Inert   Recording:    -----------------------
// Active  Replaying #1: *---------**------
// Standby Replaying #2: -----*****-----***
//
// After the recent intermediate checkpoints have been saved the process which
// took them can become active so the older intermediate checkpoints can be
// saved.
//
// Inert   Recording:    -----------------------
// Standby Replaying #1: *---------*****
// Active  Replaying #2: -----*****-----***
//
// Finally, if the replay plays forward to the end of the recording (the point
// where the recording process is situated), the recording process takes over
// again as the active child and the user can resume interacting with a live
// process.
//
// Active  Recording:    ----------------------------------------
// Standby Replaying #1: *---------*****-----*---------*-------
// Standby Replaying #2: -----*****-----***-------*---------*--

// The current active child.
static ChildProcess* gActiveChild;

// The single recording child process, or null.
static ChildProcess* gRecordingChild;

// The two replaying child processes, null if they haven't been spawned yet.
// When rewinding is disabled, there is only a single replaying child, and zero
// replaying children if there is a recording child.
static ChildProcess* gFirstReplayingChild;
static ChildProcess* gSecondReplayingChild;

// Terminate all children and kill this process.
static void
Shutdown()
{
  delete gRecordingChild;
  delete gFirstReplayingChild;
  delete gSecondReplayingChild;
  _exit(0);
}

static ChildProcess*
OtherReplayingChild(ChildProcess* aChild)
{
  MOZ_RELEASE_ASSERT(!aChild->IsRecording() && gFirstReplayingChild && gSecondReplayingChild);
  return aChild == gFirstReplayingChild ? gSecondReplayingChild : gFirstReplayingChild;
}

static void
ForEachReplayingChild(const std::function<void(ChildProcess*)>& aCallback)
{
  if (gFirstReplayingChild) {
    aCallback(gFirstReplayingChild);
  }
  if (gSecondReplayingChild) {
    aCallback(gSecondReplayingChild);
  }
}

static void
PokeChildren()
{
  ForEachReplayingChild([=](ChildProcess* aChild) {
      if (aChild->IsPaused()) {
        aChild->Role()->Poke();
      }
    });
}

static void RecvHitCheckpoint(const HitCheckpointMessage& aMsg);
static void RecvHitBreakpoint(const HitBreakpointMessage& aMsg);
static void RecvHitRecordingEndpoint();
static void RecvDebuggerResponse(const DebuggerResponseMessage& aMsg);
static void RecvRecordingFlushed();
static void RecvAlwaysMarkMajorCheckpoints();

static PaintMessage* gLastPaint;

// The role taken by the active child.
class ChildRoleActive : public ChildRole
{
public:
  ChildRoleActive()
    : ChildRole(Active)
  {}

  void Initialize() override {
    gActiveChild = mProcess;

    mProcess->SendMessage(SetIsActiveMessage(true));

    // Always run forward from the primordial checkpoint. Otherwise, the
    // debugger hooks below determine how the active child changes.
    if (mProcess->LastCheckpoint() == InvalidCheckpointId) {
      mProcess->SendMessage(ResumeMessage(/* aForward = */ true));
    }
  }

  void OnIncomingMessage(const Message& aMsg) override {
    switch (aMsg.mType) {
    case MessageType::Paint:
      UpdateGraphicsInUIProcess((const PaintMessage&) aMsg);
      free(gLastPaint);
      gLastPaint = (PaintMessage*) aMsg.Clone();
      break;
    case MessageType::HitCheckpoint:
      RecvHitCheckpoint((const HitCheckpointMessage&) aMsg);
      break;
    case MessageType::HitBreakpoint:
      RecvHitBreakpoint((const HitBreakpointMessage&) aMsg);
      break;
    case MessageType::HitRecordingEndpoint:
      RecvHitRecordingEndpoint();
      break;
    case MessageType::DebuggerResponse:
      RecvDebuggerResponse((const DebuggerResponseMessage&) aMsg);
      break;
    case MessageType::RecordingFlushed:
      RecvRecordingFlushed();
      break;
    case MessageType::AlwaysMarkMajorCheckpoints:
      RecvAlwaysMarkMajorCheckpoints();
      break;
    default:
      MOZ_CRASH("Unexpected message");
    }
  }
};

bool
ActiveChildIsRecording()
{
  return gActiveChild->IsRecording();
}

// The last checkpoint included in the recording.
static size_t gLastRecordingCheckpoint;

// The role taken by replaying children trying to stay close to the active
// child and save either major or intermediate checkpoints, depending on
// whether the active child is paused or rewinding.
class ChildRoleStandby : public ChildRole
{
public:
  ChildRoleStandby()
    : ChildRole(Standby)
  {}

  void Initialize() override {
    MOZ_RELEASE_ASSERT(mProcess->IsPausedAtCheckpoint());
    mProcess->SendMessage(SetIsActiveMessage(false));
    Poke();
  }

  void OnIncomingMessage(const Message& aMsg) override {
    MOZ_RELEASE_ASSERT(aMsg.mType == MessageType::HitCheckpoint);
    Poke();
  }

  void Poke() override;
};

// The role taken by a recording child while another child is active.
class ChildRoleInert : public ChildRole
{
public:
  ChildRoleInert()
    : ChildRole(Inert)
  {}

  void Initialize() override {
    MOZ_RELEASE_ASSERT(mProcess->IsRecording() && mProcess->IsPaused());
  }

  void OnIncomingMessage(const Message& aMsg) override {
    MOZ_CRASH("Unexpected message from inert recording child");
  }
};

// Get the last major checkpoint for a process at or before aId, or InvalidCheckpointId.
static size_t
LastMajorCheckpointPreceding(ChildProcess* aChild, size_t aId)
{
  size_t last = InvalidCheckpointId;
  for (size_t majorCheckpoint : aChild->MajorCheckpoints()) {
    if (majorCheckpoint > aId) {
      break;
    }
    last = majorCheckpoint;
  }
  return last;
}

// Get the replaying process responsible for saving aId when rewinding: the one
// with the most recent major checkpoint preceding aId.
static ChildProcess*
ReplayingChildResponsibleForSavingCheckpoint(size_t aId)
{
  MOZ_RELEASE_ASSERT(CanRewind() && gFirstReplayingChild && gSecondReplayingChild);
  size_t firstMajor = LastMajorCheckpointPreceding(gFirstReplayingChild, aId);
  size_t secondMajor = LastMajorCheckpointPreceding(gSecondReplayingChild, aId);
  return (firstMajor < secondMajor) ? gSecondReplayingChild : gFirstReplayingChild;
}

// Return whether the active child is explicitly paused somewhere, or has
// started rewinding after being explicitly paused. Standby roles must save all
// intermediate checkpoints they are responsible for, in the range from their
// most recent major checkpoint up to the checkpoint where the active child can
// rewind to.
static bool ActiveChildIsPausedOrRewinding();

static void
MaybeClearSavedNonMajorCheckpoint(ChildProcess* aChild, size_t aCheckpoint)
{
  if (aChild->ShouldSaveCheckpoint(aCheckpoint) &&
      !aChild->IsMajorCheckpoint(aCheckpoint) &&
      aCheckpoint != FirstCheckpointId)
  {
    aChild->SendMessage(SetSaveCheckpointMessage(aCheckpoint, false));
  }
}

void
ChildRoleStandby::Poke()
{
  MOZ_RELEASE_ASSERT(mProcess->IsPausedAtCheckpoint());

  // Stay paused if we need to while the recording is flushed.
  if (mProcess->PauseNeeded()) {
    return;
  }

  // Check if we need to save a range of intermediate checkpoints.
  do {
    // Intermediate checkpoints are only saved when the active child is paused
    // or rewinding.
    if (!ActiveChildIsPausedOrRewinding()) {
      break;
    }

    // The startpoint of the range is the most recent major checkpoint prior to
    // the active child's position.
    size_t targetCheckpoint = gActiveChild->RewindTargetCheckpoint();
    size_t lastMajorCheckpoint = LastMajorCheckpointPreceding(mProcess, targetCheckpoint);

    // If there is no major checkpoint prior to the active child's position,
    // just idle.
    if (!lastMajorCheckpoint) {
      return;
    }

    // The endpoint of the range is the checkpoint prior to either the active
    // child's current position, or the other replaying child's most recent
    // major checkpoint.
    size_t otherMajorCheckpoint =
      LastMajorCheckpointPreceding(OtherReplayingChild(mProcess), targetCheckpoint);
    if (otherMajorCheckpoint > lastMajorCheckpoint && otherMajorCheckpoint <= targetCheckpoint) {
      targetCheckpoint = otherMajorCheckpoint - 1;
    }

    // If we haven't reached the last major checkpoint, we need to run forward
    // without saving intermediate checkpoints.
    if (mProcess->LastCheckpoint() < lastMajorCheckpoint) {
      break;
    }

    // Find the first checkpoint in the fill range which we have not saved.
    Maybe<size_t> missingCheckpoint;
    for (size_t i = lastMajorCheckpoint; i <= targetCheckpoint; i++) {
      if (!mProcess->HasSavedCheckpoint(i)) {
        missingCheckpoint.emplace(i);
        break;
      }
    }

    // If we have already saved everything we need to, we can idle.
    if (!missingCheckpoint.isSome()) {
      return;
    }

    // Since we always save major checkpoints, we must have saved the
    // checkpoint prior to the missing one and can restore it.
    size_t restoreTarget = missingCheckpoint.ref() - 1;
    MOZ_RELEASE_ASSERT(mProcess->HasSavedCheckpoint(restoreTarget));

    // If we need to rewind to the restore target, do so.
    if (mProcess->LastCheckpoint() != restoreTarget) {
      mProcess->SendMessage(RestoreCheckpointMessage(restoreTarget));
      return;
    }

    // Otherwise, run forward to the next checkpoint and save it.
    if (!mProcess->ShouldSaveCheckpoint(missingCheckpoint.ref())) {
      mProcess->SendMessage(SetSaveCheckpointMessage(missingCheckpoint.ref(), true));
    }
    mProcess->SendMessage(ResumeMessage(/* aForward = */ true));
    return;
  } while (false);

  // Run forward until we reach either the active child's position, or the last
  // checkpoint included in the on-disk recording. Only save major checkpoints.
  if ((mProcess->LastCheckpoint() < gActiveChild->LastCheckpoint()) &&
      (!gRecordingChild || mProcess->LastCheckpoint() < gLastRecordingCheckpoint)) {
    MaybeClearSavedNonMajorCheckpoint(mProcess, mProcess->LastCheckpoint() + 1);
    mProcess->SendMessage(ResumeMessage(/* aForward = */ true));
  }
}

///////////////////////////////////////////////////////////////////////////////
// Major Checkpoints
///////////////////////////////////////////////////////////////////////////////

// For each checkpoint N, this vector keeps track of the time intervals taken
// for the active child (excluding idle time) to run from N to N+1.
static StaticInfallibleVector<TimeDuration> gCheckpointTimes;

// How much time has elapsed (per gCheckpointTimes) since the last flush or
// major checkpoint was noted.
static TimeDuration gTimeSinceLastFlush;
static TimeDuration gTimeSinceLastMajorCheckpoint;

// The replaying process that was given the last major checkpoint.
static ChildProcess* gLastAssignedMajorCheckpoint;

// For testing, mark new major checkpoints as frequently as possible.
static bool gAlwaysMarkMajorCheckpoints;

static void
RecvAlwaysMarkMajorCheckpoints()
{
  gAlwaysMarkMajorCheckpoints = true;
}

static void
AssignMajorCheckpoint(ChildProcess* aChild, size_t aId)
{
  PrintSpew("AssignMajorCheckpoint: Process %d Checkpoint %d\n",
            (int) aChild->GetId(), (int) aId);
  aChild->AddMajorCheckpoint(aId);
  if (aId != FirstCheckpointId) {
    aChild->WaitUntilPaused();
    aChild->SendMessage(SetSaveCheckpointMessage(aId, true));
  }
  gLastAssignedMajorCheckpoint = aChild;
}

static void FlushRecording();

static void
UpdateCheckpointTimes(const HitCheckpointMessage& aMsg)
{
  if (!CanRewind() || (aMsg.mCheckpointId != gCheckpointTimes.length() + 1)) {
    return;
  }
  gCheckpointTimes.append(TimeDuration::FromMicroseconds(aMsg.mDurationMicroseconds));

  if (gActiveChild->IsRecording()) {
    gTimeSinceLastFlush += gCheckpointTimes.back();

    // Occasionally flush while recording so replaying processes stay
    // reasonably current.
    if (aMsg.mCheckpointId == FirstCheckpointId ||
        gTimeSinceLastFlush >= TimeDuration::FromSeconds(FlushSeconds))
    {
      FlushRecording();
      gTimeSinceLastFlush = 0;
    }
  }

  gTimeSinceLastMajorCheckpoint += gCheckpointTimes.back();
  if (gTimeSinceLastMajorCheckpoint >= TimeDuration::FromSeconds(MajorCheckpointSeconds) ||
      gAlwaysMarkMajorCheckpoints)
  {
    // Alternate back and forth between assigning major checkpoints to the
    // two replaying processes.
    MOZ_RELEASE_ASSERT(gLastAssignedMajorCheckpoint);
    ChildProcess* child = OtherReplayingChild(gLastAssignedMajorCheckpoint);
    AssignMajorCheckpoint(child, aMsg.mCheckpointId + 1);
    gTimeSinceLastMajorCheckpoint = 0;
  }
}

///////////////////////////////////////////////////////////////////////////////
// Role Management
///////////////////////////////////////////////////////////////////////////////

static void
SpawnRecordingChild()
{
  MOZ_RELEASE_ASSERT(!gRecordingChild && !gFirstReplayingChild && !gSecondReplayingChild);
  gRecordingChild = new ChildProcess(new ChildRoleActive(), /* aRecording = */ true);
}

static void
SpawnSingleReplayingChild()
{
  MOZ_RELEASE_ASSERT(!gRecordingChild && !gFirstReplayingChild && !gSecondReplayingChild);
  gFirstReplayingChild = new ChildProcess(new ChildRoleActive(), /* aRecording = */ false);
}

static void
SpawnReplayingChildren()
{
  MOZ_RELEASE_ASSERT(CanRewind() && !gFirstReplayingChild && !gSecondReplayingChild);
  ChildRole* firstRole = gRecordingChild
                         ? (ChildRole*) new ChildRoleStandby()
                         : (ChildRole*) new ChildRoleActive();
  gFirstReplayingChild = new ChildProcess(firstRole, /* aRecording = */ false);
  gSecondReplayingChild = new ChildProcess(new ChildRoleStandby(), /* aRecording = */ false);
  AssignMajorCheckpoint(gSecondReplayingChild, FirstCheckpointId);
}

// Change the current active child, and select a new role for the old one.
static void
SwitchActiveChild(ChildProcess* aChild)
{
  MOZ_RELEASE_ASSERT(aChild != gActiveChild);
  ChildProcess* oldActiveChild = gActiveChild;
  aChild->WaitUntilPaused();
  if (!aChild->IsRecording()) {
    aChild->Recover(gActiveChild);
  }
  aChild->SetRole(new ChildRoleActive());
  if (oldActiveChild->IsRecording()) {
    oldActiveChild->SetRole(new ChildRoleInert());
  } else {
    oldActiveChild->RecoverToCheckpoint(oldActiveChild->MostRecentSavedCheckpoint());
    oldActiveChild->SetRole(new ChildRoleStandby());
  }
}

///////////////////////////////////////////////////////////////////////////////
// Saving Recordings
///////////////////////////////////////////////////////////////////////////////

static void
FlushRecording()
{
  MOZ_RELEASE_ASSERT(NS_IsMainThread());
  MOZ_RELEASE_ASSERT(gActiveChild->IsRecording() && gActiveChild->IsPaused());

  ForEachReplayingChild([=](ChildProcess* aChild) {
      aChild->SetPauseNeeded();
      aChild->WaitUntilPaused();
    });

  gActiveChild->SendMessage(FlushRecordingMessage());
  gActiveChild->WaitUntilPaused();

  gLastRecordingCheckpoint = gActiveChild->LastCheckpoint();

  // We now have a usable recording for replaying children.
  static bool gHasFlushed = false;
  if (!gHasFlushed && CanRewind()) {
    SpawnReplayingChildren();
  }
  gHasFlushed = true;
}

static void
RecvRecordingFlushed()
{
  MOZ_RELEASE_ASSERT(NS_IsMainThread());
  ForEachReplayingChild([=](ChildProcess* aChild) { aChild->ClearPauseNeeded(); });
}

// Recording children can idle indefinitely while waiting for input, without
// creating a checkpoint. If this might be a problem, this method induces the
// child to create a new checkpoint and pause.
static void
MaybeCreateCheckpointInRecordingChild()
{
  if (gActiveChild->IsRecording() && !gActiveChild->IsPaused()) {
    gActiveChild->SendMessage(CreateCheckpointMessage());
  }
}

// Send a message to the message manager in the UI process. This is consumed by
// various tests.
static void
SendMessageToUIProcess(const char* aMessage)
{
  AutoSafeJSContext cx;
  dom::ProcessGlobal* cpmm = dom::ProcessGlobal::Get();
  ErrorResult err;
  nsAutoString message;
  message.Append(NS_ConvertUTF8toUTF16(aMessage));
  JS::Rooted<JS::Value> undefined(cx);
  cpmm->SendAsyncMessage(cx, message, undefined, nullptr, nullptr, undefined, err);
  MOZ_RELEASE_ASSERT(!err.Failed());
  err.SuppressException();
}

static void
SaveRecordingInternal(char* aFilename)
{
  MOZ_RELEASE_ASSERT(gRecordingChild);

  if (gRecordingChild == gActiveChild) {
    // The recording might not be up to date, flush it now.
    MOZ_RELEASE_ASSERT(gRecordingChild == gActiveChild);
    MaybeCreateCheckpointInRecordingChild();
    gRecordingChild->WaitUntilPaused();
    FlushRecording();
  }

  // Copy the file's contents to the new file.
  int readfd = DirectOpenFile(gRecordingFilename, /* aWriting = */ false);
  int writefd = DirectOpenFile(aFilename, /* aWriting = */ true);
  char buf[4096];
  while (true) {
    size_t n = DirectRead(readfd, buf, sizeof(buf));
    if (!n) {
      break;
    }
    DirectWrite(writefd, buf, n);
  }
  DirectCloseFile(readfd);
  DirectCloseFile(writefd);

  PrintSpew("Copied Recording %s\n", aFilename);
  SendMessageToUIProcess("SaveRecordingFinished");
  free(aFilename);
}

void
SaveRecording(const nsCString& aFilename)
{
  MOZ_RELEASE_ASSERT(IsMiddleman());

  char* filename = strdup(aFilename.get());
  MainThreadMessageLoop()->PostTask(NewRunnableFunction("SaveRecordingInternal",
                                                        SaveRecordingInternal, filename));
}

///////////////////////////////////////////////////////////////////////////////
// Explicit Pauses
///////////////////////////////////////////////////////////////////////////////

// At the last time the active child was explicitly paused, the ID of the
// checkpoint that needs to be saved for the child to rewind.
static size_t gLastExplicitPause;

static bool
HasSavedCheckpointsInRange(ChildProcess* aChild, size_t aStart, size_t aEnd)
{
  for (size_t i = aStart; i <= aEnd; i++) {
    if (!aChild->HasSavedCheckpoint(i)) {
      return false;
    }
  }
  return true;
}

static void
MarkActiveChildExplicitPause()
{
  MOZ_RELEASE_ASSERT(gActiveChild->IsPaused());
  size_t targetCheckpoint = gActiveChild->RewindTargetCheckpoint();

  if (gActiveChild->IsRecording()) {
    // Make sure any replaying children can play forward to the same point as
    // the recording.
    FlushRecording();

    // When paused at a breakpoint, the JS debugger may (indeed, will) send
    // requests to the recording child which can affect the recording. These
    // side effects won't be replayed later on, so the C++ side of the debugger
    // will not provide a useful answer to these requests, reporting an
    // unhandled divergence instead. To avoid this issue and provide a
    // consistent debugger experience whether still recording or replaying, we
    // switch the active child to a replaying child when pausing at a
    // breakpoint.
    if (CanRewind() && !gActiveChild->IsPausedAtCheckpoint()) {
      ChildProcess* child =
        OtherReplayingChild(ReplayingChildResponsibleForSavingCheckpoint(targetCheckpoint));
      SwitchActiveChild(child);
    }
  } else if (CanRewind()) {
    // Make sure we have a replaying child that can rewind from this point.
    // Switch to the other one if (a) this process is responsible for rewinding
    // from this point, and (b) this process has not saved all intermediate
    // checkpoints going back to its last major checkpoint.
    if (gActiveChild == ReplayingChildResponsibleForSavingCheckpoint(targetCheckpoint)) {
      size_t lastMajorCheckpoint = LastMajorCheckpointPreceding(gActiveChild, targetCheckpoint);
      if (!HasSavedCheckpointsInRange(gActiveChild, lastMajorCheckpoint, targetCheckpoint)) {
        SwitchActiveChild(OtherReplayingChild(gActiveChild));
      }
    }
  }

  gLastExplicitPause = targetCheckpoint;
  PrintSpew("MarkActiveChildExplicitPause %d\n", (int) gLastExplicitPause);

  PokeChildren();
}

static bool
ActiveChildIsPausedOrRewinding()
{
  return gActiveChild->RewindTargetCheckpoint() <= gLastExplicitPause;
}

///////////////////////////////////////////////////////////////////////////////
// IPDL Forwarding
///////////////////////////////////////////////////////////////////////////////

// Monitor for synchronizing the main and message forwarding threads.
static Monitor* gCommunicationMonitor;

// Message loop processed on the main thread.
static MessageLoop* gMainThreadMessageLoop;

MessageLoop*
MainThreadMessageLoop()
{
  return gMainThreadMessageLoop;
}

// The routing IDs of all destroyed browsers in the parent process.
static StaticInfallibleVector<int32_t> gDeadBrowsers;

// Return whether a message from the child process to the UI process is being
// sent to a target that is being destroyed, and should be suppressed.
static bool
MessageTargetIsDead(const IPC::Message& aMessage)
{
  // After the parent process destroys a browser, we handle the destroy in
  // both the middleman and child processes. Both processes will respond to
  // the destroy by sending additional messages to the UI process indicating
  // the browser has been destroyed, but we need to ignore such messages from
  // the child process (if it is still recording) to avoid confusing the UI
  // process.
  if (aMessage.type() >= dom::PBrowser::PBrowserStart && aMessage.type() <= dom::PBrowser::PBrowserEnd) {
    for (int32_t id : gDeadBrowsers) {
      if (id == aMessage.routing_id()) {
        return true;
      }
    }
  }
  return false;
}

static bool
HandleMessageInMiddleman(ipc::Side aSide, const IPC::Message& aMessage)
{
  // Ignore messages sent from the child to dead UI process targets.
  if (aSide == ipc::ParentSide) {
    return MessageTargetIsDead(aMessage);
  }

  IPC::Message::msgid_t type = aMessage.type();

  // Handle messages that should be sent to both the middleman and the
  // content process.
  if (type == dom::PContent::Msg_PBrowserConstructor__ID ||
      type == dom::PContent::Msg_RegisterChrome__ID ||
      type == dom::PContent::Msg_SetXPCOMProcessAttributes__ID ||
      type == dom::PBrowser::Msg_SetDocShellIsActive__ID ||
      type == dom::PBrowser::Msg_PRenderFrameConstructor__ID ||
      type == dom::PBrowser::Msg_InitRendering__ID ||
      type == dom::PBrowser::Msg_RenderLayers__ID ||
      type == dom::PBrowser::Msg_LoadRemoteScript__ID ||
      type == dom::PBrowser::Msg_AsyncMessage__ID ||
      type == dom::PBrowser::Msg_Destroy__ID) {
    ipc::IProtocol::Result r = dom::ContentChild::GetSingleton()->PContentChild::OnMessageReceived(aMessage);
    MOZ_RELEASE_ASSERT(r == ipc::IProtocol::MsgProcessed);
    if (type == dom::PContent::Msg_SetXPCOMProcessAttributes__ID) {
      // Preferences are initialized via the SetXPCOMProcessAttributes message.
      PreferencesLoaded();

      if (!gRecordingChild) {
        if (CanRewind()) {
          SpawnReplayingChildren();
        } else {
          SpawnSingleReplayingChild();
        }
      }
    }
    if (type == dom::PBrowser::Msg_Destroy__ID) {
      gDeadBrowsers.append(aMessage.routing_id());
    }
    if (type == dom::PBrowser::Msg_RenderLayers__ID && gLastPaint) {
      UpdateGraphicsInUIProcess(*gLastPaint);
    }
    return false;
  }

  // Handle messages that should only be sent to the middleman.
  if (type == dom::PContent::Msg_InitRendering__ID ||
      type == dom::PContent::Msg_SaveRecording__ID ||
      type == dom::PContent::Msg_Shutdown__ID) {
    ipc::IProtocol::Result r = dom::ContentChild::GetSingleton()->PContentChild::OnMessageReceived(aMessage);
    MOZ_RELEASE_ASSERT(r == ipc::IProtocol::MsgProcessed);
    return true;
  }

  if (type >= layers::PCompositorBridge::PCompositorBridgeStart &&
      type <= layers::PCompositorBridge::PCompositorBridgeEnd) {
    layers::CompositorBridgeChild* compositorChild = layers::CompositorBridgeChild::Get();
    ipc::IProtocol::Result r = compositorChild->OnMessageReceived(aMessage);
    MOZ_RELEASE_ASSERT(r == ipc::IProtocol::MsgProcessed);
    return true;
  }

  return false;
}

class MiddlemanProtocol : public ipc::IToplevelProtocol
{
public:
  ipc::MessageChannel mChannel;
  ipc::Side mSide;
  MiddlemanProtocol* mOpposite;
  MessageLoop* mOppositeMessageLoop;

  explicit MiddlemanProtocol(ipc::Side aSide)
    : ipc::IToplevelProtocol(PContentMsgStart, aSide)
    , mChannel("MiddlemanProtocol", this)
    , mSide(aSide)
    , mOpposite(nullptr)
  {
    SetIPCChannel(&mChannel);
  }

  virtual void RemoveManagee(int32_t, IProtocol*) override {
    MOZ_CRASH("MiddlemanProtocol::RemoveManagee");
  }

  virtual const char* ProtocolName() const override {
    MOZ_CRASH("MiddlemanProtocol::ProtocolName");
  }

  static void ForwardMessageAsync(MiddlemanProtocol* aProtocol, Message* aMessage) {
    if (gActiveChild->IsRecording()) {
      PrintSpew("ForwardAsyncMsg %s\n", IPC::StringFromIPCMessageType(aMessage->type()));
      if (!aProtocol->mChannel.Send(aMessage)) {
        MOZ_CRASH("MiddlemanProtocol::ForwardMessageAsync");
      }
    } else {
      delete aMessage;
    }
  }

  virtual Result OnMessageReceived(const Message& aMessage) override {
    // If we do not have a recording process then just see if the message can
    // be handled in the middleman.
    if (!mOppositeMessageLoop) {
      MOZ_RELEASE_ASSERT(mSide == ipc::ChildSide);
      HandleMessageInMiddleman(mSide, aMessage);
      return MsgProcessed;
    }

    // Copy the message first, since HandleMessageInMiddleman may destructively
    // modify it through OnMessageReceived calls.
    Message* nMessage = new Message();
    nMessage->CopyFrom(aMessage);

    if (HandleMessageInMiddleman(mSide, aMessage)) {
      delete nMessage;
      return MsgProcessed;
    }

    mOppositeMessageLoop->PostTask(NewRunnableFunction("ForwardMessageAsync", ForwardMessageAsync,
                                                       mOpposite, nMessage));
    return MsgProcessed;
  }

  static void ForwardMessageSync(MiddlemanProtocol* aProtocol, Message* aMessage, Message** aReply) {
    PrintSpew("ForwardSyncMsg %s\n", IPC::StringFromIPCMessageType(aMessage->type()));

    MOZ_RELEASE_ASSERT(!*aReply);
    Message* nReply = new Message();
    if (!aProtocol->mChannel.Send(aMessage, nReply)) {
      MOZ_CRASH("MiddlemanProtocol::ForwardMessageSync");
    }

    MonitorAutoLock lock(*gCommunicationMonitor);
    *aReply = nReply;
    gCommunicationMonitor->Notify();
  }

  virtual Result OnMessageReceived(const Message& aMessage, Message*& aReply) override {
    MOZ_RELEASE_ASSERT(mSide == ipc::ParentSide);
    MOZ_RELEASE_ASSERT(!MessageTargetIsDead(aMessage));

    Message* nMessage = new Message();
    nMessage->CopyFrom(aMessage);
    mOppositeMessageLoop->PostTask(NewRunnableFunction("ForwardMessageSync", ForwardMessageSync,
                                                       mOpposite, nMessage, &aReply));

    MonitorAutoLock lock(*gCommunicationMonitor);
    while (!aReply) {
      gCommunicationMonitor->Wait();
    }
    return MsgProcessed;
  }

  static void ForwardCallMessage(MiddlemanProtocol* aProtocol, Message* aMessage, Message** aReply) {
    PrintSpew("ForwardSyncCall %s\n", IPC::StringFromIPCMessageType(aMessage->type()));

    MOZ_RELEASE_ASSERT(!*aReply);
    Message* nReply = new Message();
    if (!aProtocol->mChannel.Call(aMessage, nReply)) {
      MOZ_CRASH("MiddlemanProtocol::ForwardCallMessage");
    }

    MonitorAutoLock lock(*gCommunicationMonitor);
    *aReply = nReply;
    gCommunicationMonitor->Notify();
  }

  virtual Result OnCallReceived(const Message& aMessage, Message*& aReply) override {
    MOZ_RELEASE_ASSERT(mSide == ipc::ParentSide);
    MOZ_RELEASE_ASSERT(!MessageTargetIsDead(aMessage));

    Message* nMessage = new Message();
    nMessage->CopyFrom(aMessage);
    mOppositeMessageLoop->PostTask(NewRunnableFunction("ForwardCallMessage", ForwardCallMessage,
                                                       mOpposite, nMessage, &aReply));

    MonitorAutoLock lock(*gCommunicationMonitor);
    while (!aReply)
      gCommunicationMonitor->Wait();
    return MsgProcessed;
  }

  virtual int32_t GetProtocolTypeId() override {
    MOZ_CRASH("MiddlemanProtocol::GetProtocolTypeId");
  }

  virtual void OnChannelClose() override {
    MOZ_RELEASE_ASSERT(mSide == ipc::ChildSide);
    gMainThreadMessageLoop->PostTask(NewRunnableFunction("Shutdown", Shutdown));
  }

  virtual void OnChannelError() override {
    MOZ_CRASH("MiddlemanProtocol::OnChannelError");
  }
};

static MiddlemanProtocol* gChildProtocol;
static MiddlemanProtocol* gParentProtocol;

ipc::MessageChannel*
ChannelToUIProcess()
{
  return gChildProtocol->GetIPCChannel();
}

// Message loop for forwarding messages between the parent process and a
// recording process.
static MessageLoop* gForwardingMessageLoop;

static bool gParentProtocolOpened = false;

// Main routine for the forwarding message loop thread.
static void
ForwardingMessageLoopMain(void*)
{
  MOZ_RELEASE_ASSERT(gActiveChild->IsRecording());

  MessageLoop messageLoop;
  gForwardingMessageLoop = &messageLoop;

  gChildProtocol->mOppositeMessageLoop = gForwardingMessageLoop;

  gParentProtocol->Open(gActiveChild->Process()->GetChannel(),
                        base::GetProcId(gActiveChild->Process()->GetChildProcessHandle()));

  // Notify the main thread that we have finished initialization.
  {
    MonitorAutoLock lock(*gCommunicationMonitor);
    gParentProtocolOpened = true;
    gCommunicationMonitor->Notify();
  }

  messageLoop.Run();
}

// Initialize hooks used by the debugger.
static void InitDebuggerHooks();

// Contents of the prefs shmem block that is sent to the child on startup.
static char* gShmemPrefs;
static size_t gShmemPrefsLen;

void
NotePrefsShmemContents(char* aPrefs, size_t aPrefsLen)
{
  MOZ_RELEASE_ASSERT(!gShmemPrefs);
  gShmemPrefs = new char[aPrefsLen];
  memcpy(gShmemPrefs, aPrefs, aPrefsLen);
  gShmemPrefsLen = aPrefsLen;
}

void
Initialize(int aArgc, char* aArgv[], base::ProcessId aParentPid, uint64_t aChildID,
           dom::ContentChild* aContentChild)
{
  MOZ_RELEASE_ASSERT(NS_IsMainThread());
  MOZ_RELEASE_ASSERT(gShmemPrefs);

  // Construct the message that will be sent to each child when starting up.
  gIntroductionMessage = IntroductionMessage::New(aParentPid, gShmemPrefs, gShmemPrefsLen, aArgc, aArgv);

  MOZ_RELEASE_ASSERT(gProcessKind == ProcessKind::MiddlemanRecording ||
                     gProcessKind == ProcessKind::MiddlemanReplaying);

  // Use a temporary file for the recording if the filename is unspecified.
  if (!strcmp(gRecordingFilename, "*")) {
    MOZ_RELEASE_ASSERT(gProcessKind == ProcessKind::MiddlemanRecording);
    free(gRecordingFilename);
    gRecordingFilename = mktemp(strdup("/tmp/RecordingXXXXXX"));
  }

  InitDebuggerHooks();

  gCommunicationMonitor = new Monitor();

  gMainThreadMessageLoop = MessageLoop::current();

  gChildProtocol = new MiddlemanProtocol(ipc::ChildSide);

  if (gProcessKind == ProcessKind::MiddlemanRecording) {
    gParentProtocol = new MiddlemanProtocol(ipc::ParentSide);
    gParentProtocol->mOpposite = gChildProtocol;
    gChildProtocol->mOpposite = gParentProtocol;

    gParentProtocol->mOppositeMessageLoop = gMainThreadMessageLoop;

    SpawnRecordingChild();

    if (!PR_CreateThread(PR_USER_THREAD, ForwardingMessageLoopMain, nullptr,
                         PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD, PR_JOINABLE_THREAD, 0)) {
      MOZ_CRASH("parent::Initialize");
    }

    // Wait for the forwarding message loop thread to finish initialization.
    {
      MonitorAutoLock lock(*gCommunicationMonitor);
      while (!gParentProtocolOpened) {
        gCommunicationMonitor->Wait();
      }
    }
  }

  if (!aContentChild->Init(ipc::IOThreadChild::message_loop(), aParentPid,
                           ipc::IOThreadChild::channel(), aChildID, /* aIsForBrowser = */ true)) {
    MOZ_CRASH("parent::Initialize");
  }
}

///////////////////////////////////////////////////////////////////////////////
// Debugger Messages
///////////////////////////////////////////////////////////////////////////////

// Buffer for receiving the next debugger response.
static JS::replay::CharBuffer* gResponseBuffer;

static void
RecvDebuggerResponse(const DebuggerResponseMessage& aMsg)
{
  MOZ_RELEASE_ASSERT(gResponseBuffer && gResponseBuffer->empty());
  if (!gResponseBuffer->append(aMsg.Buffer(), aMsg.BufferSize())) {
    MOZ_CRASH("RecvDebuggerResponse");
  }
}

static void
HookDebuggerRequest(const JS::replay::CharBuffer& aBuffer, JS::replay::CharBuffer* aResponse)
{
  MaybeCreateCheckpointInRecordingChild();
  gActiveChild->WaitUntilPaused();

  MOZ_RELEASE_ASSERT(!gResponseBuffer);
  gResponseBuffer = aResponse;

  DebuggerRequestMessage* msg = DebuggerRequestMessage::New(aBuffer.begin(), aBuffer.length());
  gActiveChild->SendMessage(*msg);
  free(msg);

  // Wait for the child to respond to the query.
  gActiveChild->WaitUntilPaused();
  MOZ_RELEASE_ASSERT(gResponseBuffer == aResponse);
  MOZ_RELEASE_ASSERT(gResponseBuffer->length() != 0);
  gResponseBuffer = nullptr;
}

static void
HookSetBreakpoint(size_t aId, const JS::replay::ExecutionPosition& aPosition)
{
  MaybeCreateCheckpointInRecordingChild();
  gActiveChild->WaitUntilPaused();

  gActiveChild->SendMessage(SetBreakpointMessage(aId, aPosition));

  // Also set breakpoints in any recording child that is not currently active.
  // We can't recover recording processes so need to keep their breakpoints up
  // to date.
  if (!gActiveChild->IsRecording() && gRecordingChild) {
    gRecordingChild->SendMessage(SetBreakpointMessage(aId, aPosition));
  }
}

// Flags for the preferred direction of travel when execution unpauses,
// according to the last direction we were explicitly given.
static bool gChildExecuteForward = true;
static bool gChildExecuteBackward = false;

// Whether there is a ResumeForwardOrBackward task which should execute on the
// main thread. This will continue execution in the preferred direction.
static bool gResumeForwardOrBackward = false;

static void
HookResume(bool aForward)
{
  gActiveChild->WaitUntilPaused();

  // Set the preferred direction of travel.
  gResumeForwardOrBackward = false;
  gChildExecuteForward = aForward;
  gChildExecuteBackward = !aForward;

  // When rewinding, make sure the active child can rewind to the previous
  // checkpoint.
  if (!aForward && !gActiveChild->HasSavedCheckpoint(gActiveChild->RewindTargetCheckpoint())) {
    MOZ_RELEASE_ASSERT(ActiveChildIsPausedOrRewinding());
    size_t targetCheckpoint = gActiveChild->RewindTargetCheckpoint();

    // Don't rewind if we are at the beginning of the recording.
    if (targetCheckpoint == InvalidCheckpointId) {
      SendMessageToUIProcess("HitRecordingBeginning");
      return;
    }

    // Find the replaying child responsible for saving the target checkpoint.
    // We should have explicitly paused before rewinding and given fill roles
    // to the replaying children.
    ChildProcess* targetChild = ReplayingChildResponsibleForSavingCheckpoint(targetCheckpoint);
    MOZ_RELEASE_ASSERT(targetChild != gActiveChild);

    // This process will be the new active child, make sure it has saved the
    // checkpoint we need it to.
    targetChild->WaitUntil([=]() {
        return targetChild->HasSavedCheckpoint(targetCheckpoint)
            && targetChild->IsPaused();
      });

    SwitchActiveChild(targetChild);
  }

  if (aForward) {
    MaybeClearSavedNonMajorCheckpoint(gActiveChild, gActiveChild->LastCheckpoint() + 1);

    // Idle children might change their behavior as we run forward.
    PokeChildren();
  }

  gActiveChild->SendMessage(ResumeMessage(aForward));
}

static void
HookPause()
{
  MaybeCreateCheckpointInRecordingChild();
  gActiveChild->WaitUntilPaused();

  // If the debugger has explicitly paused then there is no preferred direction
  // of travel.
  gChildExecuteForward = false;
  gChildExecuteBackward = false;

  MarkActiveChildExplicitPause();
}

static void
ResumeForwardOrBackward()
{
  MOZ_RELEASE_ASSERT(!gChildExecuteForward || !gChildExecuteBackward);

  if (gResumeForwardOrBackward && (gChildExecuteForward || gChildExecuteBackward)) {
    HookResume(gChildExecuteForward);
  }
}

static void
RecvHitCheckpoint(const HitCheckpointMessage& aMsg)
{
  UpdateCheckpointTimes(aMsg);

  // Resume either forwards or backwards. Break the resume off into a separate
  // runnable, to avoid starving any code already on the stack and waiting for
  // the process to pause.
  if (!gResumeForwardOrBackward) {
    gResumeForwardOrBackward = true;
    gMainThreadMessageLoop->PostTask(NewRunnableFunction("ResumeForwardOrBackward",
                                                         ResumeForwardOrBackward));
  }
}

static void
HitBreakpoint(uint32_t* aBreakpoints, size_t aNumBreakpoints)
{
  MarkActiveChildExplicitPause();

  MOZ_RELEASE_ASSERT(!gResumeForwardOrBackward);
  gResumeForwardOrBackward = true;

  // Call breakpoint handlers until one of them explicitly resumes forward or
  // backward travel.
  for (size_t i = 0; i < aNumBreakpoints && gResumeForwardOrBackward; i++) {
    // FIXME what happens if this throws?
    AutoSafeJSContext cx;
    (void) JS::replay::hooks.hitBreakpointMiddleman(cx, aBreakpoints[i]);
  }

  // If the child was not explicitly resumed by any breakpoint handler, resume
  // travel in whichever direction it was going previously.
  if (gResumeForwardOrBackward) {
    ResumeForwardOrBackward();
  }

  delete[] aBreakpoints;
}

static void
RecvHitBreakpoint(const HitBreakpointMessage& aMsg)
{
  uint32_t* breakpoints = new uint32_t[aMsg.NumBreakpoints()];
  PodCopy(breakpoints, aMsg.Breakpoints(), aMsg.NumBreakpoints());
  gMainThreadMessageLoop->PostTask(NewRunnableFunction("HitBreakpoint", HitBreakpoint,
                                                       breakpoints, aMsg.NumBreakpoints()));
}

static void
RecvHitRecordingEndpoint()
{
  // The active replaying child tried to run off the end of the recording.
  MOZ_RELEASE_ASSERT(!gActiveChild->IsRecording());

  // Look for a recording child we can transition into.
  if (!gRecordingChild) {
    MarkActiveChildExplicitPause();
    SendMessageToUIProcess("HitRecordingEndpoint");
    return;
  }

  // Switch to the recording child as the active child and continue execution.
  SwitchActiveChild(gRecordingChild);
  gActiveChild->SendMessage(ResumeMessage(/* aForward = */ true));
}

static void
InitDebuggerHooks()
{
  JS::replay::hooks.debugRequestMiddleman = HookDebuggerRequest;
  JS::replay::hooks.setBreakpointMiddleman = HookSetBreakpoint;
  JS::replay::hooks.resumeMiddleman = HookResume;
  JS::replay::hooks.pauseMiddleman = HookPause;
  JS::replay::hooks.canRewindMiddleman = CanRewind;
}

} // namespace parent
} // namespace recordreplay
} // namespace mozilla
