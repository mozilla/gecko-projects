/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_toolkit_recordreplay_ipc_ParentInternal_h
#define mozilla_toolkit_recordreplay_ipc_ParentInternal_h

#include "ParentIPC.h"

#include "mozilla/ipc/GeckoChildProcessHost.h"

namespace mozilla {
namespace recordreplay {
namespace parent {

// This file has internal declarations for interaction between different
// components of middleman logic.

class ChildProcess;

// Get the message loop for the main thread.
MessageLoop* MainThreadMessageLoop();

// Return whether replaying processes are allowed to save checkpoints and
// rewind.
bool CanRewind();

// Whether the child currently being interacted with is recording.
bool ActiveChildIsRecording();

// Monitor used for synchronizing between the main and channel or message loop threads.
static Monitor* gMonitor;

///////////////////////////////////////////////////////////////////////////////
// Graphics
///////////////////////////////////////////////////////////////////////////////

extern mach_port_t gGraphicsPort;
extern void* gGraphicsMemory;

void InitializeGraphicsMemory();

// Update the graphics painted in the UI process, per painting data received
// from a child process, or null for the last paint performed.
void UpdateGraphicsInUIProcess(const PaintMessage* aMsg);

///////////////////////////////////////////////////////////////////////////////
// Child Processes
///////////////////////////////////////////////////////////////////////////////

// Information about the role which a child process is fulfilling, and governs
// how the process responds to incoming messages.
class ChildRole
{
public:
  // See ParentIPC.cpp for the meaning of these role types.
#define ForEachRoleType(Macro)                      \
  Macro(Active)                                     \
  Macro(Standby)                                    \
  Macro(Inert)

  enum Type {
#define DefineType(Name) Name,
    ForEachRoleType(DefineType)
#undef DefineType
  };

  static const char* TypeString(Type aType) {
    switch (aType) {
#define GetTypeString(Name) case Name: return #Name;
    ForEachRoleType(GetTypeString)
#undef GetTypeString
    default: MOZ_CRASH("Bad ChildRole type");
    }
  }

protected:
  ChildProcess* mProcess;
  Type mType;

  ChildRole(Type aType)
    : mProcess(nullptr), mType(aType)
  {}

public:
  void SetProcess(ChildProcess* aProcess) {
    MOZ_RELEASE_ASSERT(!mProcess);
    mProcess = aProcess;
  }
  Type GetType() const { return mType; }

  virtual ~ChildRole() {}

  // These are called on the main thread.
  virtual void Initialize() {}
  virtual void Poke() {}
  virtual void OnIncomingMessage(const Message& aMsg) = 0;
};

// Information about a recording or replaying child process.
class ChildProcess
{
  // Handle for the process.
  ipc::GeckoChildProcessHost* mProcess;

  // Channel for communicating with the process.
  Channel* mChannel;

  // The last time we sent or received a message from this process.
  TimeStamp mLastMessageTime;

  // Whether this process is recording.
  bool mRecording;

  // The current recovery stage of this process. When recovering, the child
  // process might not be in the exact place reflected by the state below, but
  // we will shepherd it to that spot and not be able to send or receive
  // messages until it does so.
  enum class RecoveryStage {
    None,
    ReachingCheckpoint,
    PlayingMessages
  };
  RecoveryStage mRecoveryStage;

  // Whether the process is currently paused.
  bool mPaused;

  // If the process is paused, or if it is running while handling a message
  // that won't cause it to change its execution point, the last message which
  // caused it to pause.
  Message* mPausedMessage;

  // The last checkpoint which the child process reached. The child is
  // somewhere between this and either the next or previous checkpoint,
  // depending on the messages that have been sent to it.
  size_t mLastCheckpoint;

  // Messages sent to the process which will affect its behavior as it runs
  // forward from the checkpoint.
  InfallibleVector<Message*> mMessages;

  // In the PlayingMessages recovery stage, how much of mMessages has been sent
  // to the process.
  size_t mNumRecoveredMessages;

  // The number of times we have restarted this process.
  size_t mNumRestarts;

  // Current role of this process.
  ChildRole* mRole;

  // Unsorted list of the checkpoints the process has been instructed to save.
  // Those at or before the most recent checkpoint will have been saved.
  InfallibleVector<size_t> mShouldSaveCheckpoints;

  // Sorted major checkpoints for this process. See ParentIPC.cpp.
  InfallibleVector<size_t> mMajorCheckpoints;

  // Whether we need this child to pause while the recording is updated.
  bool mPauseNeeded;

  static void Terminate(ipc::GeckoChildProcessHost* aProcess);

  void OnIncomingMessage(size_t aChannelId, const Message& aMsg);
  void OnIncomingRecoveryMessage(const Message& aMsg);
  void SendNextRecoveryMessage();
  void SendMessageRaw(const Message& aMsg);

  static void MaybeProcessPendingMessageRunnable();
  void ReceiveChildMessageOnMainThread(size_t aChannelId, Message* aMsg);

  // Get the position of this process relative to its last checkpoint.
  enum Disposition {
    AtLastCheckpoint,
    BeforeLastCheckpoint,
    AfterLastCheckpoint
  };
  Disposition GetDisposition();

  void Recover(bool aPaused, Message* aPausedMessage, size_t aLastCheckpoint,
               Message** aMessages, size_t aNumMessages);

  bool CanRestart();
  void AttemptRestart(const char* aWhy);
  void LaunchSubprocess();
  void TerminateSubprocess();

public:
  ChildProcess(ChildRole* aRole, bool aRecording);
  ~ChildProcess();

  ChildRole* Role() { return mRole; }
  ipc::GeckoChildProcessHost* Process() { return mProcess; }
  size_t GetId() { return mChannel->GetId(); }
  bool IsRecording() { return mRecording; }
  size_t LastCheckpoint() { return mLastCheckpoint; }
  bool IsRecovering() { return mRecoveryStage != RecoveryStage::None; }
  bool PauseNeeded() { return mPauseNeeded; }
  const InfallibleVector<size_t>& MajorCheckpoints() { return mMajorCheckpoints; }

  bool IsPaused() { return mPaused; }
  bool IsPausedAtCheckpoint();
  bool IsPausedAtRecordingEndpoint();

  // Return whether this process is paused at a breakpoint whose kind matches
  // the supplied filter.
  typedef std::function<bool(JS::replay::ExecutionPosition::Kind)> BreakpointFilter;
  bool IsPausedAtMatchingBreakpoint(const BreakpointFilter& aFilter);

  // Get the checkpoint at or earlier to the process' position. This is either
  // the last reached checkpoint or the previous one.
  size_t MostRecentCheckpoint() {
    return (GetDisposition() == BeforeLastCheckpoint) ? mLastCheckpoint - 1 : mLastCheckpoint;
  }

  // Get the checkpoint which needs to be saved in order for this process
  // (or another at the same place) to rewind.
  size_t RewindTargetCheckpoint() {
    switch (GetDisposition()) {
    case BeforeLastCheckpoint:
    case AtLastCheckpoint:
      // This will return InvalidCheckpointId if we are the beginning of the
      // recording.
      return LastCheckpoint() - 1;
    case AfterLastCheckpoint:
      return LastCheckpoint();
    }
  }

  bool ShouldSaveCheckpoint(size_t aId) {
    return VectorContains(mShouldSaveCheckpoints, aId);
  }

  bool IsMajorCheckpoint(size_t aId) {
    return VectorContains(mMajorCheckpoints, aId);
  }

  bool HasSavedCheckpoint(size_t aId) {
    return (aId <= MostRecentCheckpoint()) && ShouldSaveCheckpoint(aId);
  }

  size_t MostRecentSavedCheckpoint() {
    size_t id = MostRecentCheckpoint();
    while (!ShouldSaveCheckpoint(id)) {
      id--;
    }
    return id;
  }

  void SetPauseNeeded() {
    MOZ_RELEASE_ASSERT(!mPauseNeeded);
    mPauseNeeded = true;
  }

  void ClearPauseNeeded() {
    MOZ_RELEASE_ASSERT(IsPaused());
    mPauseNeeded = false;
    mRole->Poke();
  }

  void AddMajorCheckpoint(size_t aId);
  void SetRole(ChildRole* aRole);
  void SendMessage(const Message& aMessage);

  // Recover to the same state as another process.
  void Recover(ChildProcess* aTargetProcess);

  // Recover to be paused at a checkpoint with no breakpoints set.
  void RecoverToCheckpoint(size_t aCheckpoint);

  // Handle incoming messages from this process (and no others) until the
  // callback succeeds.
  void WaitUntil(const std::function<bool()>& aCallback);

  void WaitUntilPaused() { WaitUntil([=]() { return IsPaused(); }); }

  static bool MaybeProcessPendingMessage(ChildProcess* aProcess);
};

} // namespace parent
} // namespace recordreplay
} // namespace mozilla

#endif // mozilla_toolkit_recordreplay_ipc_ParentInternal_h
