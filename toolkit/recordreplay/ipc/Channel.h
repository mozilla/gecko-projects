/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_toolkit_recordreplay_ipc_Channel_h
#define mozilla_toolkit_recordreplay_ipc_Channel_h

#include "base/process.h"

#include "js/ReplayHooks.h"
#include "mozilla/gfx/Types.h"
#include "mozilla/Maybe.h"

#include "File.h"

namespace mozilla {
namespace recordreplay {
namespace channel {

// This file has definitions for creating and communicating on a special
// bidirectional channel between a middleman process and its recording or
// replaying process. This communication is not included in the recording, and
// once the child process begins replaying this is the only mechanism it can
// use to communicate with the middleman process.
//
// Recording/replaying processes can rewind themselves, restoring execution
// state and the contents of all heap memory to that at an earlier point.
// To keep the recording/replaying process and middleman from getting out of
// sync with each other, there are tight constraints on when messages may be
// sent across the channel by one process or the other. At any given time the
// child process may be either paused or unpaused. If it is paused, it is not
// doing any execution and cannot rewind itself. If it is unpaused, it may
// execute content and may rewind itself.
//
// Messages can be sent from the child process to the middleman only when the
// child process is unpaused, and messages can only be sent from the middleman
// to the child process when the child process is paused. This prevents
// messages from being lost when they are sent from the middleman as the
// replaying process rewinds itself. A few exceptions to this rule are noted
// below.

// Initialize state on the middleman side so that the child can connect to this
// process.
void InitParent();

// Block until the child has established a connection with this process.
void ConnectParent();

// In the child process, connect to the middleman process. The middleman must
// have already called InitParent().
void InitAndConnectChild(base::ProcessId aMiddlemanPid);

// Allow a single disconnection from the other side of the channel. Otherwise
// this process will be terminated when the channel disconnects.
void AllowDisconnect();

#define ForEachMessageType(Macro)                              \
  /* Messages sent in both directions. */                      \
                                                               \
  /* Save the current recording, or notify that the recording was saved. */ \
  Macro(SaveRecording)                                         \
                                                               \
  /* Messages sent from the middleman to the child process. */ \
                                                               \
  /* Sent at startup. */                                       \
  Macro(Introduction)                                          \
                                                               \
  /* Set child process runtime options. */                     \
  Macro(Initialize)                                            \
  Macro(SetAllowIntentionalCrashes)                            \
                                                               \
  /* Poke a child that is recording to take a snapshot, rather than (potentially) */ \
  /* idling indefinitely. This has no effect on a replaying process. */ \
  Macro(TakeSnapshot)                                          \
                                                               \
  /* Debugger JSON messages are initially sent from the parent. The child unpauses */ \
  /* after receiving the message and will pause after it sends a DebuggerResponse. */ \
  Macro(DebuggerRequest)                                       \
                                                               \
  /* Set or clear a JavaScript breakpoint. */                  \
  Macro(SetBreakpoint)                                         \
                                                               \
  /* Unpause the child and play execution either to the next point when a */ \
  /* breakpoint is hit, or to the next snapshot. Resumption may be either */ \
  /* forward or backward. When backward, earlier snapshots might be hit first */ \
  /* (if the last recorded snapshot precedes the most recent snapshot), in which */ \
  /* case interim HitSnapshot messages will be sent for those earlier snapshots. */ \
  Macro(Resume)                                                \
                                                               \
  /* Messages sent from the child process to the middleman. */ \
                                                               \
  /* A critical error occurred and execution cannot continue. The child will */ \
  /* stop executing after sending this message and will wait to be terminated. */ \
  Macro(FatalError)                                            \
                                                               \
  /* The child's graphics were repainted. */                   \
  Macro(Paint)                                                 \
                                                               \
  /* Notify the middleman that a snapshot or breakpoint was hit. The child will */ \
  /* pause after sending these messages, except for interim HitSnapshot messages. */ \
  Macro(HitSnapshot)                                           \
  Macro(HitBreakpoint)                                         \
                                                               \
  /* Send a response to a DebuggerRequest message. */          \
  Macro(DebuggerResponse)

enum class MessageType
{
#define DefineEnum(Kind) Kind,
  ForEachMessageType(DefineEnum)
#undef DefineEnum
};

struct Message
{
  MessageType mType;

  // Total message size, including the header.
  uint32_t mSize;

  Message(MessageType aType, uint32_t aSize)
    : mType(aType), mSize(aSize)
  {
    MOZ_RELEASE_ASSERT(mSize >= sizeof(*this));
  }

  Message* Clone() const {
    char* res = (char*) malloc(mSize);
    memcpy(res, this, mSize);
    return (Message*) res;
  }

  const char* TypeString() const {
    switch (mType) {
#define EnumToString(Kind) case MessageType::Kind: return #Kind;
  ForEachMessageType(EnumToString)
#undef EnumToString
    default: return "Unknown";
    }
  }

protected:
  template <typename T, typename Elem>
  Elem* Data() { return (Elem*) (sizeof(T) + (char*) this); }

  template <typename T, typename Elem>
  const Elem* Data() const { return (const Elem*) (sizeof(T) + (const char*) this); }

  template <typename T, typename Elem>
  size_t DataSize() const { return (mSize - sizeof(T)) / sizeof(Elem); }

  template <typename T, typename Elem, typename... Args>
  static T* NewWithData(size_t aBufferSize, Args&&... aArgs) {
    size_t size = sizeof(T) + aBufferSize * sizeof(Elem);
    void* ptr = malloc(size);
    return new(ptr) T(size, Forward<Args>(aArgs)...);
  }
};

// Send a message to the other side of the channel. This may be called from any
// thread.
void SendMessage(const Message& aMsg, bool aTakeLock = true);

// Block until a complete message is received from the other side of the
// channel. The returned message must be freed by the caller.
Message* WaitForMessage();

struct IntroductionMessage : public Message
{
  base::ProcessId mParentPid;
  uint32_t mArgc;

  IntroductionMessage(uint32_t aSize, base::ProcessId aParentPid, uint32_t aArgc)
    : Message(MessageType::Introduction, aSize)
    , mParentPid(aParentPid)
    , mArgc(aArgc)
  {}

  char* ArgvString() { return Data<IntroductionMessage, char>(); }

  static IntroductionMessage* New(base::ProcessId aParentPid, int aArgc, char* aArgv[]) {
    size_t argsLen = 0;
    for (int i = 0; i < aArgc; i++) {
      argsLen += strlen(aArgv[i]) + 1;
    }

    IntroductionMessage* res = NewWithData<IntroductionMessage, char>(argsLen, aParentPid, aArgc);

    size_t offset = 0;
    for (int i = 0; i < aArgc; i++) {
      memcpy(&res->ArgvString()[offset], aArgv[i], strlen(aArgv[i]) + 1);
      offset += strlen(aArgv[i]) + 1;
    }
    MOZ_RELEASE_ASSERT(offset == argsLen);

    return res;
  }
};

struct InitializeMessage : public Message
{
  bool mRecordSnapshots;

  explicit InitializeMessage(bool aRecordSnapshots)
    : Message(MessageType::Initialize, sizeof(*this))
    , mRecordSnapshots(aRecordSnapshots)
  {}
};

struct SetAllowIntentionalCrashesMessage : public Message
{
  bool mAllowed;

  explicit SetAllowIntentionalCrashesMessage(bool aAllowed)
    : Message(MessageType::SetAllowIntentionalCrashes, sizeof(*this))
    , mAllowed(aAllowed)
  {}
};

struct TakeSnapshotMessage : public Message
{
  TakeSnapshotMessage()
    : Message(MessageType::TakeSnapshot, sizeof(*this))
  {}
};

struct SaveRecordingMessage : public Message
{
  explicit SaveRecordingMessage(uint32_t aSize)
    : Message(MessageType::SaveRecording, aSize)
  {}

  const char* Filename() const { return Data<SaveRecordingMessage, char>(); }

  static SaveRecordingMessage* New(const char* aFilename) {
    size_t len = strlen(aFilename) + 1;
    SaveRecordingMessage* res = NewWithData<SaveRecordingMessage, char>(len);
    strcpy(res->Data<SaveRecordingMessage, char>(), aFilename);
    return res;
  }
};

struct DebuggerRequestMessage : public Message
{
  explicit DebuggerRequestMessage(uint32_t aSize)
    : Message(MessageType::DebuggerRequest, aSize)
  {}

  const char16_t* Buffer() const { return Data<DebuggerRequestMessage, char16_t>(); }
  size_t BufferSize() const { return DataSize<DebuggerRequestMessage, char16_t>(); }

  static DebuggerRequestMessage* New(const char16_t* aBuffer, size_t aBufferSize) {
    DebuggerRequestMessage* res =
      NewWithData<DebuggerRequestMessage, char16_t>(aBufferSize);
    MOZ_RELEASE_ASSERT(res->BufferSize() == aBufferSize);
    PodCopy(res->Data<DebuggerRequestMessage, char16_t>(), aBuffer, aBufferSize);
    return res;
  }
};

struct DebuggerResponseMessage : public Message
{
  explicit DebuggerResponseMessage(uint32_t aSize)
    : Message(MessageType::DebuggerResponse, aSize)
  {}

  const char16_t* Buffer() const { return Data<DebuggerResponseMessage, char16_t>(); }
  size_t BufferSize() const { return DataSize<DebuggerResponseMessage, char16_t>(); }

  static DebuggerResponseMessage* New(const char16_t* aBuffer, size_t aBufferSize) {
    DebuggerResponseMessage* res =
      NewWithData<DebuggerResponseMessage, char16_t>(aBufferSize);
    MOZ_RELEASE_ASSERT(res->BufferSize() == aBufferSize);
    PodCopy(res->Data<DebuggerResponseMessage, char16_t>(), aBuffer, aBufferSize);
    return res;
  }
};

struct SetBreakpointMessage : public Message
{
  // ID of the breakpoint to change.
  size_t mId;

  // New position of the breakpoint. If this is invalid then the breakpoint is
  // being cleared.
  JS::replay::ExecutionPosition mPosition;

  SetBreakpointMessage(size_t aId, const JS::replay::ExecutionPosition& aPosition)
    : Message(MessageType::SetBreakpoint, sizeof(*this))
    , mId(aId)
    , mPosition(aPosition)
  {}
};

struct ResumeMessage : public Message
{
  // Whether to travel forwards or backwards.
  bool mForward;

  // Whether to hit other breakpoints at the current source location, or to
  // ignore such other breakpoints and resume immediately. This has no effect
  // when paused at a snapshot.
  bool mHitOtherBreakpoints;

  ResumeMessage(bool aForward, bool aHitOtherBreakpoints)
    : Message(MessageType::Resume, sizeof(*this))
    , mForward(aForward)
    , mHitOtherBreakpoints(aHitOtherBreakpoints)
  {}
};

struct FatalErrorMessage : public Message
{
  explicit FatalErrorMessage(uint32_t aSize)
    : Message(MessageType::FatalError, aSize)
  {}

  const char* Error() const { return Data<FatalErrorMessage, const char>(); }
};

static const gfx::SurfaceFormat gSurfaceFormat = gfx::SurfaceFormat::B8G8R8X8;

struct PaintMessage : public Message
{
  uint32_t mWidth;
  uint32_t mHeight;

  PaintMessage(uint32_t aSize, uint32_t aWidth, uint32_t aHeight)
    : Message(MessageType::Paint, aSize)
    , mWidth(aWidth)
    , mHeight(aHeight)
  {}

  unsigned char* Buffer() { return Data<PaintMessage, unsigned char>(); }
  const unsigned char* Buffer() const { return Data<PaintMessage, unsigned char>(); }
  size_t BufferSize() const { return DataSize<PaintMessage, unsigned char>(); }

  static PaintMessage* New(uint32_t aWidth, uint32_t aHeight) {
    return NewWithData<PaintMessage, unsigned char>(aWidth * aHeight * 4, aWidth, aHeight);
  }
};

struct HitSnapshotMessage : public Message
{
  uint32_t mSnapshotId;
  bool mFinal;
  bool mInterim;

  HitSnapshotMessage(uint32_t aSnapshotId, bool aFinal, bool aInterim)
    : Message(MessageType::HitSnapshot, sizeof(*this))
    , mSnapshotId(aSnapshotId)
    , mFinal(aFinal)
    , mInterim(aInterim)
  {}
};

struct HitBreakpointMessage : public Message
{
  uint32_t mBreakpointId;

  explicit HitBreakpointMessage(uint32_t aBreakpointId)
    : Message(MessageType::HitBreakpoint, sizeof(*this))
    , mBreakpointId(aBreakpointId)
  {}
};

void PrintMessage(const char* aPrefix, const Message& aMsg);

} // namespace channel
} // namespace recordreplay
} // namespace mozilla

#endif // mozilla_toolkit_recordreplay_ipc_ParentIPC_h
