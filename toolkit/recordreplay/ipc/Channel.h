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
#include "Monitor.h"

namespace mozilla {
namespace recordreplay {

// This file has definitions for creating and communicating on a special
// bidirectional channel between a middleman process and a recording or
// replaying process. This communication is not included in the recording, and
// when replaying this is the only mechanism the child can use to communicate
// with the middleman process.
//
// Replaying processes can rewind themselves, restoring execution state and the
// contents of all heap memory to that at an earlier point. To keep the
// replaying process and middleman from getting out of sync with each other,
// there are tight constraints on when messages may be sent across the channel
// by one process or the other. At any given time the child process may be
// either paused or unpaused. If it is paused, it is not doing any execution
// and cannot rewind itself. If it is unpaused, it may execute content and may
// rewind itself.
//
// Messages can be sent from the child process to the middleman only when the
// child process is unpaused, and messages can only be sent from the middleman
// to the child process when the child process is paused. This prevents
// messages from being lost when they are sent from the middleman as the
// replaying process rewinds itself. A few exceptions to this rule are noted
// below.
//
// Some additional synchronization is needed between different child processes:
// replaying processes can read from the same file which a recording process is
// writing to. While it is ok for a replaying process to read from the file
// while the recording process is appending new chunks to it (see File.cpp),
// all replaying processes must be paused when the recording process is
// flushing a new index to the file.

#define ForEachMessageType(Macro)                              \
  /* Messages sent from the middleman to the child process. */ \
                                                               \
  /* Sent at startup. */                                       \
  Macro(Introduction)                                          \
                                                               \
  /* Flush the current recording to disk. */                   \
  Macro(FlushRecording)                                        \
                                                               \
  /* Poke a child that is recording to create an artificial checkpoint, rather than */ \
  /* (potentially) idling indefinitely. This has no effect on a replaying process. */ \
  Macro(CreateCheckpoint)                                      \
                                                               \
  /* Debugger JSON messages are initially sent from the parent. The child unpauses */ \
  /* after receiving the message and will pause after it sends a DebuggerResponse. */ \
  Macro(DebuggerRequest)                                       \
                                                               \
  /* Set or clear a JavaScript breakpoint. */                  \
  Macro(SetBreakpoint)                                         \
                                                               \
  /* Unpause the child and play execution either to the next point when a */ \
  /* breakpoint is hit, or to the next checkpoint. Resumption may be either */ \
  /* forward or backward. */                                   \
  Macro(Resume)                                                \
                                                               \
  /* Rewind to a particular saved checkpoint in the past. */   \
  Macro(RestoreCheckpoint)                                     \
                                                               \
  /* Notify the child whether it is the active child and should send paint and similar */ \
  /* messages to the middleman. */                             \
  Macro(SetIsActive)                                           \
                                                               \
  /* Set whether to perform intentional crashes, for testing. */ \
  Macro(SetAllowIntentionalCrashes)                            \
                                                               \
  /* Set whether to save a particular checkpoint. */           \
  Macro(SetSaveCheckpoint)                                     \
                                                               \
  /* Messages sent from the child process to the middleman. */ \
                                                               \
  /* Sent in response to a FlushRecording, telling the middleman that the flush */ \
  /* has finished. */                                          \
  Macro(RecordingFlushed)                                      \
                                                               \
  /* A critical error occurred and execution cannot continue. The child will */ \
  /* stop executing after sending this message and will wait to be terminated. */ \
  Macro(FatalError)                                            \
                                                               \
  /* The child's graphics were repainted. */                   \
  Macro(Paint)                                                 \
                                                               \
  /* Notify the middleman that a checkpoint, breakpoint, or recording endpoint */ \
  /* was hit. The child will pause after sending these messages. */ \
  Macro(HitCheckpoint)                                         \
  Macro(HitBreakpoint)                                         \
  Macro(HitRecordingEndpoint)                                  \
                                                               \
  /* Send a response to a DebuggerRequest message. */          \
  Macro(DebuggerResponse)                                      \
                                                               \
  /* Notify that the 'AlwaysMarkMajorCheckpoints' directive was invoked. */ \
  Macro(AlwaysMarkMajorCheckpoints)

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

struct IntroductionMessage : public Message
{
  base::ProcessId mParentPid;
  uint32_t mPrefsLen;
  uint32_t mArgc;

  IntroductionMessage(uint32_t aSize, base::ProcessId aParentPid, uint32_t aPrefsLen, uint32_t aArgc)
    : Message(MessageType::Introduction, aSize)
    , mParentPid(aParentPid)
    , mPrefsLen(aPrefsLen)
    , mArgc(aArgc)
  {}

  char* PrefsData() { return Data<IntroductionMessage, char>(); }
  char* ArgvString() { return Data<IntroductionMessage, char>() + mPrefsLen; }

  const char* PrefsData() const { return Data<IntroductionMessage, char>(); }
  const char* ArgvString() const { return Data<IntroductionMessage, char>() + mPrefsLen; }

  static IntroductionMessage* New(base::ProcessId aParentPid, char* aPrefs, size_t aPrefsLen, int aArgc, char* aArgv[]) {
    size_t argsLen = 0;
    for (int i = 0; i < aArgc; i++) {
      argsLen += strlen(aArgv[i]) + 1;
    }

    IntroductionMessage* res = NewWithData<IntroductionMessage, char>(aPrefsLen + argsLen, aParentPid, aPrefsLen, aArgc);

    memcpy(res->PrefsData(), aPrefs, aPrefsLen);

    size_t offset = 0;
    for (int i = 0; i < aArgc; i++) {
      memcpy(&res->ArgvString()[offset], aArgv[i], strlen(aArgv[i]) + 1);
      offset += strlen(aArgv[i]) + 1;
    }
    MOZ_RELEASE_ASSERT(offset == argsLen);

    return res;
  }
};

struct CreateCheckpointMessage : public Message
{
  CreateCheckpointMessage()
    : Message(MessageType::CreateCheckpoint, sizeof(*this))
  {}
};

struct FlushRecordingMessage : public Message
{
  FlushRecordingMessage()
    : Message(MessageType::FlushRecording, sizeof(*this))
  {}
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

  explicit ResumeMessage(bool aForward)
    : Message(MessageType::Resume, sizeof(*this))
    , mForward(aForward)
  {}
};

struct RestoreCheckpointMessage : public Message
{
  // The checkpoint to restore.
  size_t mCheckpoint;

  explicit RestoreCheckpointMessage(size_t aCheckpoint)
    : Message(MessageType::RestoreCheckpoint, sizeof(*this))
    , mCheckpoint(aCheckpoint)
  {}
};

struct SetIsActiveMessage : public Message
{
  // Whether this is the active child process (see ParentIPC.cpp).
  bool mActive;

  explicit SetIsActiveMessage(bool aActive)
    : Message(MessageType::SetIsActive, sizeof(*this))
    , mActive(aActive)
  {}
};

struct SetAllowIntentionalCrashesMessage : public Message
{
  // Whether to allow intentional crashes in the future or not.
  bool mAllowed;

  explicit SetAllowIntentionalCrashesMessage(bool aAllowed)
    : Message(MessageType::SetAllowIntentionalCrashes, sizeof(*this))
    , mAllowed(aAllowed)
  {}
};

struct SetSaveCheckpointMessage : public Message
{
  // The checkpoint in question.
  size_t mCheckpoint;

  // Whether to save this checkpoint whenever it is encountered.
  bool mSave;

  SetSaveCheckpointMessage(size_t aCheckpoint, bool aSave)
    : Message(MessageType::SetSaveCheckpoint, sizeof(*this))
    , mCheckpoint(aCheckpoint)
    , mSave(aSave)
  {}
};

struct RecordingFlushedMessage : public Message
{
  RecordingFlushedMessage()
    : Message(MessageType::RecordingFlushed, sizeof(*this))
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

struct HitCheckpointMessage : public Message
{
  uint32_t mCheckpointId;

  // When recording, the amount of non-idle time taken to get to this
  // checkpoint from the previous one.
  double mDurationMicroseconds;

  HitCheckpointMessage(uint32_t aCheckpointId, double aDurationMicroseconds)
    : Message(MessageType::HitCheckpoint, sizeof(*this))
    , mCheckpointId(aCheckpointId)
    , mDurationMicroseconds(aDurationMicroseconds)
  {}
};

struct HitBreakpointMessage : public Message
{
  explicit HitBreakpointMessage(uint32_t aSize)
    : Message(MessageType::HitBreakpoint, aSize)
  {}

  const uint32_t* Breakpoints() const { return Data<HitBreakpointMessage, uint32_t>(); }
  uint32_t NumBreakpoints() const { return DataSize<HitBreakpointMessage, uint32_t>(); }

  static HitBreakpointMessage* New(const uint32_t* aBreakpoints, size_t aNumBreakpoints) {
    HitBreakpointMessage* res = NewWithData<HitBreakpointMessage, uint32_t>(aNumBreakpoints);
    MOZ_RELEASE_ASSERT(res->NumBreakpoints() == aNumBreakpoints);
    PodCopy(res->Data<HitBreakpointMessage, uint32_t>(), aBreakpoints, aNumBreakpoints);
    return res;
  }
};

struct HitRecordingEndpointMessage : public Message
{
  HitRecordingEndpointMessage()
    : Message(MessageType::HitRecordingEndpoint, sizeof(*this))
  {}
};

struct AlwaysMarkMajorCheckpointsMessage : public Message
{
  AlwaysMarkMajorCheckpointsMessage()
    : Message(MessageType::AlwaysMarkMajorCheckpoints, sizeof(*this))
  {}
};

class Channel
{
public:
  // Note: the handler is responsible for freeing its input message. It will be
  // called on the channel's message thread.
  typedef std::function<void(Message*)> MessageHandler;

private:
  // ID for this channel, unique for the middleman.
  size_t mId;

  // Callback to invoke off thread on incoming messages.
  MessageHandler mHandler;

  // Whether the channel is initialized and ready for outgoing messages.
  Atomic<bool, SequentiallyConsistent, Behavior::DontPreserve> mInitialized;

  // Descriptor used to accept connections on the parent side.
  int mConnectionFd;

  // Descriptor used to communicate with the other side.
  int mFd;

  // For synchronizing initialization of the channel.
  Monitor mMonitor;

  // Buffer for message data received from the other side of the channel.
  InfallibleVector<char, 0, AllocPolicy<UntrackedMemoryKind::Generic>> mMessageBuffer;

  // The number of bytes of data already in the message buffer.
  size_t mMessageBytes;

  // If spew is enabled, print a message and associated info to stderr.
  void PrintMessage(const char* aPrefix, const Message& aMsg);

  // Block until a complete message is received from the other side of the
  // channel.
  Message* WaitForMessage();

  // Main routine for the channel's thread.
  static void ThreadMain(void* aChannel);

public:
  // Initialize this channel, connect to the other side, and spin up a thread
  // to process incoming messages by calling aHandler.
  Channel(size_t aId, const MessageHandler& aHandler);

  size_t GetId() { return mId; }

  // Send a message to the other side of the channel. This must be called on
  // the main thread, except for fatal error messages.
  void SendMessage(const Message& aMsg);
};

} // namespace recordreplay
} // namespace mozilla

#endif // mozilla_toolkit_recordreplay_ipc_ParentIPC_h
