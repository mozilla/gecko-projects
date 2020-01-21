/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_recordreplay_Channel_h
#define mozilla_recordreplay_Channel_h

#include "base/process.h"

#include "mozilla/gfx/Types.h"
#include "mozilla/Maybe.h"
#include "mozilla/UniquePtr.h"

#include "ExternalCall.h"
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

#define ForEachMessageType(_Macro)                                            \
  /* Messages which can be interpreted or constructed by the cloud server. */ \
  /* Avoid changing the message IDs for these. */                             \
                                                                              \
  /* Sent by the middleman at startup. */                                     \
  _Macro(Introduction)                                         \
                                                               \
  /* An error occurred in the cloud server. */                 \
  _Macro(CloudError)                                           \
                                                               \
  /* Messages sent from the middleman to the child process. */ \
                                                               \
  /* Sent to recording processes to indicate that the middleman will be running */ \
  /* developer tools server-side code instead of the recording process itself. */ \
  _Macro(SetDebuggerRunsInMiddleman)                           \
                                                               \
  /* Periodically sent to replaying processes to make sure they are */ \
  /* responsive and determine how much progress they have made. This can be */ \
  /* sent while the process is unpaused, but only when it will not be */ \
  /* rewinding. */                                             \
  _Macro(Ping)                                                 \
                                                               \
  /* Sent to child processes which should exit normally. */    \
  _Macro(Terminate)                                            \
                                                               \
  /* Force a hanged replaying process to crash and produce a dump. */ \
  _Macro(Crash)                                                \
                                                               \
  /* Poke a child that is recording to create an artificial checkpoint, rather than */ \
  /* (potentially) idling indefinitely. This has no effect on a replaying process. */ \
  _Macro(CreateCheckpoint)                                     \
                                                               \
  /* Unpause the child and perform a debugger-defined operation. */ \
  _Macro(ManifestStart)                                             \
                                                               \
  /* Respond to a ExternalCallRequest message. This is also sent between separate */ \
  /* replaying processes to fill the external call cache in root replaying processes. */ \
  _Macro(ExternalCallResponse)                                 \
                                                               \
  /* Messages sent from the child process to the middleman. */ \
                                                               \
  /* Pause after executing a manifest, specifying its response. */ \
  _Macro(ManifestFinished)                                     \
                                                               \
  /* Respond to a ping message */                              \
  _Macro(PingResponse)                                         \
                                                               \
  /* An unhandled recording divergence occurred and execution cannot continue. */ \
  _Macro(UnhandledDivergence)                                  \
                                                               \
  /* A critical error occurred and execution cannot continue. The child will */ \
  /* stop executing after sending this message and will wait to be terminated. */ \
  /* A minidump for the child has been generated. */           \
  _Macro(FatalError)                                           \
                                                               \
  /* The child's graphics were repainted into the graphics shmem. */ \
  _Macro(Paint)                                                \
                                                               \
  /* The child's graphics were repainted and have been encoded as an image. */ \
  _Macro(PaintEncoded)                                         \
                                                               \
  /* Get the result of performing an external call. */         \
  _Macro(ExternalCallRequest)                                  \
                                                               \
  /* Messages sent in both directions. */                      \
                                                               \
  /* Send recording data from a recording process to the middleman, or from the */ \
  /* middleman to a replaying process. */                      \
  _Macro(RecordingData)

enum class MessageType {
#define DefineEnum(Kind) Kind,
  ForEachMessageType(DefineEnum)
#undef DefineEnum
};

struct Message {
  MessageType mType;

  // Total message size, including the header.
  uint32_t mSize;

  // Any associated forked process ID for this message.
  uint32_t mForkId;

 protected:
  Message(MessageType aType, uint32_t aSize, uint32_t aForkId)
      : mType(aType), mSize(aSize), mForkId(aForkId) {
    MOZ_RELEASE_ASSERT(mSize >= sizeof(*this));
  }

 public:
  struct FreePolicy {
    void operator()(Message* msg) { /*free(msg);*/
    }
  };
  typedef UniquePtr<Message, FreePolicy> UniquePtr;

  UniquePtr Clone() const {
    Message* res = static_cast<Message*>(malloc(mSize));
    memcpy(res, this, mSize);
    return UniquePtr(res);
  }

  const char* TypeString() const {
    switch (mType) {
#define EnumToString(Kind) \
  case MessageType::Kind:  \
    return #Kind;
      ForEachMessageType(EnumToString)
#undef EnumToString
          default : return "Unknown";
    }
  }

  // Return whether this is a middleman->child message that can be sent while
  // the child is unpaused.
  bool CanBeSentWhileUnpaused() const {
    return mType == MessageType::CreateCheckpoint ||
           mType == MessageType::SetDebuggerRunsInMiddleman ||
           mType == MessageType::ExternalCallResponse ||
           mType == MessageType::Ping || mType == MessageType::Terminate ||
           mType == MessageType::Crash || mType == MessageType::Introduction ||
           mType == MessageType::RecordingData;
  }

 protected:
  template <typename T, typename Elem>
  Elem* Data() {
    return (Elem*)(sizeof(T) + (char*)this);
  }

  template <typename T, typename Elem>
  const Elem* Data() const {
    return (const Elem*)(sizeof(T) + (const char*)this);
  }

  template <typename T, typename Elem>
  size_t DataSize() const {
    return (mSize - sizeof(T)) / sizeof(Elem);
  }

  template <typename T, typename Elem, typename... Args>
  static T* NewWithData(size_t aBufferSize, Args&&... aArgs) {
    size_t size = sizeof(T) + aBufferSize * sizeof(Elem);
    void* ptr = malloc(size);
    return new (ptr) T(size, std::forward<Args>(aArgs)...);
  }
};

struct IntroductionMessage : public Message {
  // Used when replaying to describe the build that must be used for the replay.
  BuildId mBuildId;

  // Used when recording to specify the parent process pid.
  base::ProcessId mParentPid;

  uint32_t mArgc;

  IntroductionMessage(uint32_t aSize, base::ProcessId aParentPid,
                      uint32_t aArgc)
      : Message(MessageType::Introduction, aSize, 0),
        mParentPid(aParentPid),
        mArgc(aArgc) {}

  char* ArgvString() { return Data<IntroductionMessage, char>(); }
  const char* ArgvString() const { return Data<IntroductionMessage, char>(); }

  static IntroductionMessage* New(base::ProcessId aParentPid, int aArgc,
                                  char* aArgv[]) {
    size_t argsLen = 0;
    for (int i = 0; i < aArgc; i++) {
      argsLen += strlen(aArgv[i]) + 1;
    }

    IntroductionMessage* res =
        NewWithData<IntroductionMessage, char>(argsLen, aParentPid, aArgc);

    size_t offset = 0;
    for (int i = 0; i < aArgc; i++) {
      memcpy(&res->ArgvString()[offset], aArgv[i], strlen(aArgv[i]) + 1);
      offset += strlen(aArgv[i]) + 1;
    }
    MOZ_RELEASE_ASSERT(offset == argsLen);

    return res;
  }

  static IntroductionMessage* RecordReplay(const IntroductionMessage& aMsg) {
    size_t introductionSize = RecordReplayValue(aMsg.mSize);
    IntroductionMessage* msg = (IntroductionMessage*)malloc(introductionSize);
    if (IsRecording()) {
      memcpy(msg, &aMsg, introductionSize);
    }
    RecordReplayBytes(msg, introductionSize);
    return msg;
  }
};

template <MessageType Type>
struct EmptyMessage : public Message {
  explicit EmptyMessage(uint32_t aForkId = 0)
      : Message(Type, sizeof(*this), aForkId) {}
};

typedef EmptyMessage<MessageType::SetDebuggerRunsInMiddleman>
    SetDebuggerRunsInMiddlemanMessage;
typedef EmptyMessage<MessageType::Terminate> TerminateMessage;
typedef EmptyMessage<MessageType::Crash> CrashMessage;
typedef EmptyMessage<MessageType::CreateCheckpoint> CreateCheckpointMessage;

template <MessageType Type>
struct JSONMessage : public Message {
  explicit JSONMessage(uint32_t aSize, uint32_t aForkId)
      : Message(Type, aSize, aForkId) {}

  const char16_t* Buffer() const { return Data<JSONMessage<Type>, char16_t>(); }
  size_t BufferSize() const { return DataSize<JSONMessage<Type>, char16_t>(); }

  static JSONMessage<Type>* New(uint32_t aForkId, const char16_t* aBuffer,
                                size_t aBufferSize) {
    JSONMessage<Type>* res =
        NewWithData<JSONMessage<Type>, char16_t>(aBufferSize, aForkId);
    MOZ_RELEASE_ASSERT(res->BufferSize() == aBufferSize);
    PodCopy(res->Data<JSONMessage<Type>, char16_t>(), aBuffer, aBufferSize);
    return res;
  }
};

typedef JSONMessage<MessageType::ManifestStart> ManifestStartMessage;
typedef JSONMessage<MessageType::ManifestFinished> ManifestFinishedMessage;

template <MessageType Type>
struct ErrorMessage : public Message {
  explicit ErrorMessage(uint32_t aSize, uint32_t aForkId)
      : Message(Type, aSize, aForkId) {}

  const char* Error() const { return Data<ErrorMessage<Type>, const char>(); }
};

typedef ErrorMessage<MessageType::FatalError> FatalErrorMessage;
typedef ErrorMessage<MessageType::CloudError> CloudErrorMessage;

typedef EmptyMessage<MessageType::UnhandledDivergence>
    UnhandledDivergenceMessage;

// The format for graphics data which will be sent to the middleman process.
// This needs to match the format expected for canvas image data, to avoid
// transforming the data before rendering it in the middleman process.
static const gfx::SurfaceFormat gSurfaceFormat = gfx::SurfaceFormat::R8G8B8X8;

struct PaintMessage : public Message {
  uint32_t mWidth;
  uint32_t mHeight;

  PaintMessage(uint32_t aWidth, uint32_t aHeight)
      : Message(MessageType::Paint, sizeof(*this), 0),
        mWidth(aWidth),
        mHeight(aHeight) {}
};

template <MessageType Type>
struct BinaryMessage : public Message {
  // Associated value whose meaning depends on the message type.
  uint64_t mTag;

  explicit BinaryMessage(uint32_t aSize, uint32_t aForkId, uint64_t aTag)
      : Message(Type, aSize, aForkId), mTag(aTag) {}

  const char* BinaryData() const { return Data<BinaryMessage<Type>, char>(); }
  size_t BinaryDataSize() const {
    return DataSize<BinaryMessage<Type>, char>();
  }

  static BinaryMessage<Type>* New(uint32_t aForkId, uint64_t aTag,
                                  const char* aData, size_t aDataSize) {
    BinaryMessage<Type>* res =
        NewWithData<BinaryMessage<Type>, char>(aDataSize, aForkId, aTag);
    MOZ_RELEASE_ASSERT(res->BinaryDataSize() == aDataSize);
    PodCopy(res->Data<BinaryMessage<Type>, char>(), aData, aDataSize);
    return res;
  }
};

typedef BinaryMessage<MessageType::PaintEncoded> PaintEncodedMessage;

// The tag is the ID of the external call being performed.
typedef BinaryMessage<MessageType::ExternalCallRequest>
    ExternalCallRequestMessage;
typedef BinaryMessage<MessageType::ExternalCallResponse>
    ExternalCallResponseMessage;

// The tag is the start offset of the recording data needed.
typedef BinaryMessage<MessageType::RecordingData> RecordingDataMessage;

struct PingMessage : public Message {
  uint32_t mId;

  explicit PingMessage(uint32_t aForkId, uint32_t aId)
      : Message(MessageType::Ping, sizeof(*this), aForkId), mId(aId) {}
};

struct PingResponseMessage : public Message {
  uint32_t mId;
  uint64_t mProgress;

  PingResponseMessage(uint32_t aForkId, uint32_t aId, uint64_t aProgress)
      : Message(MessageType::PingResponse, sizeof(*this), aForkId),
        mId(aId),
        mProgress(aProgress) {}
};

class Channel {
 public:
  // Note: the handler is responsible for freeing its input message. It will be
  // called on the channel's message thread.
  typedef std::function<void(Message::UniquePtr)> MessageHandler;

  // Different kinds of channels.
  enum class Kind {
    // Connect middleman to a recording process.
    MiddlemanRecord,

    // Connect middleman to a replaying process.
    MiddlemanReplay,

    // Connect recording or replaying process to the middleman.
    RecordReplay,

    // Connect parent managing a cloud connection to a middleman.
    ParentCloud,

    // Connect a root replaying process to one of its forks.
    ReplayRoot,

    // Connect a forked replaying process to its root replaying process.
    ReplayForked,
  };

 private:
  // ID for this channel, unique for the middleman.
  size_t mId;

  // Kind of this channel.
  Kind mKind;

  // Callback to invoke off thread on incoming messages.
  MessageHandler mHandler;

  // Whether the channel is initialized and ready for outgoing messages.
  bool mInitialized;

  // Descriptor used to accept connections on the parent side.
  int mConnectionFd;

  // Descriptor used to communicate with the other side.
  int mFd;

  // For synchronizing initialization of the channel and ensuring atomic sends.
  Monitor mMonitor;

  // Buffer for message data received from the other side of the channel.
  typedef InfallibleVector<char> MessageBuffer;
  MessageBuffer mMessageBuffer;

  // The number of bytes of data already in the message buffer.
  size_t mMessageBytes;

  InfallibleVector<char> mPendingData;

  // If spew is enabled, print a message and associated info to stderr.
  void PrintMessage(const char* aPrefix, const Message& aMsg);

  // Block until a complete message is received from the other side of the
  // channel.
  Message::UniquePtr WaitForMessage();

  // Main routine for the channel's thread.
  static void ThreadMain(void* aChannel);

  void SendRaw(const char* aData, size_t aSize);

  // Return whether this is the parent side of a connection. This side is opened
  // first and the child will connect to it afterwards.
  bool IsParent() {
    switch (mKind) {
      case Kind::MiddlemanRecord:
      case Kind::MiddlemanReplay:
      case Kind::ReplayForked:
        return true;
      case Kind::RecordReplay:
      case Kind::ParentCloud:
      case Kind::ReplayRoot:
        return false;
    }
    MOZ_CRASH("Bad kind");
  }

  // Return whether to exit the process when the other side of the channel
  // disconnects.
  bool ExitProcessOnDisconnect() {
    switch (mKind) {
      case Kind::RecordReplay:
      case Kind::ReplayForked:
        return true;
      case Kind::MiddlemanRecord:
      case Kind::MiddlemanReplay:
      case Kind::ParentCloud:
      case Kind::ReplayRoot:
        return false;
    }
    MOZ_CRASH("Bad kind");
  }

 public:
  Channel(size_t aId, Kind aKind, const MessageHandler& aHandler,
          base::ProcessId aParentPid = 0);

  size_t GetId() { return mId; }

  // Send a message to the other side of the channel.
  void SendMessage(Message&& aMsg);

  // Send data which contains message(s) to the other side of the channel.
  void SendMessageData(const char* aData, size_t aSize);

  // Exit the process if the channel is not initialized before a deadline.
  void ExitIfNotInitializedBefore(const TimeStamp& aDeadline);
};

// Command line option used to specify the middleman pid for a child process.
static const char* gMiddlemanPidOption = "-middlemanPid";

// Command line option used to specify the channel ID for a child process.
static const char* gChannelIDOption = "-recordReplayChannelID";

}  // namespace recordreplay
}  // namespace mozilla

#endif  // mozilla_recordreplay_Channel_h
