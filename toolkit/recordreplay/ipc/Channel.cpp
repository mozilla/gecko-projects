/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "Channel.h"

#include "base/process_util.h"

#include <sys/socket.h>
#include <sys/un.h>

namespace mozilla {
namespace recordreplay {

static void
GetSocketAddress(struct sockaddr_un* addr, base::ProcessId aMiddlemanPid, size_t aId)
{
  addr->sun_family = AF_UNIX;
  int n = snprintf(addr->sun_path, sizeof(addr->sun_path), "/tmp/WebReplay_%d_%d", aMiddlemanPid, (int) aId);
  MOZ_RELEASE_ASSERT(n >= 0 && n < (int) sizeof(addr->sun_path));
  addr->sun_len = SUN_LEN(addr);
}

struct HelloMessage
{
  int32_t mMagic;
};

Channel::Channel(size_t aId, const MessageHandler& aHandler)
  : mId(aId)
  , mHandler(aHandler)
  , mInitialized(false)
  , mConnectionFd(0)
  , mFd(0)
  , mMessageBytes(0)
{
  MOZ_RELEASE_ASSERT(NS_IsMainThread());

  if (IsRecordingOrReplaying()) {
    MOZ_ASSERT(IsRecordingOrReplaying());
    MOZ_ASSERT(AreThreadEventsPassedThrough());

    mFd = socket(AF_UNIX, SOCK_STREAM, 0);
    MOZ_RELEASE_ASSERT(mFd > 0);

    struct sockaddr_un addr;
    GetSocketAddress(&addr, child::MiddlemanProcessId(), mId);

    int rv = connect(mFd, (sockaddr*) &addr, SUN_LEN(&addr));
    MOZ_RELEASE_ASSERT(rv >= 0);

    DirectDeleteFile(addr.sun_path);
  } else {
    MOZ_RELEASE_ASSERT(IsMiddleman());

    mConnectionFd = socket(AF_UNIX, SOCK_STREAM, 0);
    MOZ_RELEASE_ASSERT(mConnectionFd > 0);

    struct sockaddr_un addr;
    GetSocketAddress(&addr, base::GetCurrentProcId(), mId);

    int rv = bind(mConnectionFd, (sockaddr*) &addr, SUN_LEN(&addr));
    MOZ_RELEASE_ASSERT(rv >= 0);

    rv = listen(mConnectionFd, 1);
    MOZ_RELEASE_ASSERT(rv >= 0);
  }

  Thread::SpawnNonRecordedThread(ThreadMain, this);
}

/* static */ void
Channel::ThreadMain(void* aChannelArg)
{
  Channel* channel = (Channel*) aChannelArg;

  static const int32_t MagicValue = 0x914522b9;

  if (IsRecordingOrReplaying()) {
    HelloMessage msg;

    int rv = recv(channel->mFd, &msg, sizeof(msg), MSG_WAITALL);
    MOZ_RELEASE_ASSERT(rv == sizeof(msg));
    MOZ_RELEASE_ASSERT(msg.mMagic == MagicValue);
  } else {
    MOZ_RELEASE_ASSERT(IsMiddleman());

    struct sockaddr_un addr;
    socklen_t len = sizeof(addr);
    channel->mFd = accept(channel->mConnectionFd, (sockaddr*) &addr, &len);
    MOZ_RELEASE_ASSERT(channel->mFd > 0);

    HelloMessage msg;
    msg.mMagic = MagicValue;

    int rv = send(channel->mFd, &msg, sizeof(msg), 0);
    MOZ_RELEASE_ASSERT(rv == sizeof(msg));
  }

  {
    MonitorAutoLock lock(channel->mMonitor);
    channel->mInitialized = true;
    channel->mMonitor.Notify();
  }

  while (true) {
    Message* msg = channel->WaitForMessage();
    if (!msg) {
      break;
    }
    channel->mHandler(msg);
  }
}

void
Channel::SendMessage(const Message& aMsg)
{
  MOZ_RELEASE_ASSERT(NS_IsMainThread() || aMsg.mType == MessageType::FatalError);

  // Block until the channel is initialized.
  if (!mInitialized) {
    MonitorAutoLock lock(mMonitor);
    while (!mInitialized) {
      mMonitor.Wait();
    }
  }

  PrintMessage("SendMsg", aMsg);

  const char* ptr = (const char*) &aMsg;
  size_t nbytes = aMsg.mSize;
  while (nbytes) {
    int rv = send(mFd, ptr, nbytes, 0);
    if (rv < 0) {
      MOZ_RELEASE_ASSERT(errno == EINTR);
    } else {
      MOZ_RELEASE_ASSERT((size_t) rv <= nbytes);
      ptr += rv;
      nbytes -= rv;
    }
  }
}

Message*
Channel::WaitForMessage()
{
  if (!mMessageBuffer.length()) {
    mMessageBuffer.appendN(0, 1048576);
  }

  size_t messageSize = 0;
  while (true) {
    if (mMessageBytes >= sizeof(Message)) {
      Message* msg = (Message*) mMessageBuffer.begin();
      messageSize = msg->mSize;
      if (mMessageBytes >= messageSize) {
        break;
      }
    }

    // Make sure the buffer is large enough for the entire incoming message.
    if (messageSize > mMessageBuffer.length()) {
      mMessageBuffer.appendN(0, messageSize - mMessageBuffer.length());
    }

    ssize_t nbytes = recv(mFd, &mMessageBuffer[mMessageBytes],
                          mMessageBuffer.length() - mMessageBytes, 0);
    if (nbytes < 0) {
      MOZ_RELEASE_ASSERT(errno == EAGAIN);
      continue;
    } else if (nbytes == 0) {
      // The other side of the channel has shut down.
      if (IsMiddleman()) {
        return nullptr;
      }
      PrintSpew("Channel disconnected, exiting...\n");
      DeleteSnapshotFiles();
      _exit(0);
    }

    mMessageBytes += nbytes;
  }

  Message* res = ((Message*)mMessageBuffer.begin())->Clone();

  // Remove the message we just received from the incoming buffer.
  size_t remaining = mMessageBytes - messageSize;
  if (remaining) {
    memmove(mMessageBuffer.begin(), &mMessageBuffer[messageSize], remaining);
  }
  mMessageBytes = remaining;

  PrintMessage("RecvMsg", *res);
  return res;
}

static char*
WideCharString(const char16_t* aBuffer, size_t aBufferSize)
{
  char* buf = new char[aBufferSize + 1];
  for (size_t i = 0; i < aBufferSize; i++) {
    buf[i] = aBuffer[i];
  }
  buf[aBufferSize] = 0;
  return buf;
}

void
Channel::PrintMessage(const char* aPrefix, const Message& aMsg)
{
  AutoEnsurePassThroughThreadEvents pt;
  char* data = nullptr;
  switch (aMsg.mType) {
  case MessageType::Paint: {
    const PaintMessage& nmsg = (const PaintMessage&) aMsg;
    data = new char[32];
    snprintf(data, 32, "%d", (int) HashBytes(nmsg.Buffer(), nmsg.BufferSize()));
    break;
  }
  case MessageType::HitCheckpoint: {
    const HitCheckpointMessage& nmsg = (const HitCheckpointMessage&) aMsg;
    data = new char[128];
    snprintf(data, 128, "Id %d", (int) nmsg.mCheckpointId);
    break;
  }
  case MessageType::HitBreakpoint: {
    const HitBreakpointMessage& nmsg = (const HitBreakpointMessage&) aMsg;
    data = new char[nmsg.NumBreakpoints() * 32];
    int pos = 0;
    for (size_t i = 0; i < nmsg.NumBreakpoints(); i++) {
      pos += snprintf(data + pos, 32, "Id %d", nmsg.Breakpoints()[i]);
    }
    break;
  }
  case MessageType::Resume: {
    const ResumeMessage& nmsg = (const ResumeMessage&) aMsg;
    data = new char[128];
    snprintf(data, 128, "Forward %d", nmsg.mForward);
    break;
  }
  case MessageType::SetBreakpoint: {
    const SetBreakpointMessage& nmsg = (const SetBreakpointMessage&) aMsg;
    data = new char[128];
    snprintf(data, 128, "Id %d, Kind %s, Script %d, Offset %d, Frame %d",
             (int) nmsg.mId, nmsg.mPosition.kindString(), (int) nmsg.mPosition.script,
             (int) nmsg.mPosition.offset, (int) nmsg.mPosition.frameIndex);
    break;
  }
  case MessageType::DebuggerRequest: {
    const DebuggerRequestMessage& nmsg = (const DebuggerRequestMessage&) aMsg;
    data = WideCharString(nmsg.Buffer(), nmsg.BufferSize());
    break;
  }
  case MessageType::DebuggerResponse: {
    const DebuggerResponseMessage& nmsg = (const DebuggerResponseMessage&) aMsg;
    data = WideCharString(nmsg.Buffer(), nmsg.BufferSize());
    break;
  }
  case MessageType::SetIsActive: {
    const SetIsActiveMessage& nmsg = (const SetIsActiveMessage&) aMsg;
    data = new char[32];
    snprintf(data, 32, "%d", nmsg.mActive);
    break;
  }
  case MessageType::SetSaveCheckpoint: {
    const SetSaveCheckpointMessage& nmsg = (const SetSaveCheckpointMessage&) aMsg;
    data = new char[128];
    snprintf(data, 128, "Id %d, Save %d", (int) nmsg.mCheckpoint, nmsg.mSave);
    break;
  }
  default:
    break;
  }
  const char* kind = IsMiddleman() ? "Middleman" : (IsRecording() ? "Recording" : "Replaying");
  PrintSpew("%s%s:%d %s %s\n", kind, aPrefix, (int) mId, aMsg.TypeString(), data ? data : "");
  delete[] data;
}

} // namespace recordreplay
} // namespace mozilla
