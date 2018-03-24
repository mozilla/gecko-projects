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
namespace channel {

static void
GetSocketAddress(struct sockaddr_un* addr, base::ProcessId aMiddlemanPid)
{
  addr->sun_family = AF_UNIX;
  int n = snprintf(addr->sun_path, sizeof(addr->sun_path), "/tmp/WebReplay_%d", aMiddlemanPid);
  if (n < 0 || n >= (int) sizeof(addr->sun_path)) {
    MOZ_CRASH();
  }
  addr->sun_len = SUN_LEN(addr);
}

// Descriptor used to accept connections on the parent side.
static int gConnectionFd;

// Descriptor used to communicate with the other side.
static int gChannelFd;

static PRLock* gLock;

void
InitParent()
{
  MOZ_ASSERT(IsMiddleman());

  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) {
    MOZ_CRASH();
  }

  struct sockaddr_un addr;
  GetSocketAddress(&addr, base::GetCurrentProcId());

  if (bind(fd, (sockaddr*) &addr, SUN_LEN(&addr)) < 0) {
    MOZ_CRASH();
  }

  if (listen(fd, 1) < 0) {
    MOZ_CRASH();
  }

  gConnectionFd = fd;
  gLock = PR_NewLock();
}

struct HelloMessage
{
  int32_t mMagic;
};

static const int32_t MagicValue = 0x914522b9 + 0;

void
ConnectParent()
{
  MOZ_ASSERT(IsMiddleman());

  struct sockaddr_un addr;
  socklen_t len = sizeof(addr);
  gChannelFd = accept(gConnectionFd, (sockaddr*) &addr, &len);
  if (gChannelFd < 0) {
    MOZ_CRASH();
  }

  HelloMessage msg;
  msg.mMagic = MagicValue;

  if (send(gChannelFd, &msg, sizeof(msg), 0) != sizeof(msg)) {
    MOZ_CRASH();
  }
}

void
InitAndConnectChild(base::ProcessId aMiddlemanPid)
{
  MOZ_ASSERT(IsRecordingOrReplaying());
  MOZ_ASSERT(AreThreadEventsPassedThrough());

  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) {
    MOZ_CRASH();
  }

  struct sockaddr_un addr;
  GetSocketAddress(&addr, aMiddlemanPid);

  if (connect(fd, (sockaddr*) &addr, SUN_LEN(&addr)) < 0) {
    MOZ_CRASH();
  }

  gChannelFd = fd;

  HelloMessage msg;

  if (recv(gChannelFd, &msg, sizeof(msg), MSG_WAITALL) != sizeof(msg)) {
    MOZ_CRASH();
  }

  MOZ_RELEASE_ASSERT(msg.mMagic == MagicValue);

  gLock = PR_NewLock();
}

void
SendMessage(const Message& aMsg, bool aTakeLock)
{
  if (aTakeLock) {
    PR_Lock(gLock);
  }

  PrintMessage("SEND_MSG", aMsg);

  const char* ptr = (const char*) &aMsg;
  size_t nbytes = aMsg.mSize;
  while (nbytes) {
    int rv = send(gChannelFd, ptr, nbytes, 0);
    if (rv < 0) {
      MOZ_RELEASE_ASSERT(errno == EINTR);
    } else {
      MOZ_RELEASE_ASSERT((size_t) rv <= nbytes);
      ptr += rv;
      nbytes -= rv;
    }
  }

  if (aTakeLock) {
    PR_Unlock(gLock);
  }
}

// Buffer for message data received from the other side of the channel.
static StaticInfallibleVector<char, 0, AllocPolicy<UntrackedMemoryKind::Generic>> gMessageBuffer;

// The number of bytes of data already in the message buffer.
static size_t gMessageBytes;

static inline size_t
NeededMessageSize()
{
  if (gMessageBytes >= sizeof(Message)) {
    Message* msg = (Message*) gMessageBuffer.begin();
    return msg->mSize;
  }
  return sizeof(Message);
}

static size_t gNumAllowedDisconnects;

void
AllowDisconnect()
{
  gNumAllowedDisconnects++;
}

Message*
WaitForMessage()
{
  if (!gMessageBuffer.length())
    gMessageBuffer.appendN(0, 1048576);

  while (gMessageBytes < NeededMessageSize()) {
    // Make sure the buffer is large enough for the entire incoming message.
    if (NeededMessageSize() > gMessageBuffer.length()) {
      gMessageBuffer.appendN(0, NeededMessageSize() - gMessageBuffer.length());
    }

    ssize_t nbytes = recv(gChannelFd, &gMessageBuffer[gMessageBytes],
                          gMessageBuffer.length() - gMessageBytes, 0);
    if (nbytes < 0) {
      if (errno == EAGAIN) {
        continue;
      }
      MOZ_CRASH();
    } else if (nbytes == 0) {
      // The other side of the connection has shut down.
      PrintSpew("Channel disconnected.\n");
      if (gNumAllowedDisconnects) {
        gNumAllowedDisconnects--;
      } else {
        _exit(0);
      }
    }

    gMessageBytes += nbytes;
  }

  Message* res = ((Message*)gMessageBuffer.begin())->Clone();

  // Remove the message we just received from the incoming buffer.
  size_t remaining = gMessageBytes - NeededMessageSize();
  if (remaining) {
    memmove(gMessageBuffer.begin(), &gMessageBuffer[NeededMessageSize()], remaining);
  }
  gMessageBytes = remaining;

  PrintMessage("RECV_MSG", *res);
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
PrintMessage(const char* aPrefix, const Message& aMsg)
{
  AutoEnsurePassThroughThreadEvents pt;
  char* data = nullptr;
  switch (aMsg.mType) {
  case MessageType::Paint: {
    const PaintMessage& nmsg = *(const PaintMessage*)&aMsg;
    data = new char[32];
    snprintf(data, 32, "%d", (int) HashBytes(nmsg.Buffer(), nmsg.BufferSize()));
    break;
  }
  case MessageType::HitSnapshot: {
    const HitSnapshotMessage& nmsg = *(const HitSnapshotMessage*)&aMsg;
    data = new char[128];
    snprintf(data, 128, "Id %d, Final %d, Interim %d",
             (int) nmsg.mSnapshotId, nmsg.mFinal, nmsg.mInterim);
    break;
  }
  case MessageType::HitBreakpoint: {
    const HitBreakpointMessage& nmsg = *(const HitBreakpointMessage*)&aMsg;
    data = new char[128];
    snprintf(data, 128, "Id %d", (int) nmsg.mBreakpointId);
    break;
  }
  case MessageType::Resume: {
    const ResumeMessage& nmsg = *(const ResumeMessage*)&aMsg;
    data = new char[128];
    snprintf(data, 128, "Forward %d, HitOtherBreakpoints %d", nmsg.mForward, nmsg.mHitOtherBreakpoints);
    break;
  }
  case MessageType::SetBreakpoint: {
    const SetBreakpointMessage& nmsg = *(const SetBreakpointMessage*)&aMsg;
    data = new char[128];
    snprintf(data, 128, "Id %d, Kind %s, Script %d, Offset %d, Frame %d",
             (int) nmsg.mId, nmsg.mPosition.kindString(), (int) nmsg.mPosition.script,
             (int) nmsg.mPosition.offset, (int) nmsg.mPosition.frameIndex);
    break;
  }
  case MessageType::DebuggerRequest: {
    const DebuggerRequestMessage& nmsg = *(const DebuggerRequestMessage*)&aMsg;
    data = WideCharString(nmsg.Buffer(), nmsg.BufferSize());
    break;
  }
  case MessageType::DebuggerResponse: {
    const DebuggerResponseMessage& nmsg = *(const DebuggerResponseMessage*)&aMsg;
    data = WideCharString(nmsg.Buffer(), nmsg.BufferSize());
    break;
  }
  default:
    break;
  }
  PrintSpew("%s %d %s %s\n", aPrefix, getpid(), aMsg.TypeString(), data ? data : "");
  delete[] data;
}

} // namespace channel
} // namespace recordreplay
} // namespace mozilla
