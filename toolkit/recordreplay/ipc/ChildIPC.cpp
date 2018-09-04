/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

// This file has the logic which the replayed process uses to communicate with
// the middleman process.

#include "ChildInternal.h"

#include "base/message_loop.h"
#include "base/task.h"
#include "chrome/common/child_thread.h"
#include "chrome/common/mach_ipc_mac.h"
#include "ipc/Channel.h"
#include "mac/handler/exception_handler.h"
#include "mozilla/dom/ContentChild.h"
#include "mozilla/layers/ImageDataSerializer.h"
#include "mozilla/Sprintf.h"
#include "mozilla/VsyncDispatcher.h"

#include "InfallibleVector.h"
#include "MemorySnapshot.h"
#include "ParentInternal.h"
#include "ProcessRecordReplay.h"
#include "ProcessRedirect.h"
#include "ProcessRewind.h"
#include "Thread.h"
#include "Units.h"

#include <algorithm>
#include <mach/mach_vm.h>
#include <unistd.h>

namespace mozilla {
namespace recordreplay {
namespace child {

///////////////////////////////////////////////////////////////////////////////
// Record/Replay IPC
///////////////////////////////////////////////////////////////////////////////

// Monitor used for various synchronization tasks.
Monitor* gMonitor;

// The singleton channel for communicating with the middleman.
Channel* gChannel;

static base::ProcessId gMiddlemanPid;
static base::ProcessId gParentPid;
static StaticInfallibleVector<char*> gParentArgv;

// File descriptors used by a pipe to create checkpoints when instructed by the
// parent process.
static FileHandle gCheckpointWriteFd;
static FileHandle gCheckpointReadFd;

// Copy of the introduction message we got from the middleman. This is saved on
// receipt and then processed during InitRecordingOrReplayingProcess.
static IntroductionMessage* gIntroductionMessage;

// When recording, whether developer tools server code runs in the middleman.
static bool gDebuggerRunsInMiddleman;

// Processing routine for incoming channel messages.
static void
ChannelMessageHandler(Message* aMsg)
{
  MOZ_RELEASE_ASSERT(MainThreadShouldPause() || aMsg->CanBeSentWhileUnpaused());

  switch (aMsg->mType) {
  case MessageType::Introduction: {
    MOZ_RELEASE_ASSERT(!gIntroductionMessage);
    gIntroductionMessage = (IntroductionMessage*) aMsg->Clone();
    break;
  }
  case MessageType::CreateCheckpoint: {
    MOZ_RELEASE_ASSERT(IsRecording());

    // Ignore requests to create checkpoints before we have reached the first
    // paint and finished initializing.
    if (navigation::IsInitialized()) {
      uint8_t data = 0;
      DirectWrite(gCheckpointWriteFd, &data, 1);
    }
    break;
  }
  case MessageType::SetDebuggerRunsInMiddleman: {
    MOZ_RELEASE_ASSERT(IsRecording());
    PauseMainThreadAndInvokeCallback([=]() { gDebuggerRunsInMiddleman = true; });
    break;
  }
  case MessageType::Terminate: {
    // Terminate messages behave differently in recording vs. replaying
    // processes. When sent to a recording process (which the middleman manages
    // directly) they signal that a clean shutdown is needed, while when sent
    // to a replaying process (which the UI process manages) they signal that
    // the process should crash, since it seems to be hanged.
    if (IsRecording()) {
      PrintSpew("Terminate message received, exiting...\n");
      _exit(0);
    } else {
      MOZ_CRASH("Hanged replaying process");
    }
  }
  case MessageType::SetIsActive: {
    const SetIsActiveMessage& nmsg = (const SetIsActiveMessage&) *aMsg;
    PauseMainThreadAndInvokeCallback([=]() { SetIsActiveChild(nmsg.mActive); });
    break;
  }
  case MessageType::SetAllowIntentionalCrashes: {
    const SetAllowIntentionalCrashesMessage& nmsg = (const SetAllowIntentionalCrashesMessage&) *aMsg;
    PauseMainThreadAndInvokeCallback([=]() { SetAllowIntentionalCrashes(nmsg.mAllowed); });
    break;
  }
  case MessageType::SetSaveCheckpoint: {
    const SetSaveCheckpointMessage& nmsg = (const SetSaveCheckpointMessage&) *aMsg;
    PauseMainThreadAndInvokeCallback([=]() { SetSaveCheckpoint(nmsg.mCheckpoint, nmsg.mSave); });
    break;
  }
  case MessageType::FlushRecording: {
    PauseMainThreadAndInvokeCallback(FlushRecording);
    break;
  }
  case MessageType::DebuggerRequest: {
    const DebuggerRequestMessage& nmsg = (const DebuggerRequestMessage&) *aMsg;
    js::CharBuffer* buf = new js::CharBuffer();
    buf->append(nmsg.Buffer(), nmsg.BufferSize());
    PauseMainThreadAndInvokeCallback([=]() { navigation::DebuggerRequest(buf); });
    break;
  }
  case MessageType::SetBreakpoint: {
    const SetBreakpointMessage& nmsg = (const SetBreakpointMessage&) *aMsg;
    PauseMainThreadAndInvokeCallback([=]() {
        navigation::SetBreakpoint(nmsg.mId, nmsg.mPosition);
      });
    break;
  }
  case MessageType::Resume: {
    const ResumeMessage& nmsg = (const ResumeMessage&) *aMsg;
    PauseMainThreadAndInvokeCallback([=]() {
        navigation::Resume(nmsg.mForward);
      });
    break;
  }
  case MessageType::RestoreCheckpoint: {
    const RestoreCheckpointMessage& nmsg = (const RestoreCheckpointMessage&) *aMsg;
    PauseMainThreadAndInvokeCallback([=]() {
        navigation::RestoreCheckpoint(nmsg.mCheckpoint);
      });
    break;
  }
  case MessageType::RunToPoint: {
    const RunToPointMessage& nmsg = (const RunToPointMessage&) *aMsg;
    PauseMainThreadAndInvokeCallback([=]() {
        navigation::RunToPoint(nmsg.mTarget);
      });
    break;
  }
  default:
    MOZ_CRASH();
  }

  free(aMsg);
}

// Main routine for a thread whose sole purpose is to listen to requests from
// the middleman process to create a new checkpoint. This is separate from the
// channel thread because this thread is recorded and the latter is not
// recorded. By communicating between the two threads with a pipe, this
// thread's behavior will be replicated exactly when replaying and new
// checkpoints will be created at the same point as during recording.
static void
ListenForCheckpointThreadMain(void*)
{
  while (true) {
    uint8_t data = 0;
    ssize_t rv = read(gCheckpointReadFd, &data, 1);
    if (rv > 0) {
      NS_DispatchToMainThread(NewRunnableFunction("NewCheckpoint", NewCheckpoint,
                                                  /* aTemporary = */ false));
    } else {
      MOZ_RELEASE_ASSERT(errno == EINTR);
    }
  }
}

void* gGraphicsShmem;

void
InitRecordingOrReplayingProcess(int* aArgc, char*** aArgv)
{
  if (!IsRecordingOrReplaying()) {
    return;
  }

  Maybe<int> middlemanPid;
  Maybe<int> channelID;
  for (int i = 0; i < *aArgc; i++) {
    if (!strcmp((*aArgv)[i], gMiddlemanPidOption)) {
      MOZ_RELEASE_ASSERT(middlemanPid.isNothing() && i + 1 < *aArgc);
      middlemanPid.emplace(atoi((*aArgv)[i + 1]));
    }
    if (!strcmp((*aArgv)[i], gChannelIDOption)) {
      MOZ_RELEASE_ASSERT(channelID.isNothing() && i + 1 < *aArgc);
      channelID.emplace(atoi((*aArgv)[i + 1]));
    }
  }
  MOZ_RELEASE_ASSERT(middlemanPid.isSome());
  MOZ_RELEASE_ASSERT(channelID.isSome());

  gMiddlemanPid = middlemanPid.ref();

  Maybe<AutoPassThroughThreadEvents> pt;
  pt.emplace();

  gMonitor = new Monitor();
  gChannel = new Channel(channelID.ref(), /* aMiddlemanRecording = */ false, ChannelMessageHandler);

  pt.reset();

  DirectCreatePipe(&gCheckpointWriteFd, &gCheckpointReadFd);

  Thread::StartThread(ListenForCheckpointThreadMain, nullptr, false);

  pt.emplace();

  // Setup a mach port to receive the graphics shmem handle over.
  ReceivePort receivePort(nsPrintfCString("WebReplay.%d.%d", gMiddlemanPid, (int) channelID.ref()).get());

  MachSendMessage handshakeMessage(parent::GraphicsHandshakeMessageId);
  handshakeMessage.AddDescriptor(MachMsgPortDescriptor(receivePort.GetPort(), MACH_MSG_TYPE_COPY_SEND));

  MachPortSender sender(nsPrintfCString("WebReplay.%d", gMiddlemanPid).get());
  kern_return_t kr = sender.SendMessage(handshakeMessage, 1000);
  MOZ_RELEASE_ASSERT(kr == KERN_SUCCESS);

  // The parent should send us a handle to the graphics shmem.
  MachReceiveMessage message;
  kr = receivePort.WaitForMessage(&message, 0);
  MOZ_RELEASE_ASSERT(kr == KERN_SUCCESS);
  MOZ_RELEASE_ASSERT(message.GetMessageID() == parent::GraphicsMemoryMessageId);
  mach_port_t graphicsPort = message.GetTranslatedPort(0);
  MOZ_RELEASE_ASSERT(graphicsPort != MACH_PORT_NULL);

  mach_vm_address_t address = 0;
  kr = mach_vm_map(mach_task_self(), &address, parent::GraphicsMemorySize, 0, VM_FLAGS_ANYWHERE,
                   graphicsPort, 0, false,
                   VM_PROT_READ | VM_PROT_WRITE, VM_PROT_READ | VM_PROT_WRITE,
                   VM_INHERIT_NONE);
  MOZ_RELEASE_ASSERT(kr == KERN_SUCCESS);

  gGraphicsShmem = (void*) address;

  // The graphics shared memory contents are excluded from snapshots. We do not
  // want checkpoint restores in this child to interfere with drawing being
  // performed by another child.
  AddInitialUntrackedMemoryRegion((uint8_t*) gGraphicsShmem, parent::GraphicsMemorySize);

  pt.reset();

  // We are ready to receive initialization messages from the middleman, pause
  // so they can be sent.
  HitCheckpoint(CheckpointId::Invalid, /* aRecordingEndpoint = */ false);

  // Process the introduction message to fill in arguments.
  MOZ_RELEASE_ASSERT(gParentArgv.empty());

  gParentPid = gIntroductionMessage->mParentPid;

  // Record/replay the introduction message itself so we get consistent args
  // between recording and replaying.
  {
    IntroductionMessage* msg = IntroductionMessage::RecordReplay(*gIntroductionMessage);

    const char* pos = msg->ArgvString();
    for (size_t i = 0; i < msg->mArgc; i++) {
      gParentArgv.append(strdup(pos));
      pos += strlen(pos) + 1;
    }

    free(msg);
  }

  free(gIntroductionMessage);
  gIntroductionMessage = nullptr;

  // Some argument manipulation code expects a null pointer at the end.
  gParentArgv.append(nullptr);

  MOZ_RELEASE_ASSERT(*aArgc >= 1);
  MOZ_RELEASE_ASSERT(gParentArgv.back() == nullptr);

  *aArgc = gParentArgv.length() - 1; // For the trailing null.
  *aArgv = gParentArgv.begin();

  // If we failed to initialize then report it to the user.
  if (gInitializationFailureMessage) {
    ReportFatalError(Nothing(), "%s", gInitializationFailureMessage);
    Unreachable();
  }
}

base::ProcessId
MiddlemanProcessId()
{
  return gMiddlemanPid;
}

base::ProcessId
ParentProcessId()
{
  return gParentPid;
}

bool
DebuggerRunsInMiddleman()
{
  return RecordReplayValue(gDebuggerRunsInMiddleman);
}

void
MaybeCreateInitialCheckpoint()
{
  NewCheckpoint(/* aTemporary = */ false);
}

void
ReportFatalError(const Maybe<MinidumpInfo>& aMinidump, const char* aFormat, ...)
{
  // Unprotect any memory which might be written while producing the minidump.
  UnrecoverableSnapshotFailure();

  AutoEnsurePassThroughThreadEvents pt;

#ifdef MOZ_CRASHREPORTER
  MinidumpInfo info = aMinidump.isSome()
                      ? aMinidump.ref()
                      : MinidumpInfo(EXC_CRASH, 1, 0, mach_thread_self());
  google_breakpad::ExceptionHandler::WriteForwardedExceptionMinidump
    (info.mExceptionType, info.mCode, info.mSubcode, info.mThread);
#endif

  va_list ap;
  va_start(ap, aFormat);
  char buf[2048];
  VsprintfLiteral(buf, aFormat, ap);
  va_end(ap);

  // Construct a FatalErrorMessage on the stack, to avoid touching the heap.
  char msgBuf[4096];
  size_t header = sizeof(FatalErrorMessage);
  size_t len = std::min(strlen(buf) + 1, sizeof(msgBuf) - header);
  FatalErrorMessage* msg = new(msgBuf) FatalErrorMessage(header + len);
  memcpy(&msgBuf[header], buf, len);
  msgBuf[sizeof(msgBuf) - 1] = 0;

  // Don't take the message lock when sending this, to avoid touching the heap.
  gChannel->SendMessage(*msg);

  DirectPrint("***** Fatal Record/Replay Error *****\n");
  DirectPrint(buf);
  DirectPrint("\n");

  // Block until we get a terminate message and die.
  Thread::WaitForeverNoIdle();
}

void
NotifyFlushedRecording()
{
  gChannel->SendMessage(RecordingFlushedMessage());
}

void
NotifyAlwaysMarkMajorCheckpoints()
{
  if (IsActiveChild()) {
    gChannel->SendMessage(AlwaysMarkMajorCheckpointsMessage());
  }
}

///////////////////////////////////////////////////////////////////////////////
// Vsyncs
///////////////////////////////////////////////////////////////////////////////

static VsyncObserver* gVsyncObserver;

void
SetVsyncObserver(VsyncObserver* aObserver)
{
  MOZ_RELEASE_ASSERT(!gVsyncObserver || !aObserver);
  gVsyncObserver = aObserver;
}

void
NotifyVsyncObserver()
{
  if (gVsyncObserver) {
    gVsyncObserver->NotifyVsync(TimeStamp::Now());
  }
}

///////////////////////////////////////////////////////////////////////////////
// Painting
///////////////////////////////////////////////////////////////////////////////

// Graphics memory is only written on the compositor thread and read on the
// main thread and by the middleman. The gPendingPaint flag is used to
// synchronize access, so that data is not read until the paint has completed.
static Maybe<PaintMessage> gPaintMessage;
static bool gPendingPaint;

// Target buffer for the draw target created by the child process widget.
static void* gDrawTargetBuffer;
static size_t gDrawTargetBufferSize;

already_AddRefed<gfx::DrawTarget>
DrawTargetForRemoteDrawing(LayoutDeviceIntSize aSize)
{
  MOZ_RELEASE_ASSERT(!NS_IsMainThread());

  if (aSize.IsEmpty()) {
    return nullptr;
  }

  gPaintMessage = Some(PaintMessage(aSize.width, aSize.height));

  gfx::IntSize size(aSize.width, aSize.height);
  size_t bufferSize = layers::ImageDataSerializer::ComputeRGBBufferSize(size, gSurfaceFormat);
  MOZ_RELEASE_ASSERT(bufferSize <= parent::GraphicsMemorySize);

  if (bufferSize != gDrawTargetBufferSize) {
    free(gDrawTargetBuffer);
    gDrawTargetBuffer = malloc(bufferSize);
    gDrawTargetBufferSize = bufferSize;
  }

  size_t stride = layers::ImageDataSerializer::ComputeRGBStride(gSurfaceFormat, aSize.width);
  RefPtr<gfx::DrawTarget> drawTarget =
    gfx::Factory::CreateDrawTargetForData(gfx::BackendType::SKIA, (uint8_t*) gDrawTargetBuffer,
                                          size, stride, gSurfaceFormat,
                                          /* aUninitialized = */ true);
  if (!drawTarget) {
    MOZ_CRASH();
  }

  return drawTarget.forget();
}

void
NotifyPaintStart()
{
  MOZ_RELEASE_ASSERT(NS_IsMainThread());

  NewCheckpoint(/* aTemporary = */ false);

  gPendingPaint = true;
}

void
WaitForPaintToComplete()
{
  MOZ_RELEASE_ASSERT(NS_IsMainThread());

  MonitorAutoLock lock(*gMonitor);
  while (gPendingPaint) {
    gMonitor->Wait();
  }
  if (IsActiveChild() && gPaintMessage.isSome()) {
    memcpy(gGraphicsShmem, gDrawTargetBuffer, gDrawTargetBufferSize);
    gChannel->SendMessage(gPaintMessage.ref());
  }
}

void
NotifyPaintComplete()
{
  MOZ_RELEASE_ASSERT(!NS_IsMainThread());

  MonitorAutoLock lock(*gMonitor);
  MOZ_RELEASE_ASSERT(gPendingPaint);
  gPendingPaint = false;
  gMonitor->Notify();
}

///////////////////////////////////////////////////////////////////////////////
// Checkpoint Messages
///////////////////////////////////////////////////////////////////////////////

// When recording, the time when the last HitCheckpoint message was sent.
static double gLastCheckpointTime;

// When recording and we are idle, the time when we became idle.
static double gIdleTimeStart;

void
BeginIdleTime()
{
  MOZ_RELEASE_ASSERT(IsRecording() && NS_IsMainThread() && !gIdleTimeStart);
  gIdleTimeStart = CurrentTime();
}

void
EndIdleTime()
{
  MOZ_RELEASE_ASSERT(IsRecording() && NS_IsMainThread() && gIdleTimeStart);

  // Erase the idle time from our measurements by advancing the last checkpoint
  // time.
  gLastCheckpointTime += CurrentTime() - gIdleTimeStart;
  gIdleTimeStart = 0;
}

void
HitCheckpoint(size_t aId, bool aRecordingEndpoint)
{
  MOZ_RELEASE_ASSERT(NS_IsMainThread());
  double time = CurrentTime();
  PauseMainThreadAndInvokeCallback([=]() {
      double duration = 0;
      if (aId > CheckpointId::First) {
        duration = time - gLastCheckpointTime;
        MOZ_RELEASE_ASSERT(duration > 0);
      }
      gChannel->SendMessage(HitCheckpointMessage(aId, aRecordingEndpoint, duration));
    });
  gLastCheckpointTime = time;
}

///////////////////////////////////////////////////////////////////////////////
// Debugger Messages
///////////////////////////////////////////////////////////////////////////////

void
RespondToRequest(const js::CharBuffer& aBuffer)
{
  DebuggerResponseMessage* msg =
    DebuggerResponseMessage::New(aBuffer.begin(), aBuffer.length());
  gChannel->SendMessage(*msg);
  free(msg);
}

void
HitBreakpoint(bool aRecordingEndpoint, const uint32_t* aBreakpoints, size_t aNumBreakpoints)
{
  MOZ_RELEASE_ASSERT(NS_IsMainThread());
  HitBreakpointMessage* msg =
    HitBreakpointMessage::New(aRecordingEndpoint, aBreakpoints, aNumBreakpoints);
  PauseMainThreadAndInvokeCallback([=]() {
      gChannel->SendMessage(*msg);
      free(msg);
    });
}

} // namespace child
} // namespace recordreplay
} // namespace mozilla
