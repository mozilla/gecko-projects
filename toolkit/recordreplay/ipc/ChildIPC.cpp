/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

// This file has the logic which the replayed process uses to communicate with
// the middleman process.

#include "ChildIPC.h"

#include "base/message_loop.h"
#include "base/task.h"
#include "chrome/common/child_thread.h"
#include "ipc/Channel.h"
#include "mozilla/layers/ImageDataSerializer.h"
#include "mozilla/Sprintf.h"
#include "mozilla/VsyncDispatcher.h"

#include "InfallibleVector.h"
#include "MemorySnapshot.h"
#include "Monitor.h"
#include "ProcessRecordReplay.h"
#include "ProcessRedirect.h"
#include "ProcessRewind.h"
#include "Thread.h"
#include "Units.h"

#include <algorithm>
#include <unistd.h>

namespace mozilla {
namespace recordreplay {
namespace child {

///////////////////////////////////////////////////////////////////////////////
// Record/Replay IPC
///////////////////////////////////////////////////////////////////////////////

// Monitor used for various synchronization tasks.
static Monitor* gMonitor;

// The singleton channel for communicating with the middleman.
Channel* gChannel;

static base::ProcessId gMiddlemanPid;
static base::ProcessId gParentPid;
static StaticInfallibleVector<char*> gParentArgv;

static char* gShmemPrefs;
static size_t gShmemPrefsLen;

// File descriptors used by a pipe to create checkpoints when instructed by the
// parent process.
static FileHandle gCheckpointWriteFd;
static FileHandle gCheckpointReadFd;

// Copy of the introduction message we got from the middleman.
static IntroductionMessage* gIntroductionMessage;

// Processing routine for incoming channel messages.
static void
ChannelMessageHandler(Message* aMsg)
{
  MOZ_RELEASE_ASSERT(MainThreadShouldPause() ||
                     aMsg->mType == MessageType::CreateCheckpoint ||
                     aMsg->mType == MessageType::Terminate);

  switch (aMsg->mType) {
  case MessageType::Introduction: {
    gIntroductionMessage = (IntroductionMessage*) aMsg->Clone();
    break;
  }
  case MessageType::CreateCheckpoint: {
    MOZ_RELEASE_ASSERT(IsRecording());
    uint8_t data = 0;
    DirectWrite(gCheckpointWriteFd, &data, 1);
    break;
  }
  case MessageType::Terminate: {
    PrintSpew("Terminate message received, exiting...\n");
    MOZ_RELEASE_ASSERT(IsRecording());
    _exit(0);
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
    JS::replay::CharBuffer* buf = js_new<JS::replay::CharBuffer>();
    if (!buf->append(nmsg.Buffer(), nmsg.BufferSize())) {
      MOZ_CRASH();
    }
    PauseMainThreadAndInvokeCallback([=]() { JS::replay::hooks.debugRequestReplay(buf); });
    break;
  }
  case MessageType::SetBreakpoint: {
    const SetBreakpointMessage& nmsg = (const SetBreakpointMessage&) *aMsg;
    PauseMainThreadAndInvokeCallback([=]() {
        JS::replay::hooks.setBreakpointReplay(nmsg.mId, nmsg.mPosition);
      });
    break;
  }
  case MessageType::Resume: {
    const ResumeMessage& nmsg = (const ResumeMessage&) *aMsg;
    PauseMainThreadAndInvokeCallback([=]() {
        // The hooks will not have been set yet for the primordial resume.
        if (JS::replay::hooks.resumeReplay) {
          JS::replay::hooks.resumeReplay(nmsg.mForward);
        } else {
          ResumeExecution();
        }
      });
    break;
  }
  case MessageType::RestoreCheckpoint: {
    const RestoreCheckpointMessage& nmsg = (const RestoreCheckpointMessage&) *aMsg;
    PauseMainThreadAndInvokeCallback([=]() {
        JS::replay::hooks.restoreCheckpointReplay(nmsg.mCheckpoint);
      });
    break;
  }
  default:
    MOZ_CRASH();
  }

  free(aMsg);
}

char*
PrefsShmemContents(size_t aPrefsLen)
{
  MOZ_RELEASE_ASSERT(aPrefsLen == gShmemPrefsLen);
  return gShmemPrefs;
}

// The singleton channel messaging thread.
static PRThread* gChannelThread = nullptr;

// Initialize hooks used by the replay debugger.
static void InitDebuggerHooks();

static void HitCheckpoint(size_t aId);

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

void
InitRecordingOrReplayingProcess(base::ProcessId aParentPid,
                                int* aArgc, char*** aArgv)
{
  if (!IsRecordingOrReplaying()) {
    return;
  }

  gMiddlemanPid = aParentPid;

  Maybe<int> channelID;
  for (int i = 0; i < *aArgc; i++) {
    if (!strcmp((*aArgv)[i], gChannelIDOption)) {
      MOZ_RELEASE_ASSERT(channelID.isNothing() && i + 1 < *aArgc);
      channelID.emplace(atoi((*aArgv)[i + 1]));
    }
  }
  MOZ_RELEASE_ASSERT(channelID.isSome());

  {
    AutoPassThroughThreadEvents pt;
    gMonitor = new Monitor();
    gChannel = new Channel(channelID.ref(), ChannelMessageHandler);
  }

  DirectCreatePipe(&gCheckpointWriteFd, &gCheckpointReadFd);

  Thread::StartThread(ListenForCheckpointThreadMain, nullptr, false);

  InitDebuggerHooks();

  // We are ready to receive initialization messages from the middleman, pause
  // to indicate this.
  HitCheckpoint(InvalidCheckpointId);

  // Process the introduction message to fill in arguments.
  MOZ_RELEASE_ASSERT(!gShmemPrefs);
  MOZ_RELEASE_ASSERT(gParentArgv.empty());

  gParentPid = gIntroductionMessage->mParentPid;

  // Record/replay the introduction message itself so we get consistent args
  // and prefs between recording and replaying.
  size_t introductionSize = RecordReplayValue(gIntroductionMessage->mSize);
  IntroductionMessage* msg = (IntroductionMessage*) malloc(introductionSize);
  if (IsRecording()) {
    memcpy(msg, gIntroductionMessage, introductionSize);
  }
  RecordReplayBytes(msg, introductionSize);

  gShmemPrefs = new char[msg->mPrefsLen];
  memcpy(gShmemPrefs, msg->PrefsData(), msg->mPrefsLen);
  gShmemPrefsLen = msg->mPrefsLen;

  const char* pos = msg->ArgvString();
  for (size_t i = 0; i < msg->mArgc; i++) {
    gParentArgv.append(strdup(pos));
    pos += strlen(pos) + 1;
  }

  // Some argument manipulation code expects a null pointer at the end.
  gParentArgv.append(nullptr);

  MOZ_RELEASE_ASSERT(*aArgc >= 1);
  MOZ_RELEASE_ASSERT(!strcmp((*aArgv)[0], gParentArgv[0]));
  MOZ_RELEASE_ASSERT(gParentArgv.back() == nullptr);

  *aArgc = gParentArgv.length() - 1; // For the trailing null.
  *aArgv = gParentArgv.begin();

  // If we failed to initialize then report it to the user.
  if (gInitializationFailureMessage) {
    ReportFatalError("%s", gInitializationFailureMessage);
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

static Atomic<bool, SequentiallyConsistent, Behavior::DontPreserve> gSentFatalErrorMessage;

void
ReportFatalError(const char* aFormat, ...)
{
  va_list ap;
  va_start(ap, aFormat);
  char buf[2048];
  VsprintfLiteral(buf, aFormat, ap);
  va_end(ap);

  // Only send one fatal error message per child process.
  if (!gSentFatalErrorMessage.exchange(true)) {
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

    DeleteSnapshotFiles();
    UnrecoverableSnapshotFailure();
  }

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

void
NotifyHitRecordingEndpoint()
{
  MOZ_RELEASE_ASSERT(NS_IsMainThread());
  MOZ_RELEASE_ASSERT(IsReplaying());
  PauseMainThreadAndInvokeCallback([=]() {
      gChannel->SendMessage(HitRecordingEndpointMessage());
    });
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

// Message and buffer for the compositor in the recording/replaying process to
// draw into. This is only written on the compositor thread and read on the
// main thread, using the gPendingPaint flag to synchronize accesses.
static PaintMessage* gPaintMessage;
static bool gPendingPaint;

already_AddRefed<gfx::DrawTarget>
DrawTargetForRemoteDrawing(LayoutDeviceIntSize aSize)
{
  MOZ_RELEASE_ASSERT(!NS_IsMainThread());

  gfx::IntSize size(aSize.width, aSize.height);

  if (!gPaintMessage ||
      aSize.width != (int) gPaintMessage->mWidth ||
      aSize.height != (int) gPaintMessage->mHeight)
  {
    free(gPaintMessage);
    size_t bufferSize = layers::ImageDataSerializer::ComputeRGBBufferSize(size, gSurfaceFormat);
    gPaintMessage = PaintMessage::New(bufferSize, aSize.width, aSize.height);
  }

  size_t stride = layers::ImageDataSerializer::ComputeRGBStride(gSurfaceFormat, aSize.width);
  RefPtr<gfx::DrawTarget> drawTarget =
    gfx::Factory::CreateDrawTargetForData(gfx::BackendType::SKIA,
                                          gPaintMessage->Buffer(),
                                          size, stride, gSurfaceFormat,
                                          /* aUninitialized = */ true);
  if (!drawTarget) {
    MOZ_CRASH();
  }

  return drawTarget.forget();
}

void
EndRemoteDrawing()
{
  MOZ_RELEASE_ASSERT(!NS_IsMainThread());
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
  if (IsActiveChild()) {
    gChannel->SendMessage(*gPaintMessage);
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
double gLastCheckpointTime;

// When recording and we are idle, the time when we became idle.
double gIdleTimeStart;

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

static void
HitCheckpoint(size_t aId)
{
  MOZ_RELEASE_ASSERT(NS_IsMainThread());
  PauseMainThreadAndInvokeCallback([=]() {
      double duration = 0;
      double time = CurrentTime();
      if (aId > FirstCheckpointId) {
        duration = time - gLastCheckpointTime;
        MOZ_RELEASE_ASSERT(duration > 0);
      }
      gLastCheckpointTime = time;
      gChannel->SendMessage(HitCheckpointMessage(aId, duration));
    });
}

///////////////////////////////////////////////////////////////////////////////
// Debugger Messages
///////////////////////////////////////////////////////////////////////////////

static void
DebuggerResponseHook(const JS::replay::CharBuffer& aBuffer)
{
  DebuggerResponseMessage* msg =
    DebuggerResponseMessage::New(aBuffer.begin(), aBuffer.length());
  gChannel->SendMessage(*msg);
  free(msg);
}

static void
HitBreakpoint(const uint32_t* aBreakpoints, size_t aNumBreakpoints)
{
  MOZ_RELEASE_ASSERT(NS_IsMainThread());
  HitBreakpointMessage* msg = HitBreakpointMessage::New(aBreakpoints, aNumBreakpoints);
  PauseMainThreadAndInvokeCallback([=]() {
      gChannel->SendMessage(*msg);
      free(msg);
    });
}

static void
PauseAfterRecoveringFromDivergence()
{
  MOZ_RELEASE_ASSERT(NS_IsMainThread());
  PauseMainThreadAndInvokeCallback([=]() {
      JS::replay::hooks.respondAfterRecoveringFromDivergence();
    });
}

static void
InitDebuggerHooks()
{
  JS::replay::hooks.hitBreakpointReplay = HitBreakpoint;
  JS::replay::hooks.hitCheckpointReplay = HitCheckpoint;
  JS::replay::hooks.debugResponseReplay = DebuggerResponseHook;
  JS::replay::hooks.pauseAndRespondAfterRecoveringFromDivergence = PauseAfterRecoveringFromDivergence;
  JS::replay::hooks.hitCurrentRecordingEndpointReplay = HitRecordingEndpoint;
  JS::replay::hooks.hitLastRecordingEndpointReplay = NotifyHitRecordingEndpoint;
  JS::replay::hooks.canRewindReplay = HasSavedCheckpoint;
}

} // namespace child
} // namespace recordreplay
} // namespace mozilla
