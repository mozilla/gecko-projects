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
#include "js/ReplayHooks.h"
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

static base::ProcessId gMiddlemanPid;
static base::ProcessId gParentPid;
static StaticInfallibleVector<char*> gParentArgv;

// File descriptors used by a pipe to take snapshots when instructed by the
// parent process.
static FileHandle gSnapshotWriteFd;
static FileHandle gSnapshotReadFd;

// Main routine for the channel messaging thread.
void
ChannelThreadMain(void*)
{
  channel::InitAndConnectChild(gMiddlemanPid);

  while (true) {
    channel::Message* msg = channel::WaitForMessage();
    switch (msg->mType) {
    case channel::MessageType::Introduction: {
      MOZ_RELEASE_ASSERT(gParentArgv.empty());

      MonitorAutoLock lock(*gMonitor);
      channel::IntroductionMessage& nmsg = *(channel::IntroductionMessage*) msg;

      gParentPid = nmsg.mParentPid;

      char* pos = nmsg.ArgvString();
      for (size_t i = 0; i < nmsg.mArgc; i++) {
        gParentArgv.append(strdup(pos));
        pos += strlen(pos) + 1;
      }

      // Some argument manipulation code expects a null pointer at the end.
      gParentArgv.append(nullptr);

      // Notify the main thread that we have finished initialization.
      gMonitor->Notify();
      break;
    }
    case channel::MessageType::Initialize: {
      channel::InitializeMessage* nmsg = (channel::InitializeMessage*) msg;
      gTakeSnapshots = nmsg->mTakeSnapshots;
      break;
    }
    case channel::MessageType::TakeSnapshot: {
      uint8_t data = 0;
      DirectWrite(gSnapshotWriteFd, &data, 1);
      break;
    }
    case channel::MessageType::SaveRecording: {
      MOZ_RELEASE_ASSERT(MainThreadShouldPause());
      channel::SaveRecordingMessage* nmsg = (channel::SaveRecordingMessage*) msg;
      PauseMainThreadAndInvokeCallback([=]() { SaveRecording(nmsg->Filename()); },
                                       /* aSynchronous = */ true);
      break;
    }
    case channel::MessageType::DebuggerRequest: {
      MOZ_RELEASE_ASSERT(MainThreadShouldPause());
      const channel::DebuggerRequestMessage& nmsg = *(channel::DebuggerRequestMessage*) msg;
      JS::replay::CharBuffer* buf = js_new<JS::replay::CharBuffer>();
      if (!buf->append(nmsg.Buffer(), nmsg.BufferSize())) {
        MOZ_CRASH();
      }
      PauseMainThreadAndInvokeCallback([=]() { JS::replay::hooks.debugRequestReplay(buf); },
                                       /* aSynchronous = */ false);
      break;
    }
    case channel::MessageType::Resume: {
      MOZ_RELEASE_ASSERT(MainThreadShouldPause());
      const channel::ResumeMessage& nmsg = *(channel::ResumeMessage*) msg;
      PauseMainThreadAndInvokeCallback([=]() {
          JS::replay::hooks.resumeReplay(nmsg.mForward, nmsg.mHitOtherBreakpoints);
        }, /* aSynchronous = */ true);
      break;
    }
    case channel::MessageType::Terminate:
      _exit(0);
      break;
    default:
      MOZ_CRASH();
    }
    free(msg);
  }
}

// The singleton channel messaging thread.
static PRThread* gChannelThread = nullptr;

// Initialize hooks used by the replay debugger.
static void InitDebuggerHooks();

static void
TakeSnapshotInternal()
{
  TakeSnapshot(/* aFinal = */ false);
}

// Main routine for a thread whose sole purpose is to listen to requests from
// the middleman process to take a snapshot. This is separate from the channel
// thread because this thread is recorded and the latter is not recorded. By
// communicating between the two threads with a pipe, this thread's behavior
// will be replicated exactly when replaying and snapshots will be taken at the
// same point as during recording.
static void
ListenForSnapshotThreadMain(void*)
{
  while (true) {
    uint8_t data = 0;
    ssize_t rv = read(gSnapshotReadFd, &data, 1);
    if (rv > 0) {
      NS_DispatchToMainThread(NewRunnableFunction("TakeSnapshotInternal", TakeSnapshotInternal));
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

  {
    AutoPassThroughThreadEvents pt;
    gMonitor = new Monitor();
    Thread::SpawnNonRecordedThread(ChannelThreadMain, nullptr);
  }

  DirectCreatePipe(&gSnapshotWriteFd, &gSnapshotReadFd);

  Thread::StartThread(ListenForSnapshotThreadMain, nullptr);

  InitDebuggerHooks();

  {
    // Wait for the channel thread to finish initialization.
    MonitorAutoLock lock(*gMonitor);
    while (gParentArgv.empty()) {
      gMonitor->Wait();
    }
  }

  MOZ_RELEASE_ASSERT(*aArgc >= 1);
  MOZ_RELEASE_ASSERT(!strcmp((*aArgv)[0], gParentArgv[0]));
  MOZ_RELEASE_ASSERT(gParentArgv.back() == nullptr);

  *aArgc = gParentArgv.length() - 1; // For the trailing null.
  *aArgv = gParentArgv.begin();

  // If we failed to initialize then report it to the user.
  if (gInitializationFailureMessage) {
    ReportFatalError(gInitializationFailureMessage);
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

void
ReportFatalError(const char* aError)
{
  Print("***** Fatal Record/Replay Error *****\n%s\n", aError);

  // Construct a FatalErrorMessage on the stack, to avoid touching the heap.
  char buf[4096];
  size_t header = sizeof(channel::FatalErrorMessage);
  size_t len = std::min(strlen(aError) + 1, sizeof(buf) - header);
  channel::FatalErrorMessage* msg = new(buf) channel::FatalErrorMessage(header + len);
  memcpy(&buf[header], aError, len);
  buf[sizeof(buf) - 1] = 0;

  channel::SendMessage(*msg);

  UnrecoverableSnapshotFailure();

  // Block until we get a terminate message and die.
  Thread::WaitForeverNoIdle();
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
// draw into. This is only accessed on the compositor thread.
static channel::PaintMessage* gPaintMessage;

already_AddRefed<gfx::DrawTarget>
DrawTargetForRemoteDrawing(LayoutDeviceIntSize aSize)
{
  MOZ_RELEASE_ASSERT(!NS_IsMainThread());

  if (!gPaintMessage ||
      aSize.width != (int) gPaintMessage->mWidth ||
      aSize.height != (int) gPaintMessage->mHeight)
  {
    free(gPaintMessage);
    gPaintMessage = channel::PaintMessage::New(aSize.width, aSize.height);
  }

  gfx::IntSize size(aSize.width, aSize.height);
  size_t stride = aSize.width * 4;
  RefPtr<gfx::DrawTarget> drawTarget =
    gfx::Factory::CreateDrawTargetForData(gfx::BackendType::SKIA,
                                          gPaintMessage->Buffer(),
                                          size, stride, channel::gSurfaceFormat,
                                          /* aUninitialized = */ true);
  if (!drawTarget) {
    MOZ_CRASH();
  }

  return drawTarget.forget();
}

static bool gPendingPaint;

void
EndRemoteDrawing()
{
  MOZ_RELEASE_ASSERT(!NS_IsMainThread());

  // Paint messages are not sent for interim snapshots, so that when rewinding
  // painting is done in the expected reverse order, regardless of which
  // snapshots have been recorded.
  if (gPaintMessage && !LastSnapshotIsInterim()) {
    SendMessage(*gPaintMessage);
  }
}

void
NotifyPaintStart()
{
  MOZ_RELEASE_ASSERT(NS_IsMainThread());

  TakeSnapshot(/* aFinal = */ false);

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
// Snapshot Messages
///////////////////////////////////////////////////////////////////////////////

static void
HitSnapshot(size_t aId, bool aFinal, bool aInterim, bool aRecorded)
{
  MOZ_RELEASE_ASSERT(NS_IsMainThread());
  PauseMainThreadAndInvokeCallback([=]() {
      channel::SendMessage(channel::HitSnapshotMessage(aId, aFinal, aInterim, aRecorded));
    }, /* aSynchronous = */ true);
}

///////////////////////////////////////////////////////////////////////////////
// Debugger Messages
///////////////////////////////////////////////////////////////////////////////

static void
DebuggerResponseHook(const JS::replay::CharBuffer& aBuffer)
{
  channel::DebuggerResponseMessage* msg =
    channel::DebuggerResponseMessage::New(aBuffer.begin(), aBuffer.length());
  channel::SendMessage(*msg);
  free(msg);
}

static void
HitBreakpoint(size_t aId)
{
  MOZ_RELEASE_ASSERT(NS_IsMainThread());
  PauseMainThreadAndInvokeCallback([=]() {
      channel::SendMessage(channel::HitBreakpointMessage(aId));
    }, /* aSynchronous = */ true);
}

static void
InitDebuggerHooks()
{
  JS::replay::hooks.hitBreakpointReplay = HitBreakpoint;
  JS::replay::hooks.hitSnapshotReplay = HitSnapshot;
  JS::replay::hooks.debugResponseReplay = DebuggerResponseHook;
}

} // namespace child
} // namespace recordreplay
} // namespace mozilla
