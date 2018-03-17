/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

// This file has the logic which the middleman process uses to communicate with
// the parent process and with the replayed process.

#include "ParentIPC.h"

#include "base/task.h"
#include "ipc/Channel.h"
#include "js/Proxy.h"
#include "mozilla/dom/ContentChild.h"
#include "mozilla/dom/ScriptSettings.h"
#include "mozilla/dom/TabChild.h"
#include "mozilla/layers/CompositorBridgeChild.h"
#include "mozilla/layers/LayerTransactionChild.h"
#include "mozilla/layers/PTextureChild.h"
#include "mozilla/ipc/GeckoChildProcessHost.h"
#include "mozilla/ipc/IOThreadChild.h"
#include "InfallibleVector.h"
#include "Monitor.h"
#include "nsCocoaFeatures.h"
#include "ParentRecovery.h"
#include "ProcessRecordReplay.h"
#include "ProcessRedirect.h"

#include <algorithm>

using std::min;

namespace mozilla {
namespace recordreplay {
namespace parent {

///////////////////////////////////////////////////////////////////////////////
// Parent IPC
///////////////////////////////////////////////////////////////////////////////

// Information about the child process.
static ipc::GeckoChildProcessHost* gChildProcess;
static bool gChildProcessIsRecording;
static char* gChildProcessFilename;

// Monitor used for synchronization between the forwarding message loop thread
// and the main thread.
static Monitor* gCommunicationMonitor;

static void TerminateChildProcess();
static void SendInitializeMessage();

static bool
HandleMessageInMiddleman(const IPC::Message& aMessage)
{
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
      type == dom::PBrowser::Msg_AsyncMessage__ID) {
    ipc::IProtocol::Result r = dom::ContentChild::GetSingleton()->PContentChild::OnMessageReceived(aMessage);
    if (r != ipc::IProtocol::MsgProcessed) {
      MOZ_CRASH();
    }
    if (type == dom::PContent::Msg_SetXPCOMProcessAttributes__ID) {
      // Preferences are initialized via the SetXPCOMProcessAttributes message.
      SendInitializeMessage();
    }
    return false;
  }

  // Handle messages that should only be sent to the middleman.
  if (type == dom::PContent::Msg_InitRendering__ID ||
      type == dom::PContent::Msg_SaveRecording__ID) {
    ipc::IProtocol::Result r = dom::ContentChild::GetSingleton()->PContentChild::OnMessageReceived(aMessage);
    if (r != ipc::IProtocol::MsgProcessed) {
      MOZ_CRASH();
    }
    return true;
  }

  if (type >= layers::PCompositorBridge::PCompositorBridgeStart &&
      type <= layers::PCompositorBridge::PCompositorBridgeEnd) {
    layers::CompositorBridgeChild* compositorChild = layers::CompositorBridgeChild::Get();
    ipc::IProtocol::Result r = compositorChild->OnMessageReceived(aMessage);
    if (r != ipc::IProtocol::MsgProcessed) {
      MOZ_CRASH();
    }
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
    MOZ_CRASH();
  }

  virtual const char* ProtocolName() const override {
    MOZ_CRASH();
  }

  static void ForwardMessageAsync(MiddlemanProtocol* aProtocol, Message* aMessage) {
    if (gChildProcessIsRecording) {
      if (!aProtocol->mChannel.Send(aMessage)) {
        MOZ_CRASH();
      }
    } else {
      delete aMessage;
    }
  }

  virtual Result OnMessageReceived(const Message& aMessage) override {
    //fprintf(stderr, "ASYNC_MSG %s\n", IPC::StringFromIPCMessageType(aMessage.type()));

    // Copy the message first, since HandleMessageInMiddleman may destructively
    // modify it through OnMessageReceived calls.
    Message* nMessage = new Message();
    nMessage->CopyFrom(aMessage);

    if (mSide == ipc::ChildSide && HandleMessageInMiddleman(aMessage)) {
      delete nMessage;
      return MsgProcessed;
    }

    mOppositeMessageLoop->PostTask(NewRunnableFunction("ForwardMessageAsync", ForwardMessageAsync,
                                                       mOpposite, nMessage));
    return MsgProcessed;
  }

  static void ForwardMessageSync(MiddlemanProtocol* aProtocol, Message* aMessage, Message** aReply) {
    MOZ_RELEASE_ASSERT(!*aReply);
    Message* nReply = new Message();
    if (!aProtocol->mChannel.Send(aMessage, nReply)) {
      MOZ_CRASH();
    }

    MonitorAutoLock lock(*gCommunicationMonitor);
    *aReply = nReply;
    gCommunicationMonitor->Notify();
  }

  virtual Result OnMessageReceived(const Message& aMessage, Message*& aReply) override {
    //fprintf(stderr, "SYNC_MSG %s\n", IPC::StringFromIPCMessageType(aMessage.type()));

    MOZ_RELEASE_ASSERT(gChildProcessIsRecording);
    MOZ_RELEASE_ASSERT(mSide == ipc::ParentSide);

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
    MOZ_RELEASE_ASSERT(!*aReply);
    Message* nReply = new Message();
    if (!aProtocol->mChannel.Call(aMessage, nReply)) {
      MOZ_CRASH();
    }

    MonitorAutoLock lock(*gCommunicationMonitor);
    *aReply = nReply;
    gCommunicationMonitor->Notify();
  }

  virtual Result OnCallReceived(const Message& aMessage, Message*& aReply) override {
    //fprintf(stderr, "SYNC_CALL %s\n", IPC::StringFromIPCMessageType(aMessage.type()));

    MOZ_RELEASE_ASSERT(gChildProcessIsRecording);
    MOZ_RELEASE_ASSERT(mSide == ipc::ParentSide);

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
    MOZ_CRASH();
  }

  virtual void OnChannelClose() override {
    MOZ_RELEASE_ASSERT(mSide == ipc::ChildSide);
    TerminateChildProcess();
    _exit(0);
  }

  virtual void OnChannelError() override {
    MOZ_CRASH();
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

// Message loop processed on the main thread.
static MessageLoop* gMainThreadMessageLoop;

static bool gParentProtocolOpened = false;

static void
SpawnChildProcess()
{
  MOZ_RELEASE_ASSERT(!gChildProcess);
  gChildProcess = new ipc::GeckoChildProcessHost(GeckoProcessType_Content);
  std::vector<std::string> extraArgs;
  ipc::GeckoChildProcessHost::RecordReplayKind recordReplayKind =
    gChildProcessIsRecording
    ? ipc::GeckoChildProcessHost::RecordReplayKind::Record
    : ipc::GeckoChildProcessHost::RecordReplayKind::Replay;
  nsAutoString recordReplayFile;
  recordReplayFile.Append(NS_ConvertUTF8toUTF16(gChildProcessFilename));
  if (!gChildProcess->LaunchAndWaitForProcessHandle(extraArgs, recordReplayKind, recordReplayFile)) {
    MOZ_CRASH();
  }
}

// Main routine for the forwarding message loop thread.
static void
ForwardingMessageLoopMain(void*)
{
  MessageLoop messageLoop;
  gForwardingMessageLoop = &messageLoop;

  gChildProtocol->mOppositeMessageLoop = gForwardingMessageLoop;

  SpawnChildProcess();

  gParentProtocol->Open(gChildProcess->GetChannel(),
                        base::GetProcId(gChildProcess->GetChildProcessHandle()));

  // Notify the main thread that we have finished initialization.
  {
    MonitorAutoLock lock(*gCommunicationMonitor);
    gParentProtocolOpened = true;
    gCommunicationMonitor->Notify();
  }

  messageLoop.Run();
}

static void ChannelThreadMain(void*);

// Initialize hooks used by the debugger.
static void InitDebuggerHooks();

// A saved introduction message for sending to any respawned children.
static channel::IntroductionMessage* gIntroductionMessage;

// The last time we received a message from the child.
static TimeStamp gLastMessageTime;

// Whether we are allowed to recover crashed/hung child processes.
static bool gRecoveryEnabled;

void
Initialize(int aArgc, char* aArgv[], base::ProcessId aParentPid, uint64_t aChildID,
           dom::ContentChild* aContentChild)
{
  MOZ_ASSERT(NS_IsMainThread());

  gChildProcessIsRecording = TestEnv("MIDDLEMAN_RECORD");

  gChildProcessFilename =
    gChildProcessIsRecording
    ? strdup(getenv("MIDDLEMAN_RECORD"))
    : strdup(getenv("MIDDLEMAN_REPLAY"));

  gRecoveryEnabled = !getenv("NO_RECOVERY");

  InitDebuggerHooks();
  channel::InitParent();

  gCommunicationMonitor = new Monitor();

  gMainThreadMessageLoop = MessageLoop::current();

  gParentProtocol = new MiddlemanProtocol(ipc::ParentSide);
  gChildProtocol = new MiddlemanProtocol(ipc::ChildSide);

  gParentProtocol->mOpposite = gChildProtocol;
  gChildProtocol->mOpposite = gParentProtocol;

  gParentProtocol->mOppositeMessageLoop = gMainThreadMessageLoop;

  if (!PR_CreateThread(PR_USER_THREAD, ForwardingMessageLoopMain, nullptr,
                       PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD, PR_JOINABLE_THREAD, 0)) {
    MOZ_CRASH();
  }

  // Wait for the forwarding message loop thread to finish initialization.
  {
    MonitorAutoLock lock(*gCommunicationMonitor);
    while (!gParentProtocolOpened) {
      gCommunicationMonitor->Wait();
    }
  }

  if (!aContentChild->Init(ipc::IOThreadChild::message_loop(), aParentPid,
                           ipc::IOThreadChild::channel(), aChildID, /* aIsForBrowser = */ true)) {
    MOZ_CRASH();
  }

  channel::ConnectParent();

  gIntroductionMessage = channel::IntroductionMessage::New(aParentPid, aArgc, aArgv);

  channel::SendMessage(*gIntroductionMessage);

  if (!PR_CreateThread(PR_USER_THREAD, ChannelThreadMain, nullptr,
                       PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD, PR_JOINABLE_THREAD, 0)) {
    MOZ_CRASH();
  }

  // Initialize the last message time so we can always compute a deadline when
  // waiting for the child to pause.
  gLastMessageTime = TimeStamp::Now();
}

static bool gRecordSnapshotsEnabled, gReplaySnapshotsEnabled;

static void
SendInitializeMessage()
{
  // The Initialize message is separate from the Introduction message because
  // we have not yet loaded prefs at the point where the latter is sent.

  gRecordSnapshotsEnabled = Preferences::GetBool("devtools.recordreplay.enableRecordRewinding");
  gReplaySnapshotsEnabled = Preferences::GetBool("devtools.recordreplay.enableReplayRewinding");

  // Force-disable snapshots with an env var for shell based testing.
  if (getenv("NO_SNAPSHOTS")) {
    gRecordSnapshotsEnabled = gReplaySnapshotsEnabled = false;
  }

  // Force-disable snapshots while recording on older versions of macOS.
  // The memory protection used when recording snapshots interferes with GCD
  // internals and the underlying cause has not been identified.
  // See bug 1446521.
  if (!nsCocoaFeatures::IsAtLeastVersion(12, 0)) {
    gRecordSnapshotsEnabled = false;
  }

  // Because recording processes can transition into replaying processes, if
  // recording snapshots are enabled then treat replaying snapshots as enabled
  // as well.
  if (gRecordSnapshotsEnabled) {
    gReplaySnapshotsEnabled = true;
  }

  bool takeSnapshots = gChildProcessIsRecording ? gRecordSnapshotsEnabled : gReplaySnapshotsEnabled;
  channel::SendMessage(channel::InitializeMessage(takeSnapshots));
}

static bool
CanRewindHook()
{
  // If snapshots are disabled while recording but enabled while replaying, we
  // can still rewind by spinning up a new replaying process. This is mainly
  // helpful for OS releases where recording snapshots are disabled.
  return gReplaySnapshotsEnabled;
}

// Whether the main thread is waiting on its child process to be terminated.
static bool gWaitingOnTerminateChildProcess;

static void
TerminateChildProcess()
{
  // The destructor for GeckoChildProcessHost will teardown the child process.
  delete gChildProcess;
  gChildProcess = nullptr;

  MonitorAutoLock lock(*gCommunicationMonitor);
  gWaitingOnTerminateChildProcess = false;
  gCommunicationMonitor->Notify();
}

static bool CanRecoverChildProcess();

static void
DeadChildProcess(const nsCString& aWhy)
{
  PrintSpew("DeadChildProcess: %s\n", aWhy.get());

  if (CanRecoverChildProcess()) {
    recovery::BeginRecovery();

    // The channel should get a single disconnect message as the old child
    // process is torn down.
    channel::AllowDisconnect();

    MOZ_RELEASE_ASSERT(!gWaitingOnTerminateChildProcess);
    gWaitingOnTerminateChildProcess = true;

    XRE_GetIOMessageLoop()->PostTask(NewRunnableFunction("TerminateChildProcess", TerminateChildProcess));

    {
      MonitorAutoLock lock(*gCommunicationMonitor);
      while (gWaitingOnTerminateChildProcess) {
        gCommunicationMonitor->Wait();
      }
    }

    SpawnChildProcess();

    channel::ConnectParent();
    channel::SendMessage(*gIntroductionMessage);
    channel::SendMessage(channel::InitializeMessage(/* aTakeSnapshots = */ true));

    gLastMessageTime = TimeStamp::Now();
  } else {
    dom::ContentChild::GetSingleton()->SendRecordReplayFatalError(aWhy);
    Thread::WaitForeverNoIdle();
  }
}

///////////////////////////////////////////////////////////////////////////////
// Receiving Messages
///////////////////////////////////////////////////////////////////////////////

// When messages are received from the replaying process, we usually want their
// handler to execute on the main thread. The main thread might be blocked
// while waiting for the child to unpause (see the comment under 'Replay IPC')
// above, so runnables associated with the replaying process have special
// handling.

// Any pending task to execute on the main thread, which handles a message from
// the replaying process.
static RefPtr<Runnable> gReplayMessageTask;

// Whether there is a pending task on the main thread's message loop to run the
// replay message task.
static bool gHasProcessMessageTask;

// Run a replay message task if there is one, returning whether there was one.
// This must be called on the main thread with gCommunicationMonitor held.
static bool
MaybeRunReplayMessageTask()
{
  MOZ_RELEASE_ASSERT(NS_IsMainThread());
  if (!gReplayMessageTask) {
    return false;
  }

  RefPtr<Runnable> task = gReplayMessageTask.forget();
  {
    MonitorAutoUnlock unlock(*gCommunicationMonitor);
    task->Run();
  }

  // Notify the replay message loop thread if it is waiting in
  // RecvChildTaskAsync for the existing task to execute.
  gCommunicationMonitor->NotifyAll();
  return true;
}

// Whether the child is paused and can receive messages. The debugger may only
// interact with the child when it is paused.
static bool gChildIsPaused;

// How many seconds to wait after unpausing before considering the child in a
// hung state.
static const size_t ChildHangSeconds = 5;

static void
SetChildIsPaused(bool aPaused)
{
  MOZ_RELEASE_ASSERT(aPaused == !gChildIsPaused);
  gChildIsPaused = aPaused;
}

// On the main thread, block until the child is paused, handling any incoming
// tasks sent by the replay message loop thread. If aPokeChild is set, then if
// the child process is recording it will be instructed to take a snapshot
// and pause.
static void
WaitUntilChildIsPaused(bool aPokeChild = false)
{
  MOZ_RELEASE_ASSERT(NS_IsMainThread());

  if (aPokeChild && gChildProcessIsRecording && !gChildIsPaused) {
    channel::SendMessage(channel::TakeSnapshotMessage());
  }

  while (!gChildIsPaused) {
    MonitorAutoLock lock(*gCommunicationMonitor);
    if (!MaybeRunReplayMessageTask()) {
      if (gRecoveryEnabled) {
        TimeStamp deadline = gLastMessageTime + TimeDuration::FromSeconds(ChildHangSeconds);
        if (TimeStamp::Now() >= deadline) {
          MonitorAutoUnlock unlock(*gCommunicationMonitor);
          nsAutoCString why("Child process non-responsive");
          DeadChildProcess(why);
        }
        gCommunicationMonitor->WaitUntil(deadline);
      } else {
        gCommunicationMonitor->Wait();
      }
    }
  }
}

// Runnable created on the main thread to handle any tasks sent by the replay
// message loop thread which were not handled while the main thread was blocked.
static void
MaybeProcessReplayMessageTask()
{
  MOZ_RELEASE_ASSERT(NS_IsMainThread());
  MonitorAutoLock lock(*gCommunicationMonitor);
  MOZ_RELEASE_ASSERT(gHasProcessMessageTask);
  gHasProcessMessageTask = false;
  MaybeRunReplayMessageTask();
}

template <typename MsgType>
static void
ReceiveAndDestroyMessage(void (*aFn)(const MsgType&), channel::Message* aMsg)
{
  MOZ_RELEASE_ASSERT(NS_IsMainThread());
  aFn(*static_cast<MsgType*>(aMsg));
  free(aMsg);
}

// Execute a task that processes a message received from the child. This is
// called on the channel thread, and the function executes asynchronously on
// the main thread.
template <typename MsgType>
static void
ReceiveChildMessageAsync(void (*aFn)(const MsgType&), channel::Message* aMsg)
{
  MOZ_RELEASE_ASSERT(!NS_IsMainThread());

  MonitorAutoLock lock(*gCommunicationMonitor);

  // If there is already a task, wait for the main thread to clear it.
  while (gReplayMessageTask) {
    gCommunicationMonitor->Wait();
  }

  gReplayMessageTask = NewRunnableFunction("ReceiveAndDestroyMessage",
                                           ReceiveAndDestroyMessage<MsgType>, aFn, aMsg);

  // Notify the main thread, if it is waiting in WaitUntilChildIsPaused.
  gCommunicationMonitor->NotifyAll();

  // Make sure there is a task on the main thread's message loop that can
  // process this task if necessary.
  if (!gHasProcessMessageTask) {
    gHasProcessMessageTask = true;
    gMainThreadMessageLoop->PostTask(NewRunnableFunction("MaybeProcessReplayMessageTask",
                                                         MaybeProcessReplayMessageTask));
  }
}

// Synchronously execute a task on the main thread.
template <typename MsgType>
static void
ReceiveChildMessage(void (*aFn)(const MsgType&), channel::Message* aMsg)
{
  ReceiveChildMessageAsync(aFn, aMsg);
  MonitorAutoLock lock(*gCommunicationMonitor);
  while (gReplayMessageTask) {
    gCommunicationMonitor->Wait();
  }
}

static void RecvPaint(const channel::PaintMessage& aMsg);
static void RecvHitSnapshot(const channel::HitSnapshotMessage& aMsg);
static void RecvHitBreakpoint(const channel::HitBreakpointMessage& aMsg);
static void RecvDebuggerResponse(const channel::DebuggerResponseMessage& aMsg);
static void RecvFatalError(const channel::FatalErrorMessage& aMsg);
static void RecvSaveRecording(const channel::SaveRecordingMessage& aMsg);

// Main routine for the thread which receives messages from the child process.
static void
ChannelThreadMain(void*)
{
  while (true) {
    channel::Message* msg = channel::WaitForMessage();
    gLastMessageTime = TimeStamp::Now();
    if (!recovery::NoteIncomingMessage(*msg)) {
      continue;
    }
    switch (msg->mType) {
    case channel::MessageType::Paint:
      ReceiveChildMessageAsync(RecvPaint, msg);
      break;
    case channel::MessageType::HitSnapshot:
      ReceiveChildMessageAsync(RecvHitSnapshot, msg);
      break;
    case channel::MessageType::HitBreakpoint:
      ReceiveChildMessageAsync(RecvHitBreakpoint, msg);
      break;
    case channel::MessageType::DebuggerResponse:
      ReceiveChildMessageAsync(RecvDebuggerResponse, msg);
      break;
    case channel::MessageType::FatalError:
      ReceiveChildMessageAsync(RecvFatalError, msg);
      break;
    case channel::MessageType::SaveRecording:
      ReceiveChildMessageAsync(RecvSaveRecording, msg);
      break;
    default:
      MOZ_CRASH();
    }
  }
}

static void
SendMessageNoteRecovery(const channel::Message& aMsg)
{
  recovery::NoteOutgoingMessage(aMsg);
  channel::SendMessage(aMsg);
}

///////////////////////////////////////////////////////////////////////////////
// Graphics Parent IPC
///////////////////////////////////////////////////////////////////////////////

static void
UpdateTitle(dom::TabChild* aTabChild)
{
  AutoSafeJSContext cx;

  nsString message;
  message.Append(u"DOMTitleChanged");

  JS::RootedString str(cx, JS_NewStringCopyZ(cx, gChildProcessIsRecording ? "RECORDING" : "REPLAYING"));
  if (!str) {
    return;
  }
  JS::RootedValue strValue(cx, JS::StringValue(str));

  JS::RootedObject jsonObject(cx, JS_NewObject(cx, nullptr));
  if (!jsonObject || !JS_DefineProperty(cx, jsonObject, "title", strValue, JSPROP_ENUMERATE)) {
    return;
  }

  JS::RootedValue jsonValue(cx, JS::ObjectValue(*jsonObject));
  JS::RootedValue transferValue(cx);

  dom::ipc::StructuredCloneData data;
  {
    ErrorResult rv;
    data.Write(cx, jsonValue, transferValue, rv);
    MOZ_RELEASE_ASSERT(!rv.Failed());
  }

  {
    nsresult rv = aTabChild->DoSendAsyncMessage(cx, message, data, nullptr, nullptr);
    MOZ_RELEASE_ASSERT(!NS_FAILED(rv));
  }
}

static uint64_t gLayerTreeId;
static layers::PLayerTransactionChild* gLayerTransactionChild;

// Action to clean up the current paint, to be performed after the next paint.
static std::function<void()> gDestroyAction;

static void
RecvPaint(const channel::PaintMessage& aMsg)
{
  MOZ_RELEASE_ASSERT(NS_IsMainThread());

  nsTArray<dom::PBrowserChild*> browsers;
  dom::ContentChild::GetSingleton()->ManagedPBrowserChild(browsers);

  dom::TabChild* activeBrowser = nullptr;
  for (size_t i = 0; i < browsers.Length(); i++) {
    dom::TabChild* browser = static_cast<dom::TabChild*>(browsers[i]);
    if (browser->WebWidget()->IsVisible()) {
      MOZ_RELEASE_ASSERT(!activeBrowser);
      activeBrowser = browser;
    }
  }
  if (!activeBrowser) {
    return;
  }

  UpdateTitle(activeBrowser);

  layers::CompositorBridgeChild* compositorChild = layers::CompositorBridgeChild::Get();

  nsTArray<layers::LayersBackend> backends;
  backends.AppendElement(layers::LayersBackend::LAYERS_BASIC);

  if (activeBrowser->LayersId() != gLayerTreeId) {
    gLayerTransactionChild =
      compositorChild->SendPLayerTransactionConstructor(backends, activeBrowser->LayersId());
    if (!gLayerTransactionChild) {
      MOZ_CRASH();
    }
    gLayerTreeId = activeBrowser->LayersId();
  }

  ipc::Shmem shmem;
  if (!compositorChild->AllocShmem(aMsg.BufferSize(),
                                   ipc::SharedMemory::TYPE_BASIC, &shmem)) {
    MOZ_CRASH();
  }

  memcpy(shmem.get<char>(), aMsg.Buffer(), aMsg.BufferSize());

  size_t width = aMsg.mWidth;
  size_t height = aMsg.mHeight;

  layers::BufferDescriptor bufferDesc =
    layers::RGBDescriptor(gfx::IntSize(width, height), channel::gSurfaceFormat,
                          /* hasIntermediateBuffer = */ false);
  layers::SurfaceDescriptor surfaceDesc =
    layers::SurfaceDescriptorBuffer(bufferDesc, layers::MemoryOrShmem(shmem));

  static uint64_t gTextureSerial = 0;

  wr::MaybeExternalImageId externalImageId;
  layers::PTextureChild* texture =
    compositorChild->CreateTexture(surfaceDesc,
                                   layers::LayersBackend::LAYERS_BASIC,
                                   layers::TextureFlags::DISALLOW_BIGIMAGE |
                                   layers::TextureFlags::IMMEDIATE_UPLOAD,
                                   ++gTextureSerial,
                                   externalImageId,
                                   nullptr);
  if (!texture) {
    MOZ_CRASH();
  }

  static uint64_t gCompositableId = 0;
  layers::CompositableHandle ContentCompositable(++gCompositableId);

  if (!gLayerTransactionChild->SendNewCompositable(ContentCompositable,
                                                   layers::TextureInfo(
                                                     layers::CompositableType::CONTENT_TILED))) {
    MOZ_CRASH();
  }

  static uint64_t gLayerId = 0;
  layers::LayerHandle RootLayer(++gLayerId);
  layers::LayerHandle ContentLayer(++gLayerId);

  nsTArray<layers::Edit> cset;
  cset.AppendElement(layers::OpCreateContainerLayer(RootLayer));
  cset.AppendElement(layers::OpCreatePaintedLayer(ContentLayer));
  cset.AppendElement(layers::OpSetRoot(RootLayer));
  cset.AppendElement(layers::OpPrependChild(RootLayer, ContentLayer));
  cset.AppendElement(layers::OpAttachCompositable(ContentLayer, ContentCompositable));

  nsTArray<layers::OpSetLayerAttributes> setAttrs;
  setAttrs.AppendElement(layers::OpSetLayerAttributes(RootLayer,
                           layers::LayerAttributes(
                             layers::CommonLayerAttributes(
                               LayerIntRegion(LayerIntRect(0, 0, width, height)),
                               layers::EventRegions(),
                               /* useClipRect = */ false,
                               ParentLayerIntRect(),
                               layers::LayerHandle(0),
                               nsTArray<layers::LayerHandle>(),
                               layers::CompositorAnimations(
                                 nsTArray<layers::Animation>(),
                                 0),
                               nsIntRegion(),
                               nsTArray<layers::ScrollMetadata>(),
                               nsCString()),
                             layers::ContainerLayerAttributes(1, 1, 1, 1, 1, false))));
  setAttrs.AppendElement(layers::OpSetLayerAttributes(ContentLayer,
                           layers::LayerAttributes(
                             layers::CommonLayerAttributes(
                               LayerIntRegion(LayerIntRect(0, 0, width, height)),
                               layers::EventRegions(),
                               /* useClipRect = */ false,
                               ParentLayerIntRect(),
                               layers::LayerHandle(0),
                               nsTArray<layers::LayerHandle>(),
                               layers::CompositorAnimations(
                                 nsTArray<layers::Animation>(),
                                 0),
                               nsIntRegion(),
                               nsTArray<layers::ScrollMetadata>(),
                               nsCString()),
                             layers::PaintedLayerAttributes(nsIntRegion(gfx::IntRect(0, 0, width, height))))));

  nsTArray<layers::TileDescriptor> tiles;
  tiles.AppendElement(layers::TexturedTileDescriptor(nullptr, texture,
                                                     layers::MaybeTexture(null_t()),
                                                     gfx::IntRect(0, 0, width, height),
                                                     layers::ReadLockDescriptor(null_t()),
                                                     layers::ReadLockDescriptor(null_t()),
                                                     /* wasPlaceholder = */ false));

  layers::SurfaceDescriptorTiles tileSurface(nsIntRegion(gfx::IntRect(0, 0, width, height)),
                                             tiles,
                                             gfx::IntPoint(0, 0),
                                             gfx::IntSize(width, height),
                                             /* firstTileX = */ 0,
                                             /* firstTileY = */ 0,
                                             /* retainedWidth = */ 1,
                                             /* retainedHeight = */ 1,
                                             /* resolution = */ 1,
                                             /* frameXResolution = */ 2,
                                             /* frameYResolution = */ 2,
                                             /* isProgressive = */ false);

  nsTArray<layers::CompositableOperation> paints;
  paints.AppendElement(layers::CompositableOperation(ContentCompositable,
                                                     layers::OpUseTiledLayerBuffer(tileSurface)));

  nsTArray<layers::OpDestroy> destroy;

  TimeStamp now = TimeStamp::Now();

  static uint64_t FwdTransactionId = 2;
  static uint64_t TransactionId = 1;
  static uint32_t PaintSequenceNumber = 0;

  layers::TargetConfig targetConfig(gfx::IntRect(0, 0, width, height),
                                    ROTATION_0,
                                    dom::eScreenOrientation_None,
                                    gfx::IntRect(0, 0, width, height));

  layers::TransactionInfo txn(cset,
                              nsTArray<layers::OpSetSimpleLayerAttributes>(),
                              setAttrs,
                              paints,
                              destroy,
                              FwdTransactionId,
                              TransactionId,
                              targetConfig,
                              nsTArray<layers::PluginWindowData>(),
                              /* isFirstPaint = */ true,
                              layers::FocusTarget(),
                              /* scheduleComposite = */ true,
                              PaintSequenceNumber,
                              /* isRepeatTransaction = */ false,
                              now,
                              TimeStamp());
  if (!gLayerTransactionChild->SendUpdate(txn)) {
    MOZ_CRASH();
  }

  if (!activeBrowser->SendForcePaintNoOp(activeBrowser->LayerObserverEpoch())) {
    MOZ_CRASH();
  }

  if (gDestroyAction) {
    gDestroyAction();
  }

  gDestroyAction = [=]() {
    if (!texture->SendDestroy() ||
        !gLayerTransactionChild->SendReleaseLayer(RootLayer) ||
        !gLayerTransactionChild->SendReleaseLayer(ContentLayer) ||
        !gLayerTransactionChild->SendReleaseCompositable(ContentCompositable)) {
      MOZ_CRASH();
    }
  };

  FwdTransactionId++;
  TransactionId++;
  PaintSequenceNumber++;
}

///////////////////////////////////////////////////////////////////////////////
// Core IPC
///////////////////////////////////////////////////////////////////////////////

// The last snapshot which the child process reached.
static size_t gLastSnapshot;

// The last snapshot in the child process, zero if unknown.
static size_t gFinalSnapshot;

// Update snapshot state when we encounter a new or old snapshot.
static void
HandleUpdatesForSnapshot(size_t aSnapshot, bool aFinal)
{
  gLastSnapshot = aSnapshot;
  if (aFinal) {
    MOZ_RELEASE_ASSERT(!gFinalSnapshot || gFinalSnapshot == aSnapshot);
    gFinalSnapshot = aSnapshot;
  }
}

static void
RecvFatalError(const channel::FatalErrorMessage& aMsg)
{
  nsAutoCString str(aMsg.Error());
  DeadChildProcess(str);
}

static void
RecvSaveRecording(const channel::SaveRecordingMessage& aMsg)
{
  if (gChildProcessFilename)
    free(gChildProcessFilename);
  gChildProcessFilename = strdup(aMsg.Filename());
}

static bool
CanRecoverChildProcess()
{
  return gRecoveryEnabled
      && !gChildProcessIsRecording
      && strcmp(gChildProcessFilename, "*")
      && !gChildIsPaused
      && gReplaySnapshotsEnabled
      && gLastSnapshot
      && !recovery::IsRecovering();
}

///////////////////////////////////////////////////////////////////////////////
// Debugger Messages
///////////////////////////////////////////////////////////////////////////////

// Buffer for receiving the next debugger response.
static JS::replay::CharBuffer* gResponseBuffer;

static void
RecvDebuggerResponse(const channel::DebuggerResponseMessage& aMsg)
{
  MOZ_RELEASE_ASSERT(gResponseBuffer);
  if (!gResponseBuffer->append(aMsg.Buffer(), aMsg.BufferSize())) {
    MOZ_CRASH();
  }

  // Unpause the main thread from its wait under HookDebuggerRequest.
  MOZ_RELEASE_ASSERT(!gChildIsPaused);
  SetChildIsPaused(true);
}

static void
HookDebuggerRequest(const JS::replay::CharBuffer& aBuffer, JS::replay::CharBuffer* aResponse)
{
  WaitUntilChildIsPaused(/* aPokeChild = */ true);

  // The child will need to unpause while it answers the query we are sending it.
  MOZ_RELEASE_ASSERT(!gResponseBuffer);
  gResponseBuffer = aResponse;
  SetChildIsPaused(false);

  channel::DebuggerRequestMessage* msg =
    channel::DebuggerRequestMessage::New(aBuffer.begin(), aBuffer.length());
  SendMessageNoteRecovery(*msg);
  free(msg);

  // Wait for the child to respond to the query.
  WaitUntilChildIsPaused();
  MOZ_RELEASE_ASSERT(gResponseBuffer == aResponse);
  MOZ_RELEASE_ASSERT(gResponseBuffer->length() != 0);
  gResponseBuffer = nullptr;
}

static void
HookSetBreakpoint(size_t aId, const JS::replay::ExecutionPosition& aPosition)
{
  WaitUntilChildIsPaused(/* aPokeChild = */ true);

  SendMessageNoteRecovery(channel::SetBreakpointMessage(aId, aPosition));
}

// Flags for the preferred direction of travel when execution unpauses,
// according to the last direction we were explicitly given.
static bool gChildExecuteForward = true;
static bool gChildExecuteBackward = false;

// Whether there is a ResumeForwardOrBackward task which should execute on the
// main thread. This will continue execution in the preferred direction.
static bool gResumeForwardOrBackward = false;

static void
HookResume(bool aForward, bool aHitOtherBreakpoints)
{
  WaitUntilChildIsPaused();

  // Set the preferred direction of travel.
  gResumeForwardOrBackward = false;
  gChildExecuteForward = aForward;
  gChildExecuteBackward = !aForward;

  // Don't resume if we are at the beginning or end of the replay and can't go
  // in the desired direction.
  if (aForward ? (gFinalSnapshot && gFinalSnapshot == gLastSnapshot) : !gLastSnapshot) {
    return;
  }

  // If the child is recording, rewinding will convert it to a replaying process.
  if (!aForward) {
    gChildProcessIsRecording = false;
  }

  SetChildIsPaused(false);
  SendMessageNoteRecovery(channel::ResumeMessage(aForward, aHitOtherBreakpoints));

  // Enter a wait so that we will detect whether the child process is
  // non-responsive, even without further input from the debugger.
  if (!gChildProcessIsRecording) {
    WaitUntilChildIsPaused(false);
  }
}

static void
HookPause()
{
  WaitUntilChildIsPaused(/* aPokeChild = */ true);

  // If the debugger has explicitly paused then there is no preferred direction
  // of travel.
  gChildExecuteForward = false;
  gChildExecuteBackward = false;
}

static void
ResumeForwardOrBackward(bool aHitOtherBreakpoints)
{
  MOZ_RELEASE_ASSERT(!gChildExecuteForward || !gChildExecuteBackward);

  if (gResumeForwardOrBackward && (gChildExecuteForward || gChildExecuteBackward)) {
    HookResume(gChildExecuteForward, aHitOtherBreakpoints);
  }
}

static void
RecvHitSnapshot(const channel::HitSnapshotMessage& aMsg)
{
  MOZ_RELEASE_ASSERT(!gChildIsPaused);

  HandleUpdatesForSnapshot(aMsg.mSnapshotId, aMsg.mFinal);

  // Interim snapshots do not pause the child process (these are generated when
  // we rewound past the point of the last snapshot we were trying to get to).
  if (aMsg.mInterim) {
    return;
  }

  SetChildIsPaused(true);

  // Resume either forwards or backwards. Break the resume off into a separate
  // runnable, to avoid starving any debugger code already on the stack and
  // waiting for the process to pause.
  if (!gResumeForwardOrBackward) {
    gResumeForwardOrBackward = true;
    gMainThreadMessageLoop->PostTask(NewRunnableFunction("ResumeForwardOrBackward",
                                                         ResumeForwardOrBackward,
                                                         /* aHitOtherBreakpoints = */ false));
  }
}

static void
HitBreakpoint(size_t aBreakpointId)
{
  AutoSafeJSContext cx;

  MOZ_RELEASE_ASSERT(!gResumeForwardOrBackward);
  gResumeForwardOrBackward = true;

  // FIXME what happens if this throws?
  (void) JS::replay::hooks.hitBreakpointMiddleman(cx, aBreakpointId);

  // If the child was not explicitly resumed by the breakpoint handler, resume
  // travel in whichever direction it was going previously. If there are other
  // breakpoints at the current source location, call them instead.
  if (gResumeForwardOrBackward) {
    ResumeForwardOrBackward(/* aHitOtherBreakpoints = */ true);
  }
}

static void
RecvHitBreakpoint(const channel::HitBreakpointMessage& aMsg)
{
  MOZ_RELEASE_ASSERT(!gChildIsPaused);

  SetChildIsPaused(true);

  gMainThreadMessageLoop->PostTask(NewRunnableFunction("HitBreakpoint", HitBreakpoint,
                                                       aMsg.mBreakpointId));
}

static void
SaveRecordingInternal(channel::SaveRecordingMessage* aMsg)
{
  WaitUntilChildIsPaused(/* aPokeChild = */ true);

  channel::SendMessage(*aMsg);
  free(aMsg);
}

void
SaveRecording(const nsCString& aFilename)
{
  MOZ_RELEASE_ASSERT(IsMiddleman());
  channel::SaveRecordingMessage* msg = channel::SaveRecordingMessage::New(aFilename.get());
  gMainThreadMessageLoop->PostTask(NewRunnableFunction("SaveRecordingInternal", SaveRecordingInternal, msg));
}

static void
InitDebuggerHooks()
{
  JS::replay::hooks.debugRequestMiddleman = HookDebuggerRequest;
  JS::replay::hooks.setBreakpointMiddleman = HookSetBreakpoint;
  JS::replay::hooks.resumeMiddleman = HookResume;
  JS::replay::hooks.pauseMiddleman = HookPause;
  JS::replay::hooks.canRewindMiddleman = CanRewindHook;
}

} // namespace parent
} // namespace recordreplay
} // namespace mozilla
