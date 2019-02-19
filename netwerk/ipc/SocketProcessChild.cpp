/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SocketProcessChild.h"
#include "SocketProcessLogging.h"

#include "base/task.h"
#include "AltServiceChild.h"
#include "HttpInfo.h"
#include "HttpTransactionChild.h"
#include "mozilla/Assertions.h"
#include "mozilla/dom/MemoryReportRequest.h"
#include "mozilla/ipc/CrashReporterClient.h"
#include "mozilla/ipc/FileDescriptorSetChild.h"
#include "mozilla/ipc/IPCStreamAlloc.h"
#include "mozilla/ipc/ProcessChild.h"
#include "mozilla/net/DNSRequestChild.h"
#include "mozilla/ipc/PChildToParentStreamChild.h"
#include "mozilla/ipc/PParentToChildStreamChild.h"
#include "mozilla/Preferences.h"
#include "nsDebugImpl.h"
#include "nsSocketTransportService2.h"
#include "nsSupportsPrimitives.h"
#include "nsThreadManager.h"
#include "ProcessUtils.h"
#include "SocketProcessBridgeParent.h"

#ifdef MOZ_GECKO_PROFILER
#  include "ChildProfilerController.h"
#endif

#ifdef MOZ_WEBRTC
#  include "mozilla/net/WebrtcTCPSocketChild.h"
#endif

namespace mozilla {
namespace net {

using namespace ipc;

static SocketProcessChild* sSocketProcessChild;

SocketProcessChild::SocketProcessChild() {
  LOG(("CONSTRUCT SocketProcessChild::SocketProcessChild\n"));
  nsDebugImpl::SetMultiprocessMode("Socket");

  MOZ_COUNT_CTOR(SocketProcessChild);
  sSocketProcessChild = this;
}

SocketProcessChild::~SocketProcessChild() {
  LOG(("DESTRUCT SocketProcessChild::SocketProcessChild\n"));
  MOZ_COUNT_DTOR(SocketProcessChild);
  sSocketProcessChild = nullptr;
}

/* static */
SocketProcessChild* SocketProcessChild::GetSingleton() {
  return sSocketProcessChild;
}

#if defined(XP_MACOSX)
extern "C" {
void CGSShutdownServerConnections();
};
#endif

bool SocketProcessChild::Init(base::ProcessId aParentPid,
                              const char* aParentBuildID, MessageLoop* aIOLoop,
                              IPC::Channel* aChannel) {
  if (NS_WARN_IF(NS_FAILED(nsThreadManager::get().Init()))) {
    return false;
  }
  if (NS_WARN_IF(!Open(aChannel, aParentPid, aIOLoop))) {
    return false;
  }
  // This must be sent before any IPDL message, which may hit sentinel
  // errors due to parent and content processes having different
  // versions.
  MessageChannel* channel = GetIPCChannel();
  if (channel && !channel->SendBuildIDsMatchMessage(aParentBuildID)) {
    // We need to quit this process if the buildID doesn't match the parent's.
    // This can occur when an update occurred in the background.
    ProcessChild::QuickExit();
  }

  // Init crash reporter support.
  CrashReporterClient::InitSingleton(this);

  if (NS_FAILED(NS_InitMinimalXPCOM())) {
    return false;
  }

  SetThisProcessName("Socket Process");
#if defined(XP_MACOSX)
  // Close all current connections to the WindowServer. This ensures that the
  // Activity Monitor will not label the socket process as "Not responding"
  // because it's not running a native event loop. See bug 1384336.
  CGSShutdownServerConnections();
#endif  // XP_MACOSX
  return true;
}

void SocketProcessChild::ActorDestroy(ActorDestroyReason aWhy) {
  LOG(("SocketProcessChild::ActorDestroy\n"));

  if (AbnormalShutdown == aWhy) {
    NS_WARNING("Shutting down Socket process early due to a crash!");
    ProcessChild::QuickExit();
  }

#ifdef MOZ_GECKO_PROFILER
  if (mProfilerController) {
    mProfilerController->Shutdown();
    mProfilerController = nullptr;
  }
#endif

  CrashReporterClient::DestroySingleton();
  XRE_ShutdownChildProcess();
}

void SocketProcessChild::CleanUp() {
  LOG(("SocketProcessChild::CleanUp\n"));

  for (auto iter = mSocketProcessBridgeParentMap.Iter(); !iter.Done();
       iter.Next()) {
    if (!iter.Data()->Closed()) {
      iter.Data()->Close();
    }
  }
  NS_ShutdownXPCOM(nullptr);
}

IPCResult SocketProcessChild::RecvPreferenceUpdate(const Pref& aPref) {
  Preferences::SetPreference(aPref);
  return IPC_OK();
}

mozilla::ipc::IPCResult SocketProcessChild::RecvRequestMemoryReport(
    const uint32_t& aGeneration, const bool& aAnonymize,
    const bool& aMinimizeMemoryUsage,
    const Maybe<ipc::FileDescriptor>& aDMDFile) {
  nsPrintfCString processName("SocketProcess");

  mozilla::dom::MemoryReportRequestClient::Start(
      aGeneration, aAnonymize, aMinimizeMemoryUsage, aDMDFile, processName,
      [&](const MemoryReport& aReport) {
        Unused << GetSingleton()->SendAddMemoryReport(aReport);
      },
      [&](const uint32_t& aGeneration) {
        return GetSingleton()->SendFinishMemoryReport(aGeneration);
      });
  return IPC_OK();
}

mozilla::ipc::IPCResult SocketProcessChild::RecvSetOffline(
    const bool& aOffline) {
  LOG(("SocketProcessChild::RecvSetOffline aOffline=%d\n", aOffline));

  nsCOMPtr<nsIIOService> io(do_GetIOService());
  NS_ASSERTION(io, "IO Service can not be null");

  io->SetOffline(aOffline);

  return IPC_OK();
}

mozilla::ipc::IPCResult SocketProcessChild::RecvPHttpTransactionConstructor(
    PHttpTransactionChild* actor, const uint64_t& aChannelId) {
  return IPC_OK();
}

PHttpTransactionChild* SocketProcessChild::AllocPHttpTransactionChild(
    const uint64_t& aChannelId) {
  if (!gHttpHandler) {
    nsresult rv;
    nsCOMPtr<nsIIOService> ios = do_GetIOService(&rv);
    if (NS_FAILED(rv)) {
      return nullptr;
    }

    nsCOMPtr<nsIProtocolHandler> handler;
    rv = ios->GetProtocolHandler("http", getter_AddRefs(handler));
    if (NS_FAILED(rv)) {
      return nullptr;
    }

    // Initialize DNS Service here, since it needs to be done in main thread.
    nsCOMPtr<nsIDNSService> dns =
        do_GetService("@mozilla.org/network/dns-service;1", &rv);
    if (NS_FAILED(rv)) {
      return nullptr;
    }
  }
  HttpTransactionChild* p = new HttpTransactionChild(aChannelId);
  p->AddRef();
  return p;
}

bool SocketProcessChild::DeallocPHttpTransactionChild(
    PHttpTransactionChild* aActor) {
  LOG(("SocketProcessChild::DeallocPHttpTransactionChild aActor=%p\n", aActor));

  HttpTransactionChild* p = static_cast<HttpTransactionChild*>(aActor);
  p->Release();
  return true;
}

PFileDescriptorSetChild* SocketProcessChild::AllocPFileDescriptorSetChild(
    const FileDescriptor& aFD) {
  return new FileDescriptorSetChild(aFD);
}

bool SocketProcessChild::DeallocPFileDescriptorSetChild(
    PFileDescriptorSetChild* aActor) {
  delete aActor;
  return true;
}

PChildToParentStreamChild*
SocketProcessChild::AllocPChildToParentStreamChild() {
  MOZ_CRASH("PChildToParentStreamChild actors should be manually constructed!");
}

bool SocketProcessChild::DeallocPChildToParentStreamChild(
    PChildToParentStreamChild* aActor) {
  delete aActor;
  return true;
}

PParentToChildStreamChild*
SocketProcessChild::AllocPParentToChildStreamChild() {
  return mozilla::ipc::AllocPParentToChildStreamChild();
}

bool SocketProcessChild::DeallocPParentToChildStreamChild(
    PParentToChildStreamChild* aActor) {
  delete aActor;
  return true;
}

mozilla::ipc::IPCResult SocketProcessChild::RecvInitSocketProcessBridgeParent(
    const ProcessId& aContentProcessId,
    Endpoint<mozilla::net::PSocketProcessBridgeParent>&& aEndpoint) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(!mSocketProcessBridgeParentMap.Get(aContentProcessId, nullptr));

  mSocketProcessBridgeParentMap.Put(
      aContentProcessId,
      new SocketProcessBridgeParent(aContentProcessId, std::move(aEndpoint)));
  return IPC_OK();
}

mozilla::ipc::IPCResult SocketProcessChild::RecvInitProfiler(
    Endpoint<PProfilerChild>&& aEndpoint) {
#ifdef MOZ_GECKO_PROFILER
  mProfilerController =
      mozilla::ChildProfilerController::Create(std::move(aEndpoint));
#endif
  return IPC_OK();
}

mozilla::ipc::IPCResult SocketProcessChild::RecvSocketProcessTelemetryPing() {
  const uint32_t kExpectedUintValue = 42;
  Telemetry::ScalarSet(Telemetry::ScalarID::TELEMETRY_TEST_SOCKET_ONLY_UINT,
                       kExpectedUintValue);
  return IPC_OK();
}

void SocketProcessChild::DestroySocketProcessBridgeParent(ProcessId aId) {
  MOZ_ASSERT(NS_IsMainThread());

  mSocketProcessBridgeParentMap.Remove(aId);
}

PAltServiceChild* SocketProcessChild::AllocPAltServiceChild() {
  return new AltServiceChild();
}

bool SocketProcessChild::DeallocPAltServiceChild(PAltServiceChild* aActor) {
  delete static_cast<AltServiceChild*>(aActor);
  return true;
}

PWebrtcTCPSocketChild* SocketProcessChild::AllocPWebrtcTCPSocketChild(
    const Maybe<TabId>& tabId) {
  // We don't allocate here: instead we always use IPDL constructor that takes
  // an existing object
  MOZ_ASSERT_UNREACHABLE(
      "AllocPWebrtcTCPSocketChild should not be called on"
      " socket child");
  return nullptr;
}

bool SocketProcessChild::DeallocPWebrtcTCPSocketChild(
    PWebrtcTCPSocketChild* aActor) {
#ifdef MOZ_WEBRTC
  WebrtcTCPSocketChild* child = static_cast<WebrtcTCPSocketChild*>(aActor);
  child->ReleaseIPDLReference();
#endif
  return true;
}

mozilla::ipc::IPCResult SocketProcessChild::RecvNotifySocketProcessObservers(
    const nsCString& aTopic, const nsString& aData) {
  if (gHttpHandler) {
    gHttpHandler->Observe(nullptr, aTopic.get(), aData.get());
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult SocketProcessChild::RecvTopLevelOuterWindowId(
    const uint64_t& aOuterWindowId) {
  if (gHttpHandler) {
    nsCOMPtr<nsISupportsPRUint64> wrapper = new nsSupportsPRUint64();
    wrapper->SetData(aOuterWindowId);
    gHttpHandler->Observe(
        wrapper, "net:current-toplevel-outer-content-windowid", nullptr);
  }
  return IPC_OK();
}

PDNSRequestChild* SocketProcessChild::AllocPDNSRequestChild(
    const nsCString& aHost, const OriginAttributes& aOriginAttributes,
    const uint32_t& aFlags) {
  // We don't allocate here: instead we always use IPDL constructor that takes
  // an existing object
  MOZ_ASSERT_UNREACHABLE("AllocPDNSRequestChild should not be called on child");
  return nullptr;
}

bool SocketProcessChild::DeallocPDNSRequestChild(PDNSRequestChild* aChild) {
  DNSRequestChild* p = static_cast<DNSRequestChild*>(aChild);
  p->ReleaseIPDLReference();
  return true;
}

class HttpConnectionDataResolver final {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(HttpConnectionDataResolver)

  explicit HttpConnectionDataResolver(
      SocketProcessChild::GetHttpConnectionDataResolver&& aResolve)
      : mResolve(std::move(aResolve)) {}

  void OnResolve(nsTArray<HttpRetParams>&& aData) {
    MOZ_ASSERT(OnSocketThread());

    RefPtr<HttpConnectionDataResolver> self = this;
    mData = std::move(aData);
    NS_DispatchToMainThread(NS_NewRunnableFunction(
        "net::HttpConnectionDataResolver::OnResolve",
        [self{std::move(self)}]() { self->mResolve(std::move(self->mData)); }));
  }

 private:
  ~HttpConnectionDataResolver() = default;

  SocketProcessChild::GetHttpConnectionDataResolver mResolve;
  nsTArray<HttpRetParams> mData;
};

mozilla::ipc::IPCResult SocketProcessChild::RecvGetHttpConnectionData(
    GetHttpConnectionDataResolver&& aResolve) {
  if (!gSocketTransportService) {
    aResolve(nsTArray<HttpRetParams>());
    return IPC_OK();
  }

  RefPtr<HttpConnectionDataResolver> resolver =
      new HttpConnectionDataResolver(std::move(aResolve));
  gSocketTransportService->Dispatch(
      NS_NewRunnableFunction(
          "net::SocketProcessChild::RecvGetHttpConnectionData",
          [resolver{std::move(resolver)}]() {
            nsTArray<HttpRetParams> data;
            HttpInfo::GetHttpConnectionData(&data);
            resolver->OnResolve(std::move(data));
          }),
      NS_DISPATCH_NORMAL);
  return IPC_OK();
}

}  // namespace net
}  // namespace mozilla
