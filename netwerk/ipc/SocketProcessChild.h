/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_net_SocketProcessChild_h
#define mozilla_net_SocketProcessChild_h

#include "mozilla/net/PSocketProcessChild.h"
#include "nsRefPtrHashtable.h"
#include "mozilla/ipc/BackgroundParent.h"

namespace mozilla {
class ChildProfilerController;
}

namespace mozilla {
namespace net {

class SocketProcessBridgeParent;
class BackgroundDataBridgeParent;

// The IPC actor implements PSocketProcessChild in child process.
// This is allocated and kept alive by SocketProcessImpl.
class SocketProcessChild final : public PSocketProcessChild {
 public:
  SocketProcessChild();
  ~SocketProcessChild();

  static SocketProcessChild* GetSingleton();

  bool Init(base::ProcessId aParentPid, const char* aParentBuildID,
            MessageLoop* aIOLoop, IPC::Channel* aChannel);

  void ActorDestroy(ActorDestroyReason aWhy) override;

  mozilla::ipc::IPCResult RecvPreferenceUpdate(const Pref& aPref);
  mozilla::ipc::IPCResult RecvRequestMemoryReport(
      const uint32_t& generation, const bool& anonymize,
      const bool& minimizeMemoryUsage,
      const Maybe<mozilla::ipc::FileDescriptor>& DMDFile);
  mozilla::ipc::IPCResult RecvSetOffline(const bool& aOffline);
  mozilla::ipc::IPCResult RecvInitSocketProcessBridgeParent(
      const ProcessId& aContentProcessId,
      Endpoint<mozilla::net::PSocketProcessBridgeParent>&& aEndpoint);
  mozilla::ipc::IPCResult RecvInitProfiler(
      Endpoint<mozilla::PProfilerChild>&& aEndpoint);
  mozilla::ipc::IPCResult RecvSocketProcessTelemetryPing();

  PWebrtcTCPSocketChild* AllocPWebrtcTCPSocketChild(const Maybe<TabId>& tabId);
  bool DeallocPWebrtcTCPSocketChild(PWebrtcTCPSocketChild* aActor);
  PDNSRequestChild* AllocPDNSRequestChild(
      const nsCString& aHost, const OriginAttributes& aOriginAttributes,
      const uint32_t& aFlags);
  bool DeallocPDNSRequestChild(PDNSRequestChild*);
  mozilla::ipc::IPCResult RecvPHttpTransactionConstructor(
      PHttpTransactionChild* actor, const uint64_t& aChannelId) override;

  PHttpTransactionChild* AllocPHttpTransactionChild(const uint64_t& aChannelId);
  bool DeallocPHttpTransactionChild(PHttpTransactionChild* aActor);

  PFileDescriptorSetChild* AllocPFileDescriptorSetChild(
      const FileDescriptor& fd);
  bool DeallocPFileDescriptorSetChild(PFileDescriptorSetChild* aActor);

  PChildToParentStreamChild* AllocPChildToParentStreamChild();
  bool DeallocPChildToParentStreamChild(PChildToParentStreamChild* aActor);
  PParentToChildStreamChild* AllocPParentToChildStreamChild();
  bool DeallocPParentToChildStreamChild(PParentToChildStreamChild* aActor);
  PAltServiceChild* AllocPAltServiceChild();
  bool DeallocPAltServiceChild(PAltServiceChild* aActor);

  mozilla::ipc::IPCResult RecvNotifySocketProcessObservers(
      const nsCString& aTopic, const nsString& aData);
  mozilla::ipc::IPCResult RecvTopLevelOuterWindowId(
      const uint64_t& aOuterWindowId);

  void CleanUp();
  void DestroySocketProcessBridgeParent(ProcessId aId);

  void AddDataBridgeToMap(uint64_t channelId,
                          BackgroundDataBridgeParent* actor) {
    ipc::AssertIsOnBackgroundThread();
    mBackgroundDataBridgeMap.Put(channelId, actor);
  }

  void RemoveDataBridgeFromMap(uint64_t channelId) {
    ipc::AssertIsOnBackgroundThread();
    mBackgroundDataBridgeMap.Remove(channelId);
  }

  BackgroundDataBridgeParent* GetDataBridgeForChannel(uint64_t channelId) {
    ipc::AssertIsOnBackgroundThread();
    return mBackgroundDataBridgeMap.Get(channelId);
  }

  void SaveBackgroundThread() {
    ipc::AssertIsOnBackgroundThread();
    mBackgroundThread = NS_GetCurrentThread();
  }

  nsCOMPtr<nsIThread> mBackgroundThread;

 private:
  // Mapping of content process id and the SocketProcessBridgeParent.
  // This table keeps SocketProcessBridgeParent alive in socket process.
  nsRefPtrHashtable<nsUint32HashKey, SocketProcessBridgeParent>
      mSocketProcessBridgeParentMap;

  // Should only be accessed on the background thread.
  nsDataHashtable<nsUint64HashKey, BackgroundDataBridgeParent*>
      mBackgroundDataBridgeMap;

#ifdef MOZ_GECKO_PROFILER
  RefPtr<ChildProfilerController> mProfilerController;
#endif
};

}  // namespace net
}  // namespace mozilla

#endif  // mozilla_net_SocketProcessChild_h
