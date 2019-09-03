/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "StunAddrsRequestParent.h"

#include "../runnable_utils.h"
#include "nsNetUtil.h"

#include "mtransport/nricectx.h"
#include "mtransport/nricemediastream.h"  // needed only for including nricectx.h
#include "mtransport/nricestunaddr.h"

#include "../mdns_service/mdns_service.h"

extern "C" {
#include "local_addr.h"
}

using namespace mozilla::ipc;

namespace mozilla {
namespace net {

StunAddrsRequestParent::StunAddrsRequestParent() : mIPCClosed(false) {
  NS_GetMainThread(getter_AddRefs(mMainThread));

  nsresult res;
  mSTSThread = do_GetService(NS_SOCKETTRANSPORTSERVICE_CONTRACTID, &res);
  MOZ_ASSERT(mSTSThread);
}

StunAddrsRequestParent::~StunAddrsRequestParent() {
  ASSERT_ON_THREAD(mMainThread);

  if (mSharedMDNSService) {
    mSharedMDNSService = nullptr;
  }
}

mozilla::ipc::IPCResult StunAddrsRequestParent::RecvGetStunAddrs() {
  ASSERT_ON_THREAD(mMainThread);

  if (mIPCClosed) {
    return IPC_OK();
  }

  RUN_ON_THREAD(mSTSThread,
                WrapRunnable(RefPtr<StunAddrsRequestParent>(this),
                             &StunAddrsRequestParent::GetStunAddrs_s),
                NS_DISPATCH_NORMAL);

  return IPC_OK();
}

mozilla::ipc::IPCResult StunAddrsRequestParent::RecvRegisterMDNSHostname(
    const nsCString& aHostname, const nsCString& aAddress) {
  ASSERT_ON_THREAD(mMainThread);

  if (mIPCClosed) {
    return IPC_OK();
  }

  mSharedMDNSService->RegisterHostname(aHostname.BeginReading(),
                                       aAddress.BeginReading());

  return IPC_OK();
}

mozilla::ipc::IPCResult StunAddrsRequestParent::RecvUnregisterMDNSHostname(
    const nsCString& aHostname) {
  ASSERT_ON_THREAD(mMainThread);

  if (mIPCClosed) {
    return IPC_OK();
  }

  mSharedMDNSService->UnregisterHostname(aHostname.BeginReading());

  return IPC_OK();
}

mozilla::ipc::IPCResult StunAddrsRequestParent::Recv__delete__() {
  // see note below in ActorDestroy
  mIPCClosed = true;
  return IPC_OK();
}

void StunAddrsRequestParent::ActorDestroy(ActorDestroyReason why) {
  // We may still have refcount>0 if we haven't made it through
  // GetStunAddrs_s and SendStunAddrs_m yet, but child process
  // has crashed.  We must not send any more msgs to child, or
  // IPDL will kill chrome process, too.
  mIPCClosed = true;
}

void StunAddrsRequestParent::GetStunAddrs_s() {
  ASSERT_ON_THREAD(mSTSThread);

  // get the stun addresses while on STS thread
  NrIceStunAddrArray addrs = NrIceCtx::GetStunAddrs();

  if (mIPCClosed) {
    return;
  }

  // in order to return the result over IPC, we need to be on main thread
  RUN_ON_THREAD(
      mMainThread,
      WrapRunnable(RefPtr<StunAddrsRequestParent>(this),
                   &StunAddrsRequestParent::SendStunAddrs_m, std::move(addrs)),
      NS_DISPATCH_NORMAL);
}

void StunAddrsRequestParent::SendStunAddrs_m(const NrIceStunAddrArray& addrs) {
  ASSERT_ON_THREAD(mMainThread);

  if (mIPCClosed) {
    // nothing to do: child probably crashed
    return;
  }

  // This means that the mDNS service will continue running until shutdown
  // once started. The StunAddrsRequestParent destructor does not run until
  // shutdown anyway (see Bug 1569311), so there is not much we can do about
  // this here. One option would be to add a check if there are no hostnames
  // registered after UnregisterHostname is called, and if so, stop the mDNS
  // service at that time (see Bug 1569955.)
  if (!mSharedMDNSService) {
    for (auto& addr : addrs) {
      nr_transport_addr* local_addr =
          const_cast<nr_transport_addr*>(&addr.localAddr().addr);
      if (addr.localAddr().addr.ip_version == NR_IPV4 &&
          !nr_transport_addr_is_loopback(local_addr)) {
        char addrstring[16];
        nr_transport_addr_get_addrstring(local_addr, addrstring, 16);
        mSharedMDNSService = new MDNSServiceWrapper(addrstring);
        break;
      }
    }
  }

  // send the new addresses back to the child
  Unused << SendOnStunAddrsAvailable(addrs);
}

StaticRefPtr<StunAddrsRequestParent::MDNSServiceWrapper>
    StunAddrsRequestParent::mSharedMDNSService;

NS_IMPL_ADDREF(StunAddrsRequestParent)
NS_IMPL_RELEASE(StunAddrsRequestParent)

StunAddrsRequestParent::MDNSServiceWrapper::MDNSServiceWrapper(
    const std::string& ifaddr)
    : ifaddr(ifaddr) {}

void StunAddrsRequestParent::MDNSServiceWrapper::RegisterHostname(
    const char* hostname, const char* address) {
  StartIfRequired();
  if (mMDNSService) {
    mdns_service_register_hostname(mMDNSService, hostname, address);
  }
}

void StunAddrsRequestParent::MDNSServiceWrapper::UnregisterHostname(
    const char* hostname) {
  StartIfRequired();
  if (mMDNSService) {
    mdns_service_unregister_hostname(mMDNSService, hostname);
  }
}

StunAddrsRequestParent::MDNSServiceWrapper::~MDNSServiceWrapper() {
  if (mMDNSService) {
    mdns_service_stop(mMDNSService);
    mMDNSService = nullptr;
  }
}

void StunAddrsRequestParent::MDNSServiceWrapper::StartIfRequired() {
  if (!mMDNSService) {
    mMDNSService = mdns_service_start(ifaddr.c_str());
  }
}

NS_IMPL_ADDREF(StunAddrsRequestParent::MDNSServiceWrapper)
NS_IMPL_RELEASE(StunAddrsRequestParent::MDNSServiceWrapper)

}  // namespace net
}  // namespace mozilla
