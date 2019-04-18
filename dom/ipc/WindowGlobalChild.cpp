/* -*- Mode: C++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 8 -*- */
/* vim: set sw=2 ts=8 et tw=80 ft=cpp : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/WindowGlobalChild.h"

#include "mozilla/ClearOnShutdown.h"
#include "mozilla/dom/BrowsingContext.h"
#include "mozilla/dom/MozFrameLoaderOwnerBinding.h"
#include "mozilla/dom/TabChild.h"
#include "mozilla/dom/WindowGlobalActorsBinding.h"
#include "mozilla/dom/WindowGlobalParent.h"
#include "mozilla/ipc/InProcessChild.h"
#include "nsDocShell.h"
#include "nsGlobalWindowInner.h"

#include "mozilla/dom/JSWindowActorBinding.h"
#include "mozilla/dom/JSWindowActorChild.h"
#include "mozilla/dom/JSWindowActorService.h"
#include "nsIHttpChannelInternal.h"

using namespace mozilla::ipc;
using namespace mozilla::dom::ipc;

namespace mozilla {
namespace dom {

typedef nsRefPtrHashtable<nsUint64HashKey, WindowGlobalChild> WGCByIdMap;
static StaticAutoPtr<WGCByIdMap> gWindowGlobalChildById;

WindowGlobalChild::WindowGlobalChild(nsGlobalWindowInner* aWindow,
                                     dom::BrowsingContext* aBrowsingContext)
    : mWindowGlobal(aWindow),
      mBrowsingContext(aBrowsingContext),
      mInnerWindowId(aWindow->WindowID()),
      mOuterWindowId(aWindow->GetOuterWindow()->WindowID()),
      mIPCClosed(true) {}

already_AddRefed<WindowGlobalChild> WindowGlobalChild::Create(
    nsGlobalWindowInner* aWindow) {
  nsCOMPtr<nsIPrincipal> principal = aWindow->GetPrincipal();
  MOZ_ASSERT(principal);

  RefPtr<nsDocShell> docshell = nsDocShell::Cast(aWindow->GetDocShell());
  MOZ_ASSERT(docshell);

  // Initalize our WindowGlobalChild object.
  RefPtr<dom::BrowsingContext> bc = docshell->GetBrowsingContext();

  // When creating a new window global child we also need to look at the
  // channel's Cross-Origin-Opener-Policy and set it on the browsing context
  // so it's available in the parent process.
  nsCOMPtr<nsIHttpChannelInternal> chan =
      do_QueryInterface(aWindow->GetDocument()->GetChannel());
  nsILoadInfo::CrossOriginOpenerPolicy policy;
  if (chan && NS_SUCCEEDED(chan->GetCrossOriginOpenerPolicy(&policy))) {
    bc->SetOpenerPolicy(policy);
  }

  RefPtr<WindowGlobalChild> wgc = new WindowGlobalChild(aWindow, bc);

  // If we have already closed our browsing context, return a pre-closed
  // WindowGlobalChild actor.
  if (bc->GetClosed()) {
    wgc->ActorDestroy(FailedConstructor);
    return wgc.forget();
  }

  WindowGlobalInit init(principal, aWindow->GetDocumentURI(), bc,
                        wgc->mInnerWindowId, wgc->mOuterWindowId);

  // Send the link constructor over PInProcessChild or PBrowser.
  if (XRE_IsParentProcess()) {
    InProcessChild* ipc = InProcessChild::Singleton();
    if (!ipc) {
      return nullptr;
    }

    // Note: ref is released in DeallocPWindowGlobalChild
    ipc->SendPWindowGlobalConstructor(do_AddRef(wgc).take(), init);
  } else {
    RefPtr<TabChild> tabChild =
        TabChild::GetFrom(static_cast<mozIDOMWindow*>(aWindow));
    MOZ_ASSERT(tabChild);

    // Note: ref is released in DeallocPWindowGlobalChild
    tabChild->SendPWindowGlobalConstructor(do_AddRef(wgc).take(), init);
  }
  wgc->mIPCClosed = false;

  // Register this WindowGlobal in the gWindowGlobalParentsById map.
  if (!gWindowGlobalChildById) {
    gWindowGlobalChildById = new WGCByIdMap();
    ClearOnShutdown(&gWindowGlobalChildById);
  }
  auto entry = gWindowGlobalChildById->LookupForAdd(wgc->mInnerWindowId);
  MOZ_RELEASE_ASSERT(!entry, "Duplicate WindowGlobalChild entry for ID!");
  entry.OrInsert([&] { return wgc; });

  return wgc.forget();
}

/* static */
already_AddRefed<WindowGlobalChild> WindowGlobalChild::GetByInnerWindowId(
    uint64_t aInnerWindowId) {
  if (!gWindowGlobalChildById) {
    return nullptr;
  }
  return gWindowGlobalChildById->Get(aInnerWindowId);
}

bool WindowGlobalChild::IsCurrentGlobal() {
  return !mIPCClosed && mWindowGlobal->IsCurrentInnerWindow();
}

already_AddRefed<WindowGlobalParent> WindowGlobalChild::GetParentActor() {
  if (mIPCClosed) {
    return nullptr;
  }
  IProtocol* otherSide = InProcessChild::ParentActorFor(this);
  return do_AddRef(static_cast<WindowGlobalParent*>(otherSide));
}

already_AddRefed<TabChild> WindowGlobalChild::GetTabChild() {
  if (IsInProcess() || mIPCClosed) {
    return nullptr;
  }
  return do_AddRef(static_cast<TabChild*>(Manager()));
}

void WindowGlobalChild::Destroy() {
  // Perform async IPC shutdown unless we're not in-process, and our TabChild is
  // in the process of being destroyed, which will destroy us as well.
  RefPtr<TabChild> tabChild = GetTabChild();
  if (!tabChild || !tabChild->IsDestroyed()) {
    SendDestroy();
  }

  mIPCClosed = true;
}

static nsresult ChangeFrameRemoteness(WindowGlobalChild* aWgc,
                                      BrowsingContext* aBc,
                                      const nsString& aRemoteType,
                                      uint64_t aPendingSwitchId,
                                      BrowserBridgeChild** aBridge) {
  MOZ_ASSERT(XRE_IsContentProcess(), "This doesn't make sense in the parent");

  // Get the target embedder's FrameLoaderOwner, and make sure we're in the
  // right place.
  RefPtr<Element> embedderElt = aBc->GetEmbedderElement();
  if (!embedderElt) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  if (NS_WARN_IF(embedderElt->GetOwnerGlobal() != aWgc->WindowGlobal())) {
    return NS_ERROR_UNEXPECTED;
  }

  RefPtr<nsFrameLoaderOwner> flo = do_QueryObject(embedderElt);
  MOZ_ASSERT(flo, "Embedder must be a nsFrameLoaderOwner!");

  MOZ_ASSERT(nsContentUtils::IsSafeToRunScript());

  // Actually perform the remoteness swap.
  RemotenessOptions options;
  options.mRemoteType.Construct(aRemoteType);
  options.mPendingSwitchID.Construct(aPendingSwitchId);

  ErrorResult error;
  flo->ChangeRemoteness(options, error);
  if (NS_WARN_IF(error.Failed())) {
    return error.StealNSResult();
  }

  // Make sure we successfully created either an in-process nsDocShell or a
  // cross-process BrowserBridgeChild. If we didn't, produce an error.
  RefPtr<nsFrameLoader> frameLoader = flo->GetFrameLoader();
  if (NS_WARN_IF(!frameLoader)) {
    return NS_ERROR_FAILURE;
  }

  RefPtr<BrowserBridgeChild> bbc;
  if (frameLoader->IsRemoteFrame()) {
    bbc = frameLoader->GetBrowserBridgeChild();
    if (NS_WARN_IF(!bbc)) {
      return NS_ERROR_FAILURE;
    }
  } else {
    nsDocShell* ds = frameLoader->GetDocShell(error);
    if (NS_WARN_IF(error.Failed())) {
      return error.StealNSResult();
    }

    if (NS_WARN_IF(!ds)) {
      return NS_ERROR_FAILURE;
    }
  }

  bbc.forget(aBridge);
  return NS_OK;
}

IPCResult WindowGlobalChild::RecvChangeFrameRemoteness(
    dom::BrowsingContext* aBc, const nsString& aRemoteType,
    uint64_t aPendingSwitchId, ChangeFrameRemotenessResolver&& aResolver) {
  MOZ_ASSERT(XRE_IsContentProcess(), "This doesn't make sense in the parent");

  RefPtr<BrowserBridgeChild> bbc;
  nsresult rv = ChangeFrameRemoteness(this, aBc, aRemoteType, aPendingSwitchId,
                                      getter_AddRefs(bbc));

  // To make the type system happy, we've gotta do some gymnastics.
  aResolver(Tuple<const nsresult&, PBrowserBridgeChild*>(rv, bbc));
  return IPC_OK();
}

IPCResult WindowGlobalChild::RecvAsyncMessage(const nsString& aActorName,
                                              const nsString& aMessageName,
                                              const ClonedMessageData& aData) {
  StructuredCloneData data;
  data.BorrowFromClonedMessageDataForChild(aData);
  HandleAsyncMessage(aActorName, aMessageName, data);
  return IPC_OK();
}

void WindowGlobalChild::HandleAsyncMessage(const nsString& aActorName,
                                           const nsString& aMessageName,
                                           StructuredCloneData& aData) {
  if (NS_WARN_IF(mIPCClosed)) {
    return;
  }

  // Force creation of the actor if it hasn't been created yet.
  IgnoredErrorResult rv;
  RefPtr<JSWindowActorChild> actor = GetActor(aActorName, rv);
  if (NS_WARN_IF(rv.Failed())) {
    return;
  }

  // Get the JSObject for the named actor.
  JS::RootedObject obj(RootingCx(), actor->GetWrapper());
  if (NS_WARN_IF(!obj)) {
    // If we don't have a preserved wrapper, there won't be any receiver
    // method to call.
    return;
  }

  RefPtr<JSWindowActorService> actorSvc = JSWindowActorService::GetSingleton();
  if (NS_WARN_IF(!actorSvc)) {
    return;
  }

  actorSvc->ReceiveMessage(actor, obj, aMessageName, aData);
}

already_AddRefed<JSWindowActorChild> WindowGlobalChild::GetActor(
    const nsAString& aName, ErrorResult& aRv) {
  // Check if this actor has already been created, and return it if it has.
  if (mWindowActors.Contains(aName)) {
    return do_AddRef(mWindowActors.GetWeak(aName));
  }

  // Otherwise, we want to create a new instance of this actor. Call into the
  // JSWindowActorService to trigger construction.
  RefPtr<JSWindowActorService> actorSvc = JSWindowActorService::GetSingleton();
  if (!actorSvc) {
    return nullptr;
  }

  nsAutoString remoteType;
  if (XRE_IsContentProcess()) {
    remoteType = ContentChild::GetSingleton()->GetRemoteType();
  } else {
    remoteType = VoidString();
  }

  JS::RootedObject obj(RootingCx());
  actorSvc->ConstructActor(aName, /* aChildSide */ false, mBrowsingContext,
                           mWindowGlobal->GetDocumentURI(), remoteType, &obj,
                           aRv);
  if (aRv.Failed()) {
    return nullptr;
  }

  // Unwrap our actor to a JSWindowActorChild object.
  RefPtr<JSWindowActorChild> actor;
  if (NS_FAILED(UNWRAP_OBJECT(JSWindowActorChild, &obj, actor))) {
    return nullptr;
  }

  MOZ_RELEASE_ASSERT(!actor->Manager(),
                     "mManager was already initialized once!");
  actor->Init(aName, this);
  mWindowActors.Put(aName, actor);
  return actor.forget();
}

void WindowGlobalChild::ActorDestroy(ActorDestroyReason aWhy) {
  mIPCClosed = true;
  gWindowGlobalChildById->Remove(mInnerWindowId);
}

WindowGlobalChild::~WindowGlobalChild() {
  MOZ_ASSERT(!gWindowGlobalChildById ||
             !gWindowGlobalChildById->Contains(mInnerWindowId));
}

JSObject* WindowGlobalChild::WrapObject(JSContext* aCx,
                                        JS::Handle<JSObject*> aGivenProto) {
  return WindowGlobalChild_Binding::Wrap(aCx, this, aGivenProto);
}

nsISupports* WindowGlobalChild::GetParentObject() {
  return xpc::NativeGlobal(xpc::PrivilegedJunkScope());
}

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(WindowGlobalChild, mWindowGlobal,
                                      mBrowsingContext, mWindowActors)

NS_IMPL_CYCLE_COLLECTION_ROOT_NATIVE(WindowGlobalChild, AddRef)
NS_IMPL_CYCLE_COLLECTION_UNROOT_NATIVE(WindowGlobalChild, Release)

}  // namespace dom
}  // namespace mozilla
