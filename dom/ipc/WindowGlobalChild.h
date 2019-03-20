/* -*- Mode: C++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 8 -*- */
/* vim: set sw=2 ts=8 et tw=80 ft=cpp : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_WindowGlobalChild_h
#define mozilla_dom_WindowGlobalChild_h

#include "mozilla/RefPtr.h"
#include "mozilla/dom/PWindowGlobalChild.h"
#include "nsWrapperCache.h"

class nsGlobalWindowInner;
class nsDocShell;

namespace mozilla {
namespace dom {

class BrowsingContext;
class WindowGlobalParent;
class JSWindowActorChild;
class TabChild;

/**
 * Actor for a single nsGlobalWindowInner. This actor is used to communicate
 * information to the parent process asynchronously.
 */
class WindowGlobalChild : public nsWrapperCache, public PWindowGlobalChild {
  friend class PWindowGlobalChild;

 public:
  NS_INLINE_DECL_CYCLE_COLLECTING_NATIVE_REFCOUNTING(WindowGlobalChild)
  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_NATIVE_CLASS(WindowGlobalChild)

  static already_AddRefed<WindowGlobalChild> GetByInnerWindowId(
      uint64_t aInnerWindowId);

  static already_AddRefed<WindowGlobalChild> GetByInnerWindowId(
      const GlobalObject& aGlobal, uint64_t aInnerWindowId) {
    return GetByInnerWindowId(aInnerWindowId);
  }

  dom::BrowsingContext* BrowsingContext() { return mBrowsingContext; }
  nsGlobalWindowInner* WindowGlobal() { return mWindowGlobal; }

  // Has this actor been shut down
  bool IsClosed() { return mIPCClosed; }
  void Destroy();

  // Check if this actor is managed by PInProcess, as-in the document is loaded
  // in the chrome process.
  bool IsInProcess() { return XRE_IsParentProcess(); }

  // The Window ID for this WindowGlobal
  uint64_t InnerWindowId() { return mInnerWindowId; }
  uint64_t OuterWindowId() { return mOuterWindowId; }

  bool IsCurrentGlobal();

  // Get the other side of this actor if it is an in-process actor. Returns
  // |nullptr| if the actor has been torn down, or is not in-process.
  already_AddRefed<WindowGlobalParent> GetParentActor();

  // Get this actor's manager if it is not an in-process actor. Returns
  // |nullptr| if the actor has been torn down, or is in-process.
  already_AddRefed<TabChild> GetTabChild();

  void HandleAsyncMessage(const nsString& aActorName,
                          const nsString& aMessageName,
                          ipc::StructuredCloneData& aData);

  // Get a JS actor object by name.
  already_AddRefed<JSWindowActorChild> GetActor(const nsAString& aName,
                                                ErrorResult& aRv);

  // Create and initialize the WindowGlobalChild object.
  static already_AddRefed<WindowGlobalChild> Create(
      nsGlobalWindowInner* aWindow);

  nsISupports* GetParentObject();
  JSObject* WrapObject(JSContext* aCx,
                       JS::Handle<JSObject*> aGivenProto) override;

 protected:
  // IPC messages
  mozilla::ipc::IPCResult RecvAsyncMessage(const nsString& aActorName,
                                           const nsString& aMessage,
                                           const ClonedMessageData& aData);

  virtual void ActorDestroy(ActorDestroyReason aWhy) override;

 private:
  WindowGlobalChild(nsGlobalWindowInner* aWindow, dom::BrowsingContext* aBc);
  ~WindowGlobalChild();

  RefPtr<nsGlobalWindowInner> mWindowGlobal;
  RefPtr<dom::BrowsingContext> mBrowsingContext;
  nsRefPtrHashtable<nsStringHashKey, JSWindowActorChild> mWindowActors;
  uint64_t mInnerWindowId;
  uint64_t mOuterWindowId;
  bool mIPCClosed;
};

}  // namespace dom
}  // namespace mozilla

#endif  // !defined(mozilla_dom_WindowGlobalChild_h)
