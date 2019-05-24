/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/BrowserBridgeChild.h"
#include "mozilla/dom/BrowsingContext.h"
#include "nsFocusManager.h"
#include "nsFrameLoader.h"
#include "nsFrameLoaderOwner.h"
#include "nsQueryObject.h"
#include "nsIDocShellTreeOwner.h"

using namespace mozilla::ipc;

namespace mozilla {
namespace dom {

BrowserBridgeChild::BrowserBridgeChild(nsFrameLoader* aFrameLoader,
                                       BrowsingContext* aBrowsingContext)
    : mLayersId{0},
      mIPCOpen(true),
      mFrameLoader(aFrameLoader),
      mBrowsingContext(aBrowsingContext) {}

BrowserBridgeChild::~BrowserBridgeChild() {}

void BrowserBridgeChild::NavigateByKey(bool aForward,
                                       bool aForDocumentNavigation) {
  Unused << SendNavigateByKey(aForward, aForDocumentNavigation);
}

void BrowserBridgeChild::Activate() { Unused << SendActivate(); }

void BrowserBridgeChild::Deactivate() { Unused << SendDeactivate(); }

void BrowserBridgeChild::SetIsUnderHiddenEmbedderElement(
    bool aIsUnderHiddenEmbedderElement) {
  Unused << SendSetIsUnderHiddenEmbedderElement(aIsUnderHiddenEmbedderElement);
}

/*static*/
BrowserBridgeChild* BrowserBridgeChild::GetFrom(nsFrameLoader* aFrameLoader) {
  if (!aFrameLoader) {
    return nullptr;
  }
  return aFrameLoader->GetBrowserBridgeChild();
}

/*static*/
BrowserBridgeChild* BrowserBridgeChild::GetFrom(nsIContent* aContent) {
  RefPtr<nsFrameLoaderOwner> loaderOwner = do_QueryObject(aContent);
  if (!loaderOwner) {
    return nullptr;
  }
  RefPtr<nsFrameLoader> frameLoader = loaderOwner->GetFrameLoader();
  return GetFrom(frameLoader);
}

IPCResult BrowserBridgeChild::RecvSetLayersId(
    const mozilla::layers::LayersId& aLayersId) {
  MOZ_ASSERT(!mLayersId.IsValid() && aLayersId.IsValid());
  mLayersId = aLayersId;

  // Invalidate the nsSubdocumentFrame now that we have a layers ID for the
  // child browser
  if (RefPtr<Element> owner = mFrameLoader->GetOwnerContent()) {
    if (nsIFrame* frame = owner->GetPrimaryFrame()) {
      frame->InvalidateFrame();
    }
  }

  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserBridgeChild::RecvRequestFocus(
    const bool& aCanRaise) {
  // Adapted from BrowserParent
  RefPtr<Element> owner = mFrameLoader->GetOwnerContent();
  if (!owner) {
    return IPC_OK();
  }
  nsContentUtils::RequestFrameFocus(*owner, aCanRaise);
  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserBridgeChild::RecvMoveFocus(
    const bool& aForward, const bool& aForDocumentNavigation) {
  // Adapted from BrowserParent
  nsCOMPtr<nsIFocusManager> fm = nsFocusManager::GetFocusManager();
  if (!fm) {
    return IPC_OK();
  }

  RefPtr<Element> owner = mFrameLoader->GetOwnerContent();
  if (!owner) {
    return IPC_OK();
  }

  RefPtr<Element> dummy;

  uint32_t type =
      aForward
          ? (aForDocumentNavigation
                 ? static_cast<uint32_t>(nsIFocusManager::MOVEFOCUS_FORWARDDOC)
                 : static_cast<uint32_t>(nsIFocusManager::MOVEFOCUS_FORWARD))
          : (aForDocumentNavigation
                 ? static_cast<uint32_t>(nsIFocusManager::MOVEFOCUS_BACKWARDDOC)
                 : static_cast<uint32_t>(nsIFocusManager::MOVEFOCUS_BACKWARD));
  fm->MoveFocus(nullptr, owner, type, nsIFocusManager::FLAG_BYKEY,
                getter_AddRefs(dummy));
  return IPC_OK();
}

void BrowserBridgeChild::ActorDestroy(ActorDestroyReason aWhy) {
  mIPCOpen = false;
}

}  // namespace dom
}  // namespace mozilla
