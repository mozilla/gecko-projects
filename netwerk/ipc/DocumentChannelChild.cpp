/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=2 ts=8 et tw=80 : */

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "DocumentChannelChild.h"

using namespace mozilla::dom;
using namespace mozilla::ipc;

extern mozilla::LazyLogModule gDocumentChannelLog;
#define LOG(fmt) MOZ_LOG(gDocumentChannelLog, mozilla::LogLevel::Verbose, fmt)

namespace mozilla {
namespace net {

//-----------------------------------------------------------------------------
// DocumentChannelChild::nsISupports

NS_INTERFACE_MAP_BEGIN(DocumentChannelChild)
  NS_INTERFACE_MAP_ENTRY(nsIAsyncVerifyRedirectCallback)
NS_INTERFACE_MAP_END_INHERITING(DocumentChannel)

NS_IMPL_ADDREF_INHERITED(DocumentChannelChild, DocumentChannel)
NS_IMPL_RELEASE_INHERITED(DocumentChannelChild, DocumentChannel)

DocumentChannelChild::DocumentChannelChild(
    nsDocShellLoadState* aLoadState, net::LoadInfo* aLoadInfo,
    const nsString* aInitiatorType, nsLoadFlags aLoadFlags, uint32_t aLoadType,
    uint32_t aCacheKey, bool aIsActive, bool aIsTopLevelDoc,
    bool aHasNonEmptySandboxingFlags)
    : DocumentChannel(aLoadState, aLoadInfo, aInitiatorType, aLoadFlags,
                      aLoadType, aCacheKey, aIsActive, aIsTopLevelDoc,
                      aHasNonEmptySandboxingFlags) {
  LOG(("DocumentChannelChild ctor [this=%p, uri=%s]", this,
       aLoadState->URI()->GetSpecOrDefault().get()));
}

DocumentChannelChild::~DocumentChannelChild() {
  LOG(("DocumentChannelChild dtor [this=%p]", this));
}

NS_IMETHODIMP
DocumentChannelChild::AsyncOpen(nsIStreamListener* aListener) {
  nsresult rv = NS_OK;

  nsCOMPtr<nsIStreamListener> listener = aListener;
  rv = nsContentSecurityManager::doContentSecurityCheck(this, listener);
  NS_ENSURE_SUCCESS(rv, rv);

  NS_ENSURE_TRUE(gNeckoChild, NS_ERROR_FAILURE);
  NS_ENSURE_ARG_POINTER(listener);
  NS_ENSURE_TRUE(!mIsPending, NS_ERROR_IN_PROGRESS);
  NS_ENSURE_TRUE(!mWasOpened, NS_ERROR_ALREADY_OPENED);

  // Port checked in parent, but duplicate here so we can return with error
  // immediately, as we've done since before e10s.
  rv = NS_CheckPortSafety(mURI);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIURI> topWindowURI;
  nsCOMPtr<nsIPrincipal> contentBlockingAllowListPrincipal;

  nsCOMPtr<mozIThirdPartyUtil> util = services::GetThirdPartyUtil();
  if (util) {
    nsCOMPtr<nsIURI> uriBeingLoaded =
        AntiTrackingCommon::MaybeGetDocumentURIBeingLoaded(this);
    nsCOMPtr<mozIDOMWindowProxy> win;
    rv =
        util->GetTopWindowForChannel(this, uriBeingLoaded, getter_AddRefs(win));
    if (NS_SUCCEEDED(rv)) {
      util->GetURIFromWindow(win, getter_AddRefs(topWindowURI));

      Unused << util->GetContentBlockingAllowListPrincipalFromWindow(
          win, uriBeingLoaded,
          getter_AddRefs(contentBlockingAllowListPrincipal));
    }
  }

  // add ourselves to the load group.
  if (mLoadGroup) {
    // During this call, we can re-enter back into the DocumentChannelChild to
    // call SetNavigationTiming.
    mLoadGroup->AddRequest(this, nullptr);
  }

  if (mCanceled) {
    // We may have been canceled already, either by on-modify-request
    // listeners or by load group observers; in that case, don't create IPDL
    // connection. See nsHttpChannel::AsyncOpen().
    return mStatus;
  }

  gHttpHandler->OnOpeningDocumentRequest(this);

  DocumentChannelCreationArgs args;

  SerializeURI(topWindowURI, args.topWindowURI());
  args.loadState() = mLoadState->Serialize();
  Maybe<LoadInfoArgs> maybeArgs;
  rv = LoadInfoToLoadInfoArgs(mLoadInfo, &maybeArgs);
  NS_ENSURE_SUCCESS(rv, rv);
  MOZ_DIAGNOSTIC_ASSERT(maybeArgs);

  if (contentBlockingAllowListPrincipal) {
    PrincipalInfo principalInfo;
    rv = PrincipalToPrincipalInfo(contentBlockingAllowListPrincipal,
                                  &principalInfo);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }
    args.contentBlockingAllowListPrincipal() = Some(principalInfo);
  }

  args.loadInfo() = *maybeArgs;
  args.loadFlags() = mLoadFlags;
  args.initiatorType() = mInitiatorType;
  args.loadType() = mLoadType;
  args.cacheKey() = mCacheKey;
  args.isActive() = mIsActive;
  args.isTopLevelDoc() = mIsTopLevelDoc;
  args.hasNonEmptySandboxingFlags() = mHasNonEmptySandboxingFlags;
  args.channelId() = mChannelId;
  args.asyncOpenTime() = mAsyncOpenTime;
  args.documentOpenFlags() = mDocumentOpenFlags;
  args.pluginsAllowed() = mPluginsAllowed;
  if (mTiming) {
    args.timing() = Some(mTiming);
  }
  nsDocShell* docshell = GetDocShell();
  if (docshell) {
    docshell->GetCustomUserAgent(args.customUserAgent());
  }

  nsCOMPtr<nsIBrowserChild> iBrowserChild;
  NS_QueryNotificationCallbacks(mCallbacks, mLoadGroup,
                                NS_GET_TEMPLATE_IID(nsIBrowserChild),
                                getter_AddRefs(iBrowserChild));
  BrowserChild* browserChild = static_cast<BrowserChild*>(iBrowserChild.get());
  if (MissingRequiredBrowserChild(browserChild, "documentchannel")) {
    return NS_ERROR_ILLEGAL_VALUE;
  }

  // TODO: What happens if the caller has called other methods on the
  // nsIChannel after the ctor, but before this?

  gNeckoChild->SendPDocumentChannelConstructor(
      this, browserChild, IPC::SerializedLoadContext(this), args);

  mIsPending = true;
  mWasOpened = true;
  mListener = listener;

  return NS_OK;
}

IPCResult DocumentChannelChild::RecvFailedAsyncOpen(
    const nsresult& aStatusCode) {
  ShutdownListeners(aStatusCode);
  return IPC_OK();
}

void DocumentChannelChild::ShutdownListeners(nsresult aStatusCode) {
  LOG(("DocumentChannelChild ShutdownListeners [this=%p, status=%" PRIx32 "]",
       this, static_cast<uint32_t>(aStatusCode)));
  mStatus = aStatusCode;

  nsCOMPtr<nsIStreamListener> l = mListener;
  if (l) {
    l->OnStartRequest(this);
  }

  mIsPending = false;

  l = mListener;  // it might have changed!
  if (l) {
    l->OnStopRequest(this, aStatusCode);
  }
  mListener = nullptr;
  mCallbacks = nullptr;

  if (mLoadGroup) {
    mLoadGroup->RemoveRequest(this, nullptr, aStatusCode);
    mLoadGroup = nullptr;
  }

  if (CanSend()) {
    Send__delete__(this);
  }
}

IPCResult DocumentChannelChild::RecvDisconnectChildListeners(
    const nsresult& aStatus, const nsresult& aLoadGroupStatus) {
  MOZ_ASSERT(NS_FAILED(aStatus));
  mStatus = aLoadGroupStatus;
  // Make sure we remove from the load group before
  // setting mStatus, as existing tests expect the
  // status to be successful when we disconnect.
  if (mLoadGroup) {
    mLoadGroup->RemoveRequest(this, nullptr, aStatus);
    mLoadGroup = nullptr;
  }

  ShutdownListeners(aStatus);
  return IPC_OK();
}

IPCResult DocumentChannelChild::RecvDeleteSelf() {
  // This calls NeckoChild::DeallocPGenericChannel(), which deletes |this| if
  // IPDL holds the last reference.  Don't rely on |this| existing after here!
  Send__delete__(this);
  return IPC_OK();
}

IPCResult DocumentChannelChild::RecvRedirectToRealChannel(
    RedirectToRealChannelArgs&& aArgs,
    RedirectToRealChannelResolver&& aResolve) {
  LOG(("DocumentChannelChild RecvRedirectToRealChannel [this=%p, uri=%s]", this,
       aArgs.uri()->GetSpecOrDefault().get()));

  RefPtr<dom::Document> loadingDocument;
  mLoadInfo->GetLoadingDocument(getter_AddRefs(loadingDocument));

  RefPtr<dom::Document> cspToInheritLoadingDocument;
  nsCOMPtr<nsIContentSecurityPolicy> policy = mLoadInfo->GetCspToInherit();
  if (policy) {
    nsWeakPtr ctx =
        static_cast<nsCSPContext*>(policy.get())->GetLoadingContext();
    cspToInheritLoadingDocument = do_QueryReferent(ctx);
  }
  nsCOMPtr<nsILoadInfo> loadInfo;
  MOZ_ALWAYS_SUCCEEDS(LoadInfoArgsToLoadInfo(aArgs.loadInfo(), loadingDocument,
                                             cspToInheritLoadingDocument,
                                             getter_AddRefs(loadInfo)));

  mLastVisitInfo = std::move(aArgs.lastVisitInfo());
  mRedirects = std::move(aArgs.redirects());
  mRedirectResolver = std::move(aResolve);

  nsCOMPtr<nsIChannel> newChannel;
  MOZ_ASSERT((aArgs.loadStateLoadFlags() &
              nsDocShell::InternalLoad::INTERNAL_LOAD_FLAGS_IS_SRCDOC) ||
             aArgs.srcdocData().IsVoid());
  nsresult rv = nsDocShell::CreateRealChannelForDocument(
      getter_AddRefs(newChannel), aArgs.uri(), loadInfo, nullptr, nullptr,
      aArgs.newLoadFlags(), aArgs.srcdocData(), aArgs.baseUri());
  if (newChannel) {
    newChannel->SetLoadGroup(mLoadGroup);
  }

  // This is used to report any errors back to the parent by calling
  // CrossProcessRedirectFinished.
  auto scopeExit = MakeScopeExit([&]() {
    Maybe<LoadInfoArgs> dummy;
    mRedirectResolver(
        Tuple<const nsresult&, const Maybe<LoadInfoArgs>&>(rv, dummy));
    mRedirectResolver = nullptr;
  });

  if (NS_FAILED(rv)) {
    return IPC_OK();
  }

  if (nsCOMPtr<nsIHttpChannel> httpChannel = do_QueryInterface(newChannel)) {
    rv = httpChannel->SetChannelId(aArgs.channelId());
  }
  if (NS_FAILED(rv)) {
    return IPC_OK();
  }

  rv = newChannel->SetOriginalURI(aArgs.originalURI());
  if (NS_FAILED(rv)) {
    return IPC_OK();
  }

  if (nsCOMPtr<nsIHttpChannelInternal> httpChannelInternal =
          do_QueryInterface(newChannel)) {
    rv = httpChannelInternal->SetRedirectMode(aArgs.redirectMode());
  }
  if (NS_FAILED(rv)) {
    return IPC_OK();
  }

  newChannel->SetNotificationCallbacks(mCallbacks);

  if (aArgs.init()) {
    HttpBaseChannel::ReplacementChannelConfig config(*aArgs.init());
    HttpBaseChannel::ConfigureReplacementChannel(
        newChannel, config,
        HttpBaseChannel::ReplacementReason::DocumentChannel);
  }

  if (aArgs.contentDisposition()) {
    newChannel->SetContentDisposition(*aArgs.contentDisposition());
  }

  if (aArgs.contentDispositionFilename()) {
    newChannel->SetContentDispositionFilename(
        *aArgs.contentDispositionFilename());
  }

  // transfer any properties. This appears to be entirely a content-side
  // interface and isn't copied across to the parent. Copying the values
  // for this from this into the new actor will work, since the parent
  // won't have the right details anyway.
  // TODO: What about the process switch equivalent
  // (ContentChild::RecvCrossProcessRedirect)? In that case there is no local
  // existing actor in the destination process... We really need all information
  // to go up to the parent, and then come down to the new child actor.
  if (nsCOMPtr<nsIWritablePropertyBag> bag = do_QueryInterface(newChannel)) {
    nsHashPropertyBag::CopyFrom(bag, aArgs.properties());
  }

  // connect parent.
  nsCOMPtr<nsIChildChannel> childChannel = do_QueryInterface(newChannel);
  if (childChannel) {
    rv = childChannel->ConnectParent(
        aArgs.registrarId());  // creates parent channel
    if (NS_FAILED(rv)) {
      return IPC_OK();
    }
  }
  mRedirectChannel = newChannel;

  rv = gHttpHandler->AsyncOnChannelRedirect(
      this, newChannel, aArgs.redirectFlags(), GetMainThreadEventTarget());

  if (NS_SUCCEEDED(rv)) {
    scopeExit.release();
  }

  // scopeExit will call CrossProcessRedirectFinished(rv) here
  return IPC_OK();
}

NS_IMETHODIMP
DocumentChannelChild::OnRedirectVerifyCallback(nsresult aStatusCode) {
  LOG(
      ("DocumentChannelChild OnRedirectVerifyCallback [this=%p, "
       "aRv=0x%08" PRIx32 " ]",
       this, static_cast<uint32_t>(aStatusCode)));
  nsCOMPtr<nsIChannel> redirectChannel = std::move(mRedirectChannel);
  RedirectToRealChannelResolver redirectResolver = std::move(mRedirectResolver);

  // If we've already shut down, then just notify the parent that
  // we're done.
  if (NS_FAILED(mStatus)) {
    redirectChannel->SetNotificationCallbacks(nullptr);
    Maybe<LoadInfoArgs> dummy;
    redirectResolver(
        Tuple<const nsresult&, const Maybe<LoadInfoArgs>&>(aStatusCode, dummy));
    return NS_OK;
  }

  nsresult rv = aStatusCode;
  if (NS_SUCCEEDED(rv)) {
    if (nsCOMPtr<nsIChildChannel> childChannel =
            do_QueryInterface(redirectChannel)) {
      rv = childChannel->CompleteRedirectSetup(mListener, nullptr);
    } else {
      rv = redirectChannel->AsyncOpen(mListener);
    }
  } else {
    redirectChannel->SetNotificationCallbacks(nullptr);
  }

  Maybe<LoadInfoArgs> dummy;
  redirectResolver(
      Tuple<const nsresult&, const Maybe<LoadInfoArgs>&>(rv, dummy));

  if (NS_FAILED(rv)) {
    ShutdownListeners(rv);
    return NS_OK;
  }

  if (mLoadGroup) {
    mLoadGroup->RemoveRequest(this, nullptr, NS_BINDING_REDIRECTED);
  }
  mCallbacks = nullptr;
  mListener = nullptr;

  // This calls NeckoChild::DeallocPDocumentChannel(), which deletes |this| if
  // IPDL holds the last reference.  Don't rely on |this| existing after here!
  if (CanSend()) {
    Send__delete__(this);
  }

  return NS_OK;
}

IPCResult DocumentChannelChild::RecvConfirmRedirect(
    LoadInfoArgs&& aLoadInfo, nsIURI* aNewUri,
    ConfirmRedirectResolver&& aResolve) {
  // This is effectively the same as AsyncOnChannelRedirect, except since we're
  // not propagating the redirect into this process, we don't have an nsIChannel
  // for the redirection and we have to do the checks manually.
  // This just checks CSP thus far, hopefully there's not much else needed.
  RefPtr<dom::Document> loadingDocument;
  mLoadInfo->GetLoadingDocument(getter_AddRefs(loadingDocument));
  RefPtr<dom::Document> cspToInheritLoadingDocument;
  nsCOMPtr<nsIContentSecurityPolicy> policy = mLoadInfo->GetCspToInherit();
  if (policy) {
    nsWeakPtr ctx =
        static_cast<nsCSPContext*>(policy.get())->GetLoadingContext();
    cspToInheritLoadingDocument = do_QueryReferent(ctx);
  }
  nsCOMPtr<nsILoadInfo> loadInfo;
  MOZ_ALWAYS_SUCCEEDS(LoadInfoArgsToLoadInfo(
      Some(std::move(aLoadInfo)), loadingDocument, cspToInheritLoadingDocument,
      getter_AddRefs(loadInfo)));

  nsCOMPtr<nsIURI> originalUri;
  GetOriginalURI(getter_AddRefs(originalUri));
  Maybe<nsresult> cancelCode;
  nsresult rv = CSPService::ConsultCSPForRedirect(originalUri, aNewUri,
                                                  mLoadInfo, cancelCode);
  aResolve(Tuple<const nsresult&, const Maybe<nsresult>&>(rv, cancelCode));
  return IPC_OK();
}

mozilla::ipc::IPCResult DocumentChannelChild::RecvAttachStreamFilter(
    Endpoint<extensions::PStreamFilterParent>&& aEndpoint) {
  extensions::StreamFilterParent::Attach(this, std::move(aEndpoint));
  return IPC_OK();
}

NS_IMETHODIMP
DocumentChannelChild::Cancel(nsresult aStatusCode) {
  if (mCanceled) {
    return NS_OK;
  }

  mCanceled = true;
  if (CanSend()) {
    SendCancel(aStatusCode);
  }

  ShutdownListeners(aStatusCode);

  return NS_OK;
}

}  // namespace net
}  // namespace mozilla

#undef LOG
