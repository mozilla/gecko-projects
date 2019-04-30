/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ThirdPartyUtil.h"
#include "nsNetCID.h"
#include "nsNetUtil.h"
#include "nsIChannel.h"
#include "nsIServiceManager.h"
#include "nsIHttpChannelInternal.h"
#include "nsIDOMWindow.h"
#include "nsILoadContext.h"
#include "nsIPrincipal.h"
#include "nsIScriptObjectPrincipal.h"
#include "nsIURI.h"
#include "nsReadableUtils.h"
#include "nsThreadUtils.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/Logging.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/Unused.h"
#include "nsPIDOMWindow.h"

NS_IMPL_ISUPPORTS(ThirdPartyUtil, mozIThirdPartyUtil)

//
// MOZ_LOG=thirdPartyUtil:5
//
static mozilla::LazyLogModule gThirdPartyLog("thirdPartyUtil");
#undef LOG
#define LOG(args) MOZ_LOG(gThirdPartyLog, mozilla::LogLevel::Debug, args)

static mozilla::StaticRefPtr<ThirdPartyUtil> gService;

// static
void ThirdPartyUtil::Startup() {
  nsCOMPtr<mozIThirdPartyUtil> tpu;
  if (NS_WARN_IF(!(tpu = do_GetService(THIRDPARTYUTIL_CONTRACTID)))) {
    NS_WARNING("Failed to get third party util!");
  }
}

nsresult ThirdPartyUtil::Init() {
  NS_ENSURE_TRUE(NS_IsMainThread(), NS_ERROR_NOT_AVAILABLE);

  MOZ_ASSERT(!gService);
  gService = this;
  mozilla::ClearOnShutdown(&gService);

  mTLDService = nsEffectiveTLDService::GetInstance();
  return mTLDService ? NS_OK : NS_ERROR_FAILURE;
}

ThirdPartyUtil::~ThirdPartyUtil() { gService = nullptr; }

// static
ThirdPartyUtil* ThirdPartyUtil::GetInstance() {
  if (gService) {
    return gService;
  }
  nsCOMPtr<mozIThirdPartyUtil> tpuService =
      mozilla::services::GetThirdPartyUtil();
  if (!tpuService) {
    return nullptr;
  }
  MOZ_ASSERT(
      gService,
      "gService must have been initialized in nsEffectiveTLDService::Init");
  return gService;
}

// Determine if aFirstDomain is a different base domain to aSecondURI; or, if
// the concept of base domain does not apply, determine if the two hosts are not
// string-identical.
nsresult ThirdPartyUtil::IsThirdPartyInternal(const nsCString& aFirstDomain,
                                              nsIURI* aSecondURI,
                                              bool* aResult) {
  if (!aSecondURI) {
    return NS_ERROR_INVALID_ARG;
  }

  // Get the base domain for aSecondURI.
  nsAutoCString secondDomain;
  nsresult rv = GetBaseDomain(aSecondURI, secondDomain);
  LOG(("ThirdPartyUtil::IsThirdPartyInternal %s =? %s", aFirstDomain.get(),
       secondDomain.get()));
  if (NS_FAILED(rv)) return rv;

  *aResult = IsThirdPartyInternal(aFirstDomain, secondDomain);
  return NS_OK;
}

NS_IMETHODIMP
ThirdPartyUtil::GetPrincipalFromWindow(mozIDOMWindowProxy* aWin,
                                       nsIPrincipal** result) {
  nsCOMPtr<nsIScriptObjectPrincipal> scriptObjPrin = do_QueryInterface(aWin);
  if (!scriptObjPrin) {
    return NS_ERROR_INVALID_ARG;
  }

  nsCOMPtr<nsIPrincipal> prin = scriptObjPrin->GetPrincipal();
  if (!prin) {
    return NS_ERROR_INVALID_ARG;
  }

  prin.forget(result);
  return NS_OK;
}

// Get the URI associated with a window.
NS_IMETHODIMP
ThirdPartyUtil::GetURIFromWindow(mozIDOMWindowProxy* aWin, nsIURI** result) {
  nsCOMPtr<nsIPrincipal> prin;
  nsresult rv = GetPrincipalFromWindow(aWin, getter_AddRefs(prin));
  if (NS_FAILED(rv)) {
    return rv;
  }

  if (prin->GetIsNullPrincipal()) {
    LOG(("ThirdPartyUtil::GetURIFromWindow can't use null principal\n"));
    return NS_ERROR_INVALID_ARG;
  }

  rv = prin->GetURI(result);
  return rv;
}

// Determine if aFirstURI is third party with respect to aSecondURI. See docs
// for mozIThirdPartyUtil.
NS_IMETHODIMP
ThirdPartyUtil::IsThirdPartyURI(nsIURI* aFirstURI, nsIURI* aSecondURI,
                                bool* aResult) {
  NS_ENSURE_ARG(aFirstURI);
  NS_ENSURE_ARG(aSecondURI);
  NS_ASSERTION(aResult, "null outparam pointer");

  nsAutoCString firstHost;
  nsresult rv = GetBaseDomain(aFirstURI, firstHost);
  if (NS_FAILED(rv)) return rv;

  return IsThirdPartyInternal(firstHost, aSecondURI, aResult);
}

// Determine if any URI of the window hierarchy of aWindow is foreign with
// respect to aSecondURI. See docs for mozIThirdPartyUtil.
NS_IMETHODIMP
ThirdPartyUtil::IsThirdPartyWindow(mozIDOMWindowProxy* aWindow, nsIURI* aURI,
                                   bool* aResult) {
  NS_ENSURE_ARG(aWindow);
  NS_ASSERTION(aResult, "null outparam pointer");

  bool result;

  nsCString bottomDomain =
      GetBaseDomainFromWindow(nsPIDOMWindowOuter::From(aWindow));
  if (bottomDomain.IsEmpty()) {
    // We may have an about:blank window here.  Fall back to the slower code
    // path which is principal aware.
    nsCOMPtr<nsIURI> currentURI;
    nsresult rv = GetURIFromWindow(aWindow, getter_AddRefs(currentURI));
    if (NS_FAILED(rv)) {
      return rv;
    }

    rv = GetBaseDomain(currentURI, bottomDomain);
    if (NS_FAILED(rv)) {
      return rv;
    }
  }

  if (aURI) {
    // Determine whether aURI is foreign with respect to currentURI.
    nsresult rv = IsThirdPartyInternal(bottomDomain, aURI, &result);
    if (NS_FAILED(rv)) return rv;

    if (result) {
      *aResult = true;
      return NS_OK;
    }
  }

  nsPIDOMWindowOuter* current = nsPIDOMWindowOuter::From(aWindow);
  do {
    // We use GetScriptableParent rather than GetParent because we consider
    // <iframe mozbrowser> to be a top-level frame.
    nsPIDOMWindowOuter* parent = current->GetScriptableParent();
    // We don't use SameCOMIdentity here since we know that nsPIDOMWindowOuter
    // is only implemented by nsGlobalWindowOuter, so different objects of that
    // type will not have different nsISupports COM identities, and checking the
    // actual COM identity using SameCOMIdentity is expensive due to the virtual
    // calls involved.
    if (parent == current) {
      // We're at the topmost content window. We already know the answer.
      *aResult = false;
      return NS_OK;
    }

    nsCString parentDomain = GetBaseDomainFromWindow(parent);
    if (parentDomain.IsEmpty()) {
      // We may have an about:blank window here.  Fall back to the slower code
      // path which is principal aware.
      nsCOMPtr<nsIURI> parentURI;
      nsresult rv = GetURIFromWindow(parent, getter_AddRefs(parentURI));
      NS_ENSURE_SUCCESS(rv, rv);

      rv = IsThirdPartyInternal(bottomDomain, parentURI, &result);
      if (NS_FAILED(rv)) {
        return rv;
      }
    } else {
      result = IsThirdPartyInternal(bottomDomain, parentDomain);
    }

    if (result) {
      *aResult = true;
      return NS_OK;
    }

    current = parent;
  } while (1);

  MOZ_ASSERT_UNREACHABLE("should've returned");
  return NS_ERROR_UNEXPECTED;
}

// Determine if the URI associated with aChannel or any URI of the window
// hierarchy associated with the channel is foreign with respect to aSecondURI.
// See docs for mozIThirdPartyUtil.
NS_IMETHODIMP
ThirdPartyUtil::IsThirdPartyChannel(nsIChannel* aChannel, nsIURI* aURI,
                                    bool* aResult) {
  LOG(("ThirdPartyUtil::IsThirdPartyChannel [channel=%p]", aChannel));
  NS_ENSURE_ARG(aChannel);
  NS_ASSERTION(aResult, "null outparam pointer");

  nsresult rv;
  bool doForce = false;
  nsCOMPtr<nsIHttpChannelInternal> httpChannelInternal =
      do_QueryInterface(aChannel);
  if (httpChannelInternal) {
    uint32_t flags = 0;
    // Avoid checking the return value here since some channel implementations
    // may return NS_ERROR_NOT_IMPLEMENTED.
    mozilla::Unused << httpChannelInternal->GetThirdPartyFlags(&flags);

    doForce = (flags & nsIHttpChannelInternal::THIRD_PARTY_FORCE_ALLOW);

    // If aURI was not supplied, and we're forcing, then we're by definition
    // not foreign. If aURI was supplied, we still want to check whether it's
    // foreign with respect to the channel URI. (The forcing only applies to
    // whatever window hierarchy exists above the channel.)
    if (doForce && !aURI) {
      *aResult = false;
      return NS_OK;
    }
  }

  bool parentIsThird = false;

  // Obtain the URI from the channel, and its base domain.
  nsCOMPtr<nsIURI> channelURI;
  rv = NS_GetFinalChannelURI(aChannel, getter_AddRefs(channelURI));
  if (NS_FAILED(rv)) return rv;

  nsAutoCString channelDomain;
  rv = GetBaseDomain(channelURI, channelDomain);
  if (NS_FAILED(rv)) return rv;

  if (!doForce) {
    if (nsCOMPtr<nsILoadInfo> loadInfo = aChannel->LoadInfo()) {
      parentIsThird = loadInfo->GetIsInThirdPartyContext();
      if (!parentIsThird && loadInfo->GetExternalContentPolicyType() !=
                                nsIContentPolicy::TYPE_DOCUMENT) {
        // Check if the channel itself is third-party to its own requestor.
        // Unforunately, we have to go through the loading principal.
        nsCOMPtr<nsIURI> parentURI;
        rv = loadInfo->LoadingPrincipal()->GetURI(getter_AddRefs(parentURI));
        if (NS_SUCCEEDED(rv) && parentURI) {
          // We may have a principal like the system principal here which does
          // not have a URI.
          rv = IsThirdPartyInternal(channelDomain, parentURI, &parentIsThird);
          if (NS_FAILED(rv)) {
            return rv;
          }
        } else {
          // Found a principal with no URI, assuming third-party request
          parentIsThird = true;
        }
      }
    } else {
      NS_WARNING(
          "Found channel with no loadinfo, assuming third-party request");
      parentIsThird = true;
    }
  }

  // If we're not comparing to a URI, we have our answer. Otherwise, if
  // parentIsThird, we're not forcing and we know that we're a third-party
  // request.
  if (!aURI || parentIsThird) {
    *aResult = parentIsThird;
    return NS_OK;
  }

  // Determine whether aURI is foreign with respect to channelURI.
  return IsThirdPartyInternal(channelDomain, aURI, aResult);
}

NS_IMETHODIMP
ThirdPartyUtil::GetTopWindowForChannel(nsIChannel* aChannel,
                                       nsIURI* aURIBeingLoaded,
                                       mozIDOMWindowProxy** aWin) {
  NS_ENSURE_ARG(aWin);

  // Find the associated window and its parent window.
  nsCOMPtr<nsILoadContext> ctx;
  NS_QueryNotificationCallbacks(aChannel, ctx);
  if (!ctx) {
    return NS_ERROR_INVALID_ARG;
  }

  nsCOMPtr<mozIDOMWindowProxy> window;
  ctx->GetAssociatedWindow(getter_AddRefs(window));
  if (!window) {
    return NS_ERROR_INVALID_ARG;
  }

  nsCOMPtr<nsPIDOMWindowOuter> top =
      nsGlobalWindowOuter::Cast(window)
          ->GetTopExcludingExtensionAccessibleContentFrames(aURIBeingLoaded);
  top.forget(aWin);
  return NS_OK;
}

// Get the base domain for aHostURI; e.g. for "www.bbc.co.uk", this would be
// "bbc.co.uk". Only properly-formed URI's are tolerated, though a trailing
// dot may be present. If aHostURI is an IP address, an alias such as
// 'localhost', an eTLD such as 'co.uk', or the empty string, aBaseDomain will
// be the exact host. The result of this function should only be used in exact
// string comparisons, since substring comparisons will not be valid for the
// special cases elided above.
NS_IMETHODIMP
ThirdPartyUtil::GetBaseDomain(nsIURI* aHostURI, nsACString& aBaseDomain) {
  if (!aHostURI) {
    return NS_ERROR_INVALID_ARG;
  }

  // Get the base domain. this will fail if the host contains a leading dot,
  // more than one trailing dot, or is otherwise malformed.
  nsresult rv = mTLDService->GetBaseDomain(aHostURI, 0, aBaseDomain);
  if (rv == NS_ERROR_HOST_IS_IP_ADDRESS ||
      rv == NS_ERROR_INSUFFICIENT_DOMAIN_LEVELS) {
    // aHostURI is either an IP address, an alias such as 'localhost', an eTLD
    // such as 'co.uk', or the empty string. Uses the normalized host in such
    // cases.
    rv = aHostURI->GetAsciiHost(aBaseDomain);
  }
  NS_ENSURE_SUCCESS(rv, rv);

  // aHostURI (and thus aBaseDomain) may be the string '.'. If so, fail.
  if (aBaseDomain.Length() == 1 && aBaseDomain.Last() == '.')
    return NS_ERROR_INVALID_ARG;

  // Reject any URIs without a host that aren't file:// URIs. This makes it the
  // only way we can get a base domain consisting of the empty string, which
  // means we can safely perform foreign tests on such URIs where "not foreign"
  // means "the involved URIs are all file://".
  if (aBaseDomain.IsEmpty()) {
    bool isFileURI = false;
    aHostURI->SchemeIs("file", &isFileURI);
    if (!isFileURI) {
      return NS_ERROR_INVALID_ARG;
    }
  }

  return NS_OK;
}

NS_IMETHODIMP
ThirdPartyUtil::GetBaseDomainFromSchemeHost(const nsACString& aScheme,
                                            const nsACString& aAsciiHost,
                                            nsACString& aBaseDomain) {
  MOZ_DIAGNOSTIC_ASSERT(IsASCII(aAsciiHost));

  // Get the base domain. this will fail if the host contains a leading dot,
  // more than one trailing dot, or is otherwise malformed.
  nsresult rv = mTLDService->GetBaseDomainFromHost(aAsciiHost, 0, aBaseDomain);
  if (rv == NS_ERROR_HOST_IS_IP_ADDRESS ||
      rv == NS_ERROR_INSUFFICIENT_DOMAIN_LEVELS) {
    // aMozURL is either an IP address, an alias such as 'localhost', an eTLD
    // such as 'co.uk', or the empty string. Uses the normalized host in such
    // cases.
    aBaseDomain = aAsciiHost;
    rv = NS_OK;
  }
  NS_ENSURE_SUCCESS(rv, rv);

  // aMozURL (and thus aBaseDomain) may be the string '.'. If so, fail.
  if (aBaseDomain.Length() == 1 && aBaseDomain.Last() == '.')
    return NS_ERROR_INVALID_ARG;

  // Reject any URLs without a host that aren't file:// URLs. This makes it the
  // only way we can get a base domain consisting of the empty string, which
  // means we can safely perform foreign tests on such URLs where "not foreign"
  // means "the involved URLs are all file://".
  if (aBaseDomain.IsEmpty() && !aScheme.EqualsLiteral("file")) {
    return NS_ERROR_INVALID_ARG;
  }

  return NS_OK;
}
