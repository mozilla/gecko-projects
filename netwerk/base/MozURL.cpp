/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MozURL.h"
#include "rust-url-capi/src/rust-url-capi.h"

namespace mozilla {
namespace net {

NS_IMPL_ADDREF(MozURL)
NS_IMPL_RELEASE(MozURL)

/* static */ nsresult
MozURL::Init(MozURL** aURL, const nsACString& aSpec, const MozURL* aBaseURL)
{
  rusturl* base = aBaseURL ? aBaseURL->mURL.get() : nullptr;
  rusturl* ptr = rusturl_new(&aSpec, base);
  if (!ptr) {
    return NS_ERROR_FAILURE;
  }
  RefPtr<MozURL> url = new MozURL(ptr);
  url.forget(aURL);
  return NS_OK;
}

nsresult
MozURL::GetScheme(nsACString& aScheme)
{
  return rusturl_get_scheme(mURL.get(), &aScheme);
}

nsresult
MozURL::GetSpec(nsACString& aSpec)
{
  return rusturl_get_spec(mURL.get(), &aSpec);
}

nsresult
MozURL::GetUsername(nsACString& aUser)
{
  return rusturl_get_username(mURL.get(), &aUser);
}

nsresult
MozURL::GetPassword(nsACString& aPassword)
{
  return rusturl_get_password(mURL.get(), &aPassword);
}

nsresult
MozURL::GetHostname(nsACString& aHost)
{
  return rusturl_get_host(mURL.get(), &aHost);
}

nsresult
MozURL::GetHostPort(nsACString& aHostPort)
{
  nsresult rv = rusturl_get_host(mURL.get(), &aHostPort);
  if (NS_FAILED(rv)) {
    return rv;
  }

  int32_t port;
  rv = GetPort(&port);
  if (NS_FAILED(rv)) {
    aHostPort.Truncate();
    return rv;
  }
  if (port != -1) {
    aHostPort.AppendLiteral(":");
    aHostPort.AppendInt(port);
  }

  return NS_OK;
}

nsresult
MozURL::GetPort(int32_t* aPort)
{
  return rusturl_get_port(mURL.get(), aPort);
}

nsresult
MozURL::GetFilePath(nsACString& aPath)
{
  return rusturl_get_filepath(mURL.get(), &aPath);
}

nsresult
MozURL::GetQuery(nsACString& aQuery)
{
  return rusturl_get_query(mURL.get(), &aQuery);
}

nsresult
MozURL::GetRef(nsACString& aRef)
{
  return rusturl_get_fragment(mURL.get(), &aRef);
}

nsresult
MozURL::GetOrigin(nsACString& aOrigin)
{
  return rusturl_get_origin(mURL.get(), &aOrigin);
}

// MozURL::Mutator

MozURL::Mutator::Mutator(MozURL* url)
  : mURL(rusturl_clone(url->mURL.get()))
  , mFinalized(false)
  , mStatus(NS_OK)
{
}

// This macro ensures that the mutator is still valid, meaning it hasn't been
// finalized, and none of the setters have returned an error code.
#define ENSURE_VALID()                          \
  PR_BEGIN_MACRO                                \
    if (mFinalized) {                           \
      mStatus = NS_ERROR_NOT_AVAILABLE;         \
    }                                           \
    if (NS_FAILED(mStatus)) {                   \
      return *this;                             \
    }                                           \
  PR_END_MACRO

nsresult
MozURL::Mutator::Finalize(MozURL** aURL)
{
  if (mFinalized) {
    return NS_ERROR_NOT_AVAILABLE;
  }
  mFinalized = true;
  if (NS_FAILED(mStatus)) {
    return mStatus;
  }
  RefPtr<MozURL> result = new MozURL(mURL.release());
  result.forget(aURL);
  return NS_OK;
}

MozURL::Mutator&
MozURL::Mutator::SetScheme(const nsACString& aScheme)
{
  ENSURE_VALID();
  mStatus = rusturl_set_scheme(mURL.get(), &aScheme);
  return *this;
}

MozURL::Mutator&
MozURL::Mutator::SetUsername(const nsACString& aUser)
{
  ENSURE_VALID();
  mStatus = rusturl_set_username(mURL.get(), &aUser);
  return *this;
}

MozURL::Mutator&
MozURL::Mutator::SetPassword(const nsACString& aPassword)
{
  ENSURE_VALID();
  mStatus = rusturl_set_password(mURL.get(), &aPassword);
  return *this;
}

MozURL::Mutator&
MozURL::Mutator::SetHostname(const nsACString& aHost)
{
  ENSURE_VALID();
  mStatus = rusturl_set_host(mURL.get(), &aHost);
  return *this;
}

MozURL::Mutator&
MozURL::Mutator::SetHostPort(const nsACString& aHostPort)
{
  ENSURE_VALID();
  mStatus = rusturl_set_host_port(mURL.get(), &aHostPort);
  return *this;
}

MozURL::Mutator&
MozURL::Mutator::SetFilePath(const nsACString& aPath)
{
  ENSURE_VALID();
  mStatus = rusturl_set_path(mURL.get(), &aPath);
  return *this;
}

MozURL::Mutator&
MozURL::Mutator::SetQuery(const nsACString& aQuery)
{
  ENSURE_VALID();
  mStatus = rusturl_set_query(mURL.get(), &aQuery);
  return *this;
}

MozURL::Mutator&
MozURL::Mutator::SetRef(const nsACString& aRef)
{
  ENSURE_VALID();
  mStatus = rusturl_set_fragment(mURL.get(), &aRef);
  return *this;
}

MozURL::Mutator&
MozURL::Mutator::SetPort(int32_t aPort)
{
  ENSURE_VALID();
  mStatus = rusturl_set_port_no(mURL.get(), aPort);
  return *this;
}

void
MozURL::FreeRustURL::operator()(rusturl* aPtr)
{
  rusturl_free(aPtr);
}

} // namespace net
} // namespace mozilla
