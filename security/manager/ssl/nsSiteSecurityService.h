/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef __nsSiteSecurityService_h__
#define __nsSiteSecurityService_h__

#include "mozilla/DataStorage.h"
#include "mozilla/RefPtr.h"
#include "nsCOMPtr.h"
#include "nsIObserver.h"
#include "nsISiteSecurityService.h"
#include "nsString.h"
#include "nsTArray.h"
#include "pkix/pkixtypes.h"
#include "prtime.h"

class nsIURI;
class nsISSLStatus;

// {16955eee-6c48-4152-9309-c42a465138a1}
#define NS_SITE_SECURITY_SERVICE_CID \
  {0x16955eee, 0x6c48, 0x4152, \
    {0x93, 0x09, 0xc4, 0x2a, 0x46, 0x51, 0x38, 0xa1} }

/**
 * SecurityPropertyState: A utility enum for representing the different states
 * a security property can be in.
 * SecurityPropertySet and SecurityPropertyUnset correspond to indicating
 * a site has or does not have the security property in question, respectively.
 * SecurityPropertyKnockout indicates a value on a preloaded list is being
 * overridden, and the associated site does not have the security property
 * in question.
 */
enum SecurityPropertyState {
  SecurityPropertyUnset = nsISiteSecurityState::SECURITY_PROPERTY_UNSET,
  SecurityPropertySet = nsISiteSecurityState::SECURITY_PROPERTY_SET,
  SecurityPropertyKnockout = nsISiteSecurityState::SECURITY_PROPERTY_KNOCKOUT,
  SecurityPropertyNegative = nsISiteSecurityState::SECURITY_PROPERTY_NEGATIVE,
};

/**
 * SiteHPKPState: A utility class that encodes/decodes a string describing
 * the public key pins of a site.
 * HPKP state consists of:
 *  - Hostname (nsCString)
 *  - Expiry time (PRTime (aka int64_t) in milliseconds)
 *  - A state flag (SecurityPropertyState, default SecurityPropertyUnset)
 *  - An include subdomains flag (bool, default false)
 *  - An array of sha-256 hashed base 64 encoded fingerprints of required keys
 */
class SiteHPKPState : public nsISiteHPKPState
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSISITEHPKPSTATE
  NS_DECL_NSISITESECURITYSTATE

  SiteHPKPState();
  SiteHPKPState(const nsCString& aHost, const nsCString& aStateString);
  SiteHPKPState(const nsCString& aHost, PRTime aExpireTime,
                SecurityPropertyState aState, bool aIncludeSubdomains,
                nsTArray<nsCString>& SHA256keys);

  nsCString mHostname;
  PRTime mExpireTime;
  SecurityPropertyState mState;
  bool mIncludeSubdomains;
  nsTArray<nsCString> mSHA256keys;

  bool IsExpired(mozilla::pkix::Time aTime)
  {
    if (aTime > mozilla::pkix::TimeFromEpochInSeconds(mExpireTime /
                                                      PR_MSEC_PER_SEC)) {
      return true;
    }
    return false;
  }

  void ToString(nsCString& aString);

protected:
  virtual ~SiteHPKPState() {};
};

/**
 * SiteHSTSState: A utility class that encodes/decodes a string describing
 * the security state of a site. Currently only handles HSTS.
 * HSTS state consists of:
 *  - Hostname (nsCString)
 *  - Expiry time (PRTime (aka int64_t) in milliseconds)
 *  - A state flag (SecurityPropertyState, default SecurityPropertyUnset)
 *  - An include subdomains flag (bool, default false)
 */
class SiteHSTSState : public nsISiteHSTSState
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSISITEHSTSSTATE
  NS_DECL_NSISITESECURITYSTATE

  SiteHSTSState(const nsCString& aHost, const nsCString& aStateString);
  SiteHSTSState(const nsCString& aHost, PRTime aHSTSExpireTime,
                SecurityPropertyState aHSTSState, bool aHSTSIncludeSubdomains);

  nsCString mHostname;
  PRTime mHSTSExpireTime;
  SecurityPropertyState mHSTSState;
  bool mHSTSIncludeSubdomains;

  bool IsExpired(uint32_t aType)
  {
    // If mHSTSExpireTime is 0, this entry never expires (this is the case for
    // knockout entries).
    if (mHSTSExpireTime == 0) {
      return false;
    }

    PRTime now = PR_Now() / PR_USEC_PER_MSEC;
    if (now > mHSTSExpireTime) {
      return true;
    }

    return false;
  }

  void ToString(nsCString &aString);

protected:
  virtual ~SiteHSTSState() {}
};

struct nsSTSPreload;

class nsSiteSecurityService : public nsISiteSecurityService
                            , public nsIObserver
{
public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSIOBSERVER
  NS_DECL_NSISITESECURITYSERVICE

  nsSiteSecurityService();
  nsresult Init();

protected:
  virtual ~nsSiteSecurityService();

private:
  nsresult GetHost(nsIURI *aURI, nsACString &aResult);
  nsresult SetHSTSState(uint32_t aType, const char* aHost, int64_t maxage,
                        bool includeSubdomains, uint32_t flags,
                        SecurityPropertyState aHSTSState, bool aIsPreload);
  nsresult ProcessHeaderInternal(uint32_t aType, nsIURI* aSourceURI,
                                 const nsCString& aHeader,
                                 nsISSLStatus* aSSLStatus,
                                 uint32_t aFlags, uint64_t* aMaxAge,
                                 bool* aIncludeSubdomains,
                                 uint32_t* aFailureResult);
  nsresult ProcessSTSHeader(nsIURI* aSourceURI, const nsCString& aHeader,
                            uint32_t flags, uint64_t* aMaxAge,
                            bool* aIncludeSubdomains, uint32_t* aFailureResult);
  nsresult ProcessPKPHeader(nsIURI* aSourceURI, const nsCString& aHeader,
                            nsISSLStatus* aSSLStatus, uint32_t flags,
                            uint64_t* aMaxAge, bool* aIncludeSubdomains,
                            uint32_t* aFailureResult);
  nsresult SetHPKPState(const char* aHost, SiteHPKPState& entry, uint32_t flags,
                        bool aIsPreload);
  nsresult RemoveStateInternal(uint32_t aType, const nsAutoCString& aHost,
                               uint32_t aFlags, bool aIsPreload);
  bool HostHasHSTSEntry(const nsAutoCString& aHost,
                        bool aRequireIncludeSubdomains, uint32_t aFlags,
                        bool* aResult, bool* aCached);
  const nsSTSPreload *GetPreloadListEntry(const char *aHost);

  uint64_t mMaxMaxAge;
  bool mUsePreloadList;
  int64_t mPreloadListTimeOffset;
  bool mProcessPKPHeadersFromNonBuiltInRoots;
  RefPtr<mozilla::DataStorage> mSiteStateStorage;
  RefPtr<mozilla::DataStorage> mPreloadStateStorage;
};

#endif // __nsSiteSecurityService_h__
