/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsNSSCertificateFakeTransport.h"

#include "mozilla/Assertions.h"
#include "nsIClassInfoImpl.h"
#include "nsIObjectInputStream.h"
#include "nsIObjectOutputStream.h"
#include "nsISupportsPrimitives.h"
#include "nsNSSCertificate.h"
#include "nsString.h"

NS_IMPL_ISUPPORTS(nsNSSCertificateFakeTransport,
                  nsIX509Cert,
                  nsISerializable,
                  nsIClassInfo)

nsNSSCertificateFakeTransport::nsNSSCertificateFakeTransport()
  : mCertSerialization(nullptr)
{
}

nsNSSCertificateFakeTransport::~nsNSSCertificateFakeTransport()
{
  mCertSerialization = nullptr;
}

NS_IMETHODIMP
nsNSSCertificateFakeTransport::GetDbKey(nsACString&)
{
  MOZ_ASSERT_UNREACHABLE("Unimplemented on content process");
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsNSSCertificateFakeTransport::GetDisplayName(nsAString&)
{
  MOZ_ASSERT_UNREACHABLE("Unimplemented on content process");
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsNSSCertificateFakeTransport::GetEmailAddress(nsAString&)
{
  MOZ_ASSERT_UNREACHABLE("Unimplemented on content process");
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsNSSCertificateFakeTransport::GetEmailAddresses(uint32_t*, char16_t***)
{
  MOZ_ASSERT_UNREACHABLE("Unimplemented on content process");
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsNSSCertificateFakeTransport::ContainsEmailAddress(const nsAString&, bool*)
{
  MOZ_ASSERT_UNREACHABLE("Unimplemented on content process");
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsNSSCertificateFakeTransport::GetCommonName(nsAString&)
{
  MOZ_ASSERT_UNREACHABLE("Unimplemented on content process");
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsNSSCertificateFakeTransport::GetOrganization(nsAString&)
{
  MOZ_ASSERT_UNREACHABLE("Unimplemented on content process");
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsNSSCertificateFakeTransport::GetIssuerCommonName(nsAString&)
{
  MOZ_ASSERT_UNREACHABLE("Unimplemented on content process");
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsNSSCertificateFakeTransport::GetIssuerOrganization(nsAString&)
{
  MOZ_ASSERT_UNREACHABLE("Unimplemented on content process");
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsNSSCertificateFakeTransport::GetIssuerOrganizationUnit(nsAString&)
{
  MOZ_ASSERT_UNREACHABLE("Unimplemented on content process");
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsNSSCertificateFakeTransport::GetIssuer(nsIX509Cert**)
{
  MOZ_ASSERT_UNREACHABLE("Unimplemented on content process");
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsNSSCertificateFakeTransport::GetOrganizationalUnit(nsAString&)
{
  MOZ_ASSERT_UNREACHABLE("Unimplemented on content process");
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsNSSCertificateFakeTransport::GetChain(nsIArray**)
{
  MOZ_ASSERT_UNREACHABLE("Unimplemented on content process");
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsNSSCertificateFakeTransport::GetSubjectName(nsAString&)
{
  MOZ_ASSERT_UNREACHABLE("Unimplemented on content process");
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsNSSCertificateFakeTransport::GetIssuerName(nsAString&)
{
  MOZ_ASSERT_UNREACHABLE("Unimplemented on content process");
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsNSSCertificateFakeTransport::GetSerialNumber(nsAString&)
{
  MOZ_ASSERT_UNREACHABLE("Unimplemented on content process");
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsNSSCertificateFakeTransport::GetSha256Fingerprint(nsAString&)
{
  MOZ_ASSERT_UNREACHABLE("Unimplemented on content process");
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsNSSCertificateFakeTransport::GetSha1Fingerprint(nsAString&)
{
  MOZ_ASSERT_UNREACHABLE("Unimplemented on content process");
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsNSSCertificateFakeTransport::GetTokenName(nsAString&)
{
  MOZ_ASSERT_UNREACHABLE("Unimplemented on content process");
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsNSSCertificateFakeTransport::GetRawDER(uint32_t*, uint8_t**)
{
  MOZ_ASSERT_UNREACHABLE("Unimplemented on content process");
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsNSSCertificateFakeTransport::GetValidity(nsIX509CertValidity**)
{
  MOZ_ASSERT_UNREACHABLE("Unimplemented on content process");
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsNSSCertificateFakeTransport::GetKeyUsages(nsAString&)
{
  MOZ_ASSERT_UNREACHABLE("Unimplemented on content process");
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsNSSCertificateFakeTransport::GetASN1Structure(nsIASN1Object**)
{
  MOZ_ASSERT_UNREACHABLE("Unimplemented on content process");
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsNSSCertificateFakeTransport::Equals(nsIX509Cert*, bool*)
{
  MOZ_ASSERT_UNREACHABLE("Unimplemented on content process");
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsNSSCertificateFakeTransport::GetSha256SubjectPublicKeyInfoDigest(nsACString&)
{
  MOZ_ASSERT_UNREACHABLE("Unimplemented on content process");
  return NS_ERROR_NOT_IMPLEMENTED;
}

// NB: This serialization must match that of nsNSSCertificate.
NS_IMETHODIMP
nsNSSCertificateFakeTransport::Write(nsIObjectOutputStream* aStream)
{
  // On a non-chrome process we don't have mCert because we lack
  // nsNSSComponent. nsNSSCertificateFakeTransport object is used only to
  // carry the certificate serialization.

  // This serialization has to match that of nsNSSCertificate, so include this
  // now-unused field.
  nsresult rv = aStream->Write32(0);
  if (NS_FAILED(rv)) {
    return rv;
  }

  rv = aStream->Write32(mCertSerialization->len);
  if (NS_FAILED(rv)) {
    return rv;
  }

  return aStream->WriteByteArray(mCertSerialization->data,
                                 mCertSerialization->len);
}

NS_IMETHODIMP
nsNSSCertificateFakeTransport::Read(nsIObjectInputStream* aStream)
{
  // This serialization has to match that of nsNSSCertificate, so read the (now
  // unused) cachedEVStatus.
  uint32_t unusedCachedEVStatus;
  nsresult rv = aStream->Read32(&unusedCachedEVStatus);
  if (NS_FAILED(rv)) {
    return rv;
  }

  uint32_t len;
  rv = aStream->Read32(&len);
  if (NS_FAILED(rv)) {
    return rv;
  }

  nsXPIDLCString str;
  rv = aStream->ReadBytes(len, getter_Copies(str));
  if (NS_FAILED(rv)) {
    return rv;
  }

  // On a non-chrome process we cannot instatiate mCert because we lack
  // nsNSSComponent. nsNSSCertificateFakeTransport object is used only to
  // carry the certificate serialization.
  mCertSerialization =
    mozilla::UniqueSECItem(SECITEM_AllocItem(nullptr, nullptr, len));
  if (!mCertSerialization) {
    return NS_ERROR_OUT_OF_MEMORY;
  }
  PORT_Memcpy(mCertSerialization->data, str.Data(), len);

  return NS_OK;
}

NS_IMETHODIMP
nsNSSCertificateFakeTransport::GetInterfaces(uint32_t* count, nsIID*** array)
{
  *count = 0;
  *array = nullptr;
  return NS_OK;
}

NS_IMETHODIMP
nsNSSCertificateFakeTransport::GetScriptableHelper(nsIXPCScriptable** _retval)
{
  *_retval = nullptr;
  return NS_OK;
}

NS_IMETHODIMP
nsNSSCertificateFakeTransport::GetContractID(char** aContractID)
{
  *aContractID = nullptr;
  return NS_OK;
}

NS_IMETHODIMP
nsNSSCertificateFakeTransport::GetClassDescription(char** aClassDescription)
{
  *aClassDescription = nullptr;
  return NS_OK;
}

NS_IMETHODIMP
nsNSSCertificateFakeTransport::GetClassID(nsCID** aClassID)
{
  *aClassID = (nsCID*) moz_xmalloc(sizeof(nsCID));
  if (!*aClassID)
    return NS_ERROR_OUT_OF_MEMORY;
  return GetClassIDNoAlloc(*aClassID);
}

NS_IMETHODIMP
nsNSSCertificateFakeTransport::GetFlags(uint32_t* aFlags)
{
  *aFlags = nsIClassInfo::THREADSAFE;
  return NS_OK;
}

NS_IMETHODIMP
nsNSSCertificateFakeTransport::GetClassIDNoAlloc(nsCID* aClassIDNoAlloc)
{
  static NS_DEFINE_CID(kNSSCertificateCID, NS_X509CERT_CID);

  *aClassIDNoAlloc = kNSSCertificateCID;
  return NS_OK;
}

NS_IMETHODIMP
nsNSSCertificateFakeTransport::GetCertType(unsigned int*)
{
  MOZ_ASSERT_UNREACHABLE("Unimplemented on content process");
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsNSSCertificateFakeTransport::GetIsSelfSigned(bool*)
{
  MOZ_ASSERT_UNREACHABLE("Unimplemented on content process");
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsNSSCertificateFakeTransport::GetIsBuiltInRoot(bool*)
{
  MOZ_ASSERT_UNREACHABLE("Unimplemented on content process");
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsNSSCertificateFakeTransport::GetAllTokenNames(unsigned int*, char16_t***)
{
  MOZ_ASSERT_UNREACHABLE("Unimplemented on content process");
  return NS_ERROR_NOT_IMPLEMENTED;
}

CERTCertificate*
nsNSSCertificateFakeTransport::GetCert()
{
  MOZ_ASSERT_UNREACHABLE("Unimplemented on content process");
  return nullptr;
}

NS_IMETHODIMP
nsNSSCertificateFakeTransport::ExportAsCMS(unsigned int,
                                           unsigned int*,
                                           unsigned char**)
{
  MOZ_ASSERT_UNREACHABLE("Unimplemented on content process");
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsNSSCertificateFakeTransport::MarkForPermDeletion()
{
  MOZ_ASSERT_UNREACHABLE("Unimplemented on content process");
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMPL_CLASSINFO(nsNSSCertListFakeTransport,
                  nullptr,
                  // inferred from nsIX509Cert
                  nsIClassInfo::THREADSAFE,
                  NS_X509CERTLIST_CID)

NS_IMPL_ISUPPORTS_CI(nsNSSCertListFakeTransport,
                     nsIX509CertList,
                     nsISerializable)

nsNSSCertListFakeTransport::nsNSSCertListFakeTransport()
{
}

nsNSSCertListFakeTransport::~nsNSSCertListFakeTransport()
{
}

NS_IMETHODIMP
nsNSSCertListFakeTransport::AddCert(nsIX509Cert* aCert)
{
  MOZ_ASSERT_UNREACHABLE("Unimplemented on content process");
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsNSSCertListFakeTransport::DeleteCert(nsIX509Cert* aCert)
{
  MOZ_ASSERT_UNREACHABLE("Unimplemented on content process");
  return NS_ERROR_NOT_IMPLEMENTED;
}

CERTCertList*
nsNSSCertListFakeTransport::GetRawCertList()
{
  MOZ_ASSERT_UNREACHABLE("Unimplemented on content process");
  return nullptr;
}

NS_IMETHODIMP
nsNSSCertListFakeTransport::GetEnumerator(nsISimpleEnumerator**)
{
  MOZ_ASSERT_UNREACHABLE("Unimplemented on content process");
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsNSSCertListFakeTransport::Equals(nsIX509CertList*, bool*)
{
  MOZ_ASSERT_UNREACHABLE("Unimplemented on content process");
  return NS_ERROR_NOT_IMPLEMENTED;
}

// NB: This serialization must match that of nsNSSCertList.
NS_IMETHODIMP
nsNSSCertListFakeTransport::Write(nsIObjectOutputStream* aStream)
{
  uint32_t certListLen = mFakeCertList.length();
  // Write the length of the list
  nsresult rv = aStream->Write32(certListLen);
  if (NS_FAILED(rv)) {
    return rv;
  }

  for (size_t i = 0; i < certListLen; i++) {
    nsCOMPtr<nsIX509Cert> cert = mFakeCertList[i];
    nsCOMPtr<nsISerializable> serializableCert = do_QueryInterface(cert);
    rv = aStream->WriteCompoundObject(serializableCert,
                                      NS_GET_IID(nsIX509Cert), true);
    if (NS_FAILED(rv)) {
      break;
    }
  }

  return rv;
}

NS_IMETHODIMP
nsNSSCertListFakeTransport::Read(nsIObjectInputStream* aStream)
{
  uint32_t certListLen;
  nsresult rv = aStream->Read32(&certListLen);
  if (NS_FAILED(rv)) {
    return rv;
  }

  for (uint32_t i = 0; i < certListLen; i++) {
    nsCOMPtr<nsISupports> certSupports;
    rv = aStream->ReadObject(true, getter_AddRefs(certSupports));
    if (NS_FAILED(rv)) {
      break;
    }

    nsCOMPtr<nsIX509Cert> cert = do_QueryInterface(certSupports);
    if (!mFakeCertList.append(cert)) {
      return NS_ERROR_OUT_OF_MEMORY;
    }
  }

  return rv;
}
