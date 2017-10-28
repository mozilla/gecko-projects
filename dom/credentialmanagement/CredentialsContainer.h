/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_CredentialsContainer_h
#define mozilla_dom_CredentialsContainer_h

#include "mozilla/dom/CredentialManagementBinding.h"

namespace mozilla {
namespace dom {

class CredentialsContainer final : public nsISupports
                                 , public nsWrapperCache
{
public:
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_CLASS(CredentialsContainer)

  explicit CredentialsContainer(nsPIDOMWindowInner* aParent);

  nsPIDOMWindowInner*
  GetParentObject() const
  {
    return mParent;
  }

  virtual JSObject*
  WrapObject(JSContext* aCx, JS::Handle<JSObject*> aGivenProto) override;

  already_AddRefed<Promise>
  Get(const CredentialRequestOptions& aOptions);
  already_AddRefed<Promise>
  Create(const CredentialCreationOptions& aOptions);
  already_AddRefed<Promise>
  Store(const Credential& aCredential);

private:
  ~CredentialsContainer();

  nsCOMPtr<nsPIDOMWindowInner> mParent;
};

} // namespace dom
} // namespace mozilla

#endif // mozilla_dom_CredentialsContainer_h
