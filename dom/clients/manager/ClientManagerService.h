/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef _mozilla_dom_ClientManagerService_h
#define _mozilla_dom_ClientManagerService_h

#include "nsDataHashtable.h"

namespace mozilla {

namespace dom {

class ClientSourceParent;

// Define a singleton service to manage client activity throughout the
// browser.  This service runs on the PBackground thread.  To interact
// it with it please use the ClientManager and ClientHandle classes.
class ClientManagerService final
{
  // Store the ClientSourceParent objects in a hash table.  We want to
  // optimize for insertion, removal, and lookup by UUID.
  nsDataHashtable<nsIDHashKey, ClientSourceParent*> mSourceTable;

  ClientManagerService();
  ~ClientManagerService();

public:
  static already_AddRefed<ClientManagerService>
  GetOrCreateInstance();

  void
  AddSource(ClientSourceParent* aSource);

  void
  RemoveSource(ClientSourceParent* aSource);

  ClientSourceParent*
  FindSource(const nsID& aID,
             const mozilla::ipc::PrincipalInfo& aPrincipalInfo);

  NS_INLINE_DECL_REFCOUNTING(mozilla::dom::ClientManagerService)
};

} // namespace dom
} // namespace mozilla

#endif // _mozilla_dom_ClientManagerService_h
