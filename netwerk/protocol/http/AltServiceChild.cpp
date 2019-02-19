/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim:set ts=4 sw=4 sts=4 et cin: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// HttpLog.h should generally be included first
#include "HttpLog.h"

#include "AltServiceChild.h"
#include "nsHttpConnectionInfo.h"
#include "nsThreadUtils.h"

namespace mozilla {
namespace net {

static AltServiceChild* sAltServiceChild;

AltServiceChild::AltServiceChild() {
  LOG(("Creating AltServiceChild @%p\n", this));
  MOZ_COUNT_CTOR(AltServiceChild);
  sAltServiceChild = this;
}

AltServiceChild::~AltServiceChild() {
  LOG(("Deleting AltServiceChild @%p\n", this));
  MOZ_COUNT_DTOR(AltServiceChild);
  sAltServiceChild = nullptr;
}

/* static */ AltServiceChild* AltServiceChild::GetSingleton() {
  return sAltServiceChild;
}

void AltServiceChild::ClearHostMapping(nsHttpConnectionInfo* aCi) {
  MOZ_ASSERT(aCi);

  LOG(("AltServiceChild::ClearHostMapping %p\n", this));
  RefPtr<nsHttpConnectionInfo> ci(aCi);
  auto clearHostMappingFunc = [ci{std::move(ci)}]() {
    if (!ci->GetOrigin().IsEmpty()) {
      Unused << AltServiceChild::GetSingleton()->SendClearHostMapping(
          ci->GetOrigin(), ci->OriginPort(), ci->GetOriginAttributes(),
          ci->GetTopWindowOrigin());
    }
  };

  if (!NS_IsMainThread()) {
    NS_DispatchToMainThread(NS_NewRunnableFunction(
        "net::AltServiceChild::ClearHostMapping", clearHostMappingFunc));
  } else {
    clearHostMappingFunc();
  }
}

}  // namespace net
}  // namespace mozilla
