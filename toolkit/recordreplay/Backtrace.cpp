/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "Backtrace.h"

#include "mozilla/Assertions.h"
#include "SpinLock.h"

#include <dlfcn.h>
#include <execinfo.h>
#include <string.h>

namespace mozilla {
namespace recordreplay {

const char*
SymbolNameRaw(void* aPtr)
{
  Dl_info info;
  return (dladdr(aPtr, &info) && info.dli_sname) ? info.dli_sname : "???";
}

size_t
GetBacktrace(const char* aAssertion, void** aAddresses, size_t aAddressesCount)
{
  size_t frameStart = 2;
  size_t frameCount = 12;

  // Locking operations usually have extra stack goop.
  if (!strcmp(aAssertion, "Lock 1")) {
    frameCount += 8;
  } else if (!strncmp(aAssertion, "Lock ", 5)) {
    frameCount += 4;
  }

  MOZ_RELEASE_ASSERT(frameStart + frameCount < aAddressesCount);

  size_t count = backtrace(aAddresses, frameStart + frameCount);
  if (count < frameStart) {
    return 0;
  }
  count -= frameStart;

  memmove(aAddresses, &aAddresses[frameStart], count * sizeof(void*));
  return count;
}

// dladdr is very slow, so cache the names produced for backtrace addresses.
struct AddressCacheEntry
{
  void* mAddress;
  const char* mName;
};
static const size_t AddressCacheSize = 7919;
static AddressCacheEntry* gAddressCache;
static SpinLock gAddressCacheSpinLock;

const char*
SymbolName(void* aPtr, char* aBuf, size_t aSize)
{
  AddressCacheEntry* entry = &gAddressCache[((size_t)aPtr) % AddressCacheSize];

  {
    AutoSpinLock lock(gAddressCacheSpinLock);
    if (entry->mAddress == aPtr) {
      return entry->mName;
    }
  }

  // Don't hold the address cache lock while we fetch the name, to reduce
  // contention.
  const char* name = SymbolNameRaw(aPtr);

  AutoSpinLock lock(gAddressCacheSpinLock);
  entry->mAddress = aPtr;
  entry->mName = name;
  return name;
}

void
InitializeBacktraces()
{
  gAddressCache = new AddressCacheEntry[AddressCacheSize];
}

} // namespace recordreplay
} // namespace mozilla
