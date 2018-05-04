/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "Backtrace.h"

#include "mozilla/Assertions.h"
#include "SpinLock.h"

#if defined(XP_MACOSX)
#include <dlfcn.h>
#include <execinfo.h>
#endif

#if defined(WIN32)
#include <Dbghelp.h>
#include <Psapi.h>
#endif

#include <string.h>

namespace mozilla {
namespace recordreplay {

const char*
SymbolNameRaw(void* aPtr)
{
#if defined(XP_MACOSX)
  Dl_info info;
  return (dladdr(aPtr, &info) && info.dli_sname) ? info.dli_sname : "???";
#else
  return nullptr;
#endif
}

#if defined(XP_MACOSX)

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

#elif defined(WIN32)

// DLLs which we will look for instruction pointers on the stack.
struct BacktraceModule
{
  const char* mName;
  uint8_t* mBase;
  size_t mSize;
};
static BacktraceModule gBacktraceModules[] = {
  { "mozglue.dll" },
  { "nss3.dll" },
  { "xul.dll" }
};

void
InitializeBacktraces()
{
  if (gBacktraceModules[0].mBase) {
    return;
  }

  for (BacktraceModule& module : gBacktraceModules) {
    GetExecutableCodeRegionInDLL(module.mName, &module.mBase, &module.mSize);
  }
}

// Determine if aAddress is within the executable code region for one of the
// backtrace modules.
static bool
MaybeGetBacktraceModule(uint8_t* aAddress, const char** aName, size_t* aOffset)
{
  for (const BacktraceModule& module : gBacktraceModules) {
    if (MemoryContains(module.mBase, module.mSize, aAddress)) {
      if (aName) {
        *aName = module.mName;
      }
      if (aOffset) {
        *aOffset = aAddress - module.mBase;
      }
      return true;
    }
  }
  return false;
}

static void*
InvertAddress(void* aAddress)
{
  return (void*)((size_t)aAddress ^ (size_t)-1);
}

size_t
GetBacktrace(const char* aAssertion, void** aAddresses, size_t aAddressesCount)
{
  // Find the boundary of the current thread's stack.
  MEMORY_BASIC_INFORMATION buffer;
  SIZE_T nbytes = VirtualQuery(&buffer, &buffer, sizeof(buffer));
  MOZ_RELEASE_ASSERT(nbytes == sizeof(buffer);

  MOZ_RELEASE_ASSERT(buffer.AllocationBase <= buffer.BaseAddress);
  uint8_t* stackLimit = (uint8_t*)buffer.BaseAddress + buffer.RegionSize;

  size_t index = 0;

  // Look for instruction pointers on the stack which are within one of the
  // backtrace modules.
  uint8_t** cursor = (uint8_t**)&buffer;
  for (size_t i = 0; i < 5000; i++) {
    if ((uint8_t*)cursor >= stackLimit) {
      break;
    }

    uint8_t* addr = *cursor++;
    if (MaybeGetBacktraceModule(addr, nullptr, nullptr)) {
      bool found = false;
      for (size_t j = 0; j < index; j++) {
        if (aAddresses[j] == InvertAddress(addr)) {
          found = true;
          break;
        }
      }
      if (!found) {
	// Invert the bits in the stored addresses. Because we compute the
	// backtrace by looking for instruction pointers on the stack, if
	// aAddresses is itself on the stack then we might rediscover those
	// pointers as we go if we don't obfuscate them.
        aAddresses[index++] = InvertAddress(addr);
        if (index == aAddressesCount) {
          break;
        }
      }
    }
  }

  // Undo the address inversion from earlier.
  for (size_t i = 0; i < index; i++) {
    aAddresses[i] = InvertAddress(aAddresses[i]);
  }

  return index;
}

// For debugging.
void*
GetBacktraceAddress(void* aStart, size_t aIndex)
{
  uint8_t** cursor = (uint8_t**)aStart;
  for (;;) {
    uint8_t* addr = *cursor++;
    if (MaybeGetBacktraceModule(addr, nullptr, nullptr)) {
      if (--aIndex == 0) {
        return addr;
      }
    }
  }
  MOZ_CRASH();
}

const char*
SymbolName(void* aPtr, char* aBuf, size_t aSize)
{
  const char* dllName;
  size_t dllOffset;
  if (MaybeGetBacktraceModule((uint8_t*)aPtr, &dllName, &dllOffset)) {
    snprintf(aBuf, aSize, "%s+%d", dllName, (int) dllOffset);
    return aBuf;
  }
  return "???";
}

#else // WIN32
#error "Unknown platform"
#endif

} // namespace recordreplay
} // namespace mozilla
