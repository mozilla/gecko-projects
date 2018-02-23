/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_toolkit_recordreplay_Backtrace_h
#define mozilla_toolkit_recordreplay_Backtrace_h

#include "mozilla/Types.h"

namespace mozilla {
namespace recordreplay {

// This file declares some routines for quickly getting a usable backtrace for
// associating with a record/replay assertion.

// Initialize backtrace state.
void InitializeBacktraces();

// Get a list of instruction pointers to use as a backtrace for a
// record/replay assertion. The returned value is the number of addresses
// filled in.
size_t GetBacktrace(const char* aAssertion, void** aAddresses, size_t aAddressesCount);

// Get a symbol name for an instruction pointer if possible, or null.
const char* SymbolNameRaw(void* aPtr);

// Get a symbol name for an instruction pointer. Unlike SymbolNameRaw, this
// never returns null. This may use the supplied buffer for producing the
// symbol name.
const char* SymbolName(void* aPtr, char* aBuf, size_t aSize);

} // recordreplay
} // mozilla

#endif // mozilla_toolkit_recordreplay_Backtrace_h
