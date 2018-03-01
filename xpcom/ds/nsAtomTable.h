/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsAtomTable_h__
#define nsAtomTable_h__

#include "mozilla/MemoryReporting.h"
#include <stddef.h>

void NS_InitAtomTable();
void NS_ShutdownAtomTable();

size_t NS_SizeOfAtomTableIncludingThis(mozilla::MallocSizeOf aMallocSizeOf);

#endif // nsAtomTable_h__
