/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsUCConstructors.h"
#include "nsISO88595ToUnicode.h"
#include "mozilla/Telemetry.h"

using namespace mozilla;

//----------------------------------------------------------------------
// Global functions and data [declaration]

nsresult
nsISO88595ToUnicodeConstructor(nsISupports *aOuter, REFNSIID aIID,
                               void **aResult) 
{
  static const uint16_t g_utMappingTable[] = {
#include "8859-5.ut"
  };

  Telemetry::Accumulate(Telemetry::DECODER_INSTANTIATED_ISO_8859_5, true);
  return CreateOneByteDecoder((uMappingTable*) &g_utMappingTable,
                              aOuter, aIID, aResult);
}

