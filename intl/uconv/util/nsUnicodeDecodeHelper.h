/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef nsUnicodeDecodeHelper_h__
#define nsUnicodeDecodeHelper_h__

#include "nsError.h"
#include "uconvutil.h"
//----------------------------------------------------------------------
// Class nsUnicodeDecodeHelper [declaration]

/**
 *
 * @created         18/Mar/1998
 * @author  Catalin Rotaru [CATA]
 */
class nsUnicodeDecodeHelper
{
public:
  /**
   * Converts data using a lookup table and optional shift table
   */
  static nsresult ConvertByTable(const char * aSrc, int32_t * aSrcLength, 
                                 char16_t * aDest, int32_t * aDestLength,
                                 uScanClassID aScanClass,
                                 uShiftInTable * aShiftInTable,
                                 uMappingTable  * aMappingTable,
                                 bool aErrorSignal = false);

  /**
   * Converts data using a set of lookup tables.
   */
  static nsresult ConvertByMultiTable(const char * aSrc, int32_t * aSrcLength,
      char16_t * aDest, int32_t * aDestLength, int32_t aTableCount, 
      const uRange * aRangeArray, uScanClassID * aScanClassArray,
      uMappingTable ** aMappingTable, bool aErrorSignal = false);

  /**
   * Converts data using a fast lookup table.
   */
  static nsresult ConvertByFastTable(const char * aSrc, int32_t * aSrcLength, 
      char16_t * aDest, int32_t * aDestLength, const char16_t * aFastTable, 
      int32_t aTableSize, bool aErrorSignal);

  /**
   * Create a cache-like fast lookup table from a normal one.
   */
  static nsresult CreateFastTable(uMappingTable * aMappingTable,
      char16_t * aFastTable,  int32_t aTableSize);
};

#endif // nsUnicodeDecodeHelper_h__


