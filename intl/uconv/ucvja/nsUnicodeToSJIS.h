/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsUnicodeToSJIS_h___
#define nsUnicodeToSJIS_h___

#include "nsID.h"

class nsISupports;

/**
 * A character set converter from Unicode to SJIS.
 *
 * @created         17/Feb/1999
 * @author  Catalin Rotaru [CATA]
 */
nsresult
nsUnicodeToSJISConstructor(nsISupports *aOuter, REFNSIID aIID,
                           void **aResult);

#endif /* nsUnicodeToSJIS_h___ */
