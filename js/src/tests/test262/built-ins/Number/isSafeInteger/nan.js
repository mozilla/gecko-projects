// Copyright (c) 2016 The V8 Project authors. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
es6id: 20.1.2.5
esid: sec-number.issafeinteger
description: >
  Return false if argument is NaN
info: |
  Number.isSafeInteger ( number )

  [...]
  2. If number is NaN, +∞, or -∞, return false.
  [...]
---*/

assert.sameValue(Number.isSafeInteger(NaN), false);

reportCompare(0, 0);
