// |reftest| skip-if(!this.hasOwnProperty('Atomics')||!this.hasOwnProperty('SharedArrayBuffer')||(this.hasOwnProperty('getBuildConfiguration')&&getBuildConfiguration()['arm64-simulator'])) -- Atomics,SharedArrayBuffer is not enabled unconditionally, ARM64 Simulator cannot emulate atomics
// Copyright (C) 2017 Mozilla Corporation.  All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-atomics.or
description: >
  Test Atomics.or on shared non-integer TypedArrays
includes: [testTypedArray.js]
features: [Atomics, SharedArrayBuffer, TypedArray]
---*/

var buffer = new SharedArrayBuffer(1024);

testWithTypedArrayConstructors(function(TA) {
  assert.throws(TypeError, function() {
    Atomics.or(new TA(buffer), 0, 0);
  }, '`Atomics.or(new TA(buffer), 0, 0)` throws TypeError');
}, floatArrayConstructors);



reportCompare(0, 0);
