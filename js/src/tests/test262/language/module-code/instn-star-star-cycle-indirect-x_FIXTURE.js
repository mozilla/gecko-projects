// |reftest| skip -- not a test file, jstests don't yet support module tests
// Copyright (C) 2016 the V8 project authors. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

// This module should be visited exactly one time during resolution of the "x"
// binding.
export { y as x } from './instn-star-star-cycle-2_FIXTURE.js';
export var y = 45;

reportCompare(0, 0);
