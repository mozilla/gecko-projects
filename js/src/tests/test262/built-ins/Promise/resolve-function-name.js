// Copyright (C) 2015 André Bargull. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-promise-resolve-functions
description: The `name` property of Promise Resolve functions
info: |
  A promise resolve function is an anonymous built-in function.

  17 ECMAScript Standard Built-in Objects:
    Every built-in Function object, including constructors, that is not
    identified as an anonymous function has a name property whose value
    is a String.
---*/

var resolveFunction;
new Promise(function(resolve, reject) {
  resolveFunction = resolve;
});

assert.sameValue(Object.prototype.hasOwnProperty.call(resolveFunction, "name"), false);
assert.sameValue(resolveFunction.name, "");

reportCompare(0, 0);
