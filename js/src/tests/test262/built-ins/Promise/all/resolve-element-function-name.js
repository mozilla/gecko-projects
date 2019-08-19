// Copyright (C) 2015 André Bargull. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-promise.all-resolve-element-functions
description: The `name` property of Promise.all Resolve Element functions
info: |
  A promise resolve function is an anonymous built-in function.

  17 ECMAScript Standard Built-in Objects:
    Every built-in Function object, including constructors, that is not
    identified as an anonymous function has a name property whose value
    is a String.
---*/

var resolveElementFunction;
var thenable = {
  then: function(fulfill) {
    resolveElementFunction = fulfill;
  }
};

function NotPromise(executor) {
  executor(function() {}, function() {});
}
NotPromise.resolve = function(v) {
  return v;
};
Promise.all.call(NotPromise, [thenable]);

assert.sameValue(Object.prototype.hasOwnProperty.call(resolveElementFunction, "name"), false);
assert.sameValue(resolveElementFunction.name, "");

reportCompare(0, 0);
