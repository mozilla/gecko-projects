// Copyright (C) 2016 the V8 project authors. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.
/*---
esid: sec-additional-properties-of-the-object.prototype-object
description: Object.prototype.__lookupGetter__ `length` property
info: >
    ES6 Section 17:
    Every built-in Function object, including constructors, has a length
    property whose value is an integer. Unless otherwise specified, this value
    is equal to the largest number of named arguments shown in the subclause
    headings for the function description, including optional parameters.

    [...]

    Unless otherwise specified, the length property of a built-in Function
    object has the attributes { [[Writable]]: false, [[Enumerable]]: false,
    [[Configurable]]: true }.
includes: [propertyHelper.js]
---*/

assert.sameValue(Object.prototype.__lookupGetter__.length, 1);

verifyNotEnumerable(Object.prototype.__lookupGetter__, 'length');
verifyNotWritable(Object.prototype.__lookupGetter__, 'length');
verifyConfigurable(Object.prototype.__lookupGetter__, 'length');

reportCompare(0, 0);
