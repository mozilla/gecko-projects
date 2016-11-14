/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {utils: Cu} = Components;

Cu.import("chrome://marionette/content/assert.js");
Cu.import("chrome://marionette/content/error.js");

add_test(function test_platforms() {
  // at least one will fail
  let raised;
  for (let fn of [assert.firefox, assert.fennec, assert.b2g, assert.mobile]) {
    try {
      fn();
    } catch (e) {
      raised = e;
    }
  }
  ok(raised instanceof UnsupportedOperationError);

  run_next_test();
});

add_test(function test_defined() {
  assert.defined({});
  Assert.throws(() => assert.defined(undefined), InvalidArgumentError);

  run_next_test();
});

add_test(function test_integer() {
  assert.integer(1);
  assert.integer(0);
  assert.integer(-1);
  Assert.throws(() => assert.integer("foo"), InvalidArgumentError);

  run_next_test();
});

add_test(function test_positiveInteger() {
  assert.positiveInteger(1);
  assert.positiveInteger(0);
  Assert.throws(() => assert.positiveInteger(-1), InvalidArgumentError);
  Assert.throws(() => assert.positiveInteger("foo"), InvalidArgumentError);

  run_next_test();
});

add_test(function test_boolean() {
  assert.boolean(true);
  assert.boolean(false);
  Assert.throws(() => assert.boolean("false"), InvalidArgumentError);
  Assert.throws(() => assert.boolean(undefined), InvalidArgumentError);

  run_next_test();
});

add_test(function test_string() {
  assert.string("foo");
  assert.string(`bar`);
  Assert.throws(() => assert.string(42), InvalidArgumentError);

  run_next_test();
});

add_test(function test_object() {
  assert.object({});
  assert.object(new Object());
  Assert.throws(() => assert.object(42), InvalidArgumentError);

  run_next_test();
});

add_test(function test_that() {
  equal(1, assert.that(n => n + 1)(1));
  Assert.throws(() => assert.that(() => false)());
  Assert.throws(() => assert.that(val => val)(false));
  Assert.throws(() => assert.that(val => val, "foo", SessionNotCreatedError)(false),
      SessionNotCreatedError);

  run_next_test();
});
