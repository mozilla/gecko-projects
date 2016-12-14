/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {classes: Cc, interfaces: Ci, utils: Cu} = Components;

Cu.import("resource://gre/modules/AppConstants.jsm");
Cu.import("resource://gre/modules/Preferences.jsm");

Cu.import("chrome://marionette/content/error.js");

this.EXPORTED_SYMBOLS = ["assert"];

const isFennec = () => AppConstants.platform == "android";
const isB2G = () => AppConstants.MOZ_B2G;
const isFirefox = () => Services.appinfo.name == "Firefox";

/** Shorthands for common assertions made in Marionette. */
this.assert = {};

/**
 * Asserts that the current browser is Firefox Desktop.
 *
 * @param {string=} msg
 *     Custom error message.
 *
 * @throws {UnsupportedOperationError}
 *     If current browser is not Firefox.
 */
assert.firefox = function (msg = "") {
  msg = msg || "Expected Firefox";
  assert.that(isFirefox, msg, UnsupportedOperationError)();
};

/**
 * Asserts that the current browser is Fennec, or Firefox for Android.
 *
 * @param {string=} msg
 *     Custom error message.
 *
 * @throws {UnsupportedOperationError}
 *     If current browser is not Fennec.
 */
assert.fennec = function (msg = "") {
  msg = msg || "Expected Fennec";
  assert.that(isFennec, msg, UnsupportedOperationError)();
};

/**
 * Asserts that the current browser is B2G.
 *
 * @param {string=} msg
 *     Custom error message.
 *
 * @throws {UnsupportedOperationError}
 *     If the current browser is not B2G.
 */
assert.b2g = function (msg = "") {
  msg = msg || "Expected B2G"
  assert.that(isB2G, msg, UnsupportedOperationError)();
};

/**
 * Asserts that the current browser is a mobile browser, that is either
 * B2G or Fennec.
 *
 * @param {string=} msg
 *     Custom error message.
 *
 * @throws {UnsupportedOperationError}
 *     If the current browser is not B2G or Fennec.
 */
assert.mobile = function (msg = "") {
  msg = msg || "Expected Fennec or B2G";
  assert.that(() => isFennec() || isB2G(), msg, UnsupportedOperationError)();
};

/**
 * Asserts that |obj| is defined.
 *
 * @param {?} obj
 *     Value to test.
 * @param {string=} msg
 *     Custom error message.
 *
 * @return {?}
 *     |obj| is returned unaltered.
 *
 * @throws {InvalidArgumentError}
 *     If |obj| is not defined.
 */
assert.defined = function (obj, msg = "") {
  msg = msg || error.pprint`Expected ${obj} to be defined`;
  return assert.that(o => typeof o != "undefined", msg)(obj);
};

/**
 * Asserts that |obj| is an integer.
 *
 * @param {?} obj
 *     Value to test.
 * @param {string=} msg
 *     Custom error message.
 *
 * @return {number}
 *     |obj| is returned unaltered.
 *
 * @throws {InvalidArgumentError}
 *     If |obj| is not an integer.
 */
assert.integer = function (obj, msg = "") {
  msg = msg || error.pprint`Expected ${obj} to be an integer`;
  return assert.that(Number.isInteger, msg)(obj);
};

/**
 * Asserts that |obj| is a positive integer.
 *
 * @param {?} obj
 *     Value to test.
 * @param {string=} msg
 *     Custom error message.
 *
 * @return {number}
 *     |obj| is returned unaltered.
 *
 * @throws {InvalidArgumentError}
 *     If |obj| is not a positive integer.
 */
assert.positiveInteger = function (obj, msg = "") {
  assert.integer(obj, msg);
  msg = msg || error.pprint`Expected ${obj} to be >= 0`;
  return assert.that(n => n >= 0, msg)(obj);
};

/**
 * Asserts that |obj| is a boolean.
 *
 * @param {?} obj
 *     Value to test.
 * @param {string=} msg
 *     Custom error message.
 *
 * @return {boolean}
 *     |obj| is returned unaltered.
 *
 * @throws {InvalidArgumentError}
 *     If |obj| is not a boolean.
 */
assert.boolean = function (obj, msg = "") {
  msg = msg || error.pprint`Expected ${obj} to be boolean`;
  return assert.that(b => typeof b == "boolean", msg)(obj);
};

/**
 * Asserts that |obj| is a string.
 *
 * @param {?} obj
 *     Value to test.
 * @param {string=} msg
 *     Custom error message.
 *
 * @return {string}
 *     |obj| is returned unaltered.
 *
 * @throws {InvalidArgumentError}
 *     If |obj| is not a string.
 */
assert.string = function (obj, msg = "") {
  msg = msg || error.pprint`Expected ${obj} to be a string`;
  return assert.that(s => typeof s == "string", msg)(obj);
};

/**
 * Asserts that |obj| is an object.
 *
 * @param {?} obj
 *     Value to test.
 * @param {string=} msg
 *     Custom error message.
 *
 * @return {Object}
 *     |obj| is returned unaltered.
 *
 * @throws {InvalidArgumentError}
 *     If |obj| is not an object.
 */
assert.object = function (obj, msg = "") {
  msg = msg || error.pprint`Expected ${obj} to be an object`;
  return assert.that(o => typeof o == "object", msg)(obj);
};

/**
 * Returns a function that is used to assert the |predicate|.
 *
 * @param {function(?): boolean} predicate
 *     Evaluated on calling the return value of this function.  If its
 *     return value of the inner function is false, |error| is thrown
 *     with |message|.
 * @param {string=} message
 *     Custom error message.
 * @param {Error=} error
 *     Custom error type by its class.
 *
 * @return {function(?): ?}
 *     Function that takes and returns the passed in value unaltered, and
 *     which may throw |error| with |message| if |predicate| evaluates
 *     to false.
 */
assert.that = function (
    predicate, message = "", error = InvalidArgumentError) {
  return obj => {
    if (!predicate(obj)) {
      throw new error(message);
    }
    return obj;
  };
};
