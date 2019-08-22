/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Test actor designed to check that clients are properly notified of errors when calling
 * methods on old style actors.
 */
function ErrorActor(conn, tab) {
  this.conn = conn;
  this.tab = tab;
}

ErrorActor.prototype = {
  actorPrefix: "error",

  onError: function() {
    throw new Error("error");
  },
};

ErrorActor.prototype.requestTypes = {
  error: ErrorActor.prototype.onError,
};
exports.ErrorActor = ErrorActor;
