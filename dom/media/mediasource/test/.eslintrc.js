"use strict";

module.exports = {
  // Extend mochitest rules
  "extends": "plugin:mozilla/mochitest-test",
  // Globals from mediasource.js. We use false to indicate they should not
  // be overwritten in scripts.
  "globals": {
    "addMSEPrefs": false,
    "fetchAndLoad": false,
    "fetchAndLoadAsync": false,
    "fetchWithXHR": false,
    "loadSegment": false,
    "once": false,
    "range": false,
    "runWithMSE": false,
    "waitUntilTime": false
  },
  // Use const/let instead of var for tighter scoping, avoiding redeclaration
  "rules": {
    "array-bracket-spacing": ["error", "always"],
    "no-var": "error",
    "prefer-const": "error"
  }
};
