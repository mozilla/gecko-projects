"use strict";

module.exports = { // eslint-disable-line no-undef
  "extends": "../../.eslintrc.js",
  "rules": {
    // Require spacing around =>
    "arrow-spacing": 2,

    // No newline before open brace for a block
    "brace-style": [2, "1tbs", {"allowSingleLine": true}],

    // No space before always a space after a comma
    "comma-spacing": [2, {"before": false, "after": true}],

    // Commas at the end of the line not the start
    "comma-style": 2,

    // Use [] instead of Array()
    "no-array-constructor": 2,

    // Use {} instead of new Object()
    "no-new-object": 2,

    // No using undeclared variables
    "no-undef": 2,

    // Don't allow unused local variables unless they match the pattern
    "no-unused-vars": [2, {"args": "none", "vars": "local", "varsIgnorePattern": "^(ids|ignored|unused)$"}],

    // Always require semicolon at end of statement
    "semi": [2, "always"],

    // Require spaces around operators
    "space-infix-ops": 2,
  }
};
