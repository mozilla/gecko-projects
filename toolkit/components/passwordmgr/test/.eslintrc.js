"use strict";

module.exports = {
  "extends": [
    "plugin:mozilla/mochitest-test",
  ],
  "globals": {
    "promptDone": true,
    "startTest": true,
    // Make no-undef happy with our runInParent mixed environments since you
    // can't indicate a single function is a new env.
    "assert": true,
    "addMessageListener": true,
    "sendAsyncMessage": true,

  },
  "rules": {
    "brace-style": ["error", "1tbs", {"allowSingleLine": false}],
  },
};
