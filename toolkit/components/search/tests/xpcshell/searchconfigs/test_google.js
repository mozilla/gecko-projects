/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const test = new SearchConfigTest({
  identifier: "google",
  default: {
    // Included everywhere apart from the exclusions below. These are basically
    // just excluding what Yandex and Baidu include.
    excluded: [{
      regions: [
        "ru", "tr", "by", "kz",
      ],
      locales: {
        matches: ["ru", "tr", "be", "kk"],
        startsWith: ["en"],
      },
    }, {
      regions: ["cn"],
      locales: {
        matches: ["zh-CN"],
      },
    }],
  },
});

add_task(async function setup() {
  await test.setup();
});

add_task(async function test_searchConfig_google() {
  await test.run();
});
