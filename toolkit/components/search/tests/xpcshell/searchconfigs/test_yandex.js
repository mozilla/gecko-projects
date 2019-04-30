/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const test = new SearchConfigTest({
  identifier: "yandex",
  default: {
    included: [{
      regions: [
        "ru", "tr", "by", "kz",
      ],
      locales: {
        matches: ["ru", "tr", "be", "kk"],
        startsWith: ["en"],
      },
    }],
  },
});

add_task(async function setup() {
  await test.setup();
});

add_task(async function test_searchConfig_yandex() {
  await test.run();
});
