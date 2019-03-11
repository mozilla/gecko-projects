/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Tests UrlbarUtils.getTokenMatches.
 */

"use strict";

add_task(function test() {
  const tests = [
    {
      tokens: ["mozilla", "is", "i"],
      phrase: "mozilla is for the Open Web",
      expected: [[0, 7], [8, 2]],
    },
    {
      tokens: ["mo", "b"],
      phrase: "mozilla is for the Open Web",
      expected: [[0, 2], [26, 1]],
    },
    {
      tokens: ["mo", ""],
      phrase: "mozilla is for the Open Web",
      expected: [[0, 2]],
    },
    {
      tokens: ["mozilla"],
      phrase: "mozilla",
      expected: [[0, 7]],
    },
    {
      tokens: ["mo", "zilla"],
      phrase: "mozilla",
      expected: [[0, 7]],
    },
    {
      tokens: ["moz", "zilla"],
      phrase: "mozilla",
      expected: [[0, 7]],
    },
    {
      tokens: [""], // Should never happen in practice.
      phrase: "mozilla",
      expected: [],
    },
    {
      tokens: ["mo", "om"],
      phrase: "mozilla mozzarella momo",
      expected: [[0, 2], [8, 2], [19, 4]],
    },
  ];
  for (let {tokens, phrase, expected} of tests) {
    tokens = tokens.map(t => ({value: t}));
    Assert.deepEqual(UrlbarUtils.getTokenMatches(tokens, phrase), expected,
                     `Match "${tokens.map(t => t.value).join(", ")}" on "${phrase}"`);
  }
});
