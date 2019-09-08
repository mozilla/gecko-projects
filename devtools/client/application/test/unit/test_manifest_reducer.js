/* Any copyright is dedicated to the Public Domain.
http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const {
  FETCH_MANIFEST_FAILURE,
  FETCH_MANIFEST_START,
  FETCH_MANIFEST_SUCCESS,
} = require("devtools/client/application/src/constants.js");

const {
  manifestReducer,
  ManifestState,
} = require("devtools/client/application/src/reducers/manifest-state.js");

const MANIFEST_PROCESSING = [
  // empty manifest
  {
    source: {},
    processed: {},
  },
  // manifest with just one member
  {
    source: { name: "Foo" },
    processed: {
      identity: [{ key: "name", value: "Foo" }],
    },
  },
  // manifest with two members from the same category
  {
    source: {
      short_name: "Short Foo",
      name: "Long Foo",
    },
    processed: {
      identity: [
        { key: "short_name", value: "Short Foo" },
        { key: "name", value: "Long Foo" },
      ],
    },
  },
  // manifest with members from two different categories
  {
    source: {
      name: "Foo",
      background_color: "#FF0000",
    },
    processed: {
      identity: [{ key: "name", value: "Foo" }],
      presentation: [{ key: "background_color", value: "#FF0000" }],
    },
  },
  // manifest with icons
  {
    source: {
      icons: [
        {
          src: "something.png",
          type: "image/png",
          sizes: ["16x16", "32x32"],
          purpose: ["any"],
        },
        {
          src: "another.svg",
          type: "image/svg",
          sizes: ["any"],
          purpose: ["any"],
        },
      ],
    },
    processed: {
      icons: [
        { key: "16x16 32x32", value: "something.png" },
        { key: "any", value: "another.svg" },
      ],
    },
  },
  // manifest with issues
  {
    source: {
      moz_validation: [
        { warn: "A warning" },
        { error: "An error", type: "json" },
      ],
    },
    processed: {
      validation: [
        { level: "warning", message: "A warning", type: null },
        { level: "error", message: "An error", type: "json" },
      ],
    },
  },
  // manifest with URL
  {
    source: {
      moz_manifest_url: "https://example.com/manifest.json",
    },
    processed: {
      url: "https://example.com/manifest.json",
    },
  },
];

add_task(async function() {
  info("Test manifest reducer: FETCH_MANIFEST_START action");

  const state = ManifestState();
  const action = { type: FETCH_MANIFEST_START };
  const newState = manifestReducer(state, action);

  equal(newState.isLoading, true, "Loading flag is true");
});

add_task(async function() {
  info("Test manifest reducer: FETCH_MANIFEST_FAILURE action");

  const state = Object.assign(ManifestState(), { isLoading: true });
  const action = { type: FETCH_MANIFEST_FAILURE, error: "some error" };
  const newState = manifestReducer(state, action);

  equal(newState.errorMessage, "some error", "Error message is as expected");
  equal(newState.isLoading, false, "Loading flag is false");
  equal(newState.manifest, null, "Manifest is null");
});

add_task(async function() {
  info("Test manifest reducer: FETCH_MANIFEST_SUCCESS action");

  // test manifest processing
  MANIFEST_PROCESSING.forEach(({ source, processed }) => {
    test_manifest_processing(source, processed);
  });
});

function test_manifest_processing(source, processed) {
  const state = ManifestState();
  state.isLoading = true;

  const action = { type: FETCH_MANIFEST_SUCCESS, manifest: source };
  const newState = manifestReducer(state, action);

  // merge the expected processed manifst with some default values
  const expected = Object.assign(
    {
      icons: [],
      identity: [],
      presentation: [],
      url: undefined,
      validation: [],
    },
    processed
  );

  deepEqual(newState.manifest, expected, "Processed manifest as expected");
  equal(newState.errorMessage, "", "Error message is empty");
  equal(newState.isLoading, false, "Loading flag is false");
}
