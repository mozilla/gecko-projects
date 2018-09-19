"use strict";

Object.defineProperty(exports, "__esModule", {
  value: true
});
exports.createFrame = createFrame;
exports.createSource = createSource;
exports.createPause = createPause;
exports.createBreakpointLocation = createBreakpointLocation;

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at <http://mozilla.org/MPL/2.0/>. */
// This module converts Firefox specific types to the generic types
function createFrame(frame) {
  if (!frame) {
    return null;
  }

  let title;

  if (frame.type == "call") {
    const c = frame.callee;
    title = c.name || c.userDisplayName || c.displayName || L10N.getStr("anonymous");
  } else {
    title = `(${frame.type})`;
  }

  const location = {
    sourceId: frame.where.source.actor,
    line: frame.where.line,
    column: frame.where.column
  };
  return {
    id: frame.actor,
    displayName: title,
    location,
    generatedLocation: location,
    this: frame.this,
    scope: frame.environment
  };
}

function createSource(source, {
  supportsWasm
}) {
  const createdSource = {
    id: source.actor,
    url: source.url,
    relativeUrl: source.url,
    isPrettyPrinted: false,
    isWasm: false,
    sourceMapURL: source.sourceMapURL,
    isBlackBoxed: false,
    loadedState: "unloaded"
  };
  return Object.assign(createdSource, {
    isWasm: supportsWasm && source.introductionType === "wasm"
  });
}

function createPause(packet, response) {
  // NOTE: useful when the debugger is already paused
  const frame = packet.frame || response.frames[0];
  return { ...packet,
    frame: createFrame(frame),
    frames: response.frames.map(createFrame)
  };
} // Firefox only returns `actualLocation` if it actually changed,
// but we want it always to exist. Format `actualLocation` if it
// exists, otherwise use `location`.


function createBreakpointLocation(location, actualLocation) {
  if (!actualLocation) {
    return location;
  }

  return {
    sourceId: actualLocation.source.actor,
    sourceUrl: actualLocation.source.url,
    line: actualLocation.line,
    column: actualLocation.column
  };
}