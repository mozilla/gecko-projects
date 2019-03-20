/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
"use strict";

const {Arg, RetVal, generateActorSpec, types} = require("devtools/shared/protocol");

types.addDictType("sourceposition", {
  line: "number",
  column: "number",
});
types.addDictType("nullablesourceposition", {
  line: "nullable:number",
  column: "nullable:number",
});
types.addDictType("breakpointquery", {
  start: "nullable:nullablesourceposition",
  end: "nullable:nullablesourceposition",
});

const sourceSpec = generateActorSpec({
  typeName: "source",

  methods: {
    getExecutableLines: { response: { lines: RetVal("json") } },
    getBreakpointPositions: {
      request: {
        query: Arg(0, "nullable:breakpointquery"),
      },
      response: {
        positions: RetVal("array:sourceposition"),
      },
    },
    getBreakpointPositionsCompressed: {
      request: {
        query: Arg(0, "nullable:breakpointquery"),
      },
      response: {
        positions: RetVal("json"),
      },
    },
    onSource: {
      request: { type: "source" },
      response: RetVal("json"),
    },
    setPausePoints: {
      request: {
        pausePoints: Arg(0, "json"),
      },
    },
    blackbox: {
      request: { range: Arg(0, "nullable:json") },
      response: { pausedInSource: RetVal("boolean") },
    },
    unblackbox: {
      request: { range: Arg(0, "nullable:json") },
    },
  },
});

exports.sourceSpec = sourceSpec;
