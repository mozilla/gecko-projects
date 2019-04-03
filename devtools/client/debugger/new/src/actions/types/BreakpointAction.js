/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at <http://mozilla.org/MPL/2.0/>. */

// @flow

import type {
  Breakpoint,
  SourceLocation,
  XHRBreakpoint,
  Source,
  BreakpointPositions
} from "../../types";

import type { PromiseAction } from "../utils/middleware/promise";

export type BreakpointAction =
  | PromiseAction<{|
      +type: "SET_XHR_BREAKPOINT",
      +breakpoint: XHRBreakpoint
    |}>
  | PromiseAction<{|
      +type: "ENABLE_XHR_BREAKPOINT",
      +breakpoint: XHRBreakpoint,
      +index: number
    |}>
  | PromiseAction<{|
      +type: "UPDATE_XHR_BREAKPOINT",
      +breakpoint: XHRBreakpoint,
      +index: number
    |}>
  | PromiseAction<{|
      +type: "DISABLE_XHR_BREAKPOINT",
      +breakpoint: XHRBreakpoint,
      +index: number
    |}>
  | PromiseAction<{|
      +type: "REMOVE_XHR_BREAKPOINT",
      +index: number,
      +breakpoint: XHRBreakpoint
    |}>
  | {|
      +type: "SET_BREAKPOINT",
      +breakpoint: Breakpoint
    |}
  | {|
      +type: "REMOVE_BREAKPOINT",
      +location: SourceLocation
    |}
  | {|
      type: "ADD_BREAKPOINT_POSITIONS",
      positions: BreakpointPositions,
      source: Source
    |};
