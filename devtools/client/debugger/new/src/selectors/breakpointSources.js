"use strict";

Object.defineProperty(exports, "__esModule", {
  value: true
});
exports.getBreakpointSources = undefined;

var _lodash = require("devtools/client/shared/vendor/lodash");

var _reselect = require("devtools/client/debugger/new/dist/vendors").vendored["reselect"];

var _selectors = require("../selectors/index");

var _source = require("../utils/source");

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at <http://mozilla.org/MPL/2.0/>. */
function getBreakpointsForSource(source, breakpoints) {
  const bpList = breakpoints.valueSeq();
  return bpList.filter(bp => bp.location.sourceId == source.id && !bp.hidden && (bp.text || bp.originalText || bp.condition || bp.disabled)).sortBy(bp => bp.location.line).toJS();
}

function findBreakpointSources(sources, breakpoints) {
  const sourceIds = (0, _lodash.uniq)(breakpoints.valueSeq().map(bp => bp.location.sourceId).toJS());
  const breakpointSources = sourceIds.map(id => sources[id]).filter(source => source && !source.isBlackBoxed);
  return (0, _lodash.sortBy)(breakpointSources, source => (0, _source.getFilename)(source));
}

const getBreakpointSources = exports.getBreakpointSources = (0, _reselect.createSelector)(_selectors.getBreakpoints, _selectors.getSources, (breakpoints, sources) => findBreakpointSources(sources, breakpoints).map(source => ({
  source,
  breakpoints: getBreakpointsForSource(source, breakpoints)
})).filter(({
  breakpoints: bpSources
}) => bpSources.length > 0));