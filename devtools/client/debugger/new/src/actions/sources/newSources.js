"use strict";

Object.defineProperty(exports, "__esModule", {
  value: true
});
exports.newSource = newSource;
exports.newSources = newSources;

var _devtoolsSourceMap = require("devtools/client/shared/source-map/index.js");

var _lodash = require("devtools/client/shared/vendor/lodash");

var _blackbox = require("./blackbox");

var _breakpoints = require("../breakpoints/index");

var _loadSourceText = require("./loadSourceText");

var _prettyPrint = require("./prettyPrint");

var _sources = require("../sources/index");

var _source = require("../../utils/source");

var _selectors = require("../../selectors/index");

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at <http://mozilla.org/MPL/2.0/>. */

/**
 * Redux actions for the sources state
 * @module actions/sources
 */
function createOriginalSource(originalUrl, generatedSource, sourceMaps) {
  return {
    url: originalUrl,
    relativeUrl: originalUrl,
    id: (0, _devtoolsSourceMap.generatedToOriginalId)(generatedSource.id, originalUrl),
    isPrettyPrinted: false,
    isWasm: false,
    isBlackBoxed: false,
    loadedState: "unloaded"
  };
}

function loadSourceMaps(sources) {
  return async function ({
    dispatch,
    sourceMaps
  }) {
    if (!sourceMaps) {
      return;
    }

    let originalSources = await Promise.all(sources.map(({
      id
    }) => dispatch(loadSourceMap(id))));
    originalSources = (0, _lodash.flatten)(originalSources).filter(Boolean);

    if (originalSources.length > 0) {
      await dispatch(newSources(originalSources));
    }
  };
}
/**
 * @memberof actions/sources
 * @static
 */


function loadSourceMap(sourceId) {
  return async function ({
    dispatch,
    getState,
    sourceMaps
  }) {
    const source = (0, _selectors.getSource)(getState(), sourceId);

    if (!source || !(0, _devtoolsSourceMap.isGeneratedId)(sourceId) || !source.sourceMapURL) {
      return;
    }

    let urls = null;

    try {
      urls = await sourceMaps.getOriginalURLs(source);
    } catch (e) {
      console.error(e);
    }

    if (!urls) {
      // If this source doesn't have a sourcemap, enable it for pretty printing
      dispatch({
        type: "UPDATE_SOURCE",
        // NOTE: Flow https://github.com/facebook/flow/issues/6342 issue
        source: { ...source,
          sourceMapURL: ""
        }
      });
      return;
    }

    return urls.map(url => createOriginalSource(url, source, sourceMaps));
  };
} // If a request has been made to show this source, go ahead and
// select it.


function checkSelectedSource(sourceId) {
  return async ({
    dispatch,
    getState
  }) => {
    const source = (0, _selectors.getSourceFromId)(getState(), sourceId);
    const pendingLocation = (0, _selectors.getPendingSelectedLocation)(getState());

    if (!pendingLocation || !pendingLocation.url || !source.url) {
      return;
    }

    const pendingUrl = pendingLocation.url;
    const rawPendingUrl = (0, _source.getRawSourceURL)(pendingUrl);

    if (rawPendingUrl === source.url) {
      if ((0, _source.isPrettyURL)(pendingUrl)) {
        const prettySource = await dispatch((0, _prettyPrint.togglePrettyPrint)(source.id));
        return dispatch(checkPendingBreakpoints(prettySource.id));
      }

      await dispatch((0, _sources.selectLocation)({ ...pendingLocation,
        sourceId: source.id
      }));
    }
  };
}

function checkPendingBreakpoints(sourceId) {
  return async ({
    dispatch,
    getState
  }) => {
    // source may have been modified by selectLocation
    const source = (0, _selectors.getSourceFromId)(getState(), sourceId);
    const pendingBreakpoints = (0, _selectors.getPendingBreakpointsForSource)(getState(), source);

    if (pendingBreakpoints.length === 0) {
      return;
    } // load the source text if there is a pending breakpoint for it


    await dispatch((0, _loadSourceText.loadSourceText)(source));
    await Promise.all(pendingBreakpoints.map(bp => dispatch((0, _breakpoints.syncBreakpoint)(sourceId, bp))));
  };
}

function restoreBlackBoxedSources(sources) {
  return async ({
    dispatch
  }) => {
    const tabs = (0, _selectors.getBlackBoxList)();

    if (tabs.length == 0) {
      return;
    }

    for (const source of sources) {
      if (tabs.includes(source.url) && !source.isBlackBoxed) {
        dispatch((0, _blackbox.toggleBlackBox)(source));
      }
    }
  };
}
/**
 * Handler for the debugger client's unsolicited newSource notification.
 * @memberof actions/sources
 * @static
 */


function newSource(source) {
  return async ({
    dispatch
  }) => {
    await dispatch(newSources([source]));
  };
}

function newSources(sources) {
  return async ({
    dispatch,
    getState
  }) => {
    sources = sources.filter(source => !(0, _selectors.getSource)(getState(), source.id));

    if (sources.length == 0) {
      return;
    }

    dispatch({
      type: "ADD_SOURCES",
      sources: sources
    });
    await dispatch(loadSourceMaps(sources));

    for (const source of sources) {
      dispatch(checkSelectedSource(source.id));
      dispatch(checkPendingBreakpoints(source.id));
    } // We would like to restore the blackboxed state
    // after loading all states to make sure the correctness.


    await dispatch(restoreBlackBoxedSources(sources));
  };
}