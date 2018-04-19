/******/ (function(modules) { // webpackBootstrap
/******/ 	// The module cache
/******/ 	var installedModules = {};
/******/
/******/ 	// The require function
/******/ 	function __webpack_require__(moduleId) {
/******/
/******/ 		// Check if module is in cache
/******/ 		if(installedModules[moduleId]) {
/******/ 			return installedModules[moduleId].exports;
/******/ 		}
/******/ 		// Create a new module (and put it into the cache)
/******/ 		var module = installedModules[moduleId] = {
/******/ 			i: moduleId,
/******/ 			l: false,
/******/ 			exports: {}
/******/ 		};
/******/
/******/ 		// Execute the module function
/******/ 		modules[moduleId].call(module.exports, module, module.exports, __webpack_require__);
/******/
/******/ 		// Flag the module as loaded
/******/ 		module.l = true;
/******/
/******/ 		// Return the exports of the module
/******/ 		return module.exports;
/******/ 	}
/******/
/******/
/******/ 	// expose the modules object (__webpack_modules__)
/******/ 	__webpack_require__.m = modules;
/******/
/******/ 	// expose the module cache
/******/ 	__webpack_require__.c = installedModules;
/******/
/******/ 	// define getter function for harmony exports
/******/ 	__webpack_require__.d = function(exports, name, getter) {
/******/ 		if(!__webpack_require__.o(exports, name)) {
/******/ 			Object.defineProperty(exports, name, {
/******/ 				configurable: false,
/******/ 				enumerable: true,
/******/ 				get: getter
/******/ 			});
/******/ 		}
/******/ 	};
/******/
/******/ 	// getDefaultExport function for compatibility with non-harmony modules
/******/ 	__webpack_require__.n = function(module) {
/******/ 		var getter = module && module.__esModule ?
/******/ 			function getDefault() { return module['default']; } :
/******/ 			function getModuleExports() { return module; };
/******/ 		__webpack_require__.d(getter, 'a', getter);
/******/ 		return getter;
/******/ 	};
/******/
/******/ 	// Object.prototype.hasOwnProperty.call
/******/ 	__webpack_require__.o = function(object, property) { return Object.prototype.hasOwnProperty.call(object, property); };
/******/
/******/ 	// __webpack_public_path__
/******/ 	__webpack_require__.p = "";
/******/
/******/ 	// Load entry module and return exports
/******/ 	return __webpack_require__(__webpack_require__.s = 14);
/******/ })
/************************************************************************/
/******/ ([
/* 0 */
/***/ (function(module, __webpack_exports__, __webpack_require__) {

"use strict";
/* unused harmony export MAIN_MESSAGE_TYPE */
/* unused harmony export CONTENT_MESSAGE_TYPE */
/* unused harmony export PRELOAD_MESSAGE_TYPE */
/* unused harmony export UI_CODE */
/* unused harmony export BACKGROUND_PROCESS */
/* harmony export (binding) */ __webpack_require__.d(__webpack_exports__, "a", function() { return actionCreators; });
/* harmony export (binding) */ __webpack_require__.d(__webpack_exports__, "c", function() { return actionUtils; });
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */


var MAIN_MESSAGE_TYPE = "ActivityStream:Main";
var CONTENT_MESSAGE_TYPE = "ActivityStream:Content";
var PRELOAD_MESSAGE_TYPE = "ActivityStream:PreloadedBrowser";
var UI_CODE = 1;
var BACKGROUND_PROCESS = 2;

/**
 * globalImportContext - Are we in UI code (i.e. react, a dom) or some kind of background process?
 *                       Use this in action creators if you need different logic
 *                       for ui/background processes.
 */

const globalImportContext = typeof Window === "undefined" ? BACKGROUND_PROCESS : UI_CODE;
/* unused harmony export globalImportContext */

// Export for tests

// Create an object that avoids accidental differing key/value pairs:
// {
//   INIT: "INIT",
//   UNINIT: "UNINIT"
// }
const actionTypes = {};
/* harmony export (immutable) */ __webpack_exports__["b"] = actionTypes;


for (const type of ["ARCHIVE_FROM_POCKET", "BLOCK_URL", "BOOKMARK_URL", "DELETE_BOOKMARK_BY_ID", "DELETE_FROM_POCKET", "DELETE_HISTORY_URL", "DELETE_HISTORY_URL_CONFIRM", "DIALOG_CANCEL", "DIALOG_OPEN", "DISABLE_ONBOARDING", "INIT", "MIGRATION_CANCEL", "MIGRATION_COMPLETED", "MIGRATION_START", "NEW_TAB_INIT", "NEW_TAB_INITIAL_STATE", "NEW_TAB_LOAD", "NEW_TAB_REHYDRATED", "NEW_TAB_STATE_REQUEST", "NEW_TAB_UNLOAD", "OPEN_LINK", "OPEN_NEW_WINDOW", "OPEN_PRIVATE_WINDOW", "PAGE_PRERENDERED", "PLACES_BOOKMARK_ADDED", "PLACES_BOOKMARK_CHANGED", "PLACES_BOOKMARK_REMOVED", "PLACES_HISTORY_CLEARED", "PLACES_LINKS_DELETED", "PLACES_LINK_BLOCKED", "PLACES_SAVED_TO_POCKET", "PREFS_INITIAL_VALUES", "PREF_CHANGED", "RICH_ICON_MISSING", "SAVE_SESSION_PERF_DATA", "SAVE_TO_POCKET", "SCREENSHOT_UPDATED", "SECTION_DEREGISTER", "SECTION_DISABLE", "SECTION_ENABLE", "SECTION_MOVE", "SECTION_OPTIONS_CHANGED", "SECTION_REGISTER", "SECTION_UPDATE", "SECTION_UPDATE_CARD", "SETTINGS_CLOSE", "SETTINGS_OPEN", "SET_PREF", "SHOW_FIREFOX_ACCOUNTS", "SNIPPETS_BLOCKLIST_CLEARED", "SNIPPETS_BLOCKLIST_UPDATED", "SNIPPETS_DATA", "SNIPPETS_RESET", "SNIPPET_BLOCKED", "SYSTEM_TICK", "TELEMETRY_IMPRESSION_STATS", "TELEMETRY_PERFORMANCE_EVENT", "TELEMETRY_UNDESIRED_EVENT", "TELEMETRY_USER_EVENT", "TOP_SITES_CANCEL_EDIT", "TOP_SITES_EDIT", "TOP_SITES_INSERT", "TOP_SITES_PIN", "TOP_SITES_UNPIN", "TOP_SITES_UPDATED", "TOTAL_BOOKMARKS_REQUEST", "TOTAL_BOOKMARKS_RESPONSE", "UNINIT", "WEBEXT_CLICK", "WEBEXT_DISMISS"]) {
  actionTypes[type] = type;
}

// Helper function for creating routed actions between content and main
// Not intended to be used by consumers
function _RouteMessage(action, options) {
  const meta = action.meta ? Object.assign({}, action.meta) : {};
  if (!options || !options.from || !options.to) {
    throw new Error("Routed Messages must have options as the second parameter, and must at least include a .from and .to property.");
  }
  // For each of these fields, if they are passed as an option,
  // add them to the action. If they are not defined, remove them.
  ["from", "to", "toTarget", "fromTarget", "skipMain", "skipLocal"].forEach(o => {
    if (typeof options[o] !== "undefined") {
      meta[o] = options[o];
    } else if (meta[o]) {
      delete meta[o];
    }
  });
  return Object.assign({}, action, { meta });
}

/**
 * AlsoToMain - Creates a message that will be dispatched locally and also sent to the Main process.
 *
 * @param  {object} action Any redux action (required)
 * @param  {object} options
 * @param  {bool}   skipLocal Used by OnlyToMain to skip the main reducer
 * @param  {string} fromTarget The id of the content port from which the action originated. (optional)
 * @return {object} An action with added .meta properties
 */
function AlsoToMain(action, fromTarget, skipLocal) {
  return _RouteMessage(action, {
    from: CONTENT_MESSAGE_TYPE,
    to: MAIN_MESSAGE_TYPE,
    fromTarget,
    skipLocal
  });
}

/**
 * OnlyToMain - Creates a message that will be sent to the Main process and skip the local reducer.
 *
 * @param  {object} action Any redux action (required)
 * @param  {object} options
 * @param  {string} fromTarget The id of the content port from which the action originated. (optional)
 * @return {object} An action with added .meta properties
 */
function OnlyToMain(action, fromTarget) {
  return AlsoToMain(action, fromTarget, true);
}

/**
 * BroadcastToContent - Creates a message that will be dispatched to main and sent to ALL content processes.
 *
 * @param  {object} action Any redux action (required)
 * @return {object} An action with added .meta properties
 */
function BroadcastToContent(action) {
  return _RouteMessage(action, {
    from: MAIN_MESSAGE_TYPE,
    to: CONTENT_MESSAGE_TYPE
  });
}

/**
 * AlsoToOneContent - Creates a message that will be will be dispatched to the main store
 *                    and also sent to a particular Content process.
 *
 * @param  {object} action Any redux action (required)
 * @param  {string} target The id of a content port
 * @param  {bool} skipMain Used by OnlyToOneContent to skip the main process
 * @return {object} An action with added .meta properties
 */
function AlsoToOneContent(action, target, skipMain) {
  if (!target) {
    throw new Error("You must provide a target ID as the second parameter of AlsoToOneContent. If you want to send to all content processes, use BroadcastToContent");
  }
  return _RouteMessage(action, {
    from: MAIN_MESSAGE_TYPE,
    to: CONTENT_MESSAGE_TYPE,
    toTarget: target,
    skipMain
  });
}

/**
 * OnlyToOneContent - Creates a message that will be sent to a particular Content process
 *                    and skip the main reducer.
 *
 * @param  {object} action Any redux action (required)
 * @param  {string} target The id of a content port
 * @return {object} An action with added .meta properties
 */
function OnlyToOneContent(action, target) {
  return AlsoToOneContent(action, target, true);
}

/**
 * AlsoToPreloaded - Creates a message that dispatched to the main reducer and also sent to the preloaded tab.
 *
 * @param  {object} action Any redux action (required)
 * @return {object} An action with added .meta properties
 */
function AlsoToPreloaded(action) {
  return _RouteMessage(action, {
    from: MAIN_MESSAGE_TYPE,
    to: PRELOAD_MESSAGE_TYPE
  });
}

/**
 * UserEvent - A telemetry ping indicating a user action. This should only
 *                   be sent from the UI during a user session.
 *
 * @param  {object} data Fields to include in the ping (source, etc.)
 * @return {object} An AlsoToMain action
 */
function UserEvent(data) {
  return AlsoToMain({
    type: actionTypes.TELEMETRY_USER_EVENT,
    data
  });
}

/**
 * UndesiredEvent - A telemetry ping indicating an undesired state.
 *
 * @param  {object} data Fields to include in the ping (value, etc.)
 * @param  {int} importContext (For testing) Override the import context for testing.
 * @return {object} An action. For UI code, a AlsoToMain action.
 */
function UndesiredEvent(data, importContext = globalImportContext) {
  const action = {
    type: actionTypes.TELEMETRY_UNDESIRED_EVENT,
    data
  };
  return importContext === UI_CODE ? AlsoToMain(action) : action;
}

/**
 * PerfEvent - A telemetry ping indicating a performance-related event.
 *
 * @param  {object} data Fields to include in the ping (value, etc.)
 * @param  {int} importContext (For testing) Override the import context for testing.
 * @return {object} An action. For UI code, a AlsoToMain action.
 */
function PerfEvent(data, importContext = globalImportContext) {
  const action = {
    type: actionTypes.TELEMETRY_PERFORMANCE_EVENT,
    data
  };
  return importContext === UI_CODE ? AlsoToMain(action) : action;
}

/**
 * ImpressionStats - A telemetry ping indicating an impression stats.
 *
 * @param  {object} data Fields to include in the ping
 * @param  {int} importContext (For testing) Override the import context for testing.
 * #return {object} An action. For UI code, a AlsoToMain action.
 */
function ImpressionStats(data, importContext = globalImportContext) {
  const action = {
    type: actionTypes.TELEMETRY_IMPRESSION_STATS,
    data
  };
  return importContext === UI_CODE ? AlsoToMain(action) : action;
}

function SetPref(name, value, importContext = globalImportContext) {
  const action = { type: actionTypes.SET_PREF, data: { name, value } };
  return importContext === UI_CODE ? AlsoToMain(action) : action;
}

function WebExtEvent(type, data, importContext = globalImportContext) {
  if (!data || !data.source) {
    throw new Error("WebExtEvent actions should include a property \"source\", the id of the webextension that should receive the event.");
  }
  const action = { type, data };
  return importContext === UI_CODE ? AlsoToMain(action) : action;
}

var actionCreators = {
  BroadcastToContent,
  UserEvent,
  UndesiredEvent,
  PerfEvent,
  ImpressionStats,
  AlsoToOneContent,
  OnlyToOneContent,
  AlsoToMain,
  OnlyToMain,
  AlsoToPreloaded,
  SetPref,
  WebExtEvent
};

// These are helpers to test for certain kinds of actions

var actionUtils = {
  isSendToMain(action) {
    if (!action.meta) {
      return false;
    }
    return action.meta.to === MAIN_MESSAGE_TYPE && action.meta.from === CONTENT_MESSAGE_TYPE;
  },
  isBroadcastToContent(action) {
    if (!action.meta) {
      return false;
    }
    if (action.meta.to === CONTENT_MESSAGE_TYPE && !action.meta.toTarget) {
      return true;
    }
    return false;
  },
  isSendToOneContent(action) {
    if (!action.meta) {
      return false;
    }
    if (action.meta.to === CONTENT_MESSAGE_TYPE && action.meta.toTarget) {
      return true;
    }
    return false;
  },
  isSendToPreloaded(action) {
    if (!action.meta) {
      return false;
    }
    return action.meta.to === PRELOAD_MESSAGE_TYPE && action.meta.from === MAIN_MESSAGE_TYPE;
  },
  isFromMain(action) {
    if (!action.meta) {
      return false;
    }
    return action.meta.from === MAIN_MESSAGE_TYPE && action.meta.to === CONTENT_MESSAGE_TYPE;
  },
  getPortIdOfSender(action) {
    return action.meta && action.meta.fromTarget || null;
  },
  _RouteMessage
};

/***/ }),
/* 1 */
/***/ (function(module, exports) {

module.exports = React;

/***/ }),
/* 2 */
/***/ (function(module, exports) {

module.exports = ReactIntl;

/***/ }),
/* 3 */
/***/ (function(module, exports) {

var g;

// This works in non-strict mode
g = (function() {
	return this;
})();

try {
	// This works if eval is allowed (see CSP)
	g = g || Function("return this")() || (1,eval)("this");
} catch(e) {
	// This works if the window reference is available
	if(typeof window === "object")
		g = window;
}

// g can still be undefined, but nothing to do about it...
// We return undefined, instead of nothing here, so it's
// easier to handle this case. if(!global) { ...}

module.exports = g;


/***/ }),
/* 4 */
/***/ (function(module, exports) {

module.exports = ReactRedux;

/***/ }),
/* 5 */
/***/ (function(module, __webpack_exports__, __webpack_require__) {

"use strict";
const TOP_SITES_SOURCE = "TOP_SITES";
/* harmony export (immutable) */ __webpack_exports__["d"] = TOP_SITES_SOURCE;

const TOP_SITES_CONTEXT_MENU_OPTIONS = ["CheckPinTopSite", "EditTopSite", "Separator", "OpenInNewWindow", "OpenInPrivateWindow", "Separator", "BlockUrl", "DeleteUrl"];
/* harmony export (immutable) */ __webpack_exports__["c"] = TOP_SITES_CONTEXT_MENU_OPTIONS;

// minimum size necessary to show a rich icon instead of a screenshot
const MIN_RICH_FAVICON_SIZE = 96;
/* harmony export (immutable) */ __webpack_exports__["b"] = MIN_RICH_FAVICON_SIZE;

// minimum size necessary to show any icon in the top left corner with a screenshot
const MIN_CORNER_FAVICON_SIZE = 16;
/* harmony export (immutable) */ __webpack_exports__["a"] = MIN_CORNER_FAVICON_SIZE;


/***/ }),
/* 6 */
/***/ (function(module, __webpack_exports__, __webpack_require__) {

"use strict";

// EXTERNAL MODULE: ./system-addon/common/Actions.jsm
var Actions = __webpack_require__(0);

// CONCATENATED MODULE: ./system-addon/common/Dedupe.jsm
class Dedupe {
  constructor(createKey) {
    this.createKey = createKey || this.defaultCreateKey;
  }

  defaultCreateKey(item) {
    return item;
  }

  /**
   * Dedupe any number of grouped elements favoring those from earlier groups.
   *
   * @param {Array} groups Contains an arbitrary number of arrays of elements.
   * @returns {Array} A matching array of each provided group deduped.
   */
  group(...groups) {
    const globalKeys = new Set();
    const result = [];
    for (const values of groups) {
      const valueMap = new Map();
      for (const value of values) {
        const key = this.createKey(value);
        if (!globalKeys.has(key) && !valueMap.has(key)) {
          valueMap.set(key, value);
        }
      }
      result.push(valueMap);
      valueMap.forEach((value, key) => globalKeys.add(key));
    }
    return result.map(m => Array.from(m.values()));
  }
}
// CONCATENATED MODULE: ./system-addon/common/Reducers.jsm
/* unused harmony export insertPinned */
/* harmony export (binding) */ __webpack_require__.d(__webpack_exports__, "b", function() { return reducers; });
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */




const TOP_SITES_DEFAULT_ROWS = 1;
/* unused harmony export TOP_SITES_DEFAULT_ROWS */

const TOP_SITES_MAX_SITES_PER_ROW = 8;
/* harmony export (immutable) */ __webpack_exports__["a"] = TOP_SITES_MAX_SITES_PER_ROW;



const dedupe = new Dedupe(site => site && site.url);

const INITIAL_STATE = {
  App: {
    // Have we received real data from the app yet?
    initialized: false,
    // The version of the system-addon
    version: null
  },
  Snippets: { initialized: false },
  TopSites: {
    // Have we received real data from history yet?
    initialized: false,
    // The history (and possibly default) links
    rows: [],
    // Used in content only to dispatch action to TopSiteForm.
    editForm: null
  },
  Prefs: {
    initialized: false,
    values: {}
  },
  Dialog: {
    visible: false,
    data: {}
  },
  Sections: [],
  PreferencesPane: { visible: false }
};
/* unused harmony export INITIAL_STATE */



function App(prevState = INITIAL_STATE.App, action) {
  switch (action.type) {
    case Actions["b" /* actionTypes */].INIT:
      return Object.assign({}, prevState, action.data || {}, { initialized: true });
    default:
      return prevState;
  }
}

/**
 * insertPinned - Inserts pinned links in their specified slots
 *
 * @param {array} a list of links
 * @param {array} a list of pinned links
 * @return {array} resulting list of links with pinned links inserted
 */
function insertPinned(links, pinned) {
  // Remove any pinned links
  const pinnedUrls = pinned.map(link => link && link.url);
  let newLinks = links.filter(link => link ? !pinnedUrls.includes(link.url) : false);
  newLinks = newLinks.map(link => {
    if (link && link.isPinned) {
      delete link.isPinned;
      delete link.pinIndex;
    }
    return link;
  });

  // Then insert them in their specified location
  pinned.forEach((val, index) => {
    if (!val) {
      return;
    }
    let link = Object.assign({}, val, { isPinned: true, pinIndex: index });
    if (index > newLinks.length) {
      newLinks[index] = link;
    } else {
      newLinks.splice(index, 0, link);
    }
  });

  return newLinks;
}


function TopSites(prevState = INITIAL_STATE.TopSites, action) {
  let hasMatch;
  let newRows;
  switch (action.type) {
    case Actions["b" /* actionTypes */].TOP_SITES_UPDATED:
      if (!action.data) {
        return prevState;
      }
      return Object.assign({}, prevState, { initialized: true, rows: action.data });
    case Actions["b" /* actionTypes */].TOP_SITES_EDIT:
      return Object.assign({}, prevState, { editForm: { index: action.data.index } });
    case Actions["b" /* actionTypes */].TOP_SITES_CANCEL_EDIT:
      return Object.assign({}, prevState, { editForm: null });
    case Actions["b" /* actionTypes */].SCREENSHOT_UPDATED:
      newRows = prevState.rows.map(row => {
        if (row && row.url === action.data.url) {
          hasMatch = true;
          return Object.assign({}, row, { screenshot: action.data.screenshot });
        }
        return row;
      });
      return hasMatch ? Object.assign({}, prevState, { rows: newRows }) : prevState;
    case Actions["b" /* actionTypes */].PLACES_BOOKMARK_ADDED:
      if (!action.data) {
        return prevState;
      }
      newRows = prevState.rows.map(site => {
        if (site && site.url === action.data.url) {
          const { bookmarkGuid, bookmarkTitle, dateAdded } = action.data;
          return Object.assign({}, site, { bookmarkGuid, bookmarkTitle, bookmarkDateCreated: dateAdded });
        }
        return site;
      });
      return Object.assign({}, prevState, { rows: newRows });
    case Actions["b" /* actionTypes */].PLACES_BOOKMARK_REMOVED:
      if (!action.data) {
        return prevState;
      }
      newRows = prevState.rows.map(site => {
        if (site && site.url === action.data.url) {
          const newSite = Object.assign({}, site);
          delete newSite.bookmarkGuid;
          delete newSite.bookmarkTitle;
          delete newSite.bookmarkDateCreated;
          return newSite;
        }
        return site;
      });
      return Object.assign({}, prevState, { rows: newRows });
    default:
      return prevState;
  }
}

function Dialog(prevState = INITIAL_STATE.Dialog, action) {
  switch (action.type) {
    case Actions["b" /* actionTypes */].DIALOG_OPEN:
      return Object.assign({}, prevState, { visible: true, data: action.data });
    case Actions["b" /* actionTypes */].DIALOG_CANCEL:
      return Object.assign({}, prevState, { visible: false });
    case Actions["b" /* actionTypes */].DELETE_HISTORY_URL:
      return Object.assign({}, INITIAL_STATE.Dialog);
    default:
      return prevState;
  }
}

function Prefs(prevState = INITIAL_STATE.Prefs, action) {
  let newValues;
  switch (action.type) {
    case Actions["b" /* actionTypes */].PREFS_INITIAL_VALUES:
      return Object.assign({}, prevState, { initialized: true, values: action.data });
    case Actions["b" /* actionTypes */].PREF_CHANGED:
      newValues = Object.assign({}, prevState.values);
      newValues[action.data.name] = action.data.value;
      return Object.assign({}, prevState, { values: newValues });
    default:
      return prevState;
  }
}

function Sections(prevState = INITIAL_STATE.Sections, action) {
  let hasMatch;
  let newState;
  switch (action.type) {
    case Actions["b" /* actionTypes */].SECTION_DEREGISTER:
      return prevState.filter(section => section.id !== action.data);
    case Actions["b" /* actionTypes */].SECTION_REGISTER:
      // If section exists in prevState, update it
      newState = prevState.map(section => {
        if (section && section.id === action.data.id) {
          hasMatch = true;
          return Object.assign({}, section, action.data);
        }
        return section;
      });
      // Otherwise, append it
      if (!hasMatch) {
        const initialized = !!(action.data.rows && action.data.rows.length > 0);
        const section = Object.assign({ title: "", rows: [], enabled: false }, action.data, { initialized });
        newState.push(section);
      }
      return newState;
    case Actions["b" /* actionTypes */].SECTION_UPDATE:
      newState = prevState.map(section => {
        if (section && section.id === action.data.id) {
          // If the action is updating rows, we should consider initialized to be true.
          // This can be overridden if initialized is defined in the action.data
          const initialized = action.data.rows ? { initialized: true } : {};

          // Make sure pinned cards stay at their current position when rows are updated.
          // Disabling a section (SECTION_UPDATE with empty rows) does not retain pinned cards.
          if (action.data.rows && action.data.rows.length > 0 && section.rows.find(card => card.pinned)) {
            const rows = Array.from(action.data.rows);
            section.rows.forEach((card, index) => {
              if (card.pinned) {
                rows.splice(index, 0, card);
              }
            });
            return Object.assign({}, section, initialized, Object.assign({}, action.data, { rows }));
          }

          return Object.assign({}, section, initialized, action.data);
        }
        return section;
      });

      if (!action.data.dedupeConfigurations) {
        return newState;
      }

      action.data.dedupeConfigurations.forEach(dedupeConf => {
        newState = newState.map(section => {
          if (section.id === dedupeConf.id) {
            const dedupedRows = dedupeConf.dedupeFrom.reduce((rows, dedupeSectionId) => {
              const dedupeSection = newState.find(s => s.id === dedupeSectionId);
              const [, newRows] = dedupe.group(dedupeSection.rows, rows);
              return newRows;
            }, section.rows);

            return Object.assign({}, section, { rows: dedupedRows });
          }

          return section;
        });
      });

      return newState;
    case Actions["b" /* actionTypes */].SECTION_UPDATE_CARD:
      return prevState.map(section => {
        if (section && section.id === action.data.id && section.rows) {
          const newRows = section.rows.map(card => {
            if (card.url === action.data.url) {
              return Object.assign({}, card, action.data.options);
            }
            return card;
          });
          return Object.assign({}, section, { rows: newRows });
        }
        return section;
      });
    case Actions["b" /* actionTypes */].PLACES_BOOKMARK_ADDED:
      if (!action.data) {
        return prevState;
      }
      return prevState.map(section => Object.assign({}, section, {
        rows: section.rows.map(item => {
          // find the item within the rows that is attempted to be bookmarked
          if (item.url === action.data.url) {
            const { bookmarkGuid, bookmarkTitle, dateAdded } = action.data;
            return Object.assign({}, item, {
              bookmarkGuid,
              bookmarkTitle,
              bookmarkDateCreated: dateAdded,
              type: "bookmark"
            });
          }
          return item;
        })
      }));
    case Actions["b" /* actionTypes */].PLACES_SAVED_TO_POCKET:
      if (!action.data) {
        return prevState;
      }
      return prevState.map(section => Object.assign({}, section, {
        rows: section.rows.map(item => {
          if (item.url === action.data.url) {
            return Object.assign({}, item, {
              open_url: action.data.open_url,
              pocket_id: action.data.pocket_id,
              title: action.data.title,
              type: "pocket"
            });
          }
          return item;
        })
      }));
    case Actions["b" /* actionTypes */].PLACES_BOOKMARK_REMOVED:
      if (!action.data) {
        return prevState;
      }
      return prevState.map(section => Object.assign({}, section, {
        rows: section.rows.map(item => {
          // find the bookmark within the rows that is attempted to be removed
          if (item.url === action.data.url) {
            const newSite = Object.assign({}, item);
            delete newSite.bookmarkGuid;
            delete newSite.bookmarkTitle;
            delete newSite.bookmarkDateCreated;
            if (!newSite.type || newSite.type === "bookmark") {
              newSite.type = "history";
            }
            return newSite;
          }
          return item;
        })
      }));
    case Actions["b" /* actionTypes */].PLACES_LINKS_DELETED:
      return prevState.map(section => Object.assign({}, section, { rows: section.rows.filter(site => !action.data.includes(site.url)) }));
    case Actions["b" /* actionTypes */].PLACES_LINK_BLOCKED:
      return prevState.map(section => Object.assign({}, section, { rows: section.rows.filter(site => site.url !== action.data.url) }));
    case Actions["b" /* actionTypes */].DELETE_FROM_POCKET:
    case Actions["b" /* actionTypes */].ARCHIVE_FROM_POCKET:
      return prevState.map(section => Object.assign({}, section, { rows: section.rows.filter(site => site.pocket_id !== action.data.pocket_id) }));
    default:
      return prevState;
  }
}

function Snippets(prevState = INITIAL_STATE.Snippets, action) {
  switch (action.type) {
    case Actions["b" /* actionTypes */].SNIPPETS_DATA:
      return Object.assign({}, prevState, { initialized: true }, action.data);
    case Actions["b" /* actionTypes */].SNIPPET_BLOCKED:
      return Object.assign({}, prevState, { blockList: prevState.blockList.concat(action.data) });
    case Actions["b" /* actionTypes */].SNIPPETS_BLOCKLIST_CLEARED:
      return Object.assign({}, prevState, { blockList: [] });
    case Actions["b" /* actionTypes */].SNIPPETS_RESET:
      return INITIAL_STATE.Snippets;
    default:
      return prevState;
  }
}

function PreferencesPane(prevState = INITIAL_STATE.PreferencesPane, action) {
  switch (action.type) {
    case Actions["b" /* actionTypes */].SETTINGS_OPEN:
      return Object.assign({}, prevState, { visible: true });
    case Actions["b" /* actionTypes */].SETTINGS_CLOSE:
      return Object.assign({}, prevState, { visible: false });
    default:
      return prevState;
  }
}

var reducers = { TopSites, App, Snippets, Prefs, Dialog, Sections, PreferencesPane };

/***/ }),
/* 7 */
/***/ (function(module, __webpack_exports__, __webpack_require__) {

"use strict";
/* harmony import */ var __WEBPACK_IMPORTED_MODULE_0_react_intl__ = __webpack_require__(2);
/* harmony import */ var __WEBPACK_IMPORTED_MODULE_0_react_intl___default = __webpack_require__.n(__WEBPACK_IMPORTED_MODULE_0_react_intl__);
/* harmony import */ var __WEBPACK_IMPORTED_MODULE_1_react__ = __webpack_require__(1);
/* harmony import */ var __WEBPACK_IMPORTED_MODULE_1_react___default = __webpack_require__.n(__WEBPACK_IMPORTED_MODULE_1_react__);



class ErrorBoundaryFallback extends __WEBPACK_IMPORTED_MODULE_1_react___default.a.PureComponent {
  constructor(props) {
    super(props);
    this.windowObj = this.props.windowObj || window;
    this.onClick = this.onClick.bind(this);
  }

  /**
   * Since we only get here if part of the page has crashed, do a
   * forced reload to give us the best chance at recovering.
   */
  onClick() {
    this.windowObj.location.reload(true);
  }

  render() {
    const defaultClass = "as-error-fallback";
    let className;
    if ("className" in this.props) {
      className = `${this.props.className} ${defaultClass}`;
    } else {
      className = defaultClass;
    }

    // href="#" to force normal link styling stuff (eg cursor on hover)
    return __WEBPACK_IMPORTED_MODULE_1_react___default.a.createElement(
      "div",
      { className: className },
      __WEBPACK_IMPORTED_MODULE_1_react___default.a.createElement(
        "div",
        null,
        __WEBPACK_IMPORTED_MODULE_1_react___default.a.createElement(__WEBPACK_IMPORTED_MODULE_0_react_intl__["FormattedMessage"], {
          defaultMessage: "Oops, something went wrong loading this content.",
          id: "error_fallback_default_info" })
      ),
      __WEBPACK_IMPORTED_MODULE_1_react___default.a.createElement(
        "span",
        null,
        __WEBPACK_IMPORTED_MODULE_1_react___default.a.createElement(
          "a",
          { href: "#", className: "reload-button", onClick: this.onClick },
          __WEBPACK_IMPORTED_MODULE_1_react___default.a.createElement(__WEBPACK_IMPORTED_MODULE_0_react_intl__["FormattedMessage"], {
            defaultMessage: "Refresh page to try again.",
            id: "error_fallback_default_refresh_suggestion" })
        )
      )
    );
  }
}
/* unused harmony export ErrorBoundaryFallback */

ErrorBoundaryFallback.defaultProps = { className: "as-error-fallback" };

class ErrorBoundary extends __WEBPACK_IMPORTED_MODULE_1_react___default.a.PureComponent {
  constructor(props) {
    super(props);
    this.state = { hasError: false };
  }

  componentDidCatch(error, info) {
    this.setState({ hasError: true });
  }

  render() {
    if (!this.state.hasError) {
      return this.props.children;
    }

    return __WEBPACK_IMPORTED_MODULE_1_react___default.a.createElement(this.props.FallbackComponent, { className: this.props.className });
  }
}
/* harmony export (immutable) */ __webpack_exports__["a"] = ErrorBoundary;


ErrorBoundary.defaultProps = { FallbackComponent: ErrorBoundaryFallback };

/***/ }),
/* 8 */
/***/ (function(module, __webpack_exports__, __webpack_require__) {

"use strict";

// EXTERNAL MODULE: ./system-addon/common/Actions.jsm
var Actions = __webpack_require__(0);

// EXTERNAL MODULE: external "ReactRedux"
var external__ReactRedux_ = __webpack_require__(4);
var external__ReactRedux__default = /*#__PURE__*/__webpack_require__.n(external__ReactRedux_);

// EXTERNAL MODULE: ./system-addon/content-src/components/ContextMenu/ContextMenu.jsx
var ContextMenu = __webpack_require__(9);

// EXTERNAL MODULE: external "ReactIntl"
var external__ReactIntl_ = __webpack_require__(2);
var external__ReactIntl__default = /*#__PURE__*/__webpack_require__.n(external__ReactIntl_);

// CONCATENATED MODULE: ./system-addon/content-src/lib/link-menu-options.js


const _OpenInPrivateWindow = site => ({
  id: "menu_action_open_private_window",
  icon: "new-window-private",
  action: Actions["a" /* actionCreators */].OnlyToMain({
    type: Actions["b" /* actionTypes */].OPEN_PRIVATE_WINDOW,
    data: { url: site.url, referrer: site.referrer }
  }),
  userEvent: "OPEN_PRIVATE_WINDOW"
});

/**
 * List of functions that return items that can be included as menu options in a
 * LinkMenu. All functions take the site as the first parameter, and optionally
 * the index of the site.
 */
const LinkMenuOptions = {
  Separator: () => ({ type: "separator" }),
  EmptyItem: () => ({ type: "empty" }),
  RemoveBookmark: site => ({
    id: "menu_action_remove_bookmark",
    icon: "bookmark-added",
    action: Actions["a" /* actionCreators */].AlsoToMain({
      type: Actions["b" /* actionTypes */].DELETE_BOOKMARK_BY_ID,
      data: site.bookmarkGuid
    }),
    userEvent: "BOOKMARK_DELETE"
  }),
  AddBookmark: site => ({
    id: "menu_action_bookmark",
    icon: "bookmark-hollow",
    action: Actions["a" /* actionCreators */].AlsoToMain({
      type: Actions["b" /* actionTypes */].BOOKMARK_URL,
      data: { url: site.url, title: site.title, type: site.type }
    }),
    userEvent: "BOOKMARK_ADD"
  }),
  OpenInNewWindow: site => ({
    id: "menu_action_open_new_window",
    icon: "new-window",
    action: Actions["a" /* actionCreators */].AlsoToMain({
      type: Actions["b" /* actionTypes */].OPEN_NEW_WINDOW,
      data: { url: site.url, referrer: site.referrer }
    }),
    userEvent: "OPEN_NEW_WINDOW"
  }),
  BlockUrl: (site, index, eventSource) => ({
    id: "menu_action_dismiss",
    icon: "dismiss",
    action: Actions["a" /* actionCreators */].AlsoToMain({
      type: Actions["b" /* actionTypes */].BLOCK_URL,
      data: { url: site.url, pocket_id: site.pocket_id }
    }),
    impression: Actions["a" /* actionCreators */].ImpressionStats({
      source: eventSource,
      block: 0,
      tiles: [{ id: site.guid, pos: index }]
    }),
    userEvent: "BLOCK"
  }),

  // This is an option for web extentions which will result in remove items from
  // memory and notify the web extenion, rather than using the built-in block list.
  WebExtDismiss: (site, index, eventSource) => ({
    id: "menu_action_webext_dismiss",
    string_id: "menu_action_dismiss",
    icon: "dismiss",
    action: Actions["a" /* actionCreators */].WebExtEvent(Actions["b" /* actionTypes */].WEBEXT_DISMISS, {
      source: eventSource,
      url: site.url,
      action_position: index
    })
  }),
  DeleteUrl: (site, index, eventSource, isEnabled, siteInfo) => ({
    id: "menu_action_delete",
    icon: "delete",
    action: {
      type: Actions["b" /* actionTypes */].DIALOG_OPEN,
      data: {
        onConfirm: [Actions["a" /* actionCreators */].AlsoToMain({ type: Actions["b" /* actionTypes */].DELETE_HISTORY_URL, data: { url: site.url, pocket_id: site.pocket_id, forceBlock: site.bookmarkGuid } }), Actions["a" /* actionCreators */].UserEvent(Object.assign({ event: "DELETE", source: eventSource, action_position: index }, siteInfo))],
        eventSource,
        body_string_id: ["confirm_history_delete_p1", "confirm_history_delete_notice_p2"],
        confirm_button_string_id: "menu_action_delete",
        cancel_button_string_id: "topsites_form_cancel_button",
        icon: "modal-delete"
      }
    },
    userEvent: "DIALOG_OPEN"
  }),
  PinTopSite: (site, index) => ({
    id: "menu_action_pin",
    icon: "pin",
    action: Actions["a" /* actionCreators */].AlsoToMain({
      type: Actions["b" /* actionTypes */].TOP_SITES_PIN,
      data: { site: { url: site.url }, index }
    }),
    userEvent: "PIN"
  }),
  UnpinTopSite: site => ({
    id: "menu_action_unpin",
    icon: "unpin",
    action: Actions["a" /* actionCreators */].AlsoToMain({
      type: Actions["b" /* actionTypes */].TOP_SITES_UNPIN,
      data: { site: { url: site.url } }
    }),
    userEvent: "UNPIN"
  }),
  SaveToPocket: (site, index, eventSource) => ({
    id: "menu_action_save_to_pocket",
    icon: "pocket",
    action: Actions["a" /* actionCreators */].AlsoToMain({
      type: Actions["b" /* actionTypes */].SAVE_TO_POCKET,
      data: { site: { url: site.url, title: site.title } }
    }),
    impression: Actions["a" /* actionCreators */].ImpressionStats({
      source: eventSource,
      pocket: 0,
      tiles: [{ id: site.guid, pos: index }]
    }),
    userEvent: "SAVE_TO_POCKET"
  }),
  DeleteFromPocket: site => ({
    id: "menu_action_delete_pocket",
    icon: "delete",
    action: Actions["a" /* actionCreators */].AlsoToMain({
      type: Actions["b" /* actionTypes */].DELETE_FROM_POCKET,
      data: { pocket_id: site.pocket_id }
    }),
    userEvent: "DELETE_FROM_POCKET"
  }),
  ArchiveFromPocket: site => ({
    id: "menu_action_archive_pocket",
    icon: "check",
    action: Actions["a" /* actionCreators */].AlsoToMain({
      type: Actions["b" /* actionTypes */].ARCHIVE_FROM_POCKET,
      data: { pocket_id: site.pocket_id }
    }),
    userEvent: "ARCHIVE_FROM_POCKET"
  }),
  EditTopSite: (site, index) => ({
    id: "edit_topsites_button_text",
    icon: "edit",
    action: {
      type: Actions["b" /* actionTypes */].TOP_SITES_EDIT,
      data: { index }
    }
  }),
  CheckBookmark: site => site.bookmarkGuid ? LinkMenuOptions.RemoveBookmark(site) : LinkMenuOptions.AddBookmark(site),
  CheckPinTopSite: (site, index) => site.isPinned ? LinkMenuOptions.UnpinTopSite(site) : LinkMenuOptions.PinTopSite(site, index),
  CheckSavedToPocket: (site, index) => site.pocket_id ? LinkMenuOptions.DeleteFromPocket(site) : LinkMenuOptions.SaveToPocket(site, index),
  CheckBookmarkOrArchive: site => site.pocket_id ? LinkMenuOptions.ArchiveFromPocket(site) : LinkMenuOptions.CheckBookmark(site),
  CheckDeleteHistoryOrEmpty: (site, index, eventSource, isEnabled, siteInfo) => site.pocket_id ? LinkMenuOptions.EmptyItem() : LinkMenuOptions.DeleteUrl(site, index, eventSource, isEnabled, siteInfo),
  OpenInPrivateWindow: (site, index, eventSource, isEnabled) => isEnabled ? _OpenInPrivateWindow(site) : LinkMenuOptions.EmptyItem()
};
// EXTERNAL MODULE: external "React"
var external__React_ = __webpack_require__(1);
var external__React__default = /*#__PURE__*/__webpack_require__.n(external__React_);

// CONCATENATED MODULE: ./system-addon/content-src/components/LinkMenu/LinkMenu.jsx







const DEFAULT_SITE_MENU_OPTIONS = ["CheckPinTopSite", "EditTopSite", "Separator", "OpenInNewWindow", "OpenInPrivateWindow", "Separator", "BlockUrl"];

class LinkMenu__LinkMenu extends external__React__default.a.PureComponent {
  getOptions() {
    const { props } = this;
    const { site, index, source, isPrivateBrowsingEnabled, siteInfo } = props;

    // Handle special case of default site
    const propOptions = !site.isDefault ? props.options : DEFAULT_SITE_MENU_OPTIONS;

    const options = propOptions.map(o => LinkMenuOptions[o](site, index, source, isPrivateBrowsingEnabled, siteInfo)).map(option => {
      const { action, impression, id, string_id, type, userEvent } = option;
      if (!type && id) {
        option.label = props.intl.formatMessage({ id: string_id || id });
        option.onClick = () => {
          props.dispatch(action);
          if (userEvent) {
            const userEventData = Object.assign({
              event: userEvent,
              source,
              action_position: index
            }, siteInfo);
            props.dispatch(Actions["a" /* actionCreators */].UserEvent(userEventData));
          }
          if (impression && props.shouldSendImpressionStats) {
            props.dispatch(impression);
          }
        };
      }
      return option;
    });

    // This is for accessibility to support making each item tabbable.
    // We want to know which item is the first and which item
    // is the last, so we can close the context menu accordingly.
    options[0].first = true;
    options[options.length - 1].last = true;
    return options;
  }

  render() {
    return external__React__default.a.createElement(ContextMenu["a" /* ContextMenu */], {
      onUpdate: this.props.onUpdate,
      options: this.getOptions() });
  }
}
/* unused harmony export _LinkMenu */


const getState = state => ({ isPrivateBrowsingEnabled: state.Prefs.values.isPrivateBrowsingEnabled });
const LinkMenu = Object(external__ReactRedux_["connect"])(getState)(Object(external__ReactIntl_["injectIntl"])(LinkMenu__LinkMenu));
/* harmony export (immutable) */ __webpack_exports__["a"] = LinkMenu;


/***/ }),
/* 9 */
/***/ (function(module, __webpack_exports__, __webpack_require__) {

"use strict";
/* WEBPACK VAR INJECTION */(function(global) {/* harmony import */ var __WEBPACK_IMPORTED_MODULE_0_react__ = __webpack_require__(1);
/* harmony import */ var __WEBPACK_IMPORTED_MODULE_0_react___default = __webpack_require__.n(__WEBPACK_IMPORTED_MODULE_0_react__);


class ContextMenu extends __WEBPACK_IMPORTED_MODULE_0_react___default.a.PureComponent {
  constructor(props) {
    super(props);
    this.hideContext = this.hideContext.bind(this);
    this.onClick = this.onClick.bind(this);
  }

  hideContext() {
    this.props.onUpdate(false);
  }

  componentDidMount() {
    setTimeout(() => {
      global.addEventListener("click", this.hideContext);
    }, 0);
  }

  componentWillUnmount() {
    global.removeEventListener("click", this.hideContext);
  }

  onClick(event) {
    // Eat all clicks on the context menu so they don't bubble up to window.
    // This prevents the context menu from closing when clicking disabled items
    // or the separators.
    event.stopPropagation();
  }

  render() {
    return __WEBPACK_IMPORTED_MODULE_0_react___default.a.createElement(
      "span",
      { className: "context-menu", onClick: this.onClick },
      __WEBPACK_IMPORTED_MODULE_0_react___default.a.createElement(
        "ul",
        { role: "menu", className: "context-menu-list" },
        this.props.options.map((option, i) => option.type === "separator" ? __WEBPACK_IMPORTED_MODULE_0_react___default.a.createElement("li", { key: i, className: "separator" }) : option.type !== "empty" && __WEBPACK_IMPORTED_MODULE_0_react___default.a.createElement(ContextMenuItem, { key: i, option: option, hideContext: this.hideContext }))
      )
    );
  }
}
/* harmony export (immutable) */ __webpack_exports__["a"] = ContextMenu;


class ContextMenuItem extends __WEBPACK_IMPORTED_MODULE_0_react___default.a.PureComponent {
  constructor(props) {
    super(props);
    this.onClick = this.onClick.bind(this);
    this.onKeyDown = this.onKeyDown.bind(this);
  }

  onClick() {
    this.props.hideContext();
    this.props.option.onClick();
  }

  onKeyDown(event) {
    const { option } = this.props;
    switch (event.key) {
      case "Tab":
        // tab goes down in context menu, shift + tab goes up in context menu
        // if we're on the last item, one more tab will close the context menu
        // similarly, if we're on the first item, one more shift + tab will close it
        if (event.shiftKey && option.first || !event.shiftKey && option.last) {
          this.props.hideContext();
        }
        break;
      case "Enter":
        this.props.hideContext();
        option.onClick();
        break;
    }
  }

  render() {
    const { option } = this.props;
    return __WEBPACK_IMPORTED_MODULE_0_react___default.a.createElement(
      "li",
      { role: "menuitem", className: "context-menu-item" },
      __WEBPACK_IMPORTED_MODULE_0_react___default.a.createElement(
        "a",
        { onClick: this.onClick, onKeyDown: this.onKeyDown, tabIndex: "0", className: option.disabled ? "disabled" : "" },
        option.icon && __WEBPACK_IMPORTED_MODULE_0_react___default.a.createElement("span", { className: `icon icon-spacer icon-${option.icon}` }),
        option.label
      )
    );
  }
}
/* unused harmony export ContextMenuItem */

/* WEBPACK VAR INJECTION */}.call(__webpack_exports__, __webpack_require__(3)))

/***/ }),
/* 10 */
/***/ (function(module, __webpack_exports__, __webpack_require__) {

"use strict";
/* WEBPACK VAR INJECTION */(function(global) {/* harmony import */ var __WEBPACK_IMPORTED_MODULE_0_react_intl__ = __webpack_require__(2);
/* harmony import */ var __WEBPACK_IMPORTED_MODULE_0_react_intl___default = __webpack_require__.n(__WEBPACK_IMPORTED_MODULE_0_react_intl__);
/* harmony import */ var __WEBPACK_IMPORTED_MODULE_1_common_Actions_jsm__ = __webpack_require__(0);
/* harmony import */ var __WEBPACK_IMPORTED_MODULE_2_content_src_components_ErrorBoundary_ErrorBoundary__ = __webpack_require__(7);
/* harmony import */ var __WEBPACK_IMPORTED_MODULE_3_react__ = __webpack_require__(1);
/* harmony import */ var __WEBPACK_IMPORTED_MODULE_3_react___default = __webpack_require__.n(__WEBPACK_IMPORTED_MODULE_3_react__);
/* harmony import */ var __WEBPACK_IMPORTED_MODULE_4_content_src_components_SectionMenu_SectionMenu__ = __webpack_require__(20);






const VISIBLE = "visible";
const VISIBILITY_CHANGE_EVENT = "visibilitychange";

function getFormattedMessage(message) {
  return typeof message === "string" ? __WEBPACK_IMPORTED_MODULE_3_react___default.a.createElement(
    "span",
    null,
    message
  ) : __WEBPACK_IMPORTED_MODULE_3_react___default.a.createElement(__WEBPACK_IMPORTED_MODULE_0_react_intl__["FormattedMessage"], message);
}
function getCollapsed(props) {
  return props.prefName in props.Prefs.values ? props.Prefs.values[props.prefName] : false;
}

class Disclaimer extends __WEBPACK_IMPORTED_MODULE_3_react___default.a.PureComponent {
  constructor(props) {
    super(props);
    this.onAcknowledge = this.onAcknowledge.bind(this);
  }

  onAcknowledge() {
    this.props.dispatch(__WEBPACK_IMPORTED_MODULE_1_common_Actions_jsm__["a" /* actionCreators */].SetPref(this.props.disclaimerPref, false));
    this.props.dispatch(__WEBPACK_IMPORTED_MODULE_1_common_Actions_jsm__["a" /* actionCreators */].UserEvent({ event: "SECTION_DISCLAIMER_ACKNOWLEDGED", source: this.props.eventSource }));
  }

  render() {
    const { disclaimer } = this.props;
    return __WEBPACK_IMPORTED_MODULE_3_react___default.a.createElement(
      "div",
      { className: "section-disclaimer" },
      __WEBPACK_IMPORTED_MODULE_3_react___default.a.createElement(
        "div",
        { className: "section-disclaimer-text" },
        getFormattedMessage(disclaimer.text),
        disclaimer.link && __WEBPACK_IMPORTED_MODULE_3_react___default.a.createElement(
          "a",
          { href: disclaimer.link.href, target: "_blank", rel: "noopener noreferrer" },
          getFormattedMessage(disclaimer.link.title || disclaimer.link)
        )
      ),
      __WEBPACK_IMPORTED_MODULE_3_react___default.a.createElement(
        "button",
        { onClick: this.onAcknowledge },
        getFormattedMessage(disclaimer.button)
      )
    );
  }
}
/* unused harmony export Disclaimer */


const DisclaimerIntl = Object(__WEBPACK_IMPORTED_MODULE_0_react_intl__["injectIntl"])(Disclaimer);
/* unused harmony export DisclaimerIntl */


class _CollapsibleSection extends __WEBPACK_IMPORTED_MODULE_3_react___default.a.PureComponent {
  constructor(props) {
    super(props);
    this.onBodyMount = this.onBodyMount.bind(this);
    this.onHeaderClick = this.onHeaderClick.bind(this);
    this.onTransitionEnd = this.onTransitionEnd.bind(this);
    this.enableOrDisableAnimation = this.enableOrDisableAnimation.bind(this);
    this.onMenuButtonClick = this.onMenuButtonClick.bind(this);
    this.onMenuButtonMouseEnter = this.onMenuButtonMouseEnter.bind(this);
    this.onMenuButtonMouseLeave = this.onMenuButtonMouseLeave.bind(this);
    this.onMenuUpdate = this.onMenuUpdate.bind(this);
    this.state = { enableAnimation: true, isAnimating: false, menuButtonHover: false, showContextMenu: false };
  }

  componentWillMount() {
    this.props.document.addEventListener(VISIBILITY_CHANGE_EVENT, this.enableOrDisableAnimation);
  }

  componentWillUpdate(nextProps) {
    // Check if we're about to go from expanded to collapsed
    if (!getCollapsed(this.props) && getCollapsed(nextProps)) {
      // This next line forces a layout flush of the section body, which has a
      // max-height style set, so that the upcoming collapse animation can
      // animate from that height to the collapsed height. Without this, the
      // update is coalesced and there's no animation from no-max-height to 0.
      this.sectionBody.scrollHeight; // eslint-disable-line no-unused-expressions
    }
  }

  componentWillUnmount() {
    this.props.document.removeEventListener(VISIBILITY_CHANGE_EVENT, this.enableOrDisableAnimation);
  }

  enableOrDisableAnimation() {
    // Only animate the collapse/expand for visible tabs.
    const visible = this.props.document.visibilityState === VISIBLE;
    if (this.state.enableAnimation !== visible) {
      this.setState({ enableAnimation: visible });
    }
  }

  onBodyMount(node) {
    this.sectionBody = node;
  }

  onHeaderClick() {
    // If this.sectionBody is unset, it means that we're in some sort of error
    // state, probably displaying the error fallback, so we won't be able to
    // compute the height, and we don't want to persist the preference.
    if (!this.sectionBody) {
      return;
    }

    // Get the current height of the body so max-height transitions can work
    this.setState({
      isAnimating: true,
      maxHeight: `${this.sectionBody.scrollHeight}px`
    });
    this.props.dispatch(__WEBPACK_IMPORTED_MODULE_1_common_Actions_jsm__["a" /* actionCreators */].SetPref(this.props.prefName, !getCollapsed(this.props)));
  }

  onTransitionEnd(event) {
    // Only update the animating state for our own transition (not a child's)
    if (event.target === event.currentTarget) {
      this.setState({ isAnimating: false });
    }
  }

  renderIcon() {
    const { icon } = this.props;
    if (icon && icon.startsWith("moz-extension://")) {
      return __WEBPACK_IMPORTED_MODULE_3_react___default.a.createElement("span", { className: "icon icon-small-spacer", style: { backgroundImage: `url('${icon}')` } });
    }
    return __WEBPACK_IMPORTED_MODULE_3_react___default.a.createElement("span", { className: `icon icon-small-spacer icon-${icon || "webextension"}` });
  }

  onMenuButtonClick(event) {
    event.preventDefault();
    this.setState({ showContextMenu: true });
  }

  onMenuButtonMouseEnter() {
    this.setState({ menuButtonHover: true });
  }

  onMenuButtonMouseLeave() {
    this.setState({ menuButtonHover: false });
  }

  onMenuUpdate(showContextMenu) {
    this.setState({ showContextMenu });
  }

  render() {
    const isCollapsible = this.props.prefName in this.props.Prefs.values;
    const isCollapsed = getCollapsed(this.props);
    const { enableAnimation, isAnimating, maxHeight, menuButtonHover, showContextMenu } = this.state;
    const { id, eventSource, disclaimer, title, extraMenuOptions, prefName, showPrefName, privacyNoticeURL, dispatch, isFirst, isLast } = this.props;
    const disclaimerPref = `section.${id}.showDisclaimer`;
    const needsDisclaimer = disclaimer && this.props.Prefs.values[disclaimerPref];
    const active = menuButtonHover || showContextMenu;

    return __WEBPACK_IMPORTED_MODULE_3_react___default.a.createElement(
      "section",
      { className: `collapsible-section ${this.props.className}${enableAnimation ? " animation-enabled" : ""}${isCollapsed ? " collapsed" : ""}${active ? " active" : ""}` },
      __WEBPACK_IMPORTED_MODULE_3_react___default.a.createElement(
        "div",
        { className: "section-top-bar" },
        __WEBPACK_IMPORTED_MODULE_3_react___default.a.createElement(
          "h3",
          { className: "section-title" },
          __WEBPACK_IMPORTED_MODULE_3_react___default.a.createElement(
            "span",
            { className: "click-target", onClick: isCollapsible && this.onHeaderClick },
            this.renderIcon(),
            title,
            isCollapsible && __WEBPACK_IMPORTED_MODULE_3_react___default.a.createElement("span", { className: `collapsible-arrow icon ${isCollapsed ? "icon-arrowhead-forward-small" : "icon-arrowhead-down-small"}` })
          )
        ),
        __WEBPACK_IMPORTED_MODULE_3_react___default.a.createElement(
          "div",
          null,
          __WEBPACK_IMPORTED_MODULE_3_react___default.a.createElement(
            "button",
            {
              className: "context-menu-button icon",
              onClick: this.onMenuButtonClick,
              onMouseEnter: this.onMenuButtonMouseEnter,
              onMouseLeave: this.onMenuButtonMouseLeave },
            __WEBPACK_IMPORTED_MODULE_3_react___default.a.createElement(
              "span",
              { className: "sr-only" },
              __WEBPACK_IMPORTED_MODULE_3_react___default.a.createElement(__WEBPACK_IMPORTED_MODULE_0_react_intl__["FormattedMessage"], { id: "section_context_menu_button_sr" })
            )
          ),
          showContextMenu && __WEBPACK_IMPORTED_MODULE_3_react___default.a.createElement(__WEBPACK_IMPORTED_MODULE_4_content_src_components_SectionMenu_SectionMenu__["a" /* SectionMenu */], {
            id: id,
            extraOptions: extraMenuOptions,
            eventSource: eventSource,
            showPrefName: showPrefName,
            collapsePrefName: prefName,
            privacyNoticeURL: privacyNoticeURL,
            isCollapsed: isCollapsed,
            onUpdate: this.onMenuUpdate,
            isFirst: isFirst,
            isLast: isLast,
            dispatch: dispatch })
        )
      ),
      __WEBPACK_IMPORTED_MODULE_3_react___default.a.createElement(
        __WEBPACK_IMPORTED_MODULE_2_content_src_components_ErrorBoundary_ErrorBoundary__["a" /* ErrorBoundary */],
        { className: "section-body-fallback" },
        __WEBPACK_IMPORTED_MODULE_3_react___default.a.createElement(
          "div",
          {
            className: `section-body${isAnimating ? " animating" : ""}`,
            onTransitionEnd: this.onTransitionEnd,
            ref: this.onBodyMount,
            style: isAnimating && !isCollapsed ? { maxHeight } : null },
          needsDisclaimer && __WEBPACK_IMPORTED_MODULE_3_react___default.a.createElement(DisclaimerIntl, { disclaimerPref: disclaimerPref, disclaimer: disclaimer, eventSource: eventSource, dispatch: this.props.dispatch }),
          this.props.children
        )
      )
    );
  }
}
/* unused harmony export _CollapsibleSection */


_CollapsibleSection.defaultProps = {
  document: global.document || {
    addEventListener: () => {},
    removeEventListener: () => {},
    visibilityState: "hidden"
  },
  Prefs: { values: {} }
};

const CollapsibleSection = Object(__WEBPACK_IMPORTED_MODULE_0_react_intl__["injectIntl"])(_CollapsibleSection);
/* harmony export (immutable) */ __webpack_exports__["a"] = CollapsibleSection;

/* WEBPACK VAR INJECTION */}.call(__webpack_exports__, __webpack_require__(3)))

/***/ }),
/* 11 */
/***/ (function(module, __webpack_exports__, __webpack_require__) {

"use strict";
/* harmony import */ var __WEBPACK_IMPORTED_MODULE_0_common_Actions_jsm__ = __webpack_require__(0);
/* harmony import */ var __WEBPACK_IMPORTED_MODULE_1_common_PerfService_jsm__ = __webpack_require__(12);
/* harmony import */ var __WEBPACK_IMPORTED_MODULE_2_react__ = __webpack_require__(1);
/* harmony import */ var __WEBPACK_IMPORTED_MODULE_2_react___default = __webpack_require__.n(__WEBPACK_IMPORTED_MODULE_2_react__);




// Currently record only a fixed set of sections. This will prevent data
// from custom sections from showing up or from topstories.
const RECORDED_SECTIONS = ["highlights", "topsites"];

class ComponentPerfTimer extends __WEBPACK_IMPORTED_MODULE_2_react___default.a.Component {
  constructor(props) {
    super(props);
    // Just for test dependency injection:
    this.perfSvc = this.props.perfSvc || __WEBPACK_IMPORTED_MODULE_1_common_PerfService_jsm__["a" /* perfService */];

    this._sendBadStateEvent = this._sendBadStateEvent.bind(this);
    this._sendPaintedEvent = this._sendPaintedEvent.bind(this);
    this._reportMissingData = false;
    this._timestampHandled = false;
    this._recordedFirstRender = false;
  }

  componentDidMount() {
    if (!RECORDED_SECTIONS.includes(this.props.id)) {
      return;
    }

    this._maybeSendPaintedEvent();
  }

  componentDidUpdate() {
    if (!RECORDED_SECTIONS.includes(this.props.id)) {
      return;
    }

    this._maybeSendPaintedEvent();
  }

  /**
   * Call the given callback after the upcoming frame paints.
   *
   * @note Both setTimeout and requestAnimationFrame are throttled when the page
   * is hidden, so this callback may get called up to a second or so after the
   * requestAnimationFrame "paint" for hidden tabs.
   *
   * Newtabs hidden while loading will presumably be fairly rare (other than
   * preloaded tabs, which we will be filtering out on the server side), so such
   * cases should get lost in the noise.
   *
   * If we decide that it's important to find out when something that's hidden
   * has "painted", however, another option is to post a message to this window.
   * That should happen even faster than setTimeout, and, at least as of this
   * writing, it's not throttled in hidden windows in Firefox.
   *
   * @param {Function} callback
   *
   * @returns void
   */
  _afterFramePaint(callback) {
    requestAnimationFrame(() => setTimeout(callback, 0));
  }

  _maybeSendBadStateEvent() {
    // Follow up bugs:
    // https://github.com/mozilla/activity-stream/issues/3691
    if (!this.props.initialized) {
      // Remember to report back when data is available.
      this._reportMissingData = true;
    } else if (this._reportMissingData) {
      this._reportMissingData = false;
      // Report how long it took for component to become initialized.
      this._sendBadStateEvent();
    }
  }

  _maybeSendPaintedEvent() {
    // If we've already handled a timestamp, don't do it again.
    if (this._timestampHandled || !this.props.initialized) {
      return;
    }

    // And if we haven't, we're doing so now, so remember that. Even if
    // something goes wrong in the callback, we can't try again, as we'd be
    // sending back the wrong data, and we have to do it here, so that other
    // calls to this method while waiting for the next frame won't also try to
    // handle it.
    this._timestampHandled = true;
    this._afterFramePaint(this._sendPaintedEvent);
  }

  /**
   * Triggered by call to render. Only first call goes through due to
   * `_recordedFirstRender`.
   */
  _ensureFirstRenderTsRecorded() {
    // Used as t0 for recording how long component took to initialize.
    if (!this._recordedFirstRender) {
      this._recordedFirstRender = true;
      // topsites_first_render_ts, highlights_first_render_ts.
      const key = `${this.props.id}_first_render_ts`;
      this.perfSvc.mark(key);
    }
  }

  /**
   * Creates `TELEMETRY_UNDESIRED_EVENT` with timestamp in ms
   * of how much longer the data took to be ready for display than it would
   * have been the ideal case.
   * https://github.com/mozilla/ping-centre/issues/98
   */
  _sendBadStateEvent() {
    // highlights_data_ready_ts, topsites_data_ready_ts.
    const dataReadyKey = `${this.props.id}_data_ready_ts`;
    this.perfSvc.mark(dataReadyKey);

    try {
      const firstRenderKey = `${this.props.id}_first_render_ts`;
      // value has to be Int32.
      const value = parseInt(this.perfSvc.getMostRecentAbsMarkStartByName(dataReadyKey) - this.perfSvc.getMostRecentAbsMarkStartByName(firstRenderKey), 10);
      this.props.dispatch(__WEBPACK_IMPORTED_MODULE_0_common_Actions_jsm__["a" /* actionCreators */].OnlyToMain({
        type: __WEBPACK_IMPORTED_MODULE_0_common_Actions_jsm__["b" /* actionTypes */].SAVE_SESSION_PERF_DATA,
        // highlights_data_late_by_ms, topsites_data_late_by_ms.
        data: { [`${this.props.id}_data_late_by_ms`]: value }
      }));
    } catch (ex) {
      // If this failed, it's likely because the `privacy.resistFingerprinting`
      // pref is true.
    }
  }

  _sendPaintedEvent() {
    // Record first_painted event but only send if topsites.
    if (this.props.id !== "topsites") {
      return;
    }

    // topsites_first_painted_ts.
    const key = `${this.props.id}_first_painted_ts`;
    this.perfSvc.mark(key);

    try {
      const data = {};
      data[key] = this.perfSvc.getMostRecentAbsMarkStartByName(key);

      this.props.dispatch(__WEBPACK_IMPORTED_MODULE_0_common_Actions_jsm__["a" /* actionCreators */].OnlyToMain({
        type: __WEBPACK_IMPORTED_MODULE_0_common_Actions_jsm__["b" /* actionTypes */].SAVE_SESSION_PERF_DATA,
        data
      }));
    } catch (ex) {
      // If this failed, it's likely because the `privacy.resistFingerprinting`
      // pref is true.  We should at least not blow up, and should continue
      // to set this._timestampHandled to avoid going through this again.
    }
  }

  render() {
    if (RECORDED_SECTIONS.includes(this.props.id)) {
      this._ensureFirstRenderTsRecorded();
      this._maybeSendBadStateEvent();
    }
    return this.props.children;
  }
}
/* harmony export (immutable) */ __webpack_exports__["a"] = ComponentPerfTimer;


/***/ }),
/* 12 */
/***/ (function(module, __webpack_exports__, __webpack_require__) {

"use strict";
/* unused harmony export _PerfService */
/* harmony export (binding) */ __webpack_require__.d(__webpack_exports__, "a", function() { return perfService; });
/* globals Services */


/* istanbul ignore if */

if (typeof ChromeUtils !== "undefined") {
  ChromeUtils.import("resource://gre/modules/Services.jsm");
}

let usablePerfObj;

/* istanbul ignore if */
/* istanbul ignore else */
if (typeof Services !== "undefined") {
  // Borrow the high-resolution timer from the hidden window....
  usablePerfObj = Services.appShell.hiddenDOMWindow.performance;
} else if (typeof performance !== "undefined") {
  // we must be running in content space
  // eslint-disable-next-line no-undef
  usablePerfObj = performance;
} else {
  // This is a dummy object so this file doesn't crash in the node prerendering
  // task.
  usablePerfObj = {
    now() {},
    mark() {}
  };
}

function _PerfService(options) {
  // For testing, so that we can use a fake Window.performance object with
  // known state.
  if (options && options.performanceObj) {
    this._perf = options.performanceObj;
  } else {
    this._perf = usablePerfObj;
  }
}


_PerfService.prototype = {
  /**
   * Calls the underlying mark() method on the appropriate Window.performance
   * object to add a mark with the given name to the appropriate performance
   * timeline.
   *
   * @param  {String} name  the name to give the current mark
   * @return {void}
   */
  mark: function mark(str) {
    this._perf.mark(str);
  },

  /**
   * Calls the underlying getEntriesByName on the appropriate Window.performance
   * object.
   *
   * @param  {String} name
   * @param  {String} type eg "mark"
   * @return {Array}       Performance* objects
   */
  getEntriesByName: function getEntriesByName(name, type) {
    return this._perf.getEntriesByName(name, type);
  },

  /**
   * The timeOrigin property from the appropriate performance object.
   * Used to ensure that timestamps from the add-on code and the content code
   * are comparable.
   *
   * @note If this is called from a context without a window
   * (eg a JSM in chrome), it will return the timeOrigin of the XUL hidden
   * window, which appears to be the first created window (and thus
   * timeOrigin) in the browser.  Note also, however, there is also a private
   * hidden window, presumably for private browsing, which appears to be
   * created dynamically later.  Exactly how/when that shows up needs to be
   * investigated.
   *
   * @return {Number} A double of milliseconds with a precision of 0.5us.
   */
  get timeOrigin() {
    return this._perf.timeOrigin;
  },

  /**
   * Returns the "absolute" version of performance.now(), i.e. one that
   * should ([bug 1401406](https://bugzilla.mozilla.org/show_bug.cgi?id=1401406)
   * be comparable across both chrome and content.
   *
   * @return {Number}
   */
  absNow: function absNow() {
    return this.timeOrigin + this._perf.now();
  },

  /**
   * This returns the absolute startTime from the most recent performance.mark()
   * with the given name.
   *
   * @param  {String} name  the name to lookup the start time for
   *
   * @return {Number}       the returned start time, as a DOMHighResTimeStamp
   *
   * @throws {Error}        "No Marks with the name ..." if none are available
   *
   * @note Always surround calls to this by try/catch.  Otherwise your code
   * may fail when the `privacy.resistFingerprinting` pref is true.  When
   * this pref is set, all attempts to get marks will likely fail, which will
   * cause this method to throw.
   *
   * See [bug 1369303](https://bugzilla.mozilla.org/show_bug.cgi?id=1369303)
   * for more info.
   */
  getMostRecentAbsMarkStartByName(name) {
    let entries = this.getEntriesByName(name, "mark");

    if (!entries.length) {
      throw new Error(`No marks with the name ${name}`);
    }

    let mostRecentEntry = entries[entries.length - 1];
    return this._perf.timeOrigin + mostRecentEntry.startTime;
  }
};

var perfService = new _PerfService();

/***/ }),
/* 13 */
/***/ (function(module, __webpack_exports__, __webpack_require__) {

"use strict";
/* harmony import */ var __WEBPACK_IMPORTED_MODULE_0_common_Actions_jsm__ = __webpack_require__(0);
/* harmony import */ var __WEBPACK_IMPORTED_MODULE_1_react_intl__ = __webpack_require__(2);
/* harmony import */ var __WEBPACK_IMPORTED_MODULE_1_react_intl___default = __webpack_require__.n(__WEBPACK_IMPORTED_MODULE_1_react_intl__);
/* harmony import */ var __WEBPACK_IMPORTED_MODULE_2__TopSitesConstants__ = __webpack_require__(5);
/* harmony import */ var __WEBPACK_IMPORTED_MODULE_3_content_src_components_LinkMenu_LinkMenu__ = __webpack_require__(8);
/* harmony import */ var __WEBPACK_IMPORTED_MODULE_4_react__ = __webpack_require__(1);
/* harmony import */ var __WEBPACK_IMPORTED_MODULE_4_react___default = __webpack_require__.n(__WEBPACK_IMPORTED_MODULE_4_react__);
/* harmony import */ var __WEBPACK_IMPORTED_MODULE_5_common_Reducers_jsm__ = __webpack_require__(6);
var _extends = Object.assign || function (target) { for (var i = 1; i < arguments.length; i++) { var source = arguments[i]; for (var key in source) { if (Object.prototype.hasOwnProperty.call(source, key)) { target[key] = source[key]; } } } return target; };








class TopSiteLink extends __WEBPACK_IMPORTED_MODULE_4_react___default.a.PureComponent {
  constructor(props) {
    super(props);
    this.onDragEvent = this.onDragEvent.bind(this);
  }

  /*
   * Helper to determine whether the drop zone should allow a drop. We only allow
   * dropping top sites for now.
   */
  _allowDrop(e) {
    return e.dataTransfer.types.includes("text/topsite-index");
  }

  onDragEvent(event) {
    switch (event.type) {
      case "click":
        // Stop any link clicks if we started any dragging
        if (this.dragged) {
          event.preventDefault();
        }
        break;
      case "dragstart":
        this.dragged = true;
        event.dataTransfer.effectAllowed = "move";
        event.dataTransfer.setData("text/topsite-index", this.props.index);
        event.target.blur();
        this.props.onDragEvent(event, this.props.index, this.props.link, this.props.title);
        break;
      case "dragend":
        this.props.onDragEvent(event);
        break;
      case "dragenter":
      case "dragover":
      case "drop":
        if (this._allowDrop(event)) {
          event.preventDefault();
          this.props.onDragEvent(event, this.props.index);
        }
        break;
      case "mousedown":
        // Reset at the first mouse event of a potential drag
        this.dragged = false;
        break;
    }
  }

  render() {
    const { children, className, isDraggable, link, onClick, title } = this.props;
    const topSiteOuterClassName = `top-site-outer${className ? ` ${className}` : ""}${link.isDragged ? " dragged" : ""}`;
    const { tippyTopIcon, faviconSize } = link;
    const [letterFallback] = title;
    let imageClassName;
    let imageStyle;
    let showSmallFavicon = false;
    let smallFaviconStyle;
    let smallFaviconFallback;
    if (tippyTopIcon || faviconSize >= __WEBPACK_IMPORTED_MODULE_2__TopSitesConstants__["b" /* MIN_RICH_FAVICON_SIZE */]) {
      // styles and class names for top sites with rich icons
      imageClassName = "top-site-icon rich-icon";
      imageStyle = {
        backgroundColor: link.backgroundColor,
        backgroundImage: `url(${tippyTopIcon || link.favicon})`
      };
    } else {
      // styles and class names for top sites with screenshot + small icon in top left corner
      imageClassName = `screenshot${link.screenshot ? " active" : ""}`;
      imageStyle = { backgroundImage: link.screenshot ? `url(${link.screenshot})` : "none" };

      // only show a favicon in top left if it's greater than 16x16
      if (faviconSize >= __WEBPACK_IMPORTED_MODULE_2__TopSitesConstants__["a" /* MIN_CORNER_FAVICON_SIZE */]) {
        showSmallFavicon = true;
        smallFaviconStyle = { backgroundImage: `url(${link.favicon})` };
      } else if (link.screenshot) {
        // Don't show a small favicon if there is no screenshot, because that
        // would result in two fallback icons
        showSmallFavicon = true;
        smallFaviconFallback = true;
      }
    }
    let draggableProps = {};
    if (isDraggable) {
      draggableProps = {
        onClick: this.onDragEvent,
        onDragEnd: this.onDragEvent,
        onDragStart: this.onDragEvent,
        onMouseDown: this.onDragEvent
      };
    }
    return __WEBPACK_IMPORTED_MODULE_4_react___default.a.createElement(
      "li",
      _extends({ className: topSiteOuterClassName, onDrop: this.onDragEvent, onDragOver: this.onDragEvent, onDragEnter: this.onDragEvent, onDragLeave: this.onDragEvent }, draggableProps),
      __WEBPACK_IMPORTED_MODULE_4_react___default.a.createElement(
        "div",
        { className: "top-site-inner" },
        __WEBPACK_IMPORTED_MODULE_4_react___default.a.createElement(
          "a",
          { href: link.url, onClick: onClick },
          __WEBPACK_IMPORTED_MODULE_4_react___default.a.createElement(
            "div",
            { className: "tile", "aria-hidden": true, "data-fallback": letterFallback },
            __WEBPACK_IMPORTED_MODULE_4_react___default.a.createElement("div", { className: imageClassName, style: imageStyle }),
            showSmallFavicon && __WEBPACK_IMPORTED_MODULE_4_react___default.a.createElement("div", {
              className: "top-site-icon default-icon",
              "data-fallback": smallFaviconFallback && letterFallback,
              style: smallFaviconStyle })
          ),
          __WEBPACK_IMPORTED_MODULE_4_react___default.a.createElement(
            "div",
            { className: `title ${link.isPinned ? "pinned" : ""}` },
            link.isPinned && __WEBPACK_IMPORTED_MODULE_4_react___default.a.createElement("div", { className: "icon icon-pin-small" }),
            __WEBPACK_IMPORTED_MODULE_4_react___default.a.createElement(
              "span",
              { dir: "auto" },
              title
            )
          )
        ),
        children
      )
    );
  }
}
/* harmony export (immutable) */ __webpack_exports__["a"] = TopSiteLink;

TopSiteLink.defaultProps = {
  title: "",
  link: {},
  isDraggable: true
};

class TopSite extends __WEBPACK_IMPORTED_MODULE_4_react___default.a.PureComponent {
  constructor(props) {
    super(props);
    this.state = { showContextMenu: false };
    this.onLinkClick = this.onLinkClick.bind(this);
    this.onMenuButtonClick = this.onMenuButtonClick.bind(this);
    this.onMenuUpdate = this.onMenuUpdate.bind(this);
  }

  /**
   * Report to telemetry additional information about the item.
   */
  _getTelemetryInfo() {
    const value = { icon_type: this.props.link.iconType };
    // Filter out "not_pinned" type for being the default
    if (this.props.link.isPinned) {
      value.card_type = "pinned";
    }
    return { value };
  }

  userEvent(event) {
    this.props.dispatch(__WEBPACK_IMPORTED_MODULE_0_common_Actions_jsm__["a" /* actionCreators */].UserEvent(Object.assign({
      event,
      source: __WEBPACK_IMPORTED_MODULE_2__TopSitesConstants__["d" /* TOP_SITES_SOURCE */],
      action_position: this.props.index
    }, this._getTelemetryInfo())));
  }

  onLinkClick(ev) {
    this.userEvent("CLICK");
  }

  onMenuButtonClick(event) {
    event.preventDefault();
    this.props.onActivate(this.props.index);
    this.setState({ showContextMenu: true });
  }

  onMenuUpdate(showContextMenu) {
    this.setState({ showContextMenu });
  }

  render() {
    const { props } = this;
    const { link } = props;
    const isContextMenuOpen = this.state.showContextMenu && props.activeIndex === props.index;
    const title = link.label || link.hostname;
    return __WEBPACK_IMPORTED_MODULE_4_react___default.a.createElement(
      TopSiteLink,
      _extends({}, props, { onClick: this.onLinkClick, onDragEvent: this.props.onDragEvent, className: `${props.className || ""}${isContextMenuOpen ? " active" : ""}`, title: title }),
      __WEBPACK_IMPORTED_MODULE_4_react___default.a.createElement(
        "div",
        null,
        __WEBPACK_IMPORTED_MODULE_4_react___default.a.createElement(
          "button",
          { className: "context-menu-button icon", onClick: this.onMenuButtonClick },
          __WEBPACK_IMPORTED_MODULE_4_react___default.a.createElement(
            "span",
            { className: "sr-only" },
            __WEBPACK_IMPORTED_MODULE_4_react___default.a.createElement(__WEBPACK_IMPORTED_MODULE_1_react_intl__["FormattedMessage"], { id: "context_menu_button_sr", values: { title } })
          )
        ),
        isContextMenuOpen && __WEBPACK_IMPORTED_MODULE_4_react___default.a.createElement(__WEBPACK_IMPORTED_MODULE_3_content_src_components_LinkMenu_LinkMenu__["a" /* LinkMenu */], {
          dispatch: props.dispatch,
          index: props.index,
          onUpdate: this.onMenuUpdate,
          options: __WEBPACK_IMPORTED_MODULE_2__TopSitesConstants__["c" /* TOP_SITES_CONTEXT_MENU_OPTIONS */],
          site: link,
          siteInfo: this._getTelemetryInfo(),
          source: __WEBPACK_IMPORTED_MODULE_2__TopSitesConstants__["d" /* TOP_SITES_SOURCE */] })
      )
    );
  }
}
/* unused harmony export TopSite */

TopSite.defaultProps = {
  link: {},
  onActivate() {}
};

class TopSitePlaceholder extends __WEBPACK_IMPORTED_MODULE_4_react___default.a.PureComponent {
  constructor(props) {
    super(props);
    this.onEditButtonClick = this.onEditButtonClick.bind(this);
  }

  onEditButtonClick() {
    this.props.dispatch({ type: __WEBPACK_IMPORTED_MODULE_0_common_Actions_jsm__["b" /* actionTypes */].TOP_SITES_EDIT, data: { index: this.props.index } });
  }

  render() {
    return __WEBPACK_IMPORTED_MODULE_4_react___default.a.createElement(
      TopSiteLink,
      _extends({}, this.props, { className: `placeholder ${this.props.className || ""}`, isDraggable: false }),
      __WEBPACK_IMPORTED_MODULE_4_react___default.a.createElement("button", { className: "context-menu-button edit-button icon",
        title: this.props.intl.formatMessage({ id: "edit_topsites_edit_button" }),
        onClick: this.onEditButtonClick })
    );
  }
}
/* unused harmony export TopSitePlaceholder */


class _TopSiteList extends __WEBPACK_IMPORTED_MODULE_4_react___default.a.PureComponent {
  static get DEFAULT_STATE() {
    return {
      activeIndex: null,
      draggedIndex: null,
      draggedSite: null,
      draggedTitle: null,
      topSitesPreview: null
    };
  }

  constructor(props) {
    super(props);
    this.state = _TopSiteList.DEFAULT_STATE;
    this.onDragEvent = this.onDragEvent.bind(this);
    this.onActivate = this.onActivate.bind(this);
  }

  componentWillReceiveProps(nextProps) {
    if (this.state.draggedSite) {
      const prevTopSites = this.props.TopSites && this.props.TopSites.rows;
      const newTopSites = nextProps.TopSites && nextProps.TopSites.rows;
      if (prevTopSites && prevTopSites[this.state.draggedIndex] && prevTopSites[this.state.draggedIndex].url === this.state.draggedSite.url && (!newTopSites[this.state.draggedIndex] || newTopSites[this.state.draggedIndex].url !== this.state.draggedSite.url)) {
        // We got the new order from the redux store via props. We can clear state now.
        this.setState(_TopSiteList.DEFAULT_STATE);
      }
    }
  }

  userEvent(event, index) {
    this.props.dispatch(__WEBPACK_IMPORTED_MODULE_0_common_Actions_jsm__["a" /* actionCreators */].UserEvent({
      event,
      source: __WEBPACK_IMPORTED_MODULE_2__TopSitesConstants__["d" /* TOP_SITES_SOURCE */],
      action_position: index
    }));
  }

  onDragEvent(event, index, link, title) {
    switch (event.type) {
      case "dragstart":
        this.dropped = false;
        this.setState({
          draggedIndex: index,
          draggedSite: link,
          draggedTitle: title,
          activeIndex: null
        });
        this.userEvent("DRAG", index);
        break;
      case "dragend":
        if (!this.dropped) {
          // If there was no drop event, reset the state to the default.
          this.setState(_TopSiteList.DEFAULT_STATE);
        }
        break;
      case "dragenter":
        if (index === this.state.draggedIndex) {
          this.setState({ topSitesPreview: null });
        } else {
          this.setState({ topSitesPreview: this._makeTopSitesPreview(index) });
        }
        break;
      case "drop":
        if (index !== this.state.draggedIndex) {
          this.dropped = true;
          this.props.dispatch(__WEBPACK_IMPORTED_MODULE_0_common_Actions_jsm__["a" /* actionCreators */].AlsoToMain({
            type: __WEBPACK_IMPORTED_MODULE_0_common_Actions_jsm__["b" /* actionTypes */].TOP_SITES_INSERT,
            data: { site: { url: this.state.draggedSite.url, label: this.state.draggedTitle }, index, draggedFromIndex: this.state.draggedIndex }
          }));
          this.userEvent("DROP", index);
        }
        break;
    }
  }

  _getTopSites() {
    // Make a copy of the sites to truncate or extend to desired length
    let topSites = this.props.TopSites.rows.slice();
    topSites.length = this.props.TopSitesRows * __WEBPACK_IMPORTED_MODULE_5_common_Reducers_jsm__["a" /* TOP_SITES_MAX_SITES_PER_ROW */];
    return topSites;
  }

  /**
   * Make a preview of the topsites that will be the result of dropping the currently
   * dragged site at the specified index.
   */
  _makeTopSitesPreview(index) {
    const topSites = this._getTopSites();
    topSites[this.state.draggedIndex] = null;
    const pinnedOnly = topSites.map(site => site && site.isPinned ? site : null);
    const unpinned = topSites.filter(site => site && !site.isPinned);
    const siteToInsert = Object.assign({}, this.state.draggedSite, { isPinned: true, isDragged: true });
    if (!pinnedOnly[index]) {
      pinnedOnly[index] = siteToInsert;
    } else {
      // Find the hole to shift the pinned site(s) towards. We shift towards the
      // hole left by the site being dragged.
      let holeIndex = index;
      const indexStep = index > this.state.draggedIndex ? -1 : 1;
      while (pinnedOnly[holeIndex]) {
        holeIndex += indexStep;
      }

      // Shift towards the hole.
      const shiftingStep = index > this.state.draggedIndex ? 1 : -1;
      while (holeIndex !== index) {
        const nextIndex = holeIndex + shiftingStep;
        pinnedOnly[holeIndex] = pinnedOnly[nextIndex];
        holeIndex = nextIndex;
      }
      pinnedOnly[index] = siteToInsert;
    }

    // Fill in the remaining holes with unpinned sites.
    const preview = pinnedOnly;
    for (let i = 0; i < preview.length; i++) {
      if (!preview[i]) {
        preview[i] = unpinned.shift() || null;
      }
    }

    return preview;
  }

  onActivate(index) {
    this.setState({ activeIndex: index });
  }

  render() {
    const { props } = this;
    const topSites = this.state.topSitesPreview || this._getTopSites();
    const topSitesUI = [];
    const commonProps = {
      onDragEvent: this.onDragEvent,
      dispatch: props.dispatch,
      intl: props.intl
    };
    // We assign a key to each placeholder slot. We need it to be independent
    // of the slot index (i below) so that the keys used stay the same during
    // drag and drop reordering and the underlying DOM nodes are reused.
    // This mostly (only?) affects linux so be sure to test on linux before changing.
    let holeIndex = 0;

    // On narrow viewports, we only show 6 sites per row. We'll mark the rest as
    // .hide-for-narrow to hide in CSS via @media query.
    const maxNarrowVisibleIndex = props.TopSitesRows * 6;

    for (let i = 0, l = topSites.length; i < l; i++) {
      const link = topSites[i] && Object.assign({}, topSites[i], { iconType: this.props.topSiteIconType(topSites[i]) });
      const slotProps = {
        key: link ? link.url : holeIndex++,
        index: i
      };
      if (i >= maxNarrowVisibleIndex) {
        slotProps.className = "hide-for-narrow";
      }
      topSitesUI.push(!link ? __WEBPACK_IMPORTED_MODULE_4_react___default.a.createElement(TopSitePlaceholder, _extends({}, slotProps, commonProps)) : __WEBPACK_IMPORTED_MODULE_4_react___default.a.createElement(TopSite, _extends({
        link: link,
        activeIndex: this.state.activeIndex,
        onActivate: this.onActivate
      }, slotProps, commonProps)));
    }
    return __WEBPACK_IMPORTED_MODULE_4_react___default.a.createElement(
      "ul",
      { className: `top-sites-list${this.state.draggedSite ? " dnd-active" : ""}` },
      topSitesUI
    );
  }
}
/* unused harmony export _TopSiteList */


const TopSiteList = Object(__WEBPACK_IMPORTED_MODULE_1_react_intl__["injectIntl"])(_TopSiteList);
/* harmony export (immutable) */ __webpack_exports__["b"] = TopSiteList;


/***/ }),
/* 14 */
/***/ (function(module, __webpack_exports__, __webpack_require__) {

"use strict";
Object.defineProperty(__webpack_exports__, "__esModule", { value: true });
/* WEBPACK VAR INJECTION */(function(global) {/* harmony import */ var __WEBPACK_IMPORTED_MODULE_0_common_Actions_jsm__ = __webpack_require__(0);
/* harmony import */ var __WEBPACK_IMPORTED_MODULE_1_content_src_lib_snippets__ = __webpack_require__(15);
/* harmony import */ var __WEBPACK_IMPORTED_MODULE_2_content_src_components_Base_Base__ = __webpack_require__(16);
/* harmony import */ var __WEBPACK_IMPORTED_MODULE_3_content_src_lib_detect_user_session_start__ = __webpack_require__(24);
/* harmony import */ var __WEBPACK_IMPORTED_MODULE_4_content_src_lib_init_store__ = __webpack_require__(25);
/* harmony import */ var __WEBPACK_IMPORTED_MODULE_5_react_redux__ = __webpack_require__(4);
/* harmony import */ var __WEBPACK_IMPORTED_MODULE_5_react_redux___default = __webpack_require__.n(__WEBPACK_IMPORTED_MODULE_5_react_redux__);
/* harmony import */ var __WEBPACK_IMPORTED_MODULE_6_react__ = __webpack_require__(1);
/* harmony import */ var __WEBPACK_IMPORTED_MODULE_6_react___default = __webpack_require__.n(__WEBPACK_IMPORTED_MODULE_6_react__);
/* harmony import */ var __WEBPACK_IMPORTED_MODULE_7_react_dom__ = __webpack_require__(27);
/* harmony import */ var __WEBPACK_IMPORTED_MODULE_7_react_dom___default = __webpack_require__.n(__WEBPACK_IMPORTED_MODULE_7_react_dom__);
/* harmony import */ var __WEBPACK_IMPORTED_MODULE_8_common_Reducers_jsm__ = __webpack_require__(6);










const store = Object(__WEBPACK_IMPORTED_MODULE_4_content_src_lib_init_store__["a" /* initStore */])(__WEBPACK_IMPORTED_MODULE_8_common_Reducers_jsm__["b" /* reducers */], global.gActivityStreamPrerenderedState);

new __WEBPACK_IMPORTED_MODULE_3_content_src_lib_detect_user_session_start__["a" /* DetectUserSessionStart */](store).sendEventOrAddListener();

// If we are starting in a prerendered state, we must wait until the first render
// to request state rehydration (see Base.jsx). If we are NOT in a prerendered state,
// we can request it immedately.
if (!global.gActivityStreamPrerenderedState) {
  store.dispatch(__WEBPACK_IMPORTED_MODULE_0_common_Actions_jsm__["a" /* actionCreators */].AlsoToMain({ type: __WEBPACK_IMPORTED_MODULE_0_common_Actions_jsm__["b" /* actionTypes */].NEW_TAB_STATE_REQUEST }));
}

__WEBPACK_IMPORTED_MODULE_7_react_dom___default.a.hydrate(__WEBPACK_IMPORTED_MODULE_6_react___default.a.createElement(
  __WEBPACK_IMPORTED_MODULE_5_react_redux__["Provider"],
  { store: store },
  __WEBPACK_IMPORTED_MODULE_6_react___default.a.createElement(__WEBPACK_IMPORTED_MODULE_2_content_src_components_Base_Base__["a" /* Base */], {
    isPrerendered: !!global.gActivityStreamPrerenderedState,
    locale: global.document.documentElement.lang,
    strings: global.gActivityStreamStrings })
), document.getElementById("root"));

Object(__WEBPACK_IMPORTED_MODULE_1_content_src_lib_snippets__["a" /* addSnippetsSubscriber */])(store);
/* WEBPACK VAR INJECTION */}.call(__webpack_exports__, __webpack_require__(3)))

/***/ }),
/* 15 */
/***/ (function(module, __webpack_exports__, __webpack_require__) {

"use strict";
/* WEBPACK VAR INJECTION */(function(global) {/* harmony export (immutable) */ __webpack_exports__["a"] = addSnippetsSubscriber;
/* harmony import */ var __WEBPACK_IMPORTED_MODULE_0_common_Actions_jsm__ = __webpack_require__(0);
const DATABASE_NAME = "snippets_db";
const DATABASE_VERSION = 1;
const SNIPPETS_OBJECTSTORE_NAME = "snippets";
const SNIPPETS_UPDATE_INTERVAL_MS = 14400000;
/* unused harmony export SNIPPETS_UPDATE_INTERVAL_MS */
 // 4 hours.

const SNIPPETS_ENABLED_EVENT = "Snippets:Enabled";
const SNIPPETS_DISABLED_EVENT = "Snippets:Disabled";



/**
 * SnippetsMap - A utility for cacheing values related to the snippet. It has
 *               the same interface as a Map, but is optionally backed by
 *               indexedDB for persistent storage.
 *               Call .connect() to open a database connection and restore any
 *               previously cached data, if necessary.
 *
 */
class SnippetsMap extends Map {
  constructor(dispatch) {
    super();
    this._db = null;
    this._dispatch = dispatch;
  }

  set(key, value) {
    super.set(key, value);
    return this._dbTransaction(db => db.put(value, key));
  }

  delete(key) {
    super.delete(key);
    return this._dbTransaction(db => db.delete(key));
  }

  clear() {
    super.clear();
    this._dispatch(__WEBPACK_IMPORTED_MODULE_0_common_Actions_jsm__["a" /* actionCreators */].OnlyToMain({ type: __WEBPACK_IMPORTED_MODULE_0_common_Actions_jsm__["b" /* actionTypes */].SNIPPETS_BLOCKLIST_CLEARED }));
    return this._dbTransaction(db => db.clear());
  }

  get blockList() {
    return this.get("blockList") || [];
  }

  /**
   * blockSnippetById - Blocks a snippet given an id
   *
   * @param  {str|int} id   The id of the snippet
   * @return {Promise}      Resolves when the id has been written to indexedDB,
   *                        or immediately if the snippetMap is not connected
   */
  async blockSnippetById(id) {
    if (!id) {
      return;
    }
    const { blockList } = this;
    if (!blockList.includes(id)) {
      blockList.push(id);
      this._dispatch(__WEBPACK_IMPORTED_MODULE_0_common_Actions_jsm__["a" /* actionCreators */].AlsoToMain({ type: __WEBPACK_IMPORTED_MODULE_0_common_Actions_jsm__["b" /* actionTypes */].SNIPPETS_BLOCKLIST_UPDATED, data: id }));
      await this.set("blockList", blockList);
    }
  }

  disableOnboarding() {
    this._dispatch(__WEBPACK_IMPORTED_MODULE_0_common_Actions_jsm__["a" /* actionCreators */].AlsoToMain({ type: __WEBPACK_IMPORTED_MODULE_0_common_Actions_jsm__["b" /* actionTypes */].DISABLE_ONBOARDING }));
  }

  showFirefoxAccounts() {
    this._dispatch(__WEBPACK_IMPORTED_MODULE_0_common_Actions_jsm__["a" /* actionCreators */].AlsoToMain({ type: __WEBPACK_IMPORTED_MODULE_0_common_Actions_jsm__["b" /* actionTypes */].SHOW_FIREFOX_ACCOUNTS }));
  }

  getTotalBookmarksCount() {
    return new Promise(resolve => {
      this._dispatch(__WEBPACK_IMPORTED_MODULE_0_common_Actions_jsm__["a" /* actionCreators */].OnlyToMain({ type: __WEBPACK_IMPORTED_MODULE_0_common_Actions_jsm__["b" /* actionTypes */].TOTAL_BOOKMARKS_REQUEST }));
      global.addMessageListener("ActivityStream:MainToContent", function onMessage({ data: action }) {
        if (action.type === __WEBPACK_IMPORTED_MODULE_0_common_Actions_jsm__["b" /* actionTypes */].TOTAL_BOOKMARKS_RESPONSE) {
          resolve(action.data);
          global.removeMessageListener("ActivityStream:MainToContent", onMessage);
        }
      });
    });
  }

  /**
   * connect - Attaches an indexedDB back-end to the Map so that any set values
   *           are also cached in a store. It also restores any existing values
   *           that are already stored in the indexedDB store.
   *
   * @return {type}  description
   */
  async connect() {
    // Open the connection
    const db = await this._openDB();

    // Restore any existing values
    await this._restoreFromDb(db);

    // Attach a reference to the db
    this._db = db;
  }

  /**
   * _dbTransaction - Returns a db transaction wrapped with the given modifier
   *                  function as a Promise. If the db has not been connected,
   *                  it resolves immediately.
   *
   * @param  {func} modifier A function to call with the transaction
   * @return {obj}           A Promise that resolves when the transaction has
   *                         completed or errored
   */
  _dbTransaction(modifier) {
    if (!this._db) {
      return Promise.resolve();
    }
    return new Promise((resolve, reject) => {
      const transaction = modifier(this._db.transaction(SNIPPETS_OBJECTSTORE_NAME, "readwrite").objectStore(SNIPPETS_OBJECTSTORE_NAME));
      transaction.onsuccess = event => resolve();

      /* istanbul ignore next */
      transaction.onerror = event => reject(transaction.error);
    });
  }

  _openDB() {
    return new Promise((resolve, reject) => {
      const openRequest = indexedDB.open(DATABASE_NAME, DATABASE_VERSION);

      /* istanbul ignore next */
      openRequest.onerror = event => {
        // Try to delete the old database so that we can start this process over
        // next time.
        indexedDB.deleteDatabase(DATABASE_NAME);
        reject(event);
      };

      openRequest.onupgradeneeded = event => {
        const db = event.target.result;
        if (!db.objectStoreNames.contains(SNIPPETS_OBJECTSTORE_NAME)) {
          db.createObjectStore(SNIPPETS_OBJECTSTORE_NAME);
        }
      };

      openRequest.onsuccess = event => {
        let db = event.target.result;

        /* istanbul ignore next */
        db.onerror = err => console.error(err); // eslint-disable-line no-console
        /* istanbul ignore next */
        db.onversionchange = versionChangeEvent => versionChangeEvent.target.close();

        resolve(db);
      };
    });
  }

  _restoreFromDb(db) {
    return new Promise((resolve, reject) => {
      let cursorRequest;
      try {
        cursorRequest = db.transaction(SNIPPETS_OBJECTSTORE_NAME).objectStore(SNIPPETS_OBJECTSTORE_NAME).openCursor();
      } catch (err) {
        // istanbul ignore next
        reject(err);
        // istanbul ignore next
        return;
      }

      /* istanbul ignore next */
      cursorRequest.onerror = event => reject(event);

      cursorRequest.onsuccess = event => {
        let cursor = event.target.result;
        // Populate the cache from the persistent storage.
        if (cursor) {
          if (cursor.value !== "blockList") {
            this.set(cursor.key, cursor.value);
          }
          cursor.continue();
        } else {
          // We are done.
          resolve();
        }
      };
    });
  }
}
/* unused harmony export SnippetsMap */


/**
 * SnippetsProvider - Initializes a SnippetsMap and loads snippets from a
 *                    remote location, or else default snippets if the remote
 *                    snippets cannot be retrieved.
 */
class SnippetsProvider {
  constructor(dispatch) {
    // Initialize the Snippets Map and attaches it to a global so that
    // the snippet payload can interact with it.
    global.gSnippetsMap = new SnippetsMap(dispatch);
    this._onAction = this._onAction.bind(this);
  }

  get snippetsMap() {
    return global.gSnippetsMap;
  }

  async _refreshSnippets() {
    // Check if the cached version of of the snippets in snippetsMap. If it's too
    // old, blow away the entire snippetsMap.
    const cachedVersion = this.snippetsMap.get("snippets-cached-version");

    if (cachedVersion !== this.appData.version) {
      this.snippetsMap.clear();
    }

    // Has enough time passed for us to require an update?
    const lastUpdate = this.snippetsMap.get("snippets-last-update");
    const needsUpdate = !(lastUpdate >= 0) || Date.now() - lastUpdate > SNIPPETS_UPDATE_INTERVAL_MS;

    if (needsUpdate && this.appData.snippetsURL) {
      this.snippetsMap.set("snippets-last-update", Date.now());
      try {
        const response = await fetch(this.appData.snippetsURL);
        if (response.status === 200) {
          const payload = await response.text();

          this.snippetsMap.set("snippets", payload);
          this.snippetsMap.set("snippets-cached-version", this.appData.version);
        }
      } catch (e) {
        console.error(e); // eslint-disable-line no-console
      }
    }
  }

  _noSnippetFallback() {
    // TODO
  }

  _forceOnboardingVisibility(shouldBeVisible) {
    const onboardingEl = document.getElementById("onboarding-notification-bar");

    if (onboardingEl) {
      onboardingEl.style.display = shouldBeVisible ? "" : "none";
    }
  }

  _showRemoteSnippets() {
    const snippetsEl = document.getElementById(this.elementId);
    const payload = this.snippetsMap.get("snippets");

    if (!snippetsEl) {
      throw new Error(`No element was found with id '${this.elementId}'.`);
    }

    // This could happen if fetching failed
    if (!payload) {
      throw new Error("No remote snippets were found in gSnippetsMap.");
    }

    if (typeof payload !== "string") {
      throw new Error("Snippet payload was incorrectly formatted");
    }

    // Note that injecting snippets can throw if they're invalid XML.
    // eslint-disable-next-line no-unsanitized/property
    snippetsEl.innerHTML = payload;

    // Scripts injected by innerHTML are inactive, so we have to relocate them
    // through DOM manipulation to activate their contents.
    for (const scriptEl of snippetsEl.getElementsByTagName("script")) {
      const relocatedScript = document.createElement("script");
      relocatedScript.text = scriptEl.text;
      scriptEl.parentNode.replaceChild(relocatedScript, scriptEl);
    }
  }

  _onAction(msg) {
    if (msg.data.type === __WEBPACK_IMPORTED_MODULE_0_common_Actions_jsm__["b" /* actionTypes */].SNIPPET_BLOCKED) {
      if (!this.snippetsMap.blockList.includes(msg.data.data)) {
        this.snippetsMap.set("blockList", this.snippetsMap.blockList.concat(msg.data.data));
        document.getElementById("snippets-container").style.display = "none";
      }
    }
  }

  /**
   * init - Fetch the snippet payload and show snippets
   *
   * @param  {obj} options
   * @param  {str} options.appData.snippetsURL  The URL from which we fetch snippets
   * @param  {int} options.appData.version  The current snippets version
   * @param  {str} options.elementId  The id of the element in which to inject snippets
   * @param  {bool} options.connect  Should gSnippetsMap connect to indexedDB?
   */
  async init(options) {
    Object.assign(this, {
      appData: {},
      elementId: "snippets",
      connect: true
    }, options);

    // Add listener so we know when snippets are blocked on other pages
    if (global.addMessageListener) {
      global.addMessageListener("ActivityStream:MainToContent", this._onAction);
    }

    // TODO: Requires enabling indexedDB on newtab
    // Restore the snippets map from indexedDB
    if (this.connect) {
      try {
        await this.snippetsMap.connect();
      } catch (e) {
        console.error(e); // eslint-disable-line no-console
      }
    }

    // Cache app data values so they can be accessible from gSnippetsMap
    for (const key of Object.keys(this.appData)) {
      if (key === "blockList") {
        this.snippetsMap.set("blockList", this.appData[key]);
      } else {
        this.snippetsMap.set(`appData.${key}`, this.appData[key]);
      }
    }

    // Refresh snippets, if enough time has passed.
    await this._refreshSnippets();

    // Try showing remote snippets, falling back to defaults if necessary.
    try {
      this._showRemoteSnippets();
    } catch (e) {
      this._noSnippetFallback(e);
    }

    window.dispatchEvent(new Event(SNIPPETS_ENABLED_EVENT));

    this._forceOnboardingVisibility(true);
    this.initialized = true;
  }

  uninit() {
    window.dispatchEvent(new Event(SNIPPETS_DISABLED_EVENT));
    this._forceOnboardingVisibility(false);
    if (global.removeMessageListener) {
      global.removeMessageListener("ActivityStream:MainToContent", this._onAction);
    }
    this.initialized = false;
  }
}
/* unused harmony export SnippetsProvider */


/**
 * addSnippetsSubscriber - Creates a SnippetsProvider that Initializes
 *                         when the store has received the appropriate
 *                         Snippet data.
 *
 * @param  {obj} store   The redux store
 * @return {obj}         Returns the snippets instance and unsubscribe function
 */
function addSnippetsSubscriber(store) {
  const snippets = new SnippetsProvider(store.dispatch);

  let initializing = false;

  store.subscribe(async () => {
    const state = store.getState();
    // state.Prefs.values["feeds.snippets"]:  Should snippets be shown?
    // state.Snippets.initialized             Is the snippets data initialized?
    // snippets.initialized:                  Is SnippetsProvider currently initialised?
    if (state.Prefs.values["feeds.snippets"] && !state.Prefs.values.disableSnippets && state.Snippets.initialized && !snippets.initialized &&
    // Don't call init multiple times
    !initializing) {
      initializing = true;
      await snippets.init({ appData: state.Snippets });
      initializing = false;
    } else if ((state.Prefs.values["feeds.snippets"] === false || state.Prefs.values.disableSnippets === true) && snippets.initialized) {
      snippets.uninit();
    }
  });

  // These values are returned for testing purposes
  return snippets;
}
/* WEBPACK VAR INJECTION */}.call(__webpack_exports__, __webpack_require__(3)))

/***/ }),
/* 16 */
/***/ (function(module, __webpack_exports__, __webpack_require__) {

"use strict";

// EXTERNAL MODULE: ./system-addon/common/Actions.jsm
var Actions = __webpack_require__(0);

// EXTERNAL MODULE: external "ReactIntl"
var external__ReactIntl_ = __webpack_require__(2);
var external__ReactIntl__default = /*#__PURE__*/__webpack_require__.n(external__ReactIntl_);

// EXTERNAL MODULE: external "ReactRedux"
var external__ReactRedux_ = __webpack_require__(4);
var external__ReactRedux__default = /*#__PURE__*/__webpack_require__.n(external__ReactRedux_);

// EXTERNAL MODULE: external "React"
var external__React_ = __webpack_require__(1);
var external__React__default = /*#__PURE__*/__webpack_require__.n(external__React_);

// CONCATENATED MODULE: ./system-addon/content-src/components/ConfirmDialog/ConfirmDialog.jsx





/**
 * ConfirmDialog component.
 * One primary action button, one cancel button.
 *
 * Content displayed is controlled by `data` prop the component receives.
 * Example:
 * data: {
 *   // Any sort of data needed to be passed around by actions.
 *   payload: site.url,
 *   // Primary button AlsoToMain action.
 *   action: "DELETE_HISTORY_URL",
 *   // Primary button USerEvent action.
 *   userEvent: "DELETE",
 *   // Array of locale ids to display.
 *   message_body: ["confirm_history_delete_p1", "confirm_history_delete_notice_p2"],
 *   // Text for primary button.
 *   confirm_button_string_id: "menu_action_delete"
 * },
 */
class ConfirmDialog__ConfirmDialog extends external__React__default.a.PureComponent {
  constructor(props) {
    super(props);
    this._handleCancelBtn = this._handleCancelBtn.bind(this);
    this._handleConfirmBtn = this._handleConfirmBtn.bind(this);
  }

  _handleCancelBtn() {
    this.props.dispatch({ type: Actions["b" /* actionTypes */].DIALOG_CANCEL });
    this.props.dispatch(Actions["a" /* actionCreators */].UserEvent({ event: Actions["b" /* actionTypes */].DIALOG_CANCEL, source: this.props.data.eventSource }));
  }

  _handleConfirmBtn() {
    this.props.data.onConfirm.forEach(this.props.dispatch);
  }

  _renderModalMessage() {
    const message_body = this.props.data.body_string_id;

    if (!message_body) {
      return null;
    }

    return external__React__default.a.createElement(
      "span",
      null,
      message_body.map(msg => external__React__default.a.createElement(
        "p",
        { key: msg },
        external__React__default.a.createElement(external__ReactIntl_["FormattedMessage"], { id: msg })
      ))
    );
  }

  render() {
    if (!this.props.visible) {
      return null;
    }

    return external__React__default.a.createElement(
      "div",
      { className: "confirmation-dialog" },
      external__React__default.a.createElement("div", { className: "modal-overlay", onClick: this._handleCancelBtn }),
      external__React__default.a.createElement(
        "div",
        { className: "modal" },
        external__React__default.a.createElement(
          "section",
          { className: "modal-message" },
          this.props.data.icon && external__React__default.a.createElement("span", { className: `icon icon-spacer icon-${this.props.data.icon}` }),
          this._renderModalMessage()
        ),
        external__React__default.a.createElement(
          "section",
          { className: "actions" },
          external__React__default.a.createElement(
            "button",
            { onClick: this._handleCancelBtn },
            external__React__default.a.createElement(external__ReactIntl_["FormattedMessage"], { id: this.props.data.cancel_button_string_id })
          ),
          external__React__default.a.createElement(
            "button",
            { className: "done", onClick: this._handleConfirmBtn },
            external__React__default.a.createElement(external__ReactIntl_["FormattedMessage"], { id: this.props.data.confirm_button_string_id })
          )
        )
      )
    );
  }
}

const ConfirmDialog = Object(external__ReactRedux_["connect"])(state => state.Dialog)(ConfirmDialog__ConfirmDialog);
// EXTERNAL MODULE: ./system-addon/content-src/components/ErrorBoundary/ErrorBoundary.jsx
var ErrorBoundary = __webpack_require__(7);

// CONCATENATED MODULE: ./system-addon/content-src/components/ManualMigration/ManualMigration.jsx





/**
 * Manual migration component used to start the profile import wizard.
 * Message is presented temporarily and will go away if:
 * 1.  User clicks "No Thanks"
 * 2.  User completed the data import
 * 3.  After 3 active days
 * 4.  User clicks "Cancel" on the import wizard (currently not implemented).
 */
class ManualMigration__ManualMigration extends external__React__default.a.PureComponent {
  constructor(props) {
    super(props);
    this.onLaunchTour = this.onLaunchTour.bind(this);
    this.onCancelTour = this.onCancelTour.bind(this);
  }

  onLaunchTour() {
    this.props.dispatch(Actions["a" /* actionCreators */].AlsoToMain({ type: Actions["b" /* actionTypes */].MIGRATION_START }));
    this.props.dispatch(Actions["a" /* actionCreators */].UserEvent({ event: Actions["b" /* actionTypes */].MIGRATION_START }));
  }

  onCancelTour() {
    this.props.dispatch(Actions["a" /* actionCreators */].AlsoToMain({ type: Actions["b" /* actionTypes */].MIGRATION_CANCEL }));
    this.props.dispatch(Actions["a" /* actionCreators */].UserEvent({ event: Actions["b" /* actionTypes */].MIGRATION_CANCEL }));
  }

  render() {
    return external__React__default.a.createElement(
      "div",
      { className: "manual-migration-container" },
      external__React__default.a.createElement(
        "p",
        null,
        external__React__default.a.createElement("span", { className: "icon icon-import" }),
        external__React__default.a.createElement(external__ReactIntl_["FormattedMessage"], { id: "manual_migration_explanation2" })
      ),
      external__React__default.a.createElement(
        "div",
        { className: "manual-migration-actions actions" },
        external__React__default.a.createElement(
          "button",
          { className: "dismiss", onClick: this.onCancelTour },
          external__React__default.a.createElement(external__ReactIntl_["FormattedMessage"], { id: "manual_migration_cancel_button" })
        ),
        external__React__default.a.createElement(
          "button",
          { onClick: this.onLaunchTour },
          external__React__default.a.createElement(external__ReactIntl_["FormattedMessage"], { id: "manual_migration_import_button" })
        )
      )
    );
  }
}

const ManualMigration = Object(external__ReactRedux_["connect"])()(ManualMigration__ManualMigration);
// CONCATENATED MODULE: ./system-addon/content-src/components/PreferencesPane/PreferencesPane.jsx





const getFormattedMessage = message => typeof message === "string" ? external__React__default.a.createElement(
  "span",
  null,
  message
) : external__React__default.a.createElement(external__ReactIntl_["FormattedMessage"], message);

const PreferencesInput = props => external__React__default.a.createElement(
  "section",
  null,
  external__React__default.a.createElement("input", { type: "checkbox", id: props.prefName, name: props.prefName, checked: props.value, disabled: props.disabled, onChange: props.onChange, className: props.className }),
  external__React__default.a.createElement(
    "label",
    { htmlFor: props.prefName, className: props.labelClassName },
    getFormattedMessage(props.titleString)
  ),
  props.descString && external__React__default.a.createElement(
    "p",
    { className: "prefs-input-description" },
    getFormattedMessage(props.descString)
  ),
  external__React__default.a.Children.map(props.children, child => external__React__default.a.createElement(
    "div",
    { className: `options${child.props.disabled ? " disabled" : ""}` },
    child
  ))
);

class PreferencesPane__PreferencesPane extends external__React__default.a.PureComponent {
  constructor(props) {
    super(props);
    this.handleClickOutside = this.handleClickOutside.bind(this);
    this.handlePrefChange = this.handlePrefChange.bind(this);
    this.handleSectionChange = this.handleSectionChange.bind(this);
    this.togglePane = this.togglePane.bind(this);
    this.onWrapperMount = this.onWrapperMount.bind(this);
  }

  componentDidUpdate(prevProps, prevState) {
    if (prevProps.PreferencesPane.visible !== this.props.PreferencesPane.visible) {
      // While the sidebar is open, listen for all document clicks.
      if (this.isSidebarOpen()) {
        document.addEventListener("click", this.handleClickOutside);
      } else {
        document.removeEventListener("click", this.handleClickOutside);
      }
    }
  }

  isSidebarOpen() {
    return this.props.PreferencesPane.visible;
  }

  handleClickOutside(event) {
    // if we are showing the sidebar and there is a click outside, close it.
    if (this.isSidebarOpen() && !this.wrapper.contains(event.target)) {
      this.togglePane();
    }
  }

  handlePrefChange({ target: { name, checked } }) {
    let value = checked;
    if (name === "topSitesRows") {
      value = checked ? 2 : 1;
    }
    this.props.dispatch(Actions["a" /* actionCreators */].SetPref(name, value));
  }

  handleSectionChange({ target }) {
    const id = target.name;
    const type = target.checked ? Actions["b" /* actionTypes */].SECTION_ENABLE : Actions["b" /* actionTypes */].SECTION_DISABLE;
    this.props.dispatch(Actions["a" /* actionCreators */].AlsoToMain({ type, data: id }));
  }

  togglePane() {
    if (this.isSidebarOpen()) {
      this.props.dispatch({ type: Actions["b" /* actionTypes */].SETTINGS_CLOSE });
      this.props.dispatch(Actions["a" /* actionCreators */].UserEvent({ event: "CLOSE_NEWTAB_PREFS" }));
    } else {
      this.props.dispatch({ type: Actions["b" /* actionTypes */].SETTINGS_OPEN });
      this.props.dispatch(Actions["a" /* actionCreators */].UserEvent({ event: "OPEN_NEWTAB_PREFS" }));
    }
  }

  onWrapperMount(wrapper) {
    this.wrapper = wrapper;
  }

  render() {
    const { props } = this;
    const prefs = props.Prefs.values;
    const sections = props.Sections;
    const isVisible = this.isSidebarOpen();
    return external__React__default.a.createElement(
      "div",
      { className: "prefs-pane-wrapper", ref: this.onWrapperMount },
      external__React__default.a.createElement(
        "div",
        { className: "prefs-pane-button" },
        external__React__default.a.createElement("button", {
          className: `prefs-button icon ${isVisible ? "icon-dismiss" : "icon-settings"}`,
          title: props.intl.formatMessage({ id: isVisible ? "settings_pane_done_button" : "settings_pane_button_label" }),
          onClick: this.togglePane })
      ),
      external__React__default.a.createElement(
        "div",
        { className: "prefs-pane" },
        external__React__default.a.createElement(
          "div",
          { className: `sidebar ${isVisible ? "" : "hidden"}` },
          external__React__default.a.createElement(
            "div",
            { className: "prefs-modal-inner-wrapper" },
            external__React__default.a.createElement(
              "h1",
              null,
              external__React__default.a.createElement(external__ReactIntl_["FormattedMessage"], { id: "settings_pane_header" })
            ),
            external__React__default.a.createElement(
              "p",
              null,
              external__React__default.a.createElement(external__ReactIntl_["FormattedMessage"], { id: "settings_pane_body2" })
            ),
            external__React__default.a.createElement(PreferencesInput, {
              className: "showSearch",
              prefName: "showSearch",
              value: prefs.showSearch,
              onChange: this.handlePrefChange,
              titleString: { id: "settings_pane_search_header" },
              descString: { id: "settings_pane_search_body" } }),
            external__React__default.a.createElement("hr", null),
            external__React__default.a.createElement(
              PreferencesInput,
              {
                className: "showTopSites",
                prefName: "showTopSites",
                value: prefs.showTopSites,
                onChange: this.handlePrefChange,
                titleString: { id: "settings_pane_topsites_header" },
                descString: { id: "settings_pane_topsites_body" } },
              external__React__default.a.createElement(PreferencesInput, {
                className: "showMoreTopSites",
                prefName: "topSitesRows",
                disabled: !prefs.showTopSites,
                value: prefs.topSitesRows === 2,
                onChange: this.handlePrefChange,
                titleString: { id: "settings_pane_topsites_options_showmore" },
                labelClassName: "icon icon-topsites" })
            ),
            sections.filter(section => !section.shouldHidePref).map(({ id, title, enabled, pref }) => external__React__default.a.createElement(
              PreferencesInput,
              {
                key: id,
                className: "showSection",
                prefName: pref && pref.feed || id,
                value: enabled,
                onChange: pref && pref.feed ? this.handlePrefChange : this.handleSectionChange,
                titleString: pref && pref.titleString || title,
                descString: pref && pref.descString },
              pref && pref.nestedPrefs && pref.nestedPrefs.map(nestedPref => external__React__default.a.createElement(PreferencesInput, {
                key: nestedPref.name,
                prefName: nestedPref.name,
                disabled: !enabled,
                value: prefs[nestedPref.name],
                onChange: this.handlePrefChange,
                titleString: nestedPref.titleString,
                labelClassName: `icon ${nestedPref.icon}` }))
            )),
            !prefs.disableSnippets && external__React__default.a.createElement("hr", null),
            !prefs.disableSnippets && external__React__default.a.createElement(PreferencesInput, { className: "showSnippets", prefName: "feeds.snippets",
              value: prefs["feeds.snippets"], onChange: this.handlePrefChange,
              titleString: { id: "settings_pane_snippets_header" },
              descString: { id: "settings_pane_snippets_body" } })
          ),
          external__React__default.a.createElement(
            "section",
            { className: "actions" },
            external__React__default.a.createElement(
              "button",
              { className: "done", onClick: this.togglePane },
              external__React__default.a.createElement(external__ReactIntl_["FormattedMessage"], { id: "settings_pane_done_button" })
            )
          )
        )
      )
    );
  }
}

const PreferencesPane = Object(external__ReactRedux_["connect"])(state => ({
  Prefs: state.Prefs,
  PreferencesPane: state.PreferencesPane,
  Sections: state.Sections
}))(Object(external__ReactIntl_["injectIntl"])(PreferencesPane__PreferencesPane));
// CONCATENATED MODULE: ./system-addon/common/PrerenderData.jsm
class _PrerenderData {
  constructor(options) {
    this.initialPrefs = options.initialPrefs;
    this.initialSections = options.initialSections;
    this._setValidation(options.validation);
  }

  get validation() {
    return this._validation;
  }

  set validation(value) {
    this._setValidation(value);
  }

  get invalidatingPrefs() {
    return this._invalidatingPrefs;
  }

  // This is needed so we can use it in the constructor
  _setValidation(value = []) {
    this._validation = value;
    this._invalidatingPrefs = value.reduce((result, next) => {
      if (typeof next === "string") {
        result.push(next);
        return result;
      } else if (next && next.oneOf) {
        return result.concat(next.oneOf);
      }
      throw new Error("Your validation configuration is not properly configured");
    }, []);
  }

  arePrefsValid(getPref) {
    for (const prefs of this.validation) {
      // {oneOf: ["foo", "bar"]}
      if (prefs && prefs.oneOf && !prefs.oneOf.some(name => getPref(name) === this.initialPrefs[name])) {
        return false;

        // "foo"
      } else if (getPref(prefs) !== this.initialPrefs[prefs]) {
        return false;
      }
    }
    return true;
  }
}
var PrerenderData = new _PrerenderData({
  initialPrefs: {
    "migrationExpired": true,
    "showTopSites": true,
    "showSearch": true,
    "topSitesRows": 1,
    "collapseTopSites": false,
    "section.highlights.collapsed": false,
    "section.topstories.collapsed": false,
    "feeds.section.topstories": true,
    "feeds.section.highlights": true,
    "enableWideLayout": true,
    "sectionOrder": "topsites,topstories,highlights"
  },
  // Prefs listed as invalidating will prevent the prerendered version
  // of AS from being used if their value is something other than what is listed
  // here. This is required because some preferences cause the page layout to be
  // too different for the prerendered version to be used. Unfortunately, this
  // will result in users who have modified some of their preferences not being
  // able to get the benefits of prerendering.
  validation: ["showTopSites", "showSearch", "topSitesRows", "collapseTopSites", "section.highlights.collapsed", "section.topstories.collapsed", "enableWideLayout", "sectionOrder",
  // This means if either of these are set to their default values,
  // prerendering can be used.
  { oneOf: ["feeds.section.topstories", "feeds.section.highlights"] }],
  initialSections: [{
    enabled: true,
    icon: "pocket",
    id: "topstories",
    order: 1,
    title: { id: "header_recommended_by", values: { provider: "Pocket" } }
  }, {
    enabled: true,
    id: "highlights",
    icon: "highlights",
    order: 2,
    title: { id: "header_highlights" }
  }]
});
// EXTERNAL MODULE: ./system-addon/content-src/lib/constants.js
var constants = __webpack_require__(17);

// CONCATENATED MODULE: ./system-addon/content-src/components/Search/Search.jsx
/* globals ContentSearchUIController */








class Search__Search extends external__React__default.a.PureComponent {
  constructor(props) {
    super(props);
    this.onClick = this.onClick.bind(this);
    this.onInputMount = this.onInputMount.bind(this);
  }

  handleEvent(event) {
    // Also track search events with our own telemetry
    if (event.detail.type === "Search") {
      this.props.dispatch(Actions["a" /* actionCreators */].UserEvent({ event: "SEARCH" }));
    }
  }

  onClick(event) {
    window.gContentSearchController.search(event);
  }

  componentWillUnmount() {
    delete window.gContentSearchController;
  }

  onInputMount(input) {
    if (input) {
      // The "healthReportKey" and needs to be "newtab" or "abouthome" so that
      // BrowserUsageTelemetry.jsm knows to handle events with this name, and
      // can add the appropriate telemetry probes for search. Without the correct
      // name, certain tests like browser_UsageTelemetry_content.js will fail
      // (See github ticket #2348 for more details)
      const healthReportKey = constants["a" /* IS_NEWTAB */] ? "newtab" : "abouthome";

      // The "searchSource" needs to be "newtab" or "homepage" and is sent with
      // the search data and acts as context for the search request (See
      // nsISearchEngine.getSubmission). It is necessary so that search engine
      // plugins can correctly atribute referrals. (See github ticket #3321 for
      // more details)
      const searchSource = constants["a" /* IS_NEWTAB */] ? "newtab" : "homepage";

      // gContentSearchController needs to exist as a global so that tests for
      // the existing about:home can find it; and so it allows these tests to pass.
      // In the future, when activity stream is default about:home, this can be renamed
      window.gContentSearchController = new ContentSearchUIController(input, input.parentNode, healthReportKey, searchSource);
      addEventListener("ContentSearchClient", this);
    } else {
      window.gContentSearchController = null;
      removeEventListener("ContentSearchClient", this);
    }
  }

  /*
   * Do not change the ID on the input field, as legacy newtab code
   * specifically looks for the id 'newtab-search-text' on input fields
   * in order to execute searches in various tests
   */
  render() {
    return external__React__default.a.createElement(
      "div",
      { className: "search-wrapper" },
      external__React__default.a.createElement(
        "label",
        { htmlFor: "newtab-search-text", className: "search-label" },
        external__React__default.a.createElement(
          "span",
          { className: "sr-only" },
          external__React__default.a.createElement(external__ReactIntl_["FormattedMessage"], { id: "search_web_placeholder" })
        )
      ),
      external__React__default.a.createElement("input", {
        id: "newtab-search-text",
        maxLength: "256",
        placeholder: this.props.intl.formatMessage({ id: "search_web_placeholder" }),
        ref: this.onInputMount,
        title: this.props.intl.formatMessage({ id: "search_web_placeholder" }),
        type: "search" }),
      external__React__default.a.createElement(
        "button",
        {
          id: "searchSubmit",
          className: "search-button",
          onClick: this.onClick,
          title: this.props.intl.formatMessage({ id: "search_button" }) },
        external__React__default.a.createElement(
          "span",
          { className: "sr-only" },
          external__React__default.a.createElement(external__ReactIntl_["FormattedMessage"], { id: "search_button" })
        )
      )
    );
  }
}

const Search = Object(external__ReactRedux_["connect"])()(Object(external__ReactIntl_["injectIntl"])(Search__Search));
// EXTERNAL MODULE: ./system-addon/content-src/components/Sections/Sections.jsx
var Sections = __webpack_require__(18);

// CONCATENATED MODULE: ./system-addon/content-src/components/Base/Base.jsx












// Add the locale data for pluralization and relative-time formatting for now,
// this just uses english locale data. We can make this more sophisticated if
// more features are needed.
function addLocaleDataForReactIntl(locale) {
  Object(external__ReactIntl_["addLocaleData"])([{ locale, parentLocale: "en" }]);
}

class Base__Base extends external__React__default.a.PureComponent {
  componentWillMount() {
    const { App, locale } = this.props;
    this.sendNewTabRehydrated(App);
    addLocaleDataForReactIntl(locale);
  }

  componentDidMount() {
    // Request state AFTER the first render to ensure we don't cause the
    // prerendered DOM to be unmounted. Otherwise, NEW_TAB_STATE_REQUEST is
    // dispatched right after the store is ready.
    if (this.props.isPrerendered) {
      this.props.dispatch(Actions["a" /* actionCreators */].AlsoToMain({ type: Actions["b" /* actionTypes */].NEW_TAB_STATE_REQUEST }));
      this.props.dispatch(Actions["a" /* actionCreators */].AlsoToMain({ type: Actions["b" /* actionTypes */].PAGE_PRERENDERED }));
    }
  }

  componentWillUpdate({ App }) {
    this.sendNewTabRehydrated(App);
  }

  // The NEW_TAB_REHYDRATED event is used to inform feeds that their
  // data has been consumed e.g. for counting the number of tabs that
  // have rendered that data.
  sendNewTabRehydrated(App) {
    if (App && App.initialized && !this.renderNotified) {
      this.props.dispatch(Actions["a" /* actionCreators */].AlsoToMain({ type: Actions["b" /* actionTypes */].NEW_TAB_REHYDRATED, data: {} }));
      this.renderNotified = true;
    }
  }

  render() {
    const { props } = this;
    const { App, locale, strings } = props;
    const { initialized } = App;

    if (!props.isPrerendered && !initialized) {
      return null;
    }

    return external__React__default.a.createElement(
      external__ReactIntl_["IntlProvider"],
      { locale: locale, messages: strings },
      external__React__default.a.createElement(
        ErrorBoundary["a" /* ErrorBoundary */],
        { className: "base-content-fallback" },
        external__React__default.a.createElement(Base_BaseContent, this.props)
      )
    );
  }
}
/* unused harmony export _Base */


class Base_BaseContent extends external__React__default.a.PureComponent {
  render() {
    const { props } = this;
    const { App } = props;
    const { initialized } = App;
    const prefs = props.Prefs.values;

    const shouldBeFixedToTop = PrerenderData.arePrefsValid(name => prefs[name]);

    const outerClassName = `outer-wrapper${shouldBeFixedToTop ? " fixed-to-top" : ""} ${prefs.enableWideLayout ? "wide-layout-enabled" : "wide-layout-disabled"}`;

    return external__React__default.a.createElement(
      "div",
      { className: outerClassName },
      external__React__default.a.createElement(
        "main",
        null,
        prefs.showSearch && external__React__default.a.createElement(
          "div",
          { className: "non-collapsible-section" },
          external__React__default.a.createElement(
            ErrorBoundary["a" /* ErrorBoundary */],
            null,
            external__React__default.a.createElement(Search, null)
          )
        ),
        external__React__default.a.createElement(
          "div",
          { className: `body-wrapper${initialized ? " on" : ""}` },
          !prefs.migrationExpired && external__React__default.a.createElement(
            "div",
            { className: "non-collapsible-section" },
            external__React__default.a.createElement(ManualMigration, null)
          ),
          external__React__default.a.createElement(Sections["a" /* Sections */], null)
        ),
        external__React__default.a.createElement(ConfirmDialog, null)
      ),
      initialized && external__React__default.a.createElement(
        "div",
        { className: "prefs-pane" },
        external__React__default.a.createElement(
          ErrorBoundary["a" /* ErrorBoundary */],
          { className: "sidebar" },
          " ",
          external__React__default.a.createElement(PreferencesPane, null),
          " "
        )
      )
    );
  }
}
/* unused harmony export BaseContent */


const Base = Object(external__ReactRedux_["connect"])(state => ({ App: state.App, Prefs: state.Prefs }))(Base__Base);
/* harmony export (immutable) */ __webpack_exports__["a"] = Base;


/***/ }),
/* 17 */
/***/ (function(module, __webpack_exports__, __webpack_require__) {

"use strict";
/* WEBPACK VAR INJECTION */(function(global) {const IS_NEWTAB = global.document && global.document.documentURI === "about:newtab";
/* harmony export (immutable) */ __webpack_exports__["a"] = IS_NEWTAB;

/* WEBPACK VAR INJECTION */}.call(__webpack_exports__, __webpack_require__(3)))

/***/ }),
/* 18 */
/***/ (function(module, __webpack_exports__, __webpack_require__) {

"use strict";
/* WEBPACK VAR INJECTION */(function(global) {/* harmony import */ var __WEBPACK_IMPORTED_MODULE_0_content_src_components_Card_Card__ = __webpack_require__(19);
/* harmony import */ var __WEBPACK_IMPORTED_MODULE_1_react_intl__ = __webpack_require__(2);
/* harmony import */ var __WEBPACK_IMPORTED_MODULE_1_react_intl___default = __webpack_require__.n(__WEBPACK_IMPORTED_MODULE_1_react_intl__);
/* harmony import */ var __WEBPACK_IMPORTED_MODULE_2_common_Actions_jsm__ = __webpack_require__(0);
/* harmony import */ var __WEBPACK_IMPORTED_MODULE_3_content_src_components_CollapsibleSection_CollapsibleSection__ = __webpack_require__(10);
/* harmony import */ var __WEBPACK_IMPORTED_MODULE_4_content_src_components_ComponentPerfTimer_ComponentPerfTimer__ = __webpack_require__(11);
/* harmony import */ var __WEBPACK_IMPORTED_MODULE_5_react_redux__ = __webpack_require__(4);
/* harmony import */ var __WEBPACK_IMPORTED_MODULE_5_react_redux___default = __webpack_require__.n(__WEBPACK_IMPORTED_MODULE_5_react_redux__);
/* harmony import */ var __WEBPACK_IMPORTED_MODULE_6_react__ = __webpack_require__(1);
/* harmony import */ var __WEBPACK_IMPORTED_MODULE_6_react___default = __webpack_require__.n(__WEBPACK_IMPORTED_MODULE_6_react__);
/* harmony import */ var __WEBPACK_IMPORTED_MODULE_7_content_src_components_Topics_Topics__ = __webpack_require__(21);
/* harmony import */ var __WEBPACK_IMPORTED_MODULE_8_content_src_components_TopSites_TopSites__ = __webpack_require__(22);
var _extends = Object.assign || function (target) { for (var i = 1; i < arguments.length; i++) { var source = arguments[i]; for (var key in source) { if (Object.prototype.hasOwnProperty.call(source, key)) { target[key] = source[key]; } } } return target; };











const VISIBLE = "visible";
const VISIBILITY_CHANGE_EVENT = "visibilitychange";
const CARDS_PER_ROW = 3;

function getFormattedMessage(message) {
  return typeof message === "string" ? __WEBPACK_IMPORTED_MODULE_6_react___default.a.createElement(
    "span",
    null,
    message
  ) : __WEBPACK_IMPORTED_MODULE_6_react___default.a.createElement(__WEBPACK_IMPORTED_MODULE_1_react_intl__["FormattedMessage"], message);
}

class Section extends __WEBPACK_IMPORTED_MODULE_6_react___default.a.PureComponent {
  _dispatchImpressionStats() {
    const { props } = this;
    const maxCards = 3 * props.maxRows;
    const cards = props.rows.slice(0, maxCards);

    if (this.needsImpressionStats(cards)) {
      props.dispatch(__WEBPACK_IMPORTED_MODULE_2_common_Actions_jsm__["a" /* actionCreators */].ImpressionStats({
        source: props.eventSource,
        tiles: cards.map(link => ({ id: link.guid }))
      }));
      this.impressionCardGuids = cards.map(link => link.guid);
    }
  }

  // This sends an event when a user sees a set of new content. If content
  // changes while the page is hidden (i.e. preloaded or on a hidden tab),
  // only send the event if the page becomes visible again.
  sendImpressionStatsOrAddListener() {
    const { props } = this;

    if (!props.shouldSendImpressionStats || !props.dispatch) {
      return;
    }

    if (props.document.visibilityState === VISIBLE) {
      this._dispatchImpressionStats();
    } else {
      // We should only ever send the latest impression stats ping, so remove any
      // older listeners.
      if (this._onVisibilityChange) {
        props.document.removeEventListener(VISIBILITY_CHANGE_EVENT, this._onVisibilityChange);
      }

      // When the page becomes visible, send the impression stats ping if the section isn't collapsed.
      this._onVisibilityChange = () => {
        if (props.document.visibilityState === VISIBLE) {
          const { id, Prefs } = this.props;
          const isCollapsed = Prefs.values[`section.${id}.collapsed`];
          if (!isCollapsed) {
            this._dispatchImpressionStats();
          }
          props.document.removeEventListener(VISIBILITY_CHANGE_EVENT, this._onVisibilityChange);
        }
      };
      props.document.addEventListener(VISIBILITY_CHANGE_EVENT, this._onVisibilityChange);
    }
  }

  componentDidMount() {
    const { id, rows, Prefs } = this.props;
    const isCollapsed = Prefs.values[`section.${id}.collapsed`];
    if (rows.length && !isCollapsed) {
      this.sendImpressionStatsOrAddListener();
    }
  }

  componentDidUpdate(prevProps) {
    const { props } = this;
    const { id, Prefs } = props;
    const isCollapsedPref = `section.${id}.collapsed`;
    const isCollapsed = Prefs.values[isCollapsedPref];
    const wasCollapsed = prevProps.Prefs.values[isCollapsedPref];
    if (
    // Don't send impression stats for the empty state
    props.rows.length && (
    // We only want to send impression stats if the content of the cards has changed
    // and the section is not collapsed...
    props.rows !== prevProps.rows && !isCollapsed ||
    // or if we are expanding a section that was collapsed.
    wasCollapsed && !isCollapsed)) {
      this.sendImpressionStatsOrAddListener();
    }
  }

  componentWillUnmount() {
    if (this._onVisibilityChange) {
      this.props.document.removeEventListener(VISIBILITY_CHANGE_EVENT, this._onVisibilityChange);
    }
  }

  needsImpressionStats(cards) {
    if (!this.impressionCardGuids || this.impressionCardGuids.length !== cards.length) {
      return true;
    }

    for (let i = 0; i < cards.length; i++) {
      if (cards[i].guid !== this.impressionCardGuids[i]) {
        return true;
      }
    }

    return false;
  }

  numberOfPlaceholders(items) {
    if (items === 0) {
      return CARDS_PER_ROW;
    }
    const remainder = items % CARDS_PER_ROW;
    if (remainder === 0) {
      return 0;
    }
    return CARDS_PER_ROW - remainder;
  }

  render() {
    const {
      id, eventSource, title, icon, rows,
      emptyState, dispatch, maxRows,
      contextMenuOptions, initialized, disclaimer,
      pref, privacyNoticeURL, isFirst, isLast
    } = this.props;
    const maxCards = CARDS_PER_ROW * maxRows;

    // Show topics only for top stories and if it's not initialized yet (so
    // content doesn't shift when it is loaded) or has loaded with topics
    const shouldShowTopics = id === "topstories" && (!this.props.topics || this.props.topics.length > 0);

    const realRows = rows.slice(0, maxCards);
    const placeholders = this.numberOfPlaceholders(realRows.length);

    // The empty state should only be shown after we have initialized and there is no content.
    // Otherwise, we should show placeholders.
    const shouldShowEmptyState = initialized && !rows.length;

    // <Section> <-- React component
    // <section> <-- HTML5 element
    return __WEBPACK_IMPORTED_MODULE_6_react___default.a.createElement(
      __WEBPACK_IMPORTED_MODULE_4_content_src_components_ComponentPerfTimer_ComponentPerfTimer__["a" /* ComponentPerfTimer */],
      this.props,
      __WEBPACK_IMPORTED_MODULE_6_react___default.a.createElement(
        __WEBPACK_IMPORTED_MODULE_3_content_src_components_CollapsibleSection_CollapsibleSection__["a" /* CollapsibleSection */],
        { className: "section", icon: icon,
          title: getFormattedMessage(title),
          id: id,
          eventSource: eventSource,
          disclaimer: disclaimer,
          prefName: `section.${id}.collapsed`,
          showPrefName: pref && pref.feed || id,
          privacyNoticeURL: privacyNoticeURL,
          Prefs: this.props.Prefs,
          isFirst: isFirst,
          isLast: isLast,
          dispatch: this.props.dispatch },
        !shouldShowEmptyState && __WEBPACK_IMPORTED_MODULE_6_react___default.a.createElement(
          "ul",
          { className: "section-list", style: { padding: 0 } },
          realRows.map((link, index) => link && __WEBPACK_IMPORTED_MODULE_6_react___default.a.createElement(__WEBPACK_IMPORTED_MODULE_0_content_src_components_Card_Card__["a" /* Card */], { key: index, index: index, dispatch: dispatch, link: link, contextMenuOptions: contextMenuOptions,
            eventSource: eventSource, shouldSendImpressionStats: this.props.shouldSendImpressionStats, isWebExtension: this.props.isWebExtension })),
          placeholders > 0 && [...new Array(placeholders)].map((_, i) => __WEBPACK_IMPORTED_MODULE_6_react___default.a.createElement(__WEBPACK_IMPORTED_MODULE_0_content_src_components_Card_Card__["b" /* PlaceholderCard */], { key: i }))
        ),
        shouldShowEmptyState && __WEBPACK_IMPORTED_MODULE_6_react___default.a.createElement(
          "div",
          { className: "section-empty-state" },
          __WEBPACK_IMPORTED_MODULE_6_react___default.a.createElement(
            "div",
            { className: "empty-state" },
            emptyState.icon && emptyState.icon.startsWith("moz-extension://") ? __WEBPACK_IMPORTED_MODULE_6_react___default.a.createElement("img", { className: "empty-state-icon icon", style: { "background-image": `url('${emptyState.icon}')` } }) : __WEBPACK_IMPORTED_MODULE_6_react___default.a.createElement("img", { className: `empty-state-icon icon icon-${emptyState.icon}` }),
            __WEBPACK_IMPORTED_MODULE_6_react___default.a.createElement(
              "p",
              { className: "empty-state-message" },
              getFormattedMessage(emptyState.message)
            )
          )
        ),
        shouldShowTopics && __WEBPACK_IMPORTED_MODULE_6_react___default.a.createElement(__WEBPACK_IMPORTED_MODULE_7_content_src_components_Topics_Topics__["a" /* Topics */], { topics: this.props.topics, read_more_endpoint: this.props.read_more_endpoint })
      )
    );
  }
}
/* unused harmony export Section */


Section.defaultProps = {
  document: global.document,
  rows: [],
  emptyState: {},
  title: ""
};

const SectionIntl = Object(__WEBPACK_IMPORTED_MODULE_5_react_redux__["connect"])(state => ({ Prefs: state.Prefs }))(Object(__WEBPACK_IMPORTED_MODULE_1_react_intl__["injectIntl"])(Section));
/* unused harmony export SectionIntl */


class _Sections extends __WEBPACK_IMPORTED_MODULE_6_react___default.a.PureComponent {
  renderSections() {
    const sections = [];
    const enabledSections = this.props.Sections.filter(section => section.enabled);
    const { sectionOrder, showTopSites } = this.props.Prefs.values;
    // Enabled sections doesn't include Top Sites, so we add it if enabled.
    const expectedCount = enabledSections.length + ~~showTopSites;

    for (const sectionId of sectionOrder.split(",")) {
      const commonProps = {
        key: sectionId,
        isFirst: sections.length === 0,
        isLast: sections.length === expectedCount - 1
      };
      if (sectionId === "topsites" && showTopSites) {
        sections.push(__WEBPACK_IMPORTED_MODULE_6_react___default.a.createElement(__WEBPACK_IMPORTED_MODULE_8_content_src_components_TopSites_TopSites__["a" /* TopSites */], commonProps));
      } else {
        const section = enabledSections.find(s => s.id === sectionId);
        if (section) {
          sections.push(__WEBPACK_IMPORTED_MODULE_6_react___default.a.createElement(SectionIntl, _extends({}, section, commonProps)));
        }
      }
    }
    return sections;
  }

  render() {
    return __WEBPACK_IMPORTED_MODULE_6_react___default.a.createElement(
      "div",
      { className: "sections-list" },
      this.renderSections()
    );
  }
}
/* unused harmony export _Sections */


const Sections = Object(__WEBPACK_IMPORTED_MODULE_5_react_redux__["connect"])(state => ({ Sections: state.Sections, Prefs: state.Prefs }))(_Sections);
/* harmony export (immutable) */ __webpack_exports__["a"] = Sections;

/* WEBPACK VAR INJECTION */}.call(__webpack_exports__, __webpack_require__(3)))

/***/ }),
/* 19 */
/***/ (function(module, __webpack_exports__, __webpack_require__) {

"use strict";

// EXTERNAL MODULE: ./system-addon/common/Actions.jsm
var Actions = __webpack_require__(0);

// CONCATENATED MODULE: ./system-addon/content-src/components/Card/types.js
const cardContextTypes = {
  history: {
    intlID: "type_label_visited",
    icon: "historyItem"
  },
  bookmark: {
    intlID: "type_label_bookmarked",
    icon: "bookmark-added"
  },
  trending: {
    intlID: "type_label_recommended",
    icon: "trending"
  },
  now: {
    intlID: "type_label_now",
    icon: "now"
  },
  pocket: {
    intlID: "type_label_pocket",
    icon: "pocket-small"
  }
};
// EXTERNAL MODULE: external "ReactIntl"
var external__ReactIntl_ = __webpack_require__(2);
var external__ReactIntl__default = /*#__PURE__*/__webpack_require__.n(external__ReactIntl_);

// EXTERNAL MODULE: ./system-addon/content-src/components/LinkMenu/LinkMenu.jsx + 1 modules
var LinkMenu = __webpack_require__(8);

// EXTERNAL MODULE: external "React"
var external__React_ = __webpack_require__(1);
var external__React__default = /*#__PURE__*/__webpack_require__.n(external__React_);

// CONCATENATED MODULE: ./system-addon/content-src/components/Card/Card.jsx






// Keep track of pending image loads to only request once
const gImageLoading = new Map();

/**
 * Card component.
 * Cards are found within a Section component and contain information about a link such
 * as preview image, page title, page description, and some context about if the page
 * was visited, bookmarked, trending etc...
 * Each Section can make an unordered list of Cards which will create one instane of
 * this class. Each card will then get a context menu which reflects the actions that
 * can be done on this Card.
 */
class Card_Card extends external__React__default.a.PureComponent {
  constructor(props) {
    super(props);
    this.state = {
      activeCard: null,
      imageLoaded: false,
      showContextMenu: false
    };
    this.onMenuButtonClick = this.onMenuButtonClick.bind(this);
    this.onMenuUpdate = this.onMenuUpdate.bind(this);
    this.onLinkClick = this.onLinkClick.bind(this);
  }

  /**
   * Helper to conditionally load an image and update state when it loads.
   */
  async maybeLoadImage() {
    // No need to load if it's already loaded or no image
    const { image } = this.props.link;
    if (!this.state.imageLoaded && image) {
      // Initialize a promise to share a load across multiple card updates
      if (!gImageLoading.has(image)) {
        const loaderPromise = new Promise((resolve, reject) => {
          const loader = new Image();
          loader.addEventListener("load", resolve);
          loader.addEventListener("error", reject);
          loader.src = image;
        });

        // Save and remove the promise only while it's pending
        gImageLoading.set(image, loaderPromise);
        loaderPromise.catch(ex => ex).then(() => gImageLoading.delete(image)).catch();
      }

      // Wait for the image whether just started loading or reused promise
      await gImageLoading.get(image);

      // Only update state if we're still waiting to load the original image
      if (this.props.link.image === image && !this.state.imageLoaded) {
        this.setState({ imageLoaded: true });
      }
    }
  }

  onMenuButtonClick(event) {
    event.preventDefault();
    this.setState({
      activeCard: this.props.index,
      showContextMenu: true
    });
  }

  /**
   * Report to telemetry additional information about the item.
   */
  _getTelemetryInfo() {
    // Filter out "history" type for being the default
    if (this.props.link.type !== "history") {
      return { value: { card_type: this.props.link.type } };
    }

    return null;
  }

  onLinkClick(event) {
    event.preventDefault();
    const { altKey, button, ctrlKey, metaKey, shiftKey } = event;
    this.props.dispatch(Actions["a" /* actionCreators */].AlsoToMain({
      type: Actions["b" /* actionTypes */].OPEN_LINK,
      data: Object.assign(this.props.link, { event: { altKey, button, ctrlKey, metaKey, shiftKey } })
    }));

    if (this.props.isWebExtension) {
      this.props.dispatch(Actions["a" /* actionCreators */].WebExtEvent(Actions["b" /* actionTypes */].WEBEXT_CLICK, {
        source: this.props.eventSource,
        url: this.props.link.url,
        action_position: this.props.index
      }));
    } else {
      this.props.dispatch(Actions["a" /* actionCreators */].UserEvent(Object.assign({
        event: "CLICK",
        source: this.props.eventSource,
        action_position: this.props.index
      }, this._getTelemetryInfo())));

      if (this.props.shouldSendImpressionStats) {
        this.props.dispatch(Actions["a" /* actionCreators */].ImpressionStats({
          source: this.props.eventSource,
          click: 0,
          tiles: [{ id: this.props.link.guid, pos: this.props.index }]
        }));
      }
    }
  }

  onMenuUpdate(showContextMenu) {
    this.setState({ showContextMenu });
  }

  componentDidMount() {
    this.maybeLoadImage();
  }

  componentDidUpdate() {
    this.maybeLoadImage();
  }

  componentWillReceiveProps(nextProps) {
    // Clear the image state if changing images
    if (nextProps.link.image !== this.props.link.image) {
      this.setState({ imageLoaded: false });
    }
  }

  render() {
    const { index, link, dispatch, contextMenuOptions, eventSource, shouldSendImpressionStats } = this.props;
    const { props } = this;
    const isContextMenuOpen = this.state.showContextMenu && this.state.activeCard === index;
    // Display "now" as "trending" until we have new strings #3402
    const { icon, intlID } = cardContextTypes[link.type === "now" ? "trending" : link.type] || {};
    const hasImage = link.image || link.hasImage;
    const imageStyle = { backgroundImage: link.image ? `url(${link.image})` : "none" };

    return external__React__default.a.createElement(
      "li",
      { className: `card-outer${isContextMenuOpen ? " active" : ""}${props.placeholder ? " placeholder" : ""}` },
      external__React__default.a.createElement(
        "a",
        { href: link.type === "pocket" ? link.open_url : link.url, onClick: !props.placeholder ? this.onLinkClick : undefined },
        external__React__default.a.createElement(
          "div",
          { className: "card" },
          hasImage && external__React__default.a.createElement(
            "div",
            { className: "card-preview-image-outer" },
            external__React__default.a.createElement("div", { className: `card-preview-image${this.state.imageLoaded ? " loaded" : ""}`, style: imageStyle })
          ),
          external__React__default.a.createElement(
            "div",
            { className: `card-details${hasImage ? "" : " no-image"}` },
            link.hostname && external__React__default.a.createElement(
              "div",
              { className: "card-host-name" },
              link.hostname
            ),
            external__React__default.a.createElement(
              "div",
              { className: ["card-text", icon ? "" : "no-context", link.description ? "" : "no-description", link.hostname ? "" : "no-host-name", hasImage ? "" : "no-image"].join(" ") },
              external__React__default.a.createElement(
                "h4",
                { className: "card-title", dir: "auto" },
                link.title
              ),
              external__React__default.a.createElement(
                "p",
                { className: "card-description", dir: "auto" },
                link.description
              )
            ),
            external__React__default.a.createElement(
              "div",
              { className: "card-context" },
              icon && !link.context && external__React__default.a.createElement("span", { className: `card-context-icon icon icon-${icon}` }),
              link.icon && link.context && external__React__default.a.createElement("span", { className: "card-context-icon icon", style: { backgroundImage: `url('${link.icon}')` } }),
              intlID && !link.context && external__React__default.a.createElement(
                "div",
                { className: "card-context-label" },
                external__React__default.a.createElement(external__ReactIntl_["FormattedMessage"], { id: intlID, defaultMessage: "Visited" })
              ),
              link.context && external__React__default.a.createElement(
                "div",
                { className: "card-context-label" },
                link.context
              )
            )
          )
        )
      ),
      !props.placeholder && external__React__default.a.createElement(
        "button",
        { className: "context-menu-button icon",
          onClick: this.onMenuButtonClick },
        external__React__default.a.createElement(
          "span",
          { className: "sr-only" },
          `Open context menu for ${link.title}`
        )
      ),
      isContextMenuOpen && external__React__default.a.createElement(LinkMenu["a" /* LinkMenu */], {
        dispatch: dispatch,
        index: index,
        source: eventSource,
        onUpdate: this.onMenuUpdate,
        options: link.contextMenuOptions || contextMenuOptions,
        site: link,
        siteInfo: this._getTelemetryInfo(),
        shouldSendImpressionStats: shouldSendImpressionStats })
    );
  }
}
/* harmony export (immutable) */ __webpack_exports__["a"] = Card_Card;

Card_Card.defaultProps = { link: {} };

const PlaceholderCard = () => external__React__default.a.createElement(Card_Card, { placeholder: true });
/* harmony export (immutable) */ __webpack_exports__["b"] = PlaceholderCard;


/***/ }),
/* 20 */
/***/ (function(module, __webpack_exports__, __webpack_require__) {

"use strict";

// EXTERNAL MODULE: ./system-addon/common/Actions.jsm
var Actions = __webpack_require__(0);

// EXTERNAL MODULE: ./system-addon/content-src/components/ContextMenu/ContextMenu.jsx
var ContextMenu = __webpack_require__(9);

// EXTERNAL MODULE: external "ReactIntl"
var external__ReactIntl_ = __webpack_require__(2);
var external__ReactIntl__default = /*#__PURE__*/__webpack_require__.n(external__ReactIntl_);

// EXTERNAL MODULE: external "React"
var external__React_ = __webpack_require__(1);
var external__React__default = /*#__PURE__*/__webpack_require__.n(external__React_);

// CONCATENATED MODULE: ./system-addon/content-src/lib/section-menu-options.js


/**
 * List of functions that return items that can be included as menu options in a
 * SectionMenu. All functions take the section as the only parameter.
 */
const SectionMenuOptions = {
  Separator: () => ({ type: "separator" }),
  MoveUp: section => ({
    id: "section_menu_action_move_up",
    icon: "arrowhead-up",
    action: Actions["a" /* actionCreators */].OnlyToMain({
      type: Actions["b" /* actionTypes */].SECTION_MOVE,
      data: { id: section.id, direction: -1 }
    }),
    userEvent: "SECTION_MENU_MOVE_UP",
    disabled: !!section.isFirst
  }),
  MoveDown: section => ({
    id: "section_menu_action_move_down",
    icon: "arrowhead-down",
    action: Actions["a" /* actionCreators */].OnlyToMain({
      type: Actions["b" /* actionTypes */].SECTION_MOVE,
      data: { id: section.id, direction: +1 }
    }),
    userEvent: "SECTION_MENU_MOVE_DOWN",
    disabled: !!section.isLast
  }),
  RemoveSection: section => ({
    id: "section_menu_action_remove_section",
    icon: "dismiss",
    action: Actions["a" /* actionCreators */].SetPref(section.showPrefName, false),
    userEvent: "SECTION_MENU_REMOVE"
  }),
  CollapseSection: section => ({
    id: "section_menu_action_collapse_section",
    icon: "minimize",
    action: Actions["a" /* actionCreators */].SetPref(section.collapsePrefName, true),
    userEvent: "SECTION_MENU_COLLAPSE"
  }),
  ExpandSection: section => ({
    id: "section_menu_action_expand_section",
    icon: "maximize",
    action: Actions["a" /* actionCreators */].SetPref(section.collapsePrefName, false),
    userEvent: "SECTION_MENU_EXPAND"
  }),
  ManageSection: section => ({
    id: "section_menu_action_manage_section",
    icon: "settings",
    action: { type: Actions["b" /* actionTypes */].SETTINGS_OPEN },
    userEvent: "SECTION_MENU_MANAGE"
  }),
  AddTopSite: section => ({
    id: "section_menu_action_add_topsite",
    icon: "add",
    action: { type: Actions["b" /* actionTypes */].TOP_SITES_EDIT, data: { index: -1 } },
    userEvent: "SECTION_MENU_ADD_TOPSITE"
  }),
  PrivacyNotice: section => ({
    id: "section_menu_action_privacy_notice",
    icon: "info",
    action: Actions["a" /* actionCreators */].OnlyToMain({
      type: Actions["b" /* actionTypes */].OPEN_LINK,
      data: { url: section.privacyNoticeURL }
    }),
    userEvent: "SECTION_MENU_PRIVACY_NOTICE"
  }),
  CheckCollapsed: section => section.isCollapsed ? SectionMenuOptions.ExpandSection(section) : SectionMenuOptions.CollapseSection(section)
};
// CONCATENATED MODULE: ./system-addon/content-src/components/SectionMenu/SectionMenu.jsx






const DEFAULT_SECTION_MENU_OPTIONS = ["MoveUp", "MoveDown", "Separator", "RemoveSection", "CheckCollapsed", "Separator", "ManageSection"];

class SectionMenu__SectionMenu extends external__React__default.a.PureComponent {
  getOptions() {
    const { props } = this;

    const propOptions = Array.from(DEFAULT_SECTION_MENU_OPTIONS);
    // Prepend custom options and a separator
    if (props.extraOptions) {
      propOptions.splice(0, 0, ...props.extraOptions, "Separator");
    }
    // Insert privacy notice before the last option ("ManageSection")
    if (props.privacyNoticeURL) {
      propOptions.splice(-1, 0, "PrivacyNotice");
    }

    const options = propOptions.map(o => SectionMenuOptions[o](props)).map(option => {
      const { action, id, type, userEvent } = option;
      if (!type && id) {
        option.label = props.intl.formatMessage({ id });
        option.onClick = () => {
          props.dispatch(action);
          if (userEvent) {
            props.dispatch(Actions["a" /* actionCreators */].UserEvent({
              event: userEvent,
              source: props.source
            }));
          }
        };
      }
      return option;
    });

    // This is for accessibility to support making each item tabbable.
    // We want to know which item is the first and which item
    // is the last, so we can close the context menu accordingly.
    options[0].first = true;
    options[options.length - 1].last = true;
    return options;
  }

  render() {
    return external__React__default.a.createElement(ContextMenu["a" /* ContextMenu */], {
      onUpdate: this.props.onUpdate,
      options: this.getOptions() });
  }
}
/* unused harmony export _SectionMenu */


const SectionMenu = Object(external__ReactIntl_["injectIntl"])(SectionMenu__SectionMenu);
/* harmony export (immutable) */ __webpack_exports__["a"] = SectionMenu;


/***/ }),
/* 21 */
/***/ (function(module, __webpack_exports__, __webpack_require__) {

"use strict";
/* harmony import */ var __WEBPACK_IMPORTED_MODULE_0_react_intl__ = __webpack_require__(2);
/* harmony import */ var __WEBPACK_IMPORTED_MODULE_0_react_intl___default = __webpack_require__.n(__WEBPACK_IMPORTED_MODULE_0_react_intl__);
/* harmony import */ var __WEBPACK_IMPORTED_MODULE_1_react__ = __webpack_require__(1);
/* harmony import */ var __WEBPACK_IMPORTED_MODULE_1_react___default = __webpack_require__.n(__WEBPACK_IMPORTED_MODULE_1_react__);



class Topic extends __WEBPACK_IMPORTED_MODULE_1_react___default.a.PureComponent {
  render() {
    const { url, name } = this.props;
    return __WEBPACK_IMPORTED_MODULE_1_react___default.a.createElement(
      "li",
      null,
      __WEBPACK_IMPORTED_MODULE_1_react___default.a.createElement(
        "a",
        { key: name, className: "topic-link", href: url },
        name
      )
    );
  }
}
/* unused harmony export Topic */


class Topics extends __WEBPACK_IMPORTED_MODULE_1_react___default.a.PureComponent {
  render() {
    const { topics, read_more_endpoint } = this.props;
    return __WEBPACK_IMPORTED_MODULE_1_react___default.a.createElement(
      "div",
      { className: "topic" },
      __WEBPACK_IMPORTED_MODULE_1_react___default.a.createElement(
        "span",
        null,
        __WEBPACK_IMPORTED_MODULE_1_react___default.a.createElement(__WEBPACK_IMPORTED_MODULE_0_react_intl__["FormattedMessage"], { id: "pocket_read_more" })
      ),
      __WEBPACK_IMPORTED_MODULE_1_react___default.a.createElement(
        "ul",
        null,
        topics && topics.map(t => __WEBPACK_IMPORTED_MODULE_1_react___default.a.createElement(Topic, { key: t.name, url: t.url, name: t.name }))
      ),
      read_more_endpoint && __WEBPACK_IMPORTED_MODULE_1_react___default.a.createElement(
        "a",
        { className: "topic-read-more", href: read_more_endpoint },
        __WEBPACK_IMPORTED_MODULE_1_react___default.a.createElement(__WEBPACK_IMPORTED_MODULE_0_react_intl__["FormattedMessage"], { id: "pocket_read_even_more" })
      )
    );
  }
}
/* harmony export (immutable) */ __webpack_exports__["a"] = Topics;


/***/ }),
/* 22 */
/***/ (function(module, __webpack_exports__, __webpack_require__) {

"use strict";
/* WEBPACK VAR INJECTION */(function(global) {/* harmony import */ var __WEBPACK_IMPORTED_MODULE_0_common_Actions_jsm__ = __webpack_require__(0);
/* harmony import */ var __WEBPACK_IMPORTED_MODULE_1__TopSitesConstants__ = __webpack_require__(5);
/* harmony import */ var __WEBPACK_IMPORTED_MODULE_2_content_src_components_CollapsibleSection_CollapsibleSection__ = __webpack_require__(10);
/* harmony import */ var __WEBPACK_IMPORTED_MODULE_3_content_src_components_ComponentPerfTimer_ComponentPerfTimer__ = __webpack_require__(11);
/* harmony import */ var __WEBPACK_IMPORTED_MODULE_4_react_redux__ = __webpack_require__(4);
/* harmony import */ var __WEBPACK_IMPORTED_MODULE_4_react_redux___default = __webpack_require__.n(__WEBPACK_IMPORTED_MODULE_4_react_redux__);
/* harmony import */ var __WEBPACK_IMPORTED_MODULE_5_react_intl__ = __webpack_require__(2);
/* harmony import */ var __WEBPACK_IMPORTED_MODULE_5_react_intl___default = __webpack_require__.n(__WEBPACK_IMPORTED_MODULE_5_react_intl__);
/* harmony import */ var __WEBPACK_IMPORTED_MODULE_6_react__ = __webpack_require__(1);
/* harmony import */ var __WEBPACK_IMPORTED_MODULE_6_react___default = __webpack_require__.n(__WEBPACK_IMPORTED_MODULE_6_react__);
/* harmony import */ var __WEBPACK_IMPORTED_MODULE_7_common_Reducers_jsm__ = __webpack_require__(6);
/* harmony import */ var __WEBPACK_IMPORTED_MODULE_8__TopSiteForm__ = __webpack_require__(23);
/* harmony import */ var __WEBPACK_IMPORTED_MODULE_9__TopSite__ = __webpack_require__(13);











function topSiteIconType(link) {
  if (link.tippyTopIcon || link.faviconRef === "tippytop") {
    return "tippytop";
  }
  if (link.faviconSize >= __WEBPACK_IMPORTED_MODULE_1__TopSitesConstants__["b" /* MIN_RICH_FAVICON_SIZE */]) {
    return "rich_icon";
  }
  if (link.screenshot && link.faviconSize >= __WEBPACK_IMPORTED_MODULE_1__TopSitesConstants__["a" /* MIN_CORNER_FAVICON_SIZE */]) {
    return "screenshot_with_icon";
  }
  if (link.screenshot) {
    return "screenshot";
  }
  return "no_image";
}

/**
 * Iterates through TopSites and counts types of images.
 * @param acc Accumulator for reducer.
 * @param topsite Entry in TopSites.
 */
function countTopSitesIconsTypes(topSites) {
  const countTopSitesTypes = (acc, link) => {
    acc[topSiteIconType(link)]++;
    return acc;
  };

  return topSites.reduce(countTopSitesTypes, {
    "screenshot_with_icon": 0,
    "screenshot": 0,
    "tippytop": 0,
    "rich_icon": 0,
    "no_image": 0
  });
}

class _TopSites extends __WEBPACK_IMPORTED_MODULE_6_react___default.a.PureComponent {
  constructor(props) {
    super(props);
    this.onFormClose = this.onFormClose.bind(this);
  }

  /**
   * Dispatch session statistics about the quality of TopSites icons and pinned count.
   */
  _dispatchTopSitesStats() {
    const topSites = this._getVisibleTopSites();
    const topSitesIconsStats = countTopSitesIconsTypes(topSites);
    const topSitesPinned = topSites.filter(site => !!site.isPinned).length;
    // Dispatch telemetry event with the count of TopSites images types.
    this.props.dispatch(__WEBPACK_IMPORTED_MODULE_0_common_Actions_jsm__["a" /* actionCreators */].AlsoToMain({
      type: __WEBPACK_IMPORTED_MODULE_0_common_Actions_jsm__["b" /* actionTypes */].SAVE_SESSION_PERF_DATA,
      data: { topsites_icon_stats: topSitesIconsStats, topsites_pinned: topSitesPinned }
    }));
  }

  /**
   * Return the TopSites that are visible based on prefs and window width.
   */
  _getVisibleTopSites() {
    // We hide 2 sites per row when not in the wide layout.
    let sitesPerRow = __WEBPACK_IMPORTED_MODULE_7_common_Reducers_jsm__["a" /* TOP_SITES_MAX_SITES_PER_ROW */];
    // $break-point-widest = 1072px (from _variables.scss)
    if (!global.matchMedia(`(min-width: 1072px)`).matches) {
      sitesPerRow -= 2;
    }
    return this.props.TopSites.rows.slice(0, this.props.TopSitesRows * sitesPerRow);
  }

  componentDidUpdate() {
    this._dispatchTopSitesStats();
  }

  componentDidMount() {
    this._dispatchTopSitesStats();
  }

  onFormClose() {
    this.props.dispatch(__WEBPACK_IMPORTED_MODULE_0_common_Actions_jsm__["a" /* actionCreators */].UserEvent({
      source: __WEBPACK_IMPORTED_MODULE_1__TopSitesConstants__["d" /* TOP_SITES_SOURCE */],
      event: "TOP_SITES_EDIT_CLOSE"
    }));
    this.props.dispatch({ type: __WEBPACK_IMPORTED_MODULE_0_common_Actions_jsm__["b" /* actionTypes */].TOP_SITES_CANCEL_EDIT });
  }

  render() {
    const { props } = this;
    const { editForm } = props.TopSites;

    return __WEBPACK_IMPORTED_MODULE_6_react___default.a.createElement(
      __WEBPACK_IMPORTED_MODULE_3_content_src_components_ComponentPerfTimer_ComponentPerfTimer__["a" /* ComponentPerfTimer */],
      { id: "topsites", initialized: props.TopSites.initialized, dispatch: props.dispatch },
      __WEBPACK_IMPORTED_MODULE_6_react___default.a.createElement(
        __WEBPACK_IMPORTED_MODULE_2_content_src_components_CollapsibleSection_CollapsibleSection__["a" /* CollapsibleSection */],
        {
          className: "top-sites",
          icon: "topsites",
          id: "topsites",
          title: props.intl.formatMessage({ id: "header_top_sites" }),
          extraMenuOptions: ["AddTopSite"],
          prefName: "collapseTopSites",
          showPrefName: "showTopSites",
          eventSource: __WEBPACK_IMPORTED_MODULE_1__TopSitesConstants__["d" /* TOP_SITES_SOURCE */],
          Prefs: props.Prefs,
          isFirst: props.isFirst,
          isLast: props.isLast,
          dispatch: props.dispatch },
        __WEBPACK_IMPORTED_MODULE_6_react___default.a.createElement(__WEBPACK_IMPORTED_MODULE_9__TopSite__["b" /* TopSiteList */], { TopSites: props.TopSites, TopSitesRows: props.TopSitesRows, dispatch: props.dispatch, intl: props.intl, topSiteIconType: topSiteIconType }),
        __WEBPACK_IMPORTED_MODULE_6_react___default.a.createElement(
          "div",
          { className: "edit-topsites-wrapper" },
          editForm && __WEBPACK_IMPORTED_MODULE_6_react___default.a.createElement(
            "div",
            { className: "edit-topsites" },
            __WEBPACK_IMPORTED_MODULE_6_react___default.a.createElement("div", { className: "modal-overlay", onClick: this.onFormClose }),
            __WEBPACK_IMPORTED_MODULE_6_react___default.a.createElement(
              "div",
              { className: "modal" },
              __WEBPACK_IMPORTED_MODULE_6_react___default.a.createElement(__WEBPACK_IMPORTED_MODULE_8__TopSiteForm__["a" /* TopSiteForm */], {
                site: props.TopSites.rows[editForm.index],
                index: editForm.index,
                onClose: this.onFormClose,
                dispatch: this.props.dispatch,
                intl: this.props.intl })
            )
          )
        )
      )
    );
  }
}
/* unused harmony export _TopSites */


const TopSites = Object(__WEBPACK_IMPORTED_MODULE_4_react_redux__["connect"])(state => ({
  TopSites: state.TopSites,
  Prefs: state.Prefs,
  TopSitesRows: state.Prefs.values.topSitesRows
}))(Object(__WEBPACK_IMPORTED_MODULE_5_react_intl__["injectIntl"])(_TopSites));
/* harmony export (immutable) */ __webpack_exports__["a"] = TopSites;

/* WEBPACK VAR INJECTION */}.call(__webpack_exports__, __webpack_require__(3)))

/***/ }),
/* 23 */
/***/ (function(module, __webpack_exports__, __webpack_require__) {

"use strict";

// EXTERNAL MODULE: ./system-addon/common/Actions.jsm
var Actions = __webpack_require__(0);

// EXTERNAL MODULE: external "ReactIntl"
var external__ReactIntl_ = __webpack_require__(2);
var external__ReactIntl__default = /*#__PURE__*/__webpack_require__.n(external__ReactIntl_);

// EXTERNAL MODULE: external "React"
var external__React_ = __webpack_require__(1);
var external__React__default = /*#__PURE__*/__webpack_require__.n(external__React_);

// EXTERNAL MODULE: ./system-addon/content-src/components/TopSites/TopSitesConstants.js
var TopSitesConstants = __webpack_require__(5);

// CONCATENATED MODULE: ./system-addon/content-src/components/TopSites/TopSiteFormInput.jsx



class TopSiteFormInput_TopSiteFormInput extends external__React__default.a.PureComponent {
  constructor(props) {
    super(props);
    this.onMount = this.onMount.bind(this);
  }

  componentWillReceiveProps(nextProps) {
    if (nextProps.validationError && !this.props.validationError) {
      this.input.focus();
    }
  }

  onMount(input) {
    this.input = input;
  }

  render() {
    const showClearButton = this.props.value && this.props.onClear;
    const { validationError, typeUrl } = this.props;

    return external__React__default.a.createElement(
      "label",
      null,
      external__React__default.a.createElement(external__ReactIntl_["FormattedMessage"], { id: this.props.titleId }),
      external__React__default.a.createElement(
        "div",
        { className: `field ${typeUrl ? "url" : ""}${validationError ? " invalid" : ""}` },
        showClearButton && external__React__default.a.createElement("div", { className: "icon icon-clear-input", onClick: this.props.onClear }),
        external__React__default.a.createElement("input", { type: "text",
          value: this.props.value,
          ref: this.onMount,
          onChange: this.props.onChange,
          placeholder: this.props.intl.formatMessage({ id: this.props.placeholderId }) }),
        validationError && external__React__default.a.createElement(
          "aside",
          { className: "error-tooltip" },
          external__React__default.a.createElement(external__ReactIntl_["FormattedMessage"], { id: this.props.errorMessageId })
        )
      )
    );
  }
}

TopSiteFormInput_TopSiteFormInput.defaultProps = {
  showClearButton: false,
  value: "",
  validationError: false
};
// EXTERNAL MODULE: ./system-addon/content-src/components/TopSites/TopSite.jsx
var TopSite = __webpack_require__(13);

// CONCATENATED MODULE: ./system-addon/content-src/components/TopSites/TopSiteForm.jsx







class TopSiteForm_TopSiteForm extends external__React__default.a.PureComponent {
  constructor(props) {
    super(props);
    const { site } = props;
    this.state = {
      label: site ? site.label || site.hostname : "",
      url: site ? site.url : "",
      validationError: false
    };
    this.onLabelChange = this.onLabelChange.bind(this);
    this.onUrlChange = this.onUrlChange.bind(this);
    this.onCancelButtonClick = this.onCancelButtonClick.bind(this);
    this.onClearUrlClick = this.onClearUrlClick.bind(this);
    this.onDoneButtonClick = this.onDoneButtonClick.bind(this);
  }

  onLabelChange(event) {
    this.setState({ "label": event.target.value });
  }

  onUrlChange(event) {
    this.setState({
      url: event.target.value,
      validationError: false
    });
  }

  onClearUrlClick() {
    this.setState({
      url: "",
      validationError: false
    });
  }

  onCancelButtonClick(ev) {
    ev.preventDefault();
    this.props.onClose();
  }

  onDoneButtonClick(ev) {
    ev.preventDefault();

    if (this.validateForm()) {
      const site = { url: this.cleanUrl(this.state.url) };
      const { index } = this.props;
      if (this.state.label !== "") {
        site.label = this.state.label;
      }

      this.props.dispatch(Actions["a" /* actionCreators */].AlsoToMain({
        type: Actions["b" /* actionTypes */].TOP_SITES_PIN,
        data: { site, index }
      }));
      this.props.dispatch(Actions["a" /* actionCreators */].UserEvent({
        source: TopSitesConstants["d" /* TOP_SITES_SOURCE */],
        event: "TOP_SITES_EDIT",
        action_position: index
      }));

      this.props.onClose();
    }
  }

  cleanUrl(url) {
    // If we are missing a protocol, prepend http://
    if (!url.startsWith("http:") && !url.startsWith("https:")) {
      return `http://${url}`;
    }
    return url;
  }

  validateUrl(url) {
    try {
      return !!new URL(this.cleanUrl(url));
    } catch (e) {
      return false;
    }
  }

  validateForm() {
    const validate = this.validateUrl(this.state.url);
    this.setState({ validationError: !validate });
    return validate;
  }

  render() {
    // For UI purposes, editing without an existing link is "add"
    const showAsAdd = !this.props.site;

    return external__React__default.a.createElement(
      "form",
      { className: "topsite-form" },
      external__React__default.a.createElement(
        "div",
        { className: "form-input-container" },
        external__React__default.a.createElement(
          "h3",
          { className: "section-title" },
          external__React__default.a.createElement(external__ReactIntl_["FormattedMessage"], { id: showAsAdd ? "topsites_form_add_header" : "topsites_form_edit_header" })
        ),
        external__React__default.a.createElement(
          "div",
          { className: "fields-and-preview" },
          external__React__default.a.createElement(
            "div",
            { className: "form-wrapper" },
            external__React__default.a.createElement(TopSiteFormInput_TopSiteFormInput, { onChange: this.onLabelChange,
              value: this.state.label,
              titleId: "topsites_form_title_label",
              placeholderId: "topsites_form_title_placeholder",
              intl: this.props.intl }),
            external__React__default.a.createElement(TopSiteFormInput_TopSiteFormInput, { onChange: this.onUrlChange,
              value: this.state.url,
              onClear: this.onClearUrlClick,
              validationError: this.state.validationError,
              titleId: "topsites_form_url_label",
              typeUrl: true,
              placeholderId: "topsites_form_url_placeholder",
              errorMessageId: "topsites_form_url_validation",
              intl: this.props.intl })
          ),
          external__React__default.a.createElement(TopSite["a" /* TopSiteLink */], { link: this.props.site || {}, title: this.state.label })
        )
      ),
      external__React__default.a.createElement(
        "section",
        { className: "actions" },
        external__React__default.a.createElement(
          "button",
          { className: "cancel", type: "button", onClick: this.onCancelButtonClick },
          external__React__default.a.createElement(external__ReactIntl_["FormattedMessage"], { id: "topsites_form_cancel_button" })
        ),
        external__React__default.a.createElement(
          "button",
          { className: "done", type: "submit", onClick: this.onDoneButtonClick },
          external__React__default.a.createElement(external__ReactIntl_["FormattedMessage"], { id: showAsAdd ? "topsites_form_add_button" : "topsites_form_save_button" })
        )
      )
    );
  }
}
/* harmony export (immutable) */ __webpack_exports__["a"] = TopSiteForm_TopSiteForm;


TopSiteForm_TopSiteForm.defaultProps = {
  TopSite: null,
  index: -1
};

/***/ }),
/* 24 */
/***/ (function(module, __webpack_exports__, __webpack_require__) {

"use strict";
/* WEBPACK VAR INJECTION */(function(global) {/* harmony import */ var __WEBPACK_IMPORTED_MODULE_0_common_Actions_jsm__ = __webpack_require__(0);
/* harmony import */ var __WEBPACK_IMPORTED_MODULE_1_common_PerfService_jsm__ = __webpack_require__(12);



const VISIBLE = "visible";
const VISIBILITY_CHANGE_EVENT = "visibilitychange";

class DetectUserSessionStart {
  constructor(store, options = {}) {
    this._store = store;
    // Overrides for testing
    this.document = options.document || global.document;
    this._perfService = options.perfService || __WEBPACK_IMPORTED_MODULE_1_common_PerfService_jsm__["a" /* perfService */];
    this._onVisibilityChange = this._onVisibilityChange.bind(this);
  }

  /**
   * sendEventOrAddListener - Notify immediately if the page is already visible,
   *                    or else set up a listener for when visibility changes.
   *                    This is needed for accurate session tracking for telemetry,
   *                    because tabs are pre-loaded.
   */
  sendEventOrAddListener() {
    if (this.document.visibilityState === VISIBLE) {
      // If the document is already visible, to the user, send a notification
      // immediately that a session has started.
      this._sendEvent();
    } else {
      // If the document is not visible, listen for when it does become visible.
      this.document.addEventListener(VISIBILITY_CHANGE_EVENT, this._onVisibilityChange);
    }
  }

  /**
   * _sendEvent - Sends a message to the main process to indicate the current
   *              tab is now visible to the user, includes the
   *              visibility_event_rcvd_ts time in ms from the UNIX epoch.
   */
  _sendEvent() {
    this._perfService.mark("visibility_event_rcvd_ts");

    try {
      let visibility_event_rcvd_ts = this._perfService.getMostRecentAbsMarkStartByName("visibility_event_rcvd_ts");

      this._store.dispatch(__WEBPACK_IMPORTED_MODULE_0_common_Actions_jsm__["a" /* actionCreators */].AlsoToMain({
        type: __WEBPACK_IMPORTED_MODULE_0_common_Actions_jsm__["b" /* actionTypes */].SAVE_SESSION_PERF_DATA,
        data: { visibility_event_rcvd_ts }
      }));
    } catch (ex) {
      // If this failed, it's likely because the `privacy.resistFingerprinting`
      // pref is true.  We should at least not blow up.
    }
  }

  /**
   * _onVisibilityChange - If the visibility has changed to visible, sends a notification
   *                      and removes the event listener. This should only be called once per tab.
   */
  _onVisibilityChange() {
    if (this.document.visibilityState === VISIBLE) {
      this._sendEvent();
      this.document.removeEventListener(VISIBILITY_CHANGE_EVENT, this._onVisibilityChange);
    }
  }
}
/* harmony export (immutable) */ __webpack_exports__["a"] = DetectUserSessionStart;

/* WEBPACK VAR INJECTION */}.call(__webpack_exports__, __webpack_require__(3)))

/***/ }),
/* 25 */
/***/ (function(module, __webpack_exports__, __webpack_require__) {

"use strict";
/* WEBPACK VAR INJECTION */(function(global) {/* harmony export (immutable) */ __webpack_exports__["a"] = initStore;
/* harmony import */ var __WEBPACK_IMPORTED_MODULE_0_common_Actions_jsm__ = __webpack_require__(0);
/* harmony import */ var __WEBPACK_IMPORTED_MODULE_1_redux__ = __webpack_require__(26);
/* harmony import */ var __WEBPACK_IMPORTED_MODULE_1_redux___default = __webpack_require__.n(__WEBPACK_IMPORTED_MODULE_1_redux__);
/* eslint-env mozilla/frame-script */




const MERGE_STORE_ACTION = "NEW_TAB_INITIAL_STATE";
/* unused harmony export MERGE_STORE_ACTION */

const OUTGOING_MESSAGE_NAME = "ActivityStream:ContentToMain";
/* unused harmony export OUTGOING_MESSAGE_NAME */

const INCOMING_MESSAGE_NAME = "ActivityStream:MainToContent";
/* unused harmony export INCOMING_MESSAGE_NAME */

const EARLY_QUEUED_ACTIONS = [__WEBPACK_IMPORTED_MODULE_0_common_Actions_jsm__["b" /* actionTypes */].SAVE_SESSION_PERF_DATA, __WEBPACK_IMPORTED_MODULE_0_common_Actions_jsm__["b" /* actionTypes */].PAGE_PRERENDERED];
/* unused harmony export EARLY_QUEUED_ACTIONS */


/**
 * A higher-order function which returns a reducer that, on MERGE_STORE action,
 * will return the action.data object merged into the previous state.
 *
 * For all other actions, it merely calls mainReducer.
 *
 * Because we want this to merge the entire state object, it's written as a
 * higher order function which takes the main reducer (itself often a call to
 * combineReducers) as a parameter.
 *
 * @param  {function} mainReducer reducer to call if action != MERGE_STORE_ACTION
 * @return {function}             a reducer that, on MERGE_STORE_ACTION action,
 *                                will return the action.data object merged
 *                                into the previous state, and the result
 *                                of calling mainReducer otherwise.
 */
function mergeStateReducer(mainReducer) {
  return (prevState, action) => {
    if (action.type === MERGE_STORE_ACTION) {
      return Object.assign({}, prevState, action.data);
    }

    return mainReducer(prevState, action);
  };
}

/**
 * messageMiddleware - Middleware that looks for SentToMain type actions, and sends them if necessary
 */
const messageMiddleware = store => next => action => {
  const skipLocal = action.meta && action.meta.skipLocal;
  if (__WEBPACK_IMPORTED_MODULE_0_common_Actions_jsm__["c" /* actionUtils */].isSendToMain(action)) {
    sendAsyncMessage(OUTGOING_MESSAGE_NAME, action);
  }
  if (!skipLocal) {
    next(action);
  }
};

const rehydrationMiddleware = store => next => action => {
  if (store._didRehydrate) {
    return next(action);
  }

  const isMergeStoreAction = action.type === MERGE_STORE_ACTION;
  const isRehydrationRequest = action.type === __WEBPACK_IMPORTED_MODULE_0_common_Actions_jsm__["b" /* actionTypes */].NEW_TAB_STATE_REQUEST;

  if (isRehydrationRequest) {
    store._didRequestInitialState = true;
    return next(action);
  }

  if (isMergeStoreAction) {
    store._didRehydrate = true;
    return next(action);
  }

  // If init happened after our request was made, we need to re-request
  if (store._didRequestInitialState && action.type === __WEBPACK_IMPORTED_MODULE_0_common_Actions_jsm__["b" /* actionTypes */].INIT) {
    return next(__WEBPACK_IMPORTED_MODULE_0_common_Actions_jsm__["a" /* actionCreators */].AlsoToMain({ type: __WEBPACK_IMPORTED_MODULE_0_common_Actions_jsm__["b" /* actionTypes */].NEW_TAB_STATE_REQUEST }));
  }

  if (__WEBPACK_IMPORTED_MODULE_0_common_Actions_jsm__["c" /* actionUtils */].isBroadcastToContent(action) || __WEBPACK_IMPORTED_MODULE_0_common_Actions_jsm__["c" /* actionUtils */].isSendToOneContent(action) || __WEBPACK_IMPORTED_MODULE_0_common_Actions_jsm__["c" /* actionUtils */].isSendToPreloaded(action)) {
    // Note that actions received before didRehydrate will not be dispatched
    // because this could negatively affect preloading and the the state
    // will be replaced by rehydration anyway.
    return null;
  }

  return next(action);
};
/* unused harmony export rehydrationMiddleware */


/**
 * This middleware queues up all the EARLY_QUEUED_ACTIONS until it receives
 * the first action from main. This is useful for those actions for main which
 * require higher reliability, i.e. the action will not be lost in the case
 * that it gets sent before the main is ready to receive it. Conversely, any
 * actions allowed early are accepted to be ignorable or re-sendable.
 */
const queueEarlyMessageMiddleware = store => next => action => {
  if (store._receivedFromMain) {
    next(action);
  } else if (__WEBPACK_IMPORTED_MODULE_0_common_Actions_jsm__["c" /* actionUtils */].isFromMain(action)) {
    next(action);
    store._receivedFromMain = true;
    // Sending out all the early actions as main is ready now
    if (store._earlyActionQueue) {
      store._earlyActionQueue.forEach(next);
      store._earlyActionQueue = [];
    }
  } else if (EARLY_QUEUED_ACTIONS.includes(action.type)) {
    store._earlyActionQueue = store._earlyActionQueue || [];
    store._earlyActionQueue.push(action);
  } else {
    // Let any other type of action go through
    next(action);
  }
};
/* unused harmony export queueEarlyMessageMiddleware */


/**
 * initStore - Create a store and listen for incoming actions
 *
 * @param  {object} reducers An object containing Redux reducers
 * @param  {object} intialState (optional) The initial state of the store, if desired
 * @return {object}          A redux store
 */
function initStore(reducers, initialState) {
  const store = Object(__WEBPACK_IMPORTED_MODULE_1_redux__["createStore"])(mergeStateReducer(Object(__WEBPACK_IMPORTED_MODULE_1_redux__["combineReducers"])(reducers)), initialState, global.addMessageListener && Object(__WEBPACK_IMPORTED_MODULE_1_redux__["applyMiddleware"])(rehydrationMiddleware, queueEarlyMessageMiddleware, messageMiddleware));

  store._didRehydrate = false;
  store._didRequestInitialState = false;

  if (global.addMessageListener) {
    global.addMessageListener(INCOMING_MESSAGE_NAME, msg => {
      try {
        store.dispatch(msg.data);
      } catch (ex) {
        console.error("Content msg:", msg, "Dispatch error: ", ex); // eslint-disable-line no-console
        dump(`Content msg: ${JSON.stringify(msg)}\nDispatch error: ${ex}\n${ex.stack}`);
      }
    });
  }

  return store;
}
/* WEBPACK VAR INJECTION */}.call(__webpack_exports__, __webpack_require__(3)))

/***/ }),
/* 26 */
/***/ (function(module, exports) {

module.exports = Redux;

/***/ }),
/* 27 */
/***/ (function(module, exports) {

module.exports = ReactDOM;

/***/ })
/******/ ]);
//# sourceMappingURL=activity-stream.bundle.js.map