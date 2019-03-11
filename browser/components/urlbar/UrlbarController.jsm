/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

var EXPORTED_SYMBOLS = [
  "UrlbarController",
];

const {XPCOMUtils} = ChromeUtils.import("resource://gre/modules/XPCOMUtils.jsm");
const {Services} = ChromeUtils.import("resource://gre/modules/Services.jsm");
XPCOMUtils.defineLazyModuleGetters(this, {
  AppConstants: "resource://gre/modules/AppConstants.jsm",
  BrowserUsageTelemetry: "resource:///modules/BrowserUsageTelemetry.jsm",
  ExtensionSearchHandler: "resource://gre/modules/ExtensionSearchHandler.jsm",
  PlacesUtils: "resource://gre/modules/PlacesUtils.jsm",
  UrlbarPrefs: "resource:///modules/UrlbarPrefs.jsm",
  UrlbarProvidersManager: "resource:///modules/UrlbarProvidersManager.jsm",
  UrlbarUtils: "resource:///modules/UrlbarUtils.jsm",
  URLBAR_SELECTED_RESULT_TYPES: "resource:///modules/BrowserUsageTelemetry.jsm",
});

const TELEMETRY_1ST_RESULT = "PLACES_AUTOCOMPLETE_1ST_RESULT_TIME_MS";
const TELEMETRY_6_FIRST_RESULTS = "PLACES_AUTOCOMPLETE_6_FIRST_RESULTS_TIME_MS";

/**
 * The address bar controller handles queries from the address bar, obtains
 * results and returns them to the UI for display.
 *
 * Listeners may be added to listen for the results. They must support the
 * following methods which may be called when a query is run:
 *
 * - onQueryStarted(queryContext)
 * - onQueryResults(queryContext)
 * - onQueryCancelled(queryContext)
 * - onQueryFinished(queryContext)
 * - onQueryResultRemoved(index)
 */
class UrlbarController {
  /**
   * Initialises the class. The manager may be overridden here, this is for
   * test purposes.
   *
   * @param {object} options
   *   The initial options for UrlbarController.
   * @param {object} options.browserWindow
   *   The browser window this controller is operating within.
   * @param {object} [options.manager]
   *   Optional fake providers manager to override the built-in providers manager.
   *   Intended for use in unit tests only.
   */
  constructor(options = {}) {
    if (!options.browserWindow) {
      throw new Error("Missing options: browserWindow");
    }
    if (!options.browserWindow.location ||
        options.browserWindow.location.href != AppConstants.BROWSER_CHROME_URL) {
      throw new Error("browserWindow should be an actual browser window.");
    }

    this.manager = options.manager || UrlbarProvidersManager;
    this.browserWindow = options.browserWindow;

    this._listeners = new Set();
    this._userSelectionBehavior = "none";
  }

  /**
   * Hooks up the controller with an input.
   *
   * @param {UrlbarInput} input
   *   The UrlbarInput instance associated with this controller.
   */
  setInput(input) {
    this.input = input;
  }

  /**
   * Hooks up the controller with a view.
   *
   * @param {UrlbarView} view
   *   The UrlbarView instance associated with this controller.
   */
  setView(view) {
    this.view = view;
  }

  /**
   * Takes a query context and starts the query based on the user input.
   *
   * @param {UrlbarQueryContext} queryContext The query details.
   */
  async startQuery(queryContext) {
    // Cancel any running query.
    this.cancelQuery();

    this._lastQueryContext = queryContext;

    queryContext.lastResultCount = 0;
    TelemetryStopwatch.start(TELEMETRY_1ST_RESULT, queryContext);
    TelemetryStopwatch.start(TELEMETRY_6_FIRST_RESULTS, queryContext);

    this._notify("onQueryStarted", queryContext);
    await this.manager.startQuery(queryContext, this);
    this._notify("onQueryFinished", queryContext);
    return queryContext;
  }

  /**
   * Cancels an in-progress query. Note, queries may continue running if they
   * can't be cancelled.
   *
   * @param {UrlbarUtils.CANCEL_REASON} [reason]
   *   The reason the query was cancelled.
   */
  cancelQuery(reason) {
    if (!this._lastQueryContext) {
      return;
    }

    TelemetryStopwatch.cancel(TELEMETRY_1ST_RESULT, this._lastQueryContext);
    TelemetryStopwatch.cancel(TELEMETRY_6_FIRST_RESULTS, this._lastQueryContext);

    this.manager.cancelQuery(this._lastQueryContext);
    this._notify("onQueryCancelled", this._lastQueryContext);
    delete this._lastQueryContext;

    if (reason == UrlbarUtils.CANCEL_REASON.BLUR &&
        ExtensionSearchHandler.hasActiveInputSession()) {
      ExtensionSearchHandler.handleInputCancelled();
    }
  }

  /**
   * Receives results from a query.
   *
   * @param {UrlbarQueryContext} queryContext The query details.
   */
  receiveResults(queryContext) {
    if (queryContext.lastResultCount < 1 && queryContext.results.length >= 1) {
      TelemetryStopwatch.finish(TELEMETRY_1ST_RESULT, queryContext);
    }
    if (queryContext.lastResultCount < 6 && queryContext.results.length >= 6) {
      TelemetryStopwatch.finish(TELEMETRY_6_FIRST_RESULTS, queryContext);
    }

    if (queryContext.lastResultCount == 0) {
      if (queryContext.results.length && queryContext.results[0].autofill) {
        this.input.setValueFromResult(queryContext.results[0]);
      }
      // The first time we receive results try to connect to the heuristic
      // result.
      this.speculativeConnect(queryContext, 0, "resultsadded");
    }

    this._notify("onQueryResults", queryContext);
    // Update lastResultCount after notifying, so the view can use it.
    queryContext.lastResultCount = queryContext.results.length;
  }

  /**
   * Adds a listener for query actions and results.
   *
   * @param {object} listener The listener to add.
   * @throws {TypeError} Throws if the listener is not an object.
   */
  addQueryListener(listener) {
    if (!listener || typeof listener != "object") {
      throw new TypeError("Expected listener to be an object");
    }
    this._listeners.add(listener);
  }

  /**
   * Removes a query listener.
   *
   * @param {object} listener The listener to add.
   */
  removeQueryListener(listener) {
    this._listeners.delete(listener);
  }

  /**
   * When the containing context changes (for example when switching tabs),
   * clear any caches that connects consecutive searches in the same context.
   * For example it can be used to clear information used to improve autofill
   * or save resourced on repeated searches.
   */
  viewContextChanged() {
    this.cancelQuery();
    this._notify("onViewContextChanged");
  }

  /**
   * Checks whether a keyboard event that would normally open the view should
   * instead be handled natively by the input field.
   * On certain platforms, the up and down keys can be used to move the caret,
   * in which case we only want to open the view if the caret is at the
   * start or end of the input.
   *
   * @param {KeyboardEvent} event
   *   The DOM KeyboardEvent.
   * @returns {boolean}
   *   Returns true if the event should move the caret instead of opening the
   *   view.
   */
  keyEventMovesCaret(event) {
    if (this.view.isOpen) {
      return false;
    }
    if (AppConstants.platform != "macosx" &&
        AppConstants.platform != "linux") {
      return false;
    }
    let isArrowUp = event.keyCode == KeyEvent.DOM_VK_UP;
    let isArrowDown = event.keyCode == KeyEvent.DOM_VK_DOWN;
    if (!isArrowUp && !isArrowDown) {
      return false;
    }
    let start = this.input.selectionStart;
    let end = this.input.selectionEnd;
    if (end != start ||
        (isArrowUp && start > 0) ||
        (isArrowDown && end < this.input.textValue.length)) {
      return true;
    }
    return false;
  }

  /**
   * Receives keyboard events from the input and handles those that should
   * navigate within the view or pick the currently selected item.
   *
   * @param {KeyboardEvent} event
   *   The DOM KeyboardEvent.
   */
  handleKeyNavigation(event) {
    const isMac = AppConstants.platform == "macosx";
    // Handle readline/emacs-style navigation bindings on Mac.
    if (isMac &&
        this.view.isOpen &&
        event.ctrlKey &&
        (event.key == "n" || event.key == "p")) {
      this.view.selectBy(1, { reverse: event.key == "p" });
      event.preventDefault();
      return;
    }

    if (this.view.isOpen) {
      let queryContext = this._lastQueryContext;
      if (queryContext) {
        this.view.oneOffSearchButtons.handleKeyPress(
          event,
          queryContext.results.length,
          this.view.allowEmptySelection,
          queryContext.searchString);
        if (event.defaultPrevented) {
          return;
        }
      }
    }

    switch (event.keyCode) {
      case KeyEvent.DOM_VK_ESCAPE:
        this.input.handleRevert();
        event.preventDefault();
        break;
      case KeyEvent.DOM_VK_RETURN:
        if (isMac &&
            event.metaKey) {
          // Prevent beep on Mac.
          event.preventDefault();
        }
        this.input.handleCommand(event);
        break;
      case KeyEvent.DOM_VK_TAB:
        if (this.view.isOpen) {
          this.view.selectBy(1, { reverse: event.shiftKey });
          this.userSelectionBehavior = "tab";
          event.preventDefault();
        }
        break;
      case KeyEvent.DOM_VK_DOWN:
      case KeyEvent.DOM_VK_UP:
      case KeyEvent.DOM_VK_PAGE_DOWN:
      case KeyEvent.DOM_VK_PAGE_UP:
        if (event.ctrlKey || event.altKey) {
          break;
        }
        if (this.view.isOpen) {
          this.userSelectionBehavior = "arrow";
          this.view.selectBy(
            event.keyCode == KeyEvent.DOM_VK_PAGE_DOWN ||
            event.keyCode == KeyEvent.DOM_VK_PAGE_UP ?
              5 : 1,
            { reverse: event.keyCode == KeyEvent.DOM_VK_UP ||
                       event.keyCode == KeyEvent.DOM_VK_PAGE_UP });
        } else {
          if (this.keyEventMovesCaret(event)) {
            break;
          }
          this.input.startQuery();
        }
        event.preventDefault();
        break;
      case KeyEvent.DOM_VK_DELETE:
      case KeyEvent.DOM_VK_BACK_SPACE:
        if (event.shiftKey && this.view.isOpen && this._handleDeleteEntry()) {
          event.preventDefault();
        }
        break;
    }
  }

  /**
   * Tries to initialize a speculative connection on a result.
   * Speculative connections are only supported for a subset of all the results.
   * @param {UrlbarQueryContext} context The queryContext
   * @param {number} resultIndex index of the result to speculative connect to.
   * @param {string} reason Reason for the speculative connect request.
   * @note speculative connect to:
   *  - Search engine heuristic results
   *  - autofill results
   *  - http/https results
   */
  speculativeConnect(context, resultIndex, reason) {
    // Never speculative connect in private contexts.
    if (!this.input || context.isPrivate || context.results.length == 0) {
      return;
    }
    let result = context.results[resultIndex];
    let {url} = UrlbarUtils.getUrlFromResult(result);
    if (!url) {
      return;
    }

    switch (reason) {
      case "resultsadded": {
        // We should connect to an heuristic result, if it exists.
        if ((resultIndex == 0 && context.preselected) || result.autofill) {
          if (result.type == UrlbarUtils.RESULT_TYPE.SEARCH) {
            // Speculative connect only if search suggestions are enabled.
            if (UrlbarPrefs.get("suggest.searches") &&
                UrlbarPrefs.get("browser.search.suggest.enabled")) {
              let engine = Services.search.defaultEngine;
              UrlbarUtils.setupSpeculativeConnection(engine, this.browserWindow);
            }
          } else if (result.autofill) {
            UrlbarUtils.setupSpeculativeConnection(url, this.browserWindow);
          }
        }
        return;
      }
      case "mousedown": {
        // On mousedown, connect only to http/https urls.
        if (url.startsWith("http")) {
          UrlbarUtils.setupSpeculativeConnection(url, this.browserWindow);
        }
        return;
      }
      default: {
        throw new Error("Invalid speculative connection reason");
      }
    }
  }

  /**
   * Stores the selection behavior that the user has used to select a result.
   *
   * @param {"arrow"|"tab"|"none"} behavior
   *   The behavior the user used.
   */
  set userSelectionBehavior(behavior) {
    // Don't change the behavior to arrow if tab has already been recorded,
    // as we want to know that the tab was used first.
    if (behavior == "arrow" && this._userSelectionBehavior == "tab") {
      return;
    }
    this._userSelectionBehavior = behavior;
  }

  /**
   * Records details of the selected result in telemetry. We only record the
   * selection behavior, type and index.
   *
   * @param {Event} event
   *   The event which triggered the result to be selected.
   * @param {number} resultIndex
   *   The index of the result.
   */
  recordSelectedResult(event, resultIndex) {
    let result;
    let selectedResult = -1;

    if (resultIndex >= 0) {
      result = this.view.getResult(resultIndex);
      // Except for the history popup, the urlbar always has a selection.  The
      // first result at index 0 is the "heuristic" result that indicates what
      // will happen when you press the Enter key.  Treat it as no selection.
      selectedResult = resultIndex > 0 || !result.heuristic ? resultIndex : -1;
    }
    BrowserUsageTelemetry.recordUrlbarSelectedResultMethod(
      event, selectedResult, this._userSelectionBehavior);

    if (!result) {
      return;
    }

    let telemetryType;
    switch (result.type) {
      case UrlbarUtils.RESULT_TYPE.TAB_SWITCH:
        telemetryType = "switchtab";
        break;
      case UrlbarUtils.RESULT_TYPE.SEARCH:
        telemetryType = result.payload.suggestion ? "searchsuggestion" : "searchengine";
        break;
      case UrlbarUtils.RESULT_TYPE.URL:
        if (result.autofill) {
          telemetryType = "autofill";
        } else if (result.source == UrlbarUtils.RESULT_SOURCE.OTHER_LOCAL &&
                   result.heuristic) {
          telemetryType = "visiturl";
        } else {
          telemetryType = result.source == UrlbarUtils.RESULT_SOURCE.BOOKMARKS ? "bookmark" : "history";
        }
        break;
      case UrlbarUtils.RESULT_TYPE.KEYWORD:
        telemetryType = "keyword";
        break;
      case UrlbarUtils.RESULT_TYPE.OMNIBOX:
        telemetryType = "extension";
        break;
      case UrlbarUtils.RESULT_TYPE.REMOTE_TAB:
        telemetryType = "remotetab";
        break;
      default:
        Cu.reportError(`Unknown Result Type ${result.type}`);
        return;
    }

    Services.telemetry
            .getHistogramById("FX_URLBAR_SELECTED_RESULT_INDEX")
            .add(resultIndex);
    // You can add values but don't change any of the existing values.
    // Otherwise you'll break our data.
    if (telemetryType in URLBAR_SELECTED_RESULT_TYPES) {
      Services.telemetry
              .getHistogramById("FX_URLBAR_SELECTED_RESULT_TYPE")
              .add(URLBAR_SELECTED_RESULT_TYPES[telemetryType]);
      Services.telemetry
              .getKeyedHistogramById("FX_URLBAR_SELECTED_RESULT_INDEX_BY_TYPE")
              .add(telemetryType, resultIndex);
    } else {
      Cu.reportError("Unknown FX_URLBAR_SELECTED_RESULT_TYPE type: " +
                     telemetryType);
    }
  }

  /**
   * Internal function handling deletion of entries. We only support removing
   * of history entries - other result sources will be ignored.
   *
   * @returns {boolean} Returns true if the deletion was acted upon.
   */
  _handleDeleteEntry() {
    if (!this._lastQueryContext) {
      Cu.reportError("Cannot delete - the latest query is not present");
      return false;
    }

    const selectedResult = this.input.view.selectedResult;
    if (!selectedResult ||
        selectedResult.source != UrlbarUtils.RESULT_SOURCE.HISTORY) {
      return false;
    }

    let index = this._lastQueryContext.results.indexOf(selectedResult);
    if (!index) {
      Cu.reportError("Failed to find the selected result in the results");
      return false;
    }

    this._lastQueryContext.results.splice(index, 1);
    this._notify("onQueryResultRemoved", index);

    PlacesUtils.history.remove(selectedResult.payload.url).catch(Cu.reportError);
    return true;
  }

  /**
   * Internal function to notify listeners of results.
   *
   * @param {string} name Name of the notification.
   * @param {object} params Parameters to pass with the notification.
   */
  _notify(name, ...params) {
    for (let listener of this._listeners) {
      // Can't use "in" because some tests proxify these.
      if (typeof listener[name] != "undefined") {
        try {
          listener[name](...params);
        } catch (ex) {
          Cu.reportError(ex);
        }
      }
    }
  }
}
