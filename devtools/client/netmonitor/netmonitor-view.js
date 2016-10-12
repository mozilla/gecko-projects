/* -*- indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim: set ft=javascript ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
/* import-globals-from ./netmonitor-controller.js */
/* globals Prefs, gNetwork, setInterval, setTimeout, clearInterval, clearTimeout, btoa */
/* exported $, $all */
"use strict";

XPCOMUtils.defineLazyGetter(this, "NetworkHelper", function () {
  return require("devtools/shared/webconsole/network-helper");
});

const {VariablesView} = require("resource://devtools/client/shared/widgets/VariablesView.jsm");
const {VariablesViewController} = require("resource://devtools/client/shared/widgets/VariablesViewController.jsm");
const {ToolSidebar} = require("devtools/client/framework/sidebar");
const { testing: isTesting } = require("devtools/shared/flags");
const {ViewHelpers, Heritage} = require("devtools/client/shared/widgets/view-helpers");
const {PluralForm} = require("devtools/shared/plural-form");
const {Filters} = require("./filter-predicates");
const {getFormDataSections, formDataURI, writeHeaderText, getKeyWithEvent} = require("./request-utils");
const {L10N} = require("./l10n");
const {RequestsMenuView} = require("./requests-menu-view");

// ms
const WDA_DEFAULT_VERIFY_INTERVAL = 50;

// Use longer timeout during testing as the tests need this process to succeed
// and two seconds is quite short on slow debug builds. The timeout here should
// be at least equal to the general mochitest timeout of 45 seconds so that this
// never gets hit during testing.
// ms
const WDA_DEFAULT_GIVE_UP_TIMEOUT = isTesting ? 45000 : 2000;

// 100 KB in bytes
const SOURCE_SYNTAX_HIGHLIGHT_MAX_FILE_SIZE = 102400;
const REQUEST_TIME_DECIMALS = 2;
const HEADERS_SIZE_DECIMALS = 3;
const CONTENT_SIZE_DECIMALS = 2;
const CONTENT_MIME_TYPE_MAPPINGS = {
  "/ecmascript": Editor.modes.js,
  "/javascript": Editor.modes.js,
  "/x-javascript": Editor.modes.js,
  "/html": Editor.modes.html,
  "/xhtml": Editor.modes.html,
  "/xml": Editor.modes.html,
  "/atom": Editor.modes.html,
  "/soap": Editor.modes.html,
  "/vnd.mpeg.dash.mpd": Editor.modes.html,
  "/rdf": Editor.modes.css,
  "/rss": Editor.modes.css,
  "/css": Editor.modes.css
};

const DEFAULT_EDITOR_CONFIG = {
  mode: Editor.modes.text,
  readOnly: true,
  lineNumbers: true
};
const GENERIC_VARIABLES_VIEW_SETTINGS = {
  lazyEmpty: true,
  // ms
  lazyEmptyDelay: 10,
  searchEnabled: true,
  editableValueTooltip: "",
  editableNameTooltip: "",
  preventDisableOnChange: true,
  preventDescriptorModifiers: true,
  eval: () => {}
};
// px
const NETWORK_ANALYSIS_PIE_CHART_DIAMETER = 200;

/**
 * Object defining the network monitor view components.
 */
var NetMonitorView = {
  /**
   * Initializes the network monitor view.
   */
  initialize: function () {
    this._initializePanes();

    this.Toolbar.initialize();
    this.RequestsMenu.initialize();
    this.NetworkDetails.initialize();
    this.CustomRequest.initialize();
  },

  /**
   * Destroys the network monitor view.
   */
  destroy: function () {
    this._isDestroyed = true;
    this.Toolbar.destroy();
    this.RequestsMenu.destroy();
    this.NetworkDetails.destroy();
    this.CustomRequest.destroy();

    this._destroyPanes();
  },

  /**
   * Initializes the UI for all the displayed panes.
   */
  _initializePanes: function () {
    dumpn("Initializing the NetMonitorView panes");

    this._body = $("#body");
    this._detailsPane = $("#details-pane");
    this._detailsPaneToggleButton = $("#details-pane-toggle");

    this._collapsePaneString = L10N.getStr("collapseDetailsPane");
    this._expandPaneString = L10N.getStr("expandDetailsPane");

    this._detailsPane.setAttribute("width", Prefs.networkDetailsWidth);
    this._detailsPane.setAttribute("height", Prefs.networkDetailsHeight);
    this.toggleDetailsPane({ visible: false });

    // Disable the performance statistics mode.
    if (!Prefs.statistics) {
      $("#request-menu-context-perf").hidden = true;
      $("#notice-perf-message").hidden = true;
      $("#requests-menu-network-summary-button").hidden = true;
    }
  },

  /**
   * Destroys the UI for all the displayed panes.
   */
  _destroyPanes: Task.async(function* () {
    dumpn("Destroying the NetMonitorView panes");

    Prefs.networkDetailsWidth = this._detailsPane.getAttribute("width");
    Prefs.networkDetailsHeight = this._detailsPane.getAttribute("height");

    this._detailsPane = null;
    this._detailsPaneToggleButton = null;

    for (let p of this._editorPromises.values()) {
      let editor = yield p;
      editor.destroy();
    }
  }),

  /**
   * Gets the visibility state of the network details pane.
   * @return boolean
   */
  get detailsPaneHidden() {
    return this._detailsPane.classList.contains("pane-collapsed");
  },

  /**
   * Sets the network details pane hidden or visible.
   *
   * @param object flags
   *        An object containing some of the following properties:
   *        - visible: true if the pane should be shown, false to hide
   *        - animated: true to display an animation on toggle
   *        - delayed: true to wait a few cycles before toggle
   *        - callback: a function to invoke when the toggle finishes
   * @param number tabIndex [optional]
   *        The index of the intended selected tab in the details pane.
   */
  toggleDetailsPane: function (flags, tabIndex) {
    let pane = this._detailsPane;
    let button = this._detailsPaneToggleButton;

    ViewHelpers.togglePane(flags, pane);

    if (flags.visible) {
      this._body.classList.remove("pane-collapsed");
      button.classList.remove("pane-collapsed");
      button.setAttribute("tooltiptext", this._collapsePaneString);
    } else {
      this._body.classList.add("pane-collapsed");
      button.classList.add("pane-collapsed");
      button.setAttribute("tooltiptext", this._expandPaneString);
    }

    if (tabIndex !== undefined) {
      $("#event-details-pane").selectedIndex = tabIndex;
    }
  },

  /**
   * Gets the current mode for this tool.
   * @return string (e.g, "network-inspector-view" or "network-statistics-view")
   */
  get currentFrontendMode() {
    // The getter may be called from a timeout after the panel is destroyed.
    if (!this._body.selectedPanel) {
      return null;
    }
    return this._body.selectedPanel.id;
  },

  /**
   * Toggles between the frontend view modes ("Inspector" vs. "Statistics").
   */
  toggleFrontendMode: function () {
    if (this.currentFrontendMode != "network-inspector-view") {
      this.showNetworkInspectorView();
    } else {
      this.showNetworkStatisticsView();
    }
  },

  /**
   * Switches to the "Inspector" frontend view mode.
   */
  showNetworkInspectorView: function () {
    this._body.selectedPanel = $("#network-inspector-view");
    this.RequestsMenu._flushWaterfallViews(true);
  },

  /**
   * Switches to the "Statistics" frontend view mode.
   */
  showNetworkStatisticsView: function () {
    this._body.selectedPanel = $("#network-statistics-view");

    let controller = NetMonitorController;
    let requestsView = this.RequestsMenu;
    let statisticsView = this.PerformanceStatistics;

    Task.spawn(function* () {
      statisticsView.displayPlaceholderCharts();
      yield controller.triggerActivity(ACTIVITY_TYPE.RELOAD.WITH_CACHE_ENABLED);

      try {
        // • The response headers and status code are required for determining
        // whether a response is "fresh" (cacheable).
        // • The response content size and request total time are necessary for
        // populating the statistics view.
        // • The response mime type is used for categorization.
        yield whenDataAvailable(requestsView.attachments, [
          "responseHeaders", "status", "contentSize", "mimeType", "totalTime"
        ]);
      } catch (ex) {
        // Timed out while waiting for data. Continue with what we have.
        console.error(ex);
      }

      statisticsView.createPrimedCacheChart(requestsView.items);
      statisticsView.createEmptyCacheChart(requestsView.items);
    });
  },

  reloadPage: function () {
    NetMonitorController.triggerActivity(
      ACTIVITY_TYPE.RELOAD.WITH_CACHE_DEFAULT);
  },

  /**
   * Lazily initializes and returns a promise for a Editor instance.
   *
   * @param string id
   *        The id of the editor placeholder node.
   * @return object
   *         A promise that is resolved when the editor is available.
   */
  editor: function (id) {
    dumpn("Getting a NetMonitorView editor: " + id);

    if (this._editorPromises.has(id)) {
      return this._editorPromises.get(id);
    }

    let deferred = promise.defer();
    this._editorPromises.set(id, deferred.promise);

    // Initialize the source editor and store the newly created instance
    // in the ether of a resolved promise's value.
    let editor = new Editor(DEFAULT_EDITOR_CONFIG);
    editor.appendTo($(id)).then(() => deferred.resolve(editor));

    return deferred.promise;
  },

  _body: null,
  _detailsPane: null,
  _detailsPaneToggleButton: null,
  _collapsePaneString: "",
  _expandPaneString: "",
  _editorPromises: new Map()
};

/**
 * Functions handling the toolbar view: expand/collapse button etc.
 */
function ToolbarView() {
  dumpn("ToolbarView was instantiated");

  this._onTogglePanesPressed = this._onTogglePanesPressed.bind(this);
}

ToolbarView.prototype = {
  /**
   * Initialization function, called when the debugger is started.
   */
  initialize: function () {
    dumpn("Initializing the ToolbarView");

    this._detailsPaneToggleButton = $("#details-pane-toggle");
    this._detailsPaneToggleButton.addEventListener("mousedown",
      this._onTogglePanesPressed, false);
  },

  /**
   * Destruction function, called when the debugger is closed.
   */
  destroy: function () {
    dumpn("Destroying the ToolbarView");

    this._detailsPaneToggleButton.removeEventListener("mousedown",
      this._onTogglePanesPressed, false);
  },

  /**
   * Listener handling the toggle button click event.
   */
  _onTogglePanesPressed: function () {
    let requestsMenu = NetMonitorView.RequestsMenu;
    let selectedIndex = requestsMenu.selectedIndex;

    // Make sure there's a selection if the button is pressed, to avoid
    // showing an empty network details pane.
    if (selectedIndex == -1 && requestsMenu.itemCount) {
      requestsMenu.selectedIndex = 0;
    } else {
      requestsMenu.selectedIndex = -1;
    }
  },

  _detailsPaneToggleButton: null
};

/**
 * Functions handling the sidebar details view.
 */
function SidebarView() {
  dumpn("SidebarView was instantiated");
}

SidebarView.prototype = {
  /**
   * Sets this view hidden or visible. It's visible by default.
   *
   * @param boolean visibleFlag
   *        Specifies the intended visibility.
   */
  toggle: function (visibleFlag) {
    NetMonitorView.toggleDetailsPane({ visible: visibleFlag });
    NetMonitorView.RequestsMenu._flushWaterfallViews(true);
  },

  /**
   * Populates this view with the specified data.
   *
   * @param object data
   *        The data source (this should be the attachment of a request item).
   * @return object
   *        Returns a promise that resolves upon population of the subview.
   */
  populate: Task.async(function* (data) {
    let isCustom = data.isCustom;
    let view = isCustom ?
      NetMonitorView.CustomRequest :
      NetMonitorView.NetworkDetails;

    yield view.populate(data);
    $("#details-pane").selectedIndex = isCustom ? 0 : 1;

    window.emit(EVENTS.SIDEBAR_POPULATED);
  })
};

/**
 * Functions handling the custom request view.
 */
function CustomRequestView() {
  dumpn("CustomRequestView was instantiated");
}

CustomRequestView.prototype = {
  /**
   * Initialization function, called when the network monitor is started.
   */
  initialize: function () {
    dumpn("Initializing the CustomRequestView");

    this.updateCustomRequestEvent = getKeyWithEvent(this.onUpdate.bind(this));
    $("#custom-pane").addEventListener("input",
      this.updateCustomRequestEvent, false);
  },

  /**
   * Destruction function, called when the network monitor is closed.
   */
  destroy: function () {
    dumpn("Destroying the CustomRequestView");

    $("#custom-pane").removeEventListener("input",
      this.updateCustomRequestEvent, false);
  },

  /**
   * Populates this view with the specified data.
   *
   * @param object data
   *        The data source (this should be the attachment of a request item).
   * @return object
   *        Returns a promise that resolves upon population the view.
   */
  populate: Task.async(function* (data) {
    $("#custom-url-value").value = data.url;
    $("#custom-method-value").value = data.method;
    this.updateCustomQuery(data.url);

    if (data.requestHeaders) {
      let headers = data.requestHeaders.headers;
      $("#custom-headers-value").value = writeHeaderText(headers);
    }
    if (data.requestPostData) {
      let postData = data.requestPostData.postData.text;
      $("#custom-postdata-value").value = yield gNetwork.getString(postData);
    }

    window.emit(EVENTS.CUSTOMREQUESTVIEW_POPULATED);
  }),

  /**
   * Handle user input in the custom request form.
   *
   * @param object field
   *        the field that the user updated.
   */
  onUpdate: function (field) {
    let selectedItem = NetMonitorView.RequestsMenu.selectedItem;
    let value;

    switch (field) {
      case "method":
        value = $("#custom-method-value").value.trim();
        selectedItem.attachment.method = value;
        break;
      case "url":
        value = $("#custom-url-value").value;
        this.updateCustomQuery(value);
        selectedItem.attachment.url = value;
        break;
      case "query":
        let query = $("#custom-query-value").value;
        this.updateCustomUrl(query);
        field = "url";
        value = $("#custom-url-value").value;
        selectedItem.attachment.url = value;
        break;
      case "body":
        value = $("#custom-postdata-value").value;
        selectedItem.attachment.requestPostData = { postData: { text: value } };
        break;
      case "headers":
        let headersText = $("#custom-headers-value").value;
        value = parseHeadersText(headersText);
        selectedItem.attachment.requestHeaders = { headers: value };
        break;
    }

    NetMonitorView.RequestsMenu.updateMenuView(selectedItem, field, value);
  },

  /**
   * Update the query string field based on the url.
   *
   * @param object url
   *        The URL to extract query string from.
   */
  updateCustomQuery: function (url) {
    let paramsArray = NetworkHelper.parseQueryString(
      NetworkHelper.nsIURL(url).query);

    if (!paramsArray) {
      $("#custom-query").hidden = true;
      return;
    }

    $("#custom-query").hidden = false;
    $("#custom-query-value").value = writeQueryText(paramsArray);
  },

  /**
   * Update the url based on the query string field.
   *
   * @param object queryText
   *        The contents of the query string field.
   */
  updateCustomUrl: function (queryText) {
    let params = parseQueryText(queryText);
    let queryString = writeQueryString(params);

    let url = $("#custom-url-value").value;
    let oldQuery = NetworkHelper.nsIURL(url).query;
    let path = url.replace(oldQuery, queryString);

    $("#custom-url-value").value = path;
  }
};

/**
 * Functions handling the requests details view.
 */
function NetworkDetailsView() {
  dumpn("NetworkDetailsView was instantiated");

  // The ToolSidebar requires the panel object to be able to emit events.
  EventEmitter.decorate(this);

  this._onTabSelect = this._onTabSelect.bind(this);
}

NetworkDetailsView.prototype = {
  /**
   * An object containing the state of tabs.
   */
  _viewState: {
    // if updating[tab] is true a task is currently updating the given tab.
    updating: [],
    // if dirty[tab] is true, the tab needs to be repopulated once current
    // update task finishes
    dirty: [],
    // the most recently received attachment data for the request
    latestData: null,
  },

  /**
   * Initialization function, called when the network monitor is started.
   */
  initialize: function () {
    dumpn("Initializing the NetworkDetailsView");

    this.widget = $("#event-details-pane");
    this.sidebar = new ToolSidebar(this.widget, this, "netmonitor", {
      disableTelemetry: true,
      showAllTabsMenu: true
    });

    this._headers = new VariablesView($("#all-headers"),
      Heritage.extend(GENERIC_VARIABLES_VIEW_SETTINGS, {
        emptyText: L10N.getStr("headersEmptyText"),
        searchPlaceholder: L10N.getStr("headersFilterText")
      }));
    this._cookies = new VariablesView($("#all-cookies"),
      Heritage.extend(GENERIC_VARIABLES_VIEW_SETTINGS, {
        emptyText: L10N.getStr("cookiesEmptyText"),
        searchPlaceholder: L10N.getStr("cookiesFilterText")
      }));
    this._params = new VariablesView($("#request-params"),
      Heritage.extend(GENERIC_VARIABLES_VIEW_SETTINGS, {
        emptyText: L10N.getStr("paramsEmptyText"),
        searchPlaceholder: L10N.getStr("paramsFilterText")
      }));
    this._json = new VariablesView($("#response-content-json"),
      Heritage.extend(GENERIC_VARIABLES_VIEW_SETTINGS, {
        onlyEnumVisible: true,
        searchPlaceholder: L10N.getStr("jsonFilterText")
      }));
    VariablesViewController.attach(this._json);

    this._paramsQueryString = L10N.getStr("paramsQueryString");
    this._paramsFormData = L10N.getStr("paramsFormData");
    this._paramsPostPayload = L10N.getStr("paramsPostPayload");
    this._requestHeaders = L10N.getStr("requestHeaders");
    this._requestHeadersFromUpload = L10N.getStr("requestHeadersFromUpload");
    this._responseHeaders = L10N.getStr("responseHeaders");
    this._requestCookies = L10N.getStr("requestCookies");
    this._responseCookies = L10N.getStr("responseCookies");

    $("tabpanels", this.widget).addEventListener("select", this._onTabSelect);
  },

  /**
   * Destruction function, called when the network monitor is closed.
   */
  destroy: function () {
    dumpn("Destroying the NetworkDetailsView");
    this.sidebar.destroy();
    $("tabpanels", this.widget).removeEventListener("select",
      this._onTabSelect);
  },

  /**
   * Populates this view with the specified data.
   *
   * @param object data
   *        The data source (this should be the attachment of a request item).
   * @return object
   *        Returns a promise that resolves upon population the view.
   */
  populate: function (data) {
    $("#request-params-box").setAttribute("flex", "1");
    $("#request-params-box").hidden = false;
    $("#request-post-data-textarea-box").hidden = true;
    $("#response-content-info-header").hidden = true;
    $("#response-content-json-box").hidden = true;
    $("#response-content-textarea-box").hidden = true;
    $("#raw-headers").hidden = true;
    $("#response-content-image-box").hidden = true;

    let isHtml = Filters.html(data);

    // Show the "Preview" tabpanel only for plain HTML responses.
    this.sidebar.toggleTab(isHtml, "preview-tab");

    // Show the "Security" tab only for requests that
    //   1) are https (state != insecure)
    //   2) come from a target that provides security information.
    let hasSecurityInfo = data.securityState &&
                          data.securityState !== "insecure";
    this.sidebar.toggleTab(hasSecurityInfo, "security-tab");

    // Switch to the "Headers" tabpanel if the "Preview" previously selected
    // and this is not an HTML response or "Security" was selected but this
    // request has no security information.

    if (!isHtml && this.widget.selectedPanel === $("#preview-tabpanel") ||
        !hasSecurityInfo && this.widget.selectedPanel ===
          $("#security-tabpanel")) {
      this.widget.selectedIndex = 0;
    }

    this._headers.empty();
    this._cookies.empty();
    this._params.empty();
    this._json.empty();

    this._dataSrc = { src: data, populated: [] };
    this._onTabSelect();
    window.emit(EVENTS.NETWORKDETAILSVIEW_POPULATED);

    return promise.resolve();
  },

  /**
   * Listener handling the tab selection event.
   */
  _onTabSelect: function () {
    let { src, populated } = this._dataSrc || {};
    let tab = this.widget.selectedIndex;
    let view = this;

    // Make sure the data source is valid and don't populate the same tab twice.
    if (!src || populated[tab]) {
      return;
    }

    let viewState = this._viewState;
    if (viewState.updating[tab]) {
      // A task is currently updating this tab. If we started another update
      // task now it would result in a duplicated content as described in bugs
      // 997065 and 984687. As there's no way to stop the current task mark the
      // tab dirty and refresh the panel once the current task finishes.
      viewState.dirty[tab] = true;
      viewState.latestData = src;
      return;
    }

    Task.spawn(function* () {
      viewState.updating[tab] = true;
      switch (tab) {
        // "Headers"
        case 0:
          yield view._setSummary(src);
          yield view._setResponseHeaders(src.responseHeaders);
          yield view._setRequestHeaders(
            src.requestHeaders,
            src.requestHeadersFromUploadStream);
          break;
        // "Cookies"
        case 1:
          yield view._setResponseCookies(src.responseCookies);
          yield view._setRequestCookies(src.requestCookies);
          break;
        // "Params"
        case 2:
          yield view._setRequestGetParams(src.url);
          yield view._setRequestPostParams(
            src.requestHeaders,
            src.requestHeadersFromUploadStream,
            src.requestPostData);
          break;
        // "Response"
        case 3:
          yield view._setResponseBody(src.url, src.responseContent);
          break;
        // "Timings"
        case 4:
          yield view._setTimingsInformation(src.eventTimings);
          break;
        // "Security"
        case 5:
          yield view._setSecurityInfo(src.securityInfo, src.url);
          break;
        // "Preview"
        case 6:
          yield view._setHtmlPreview(src.responseContent);
          break;
      }
      viewState.updating[tab] = false;
    }).then(() => {
      if (tab == this.widget.selectedIndex) {
        if (viewState.dirty[tab]) {
          // The request information was updated while the task was running.
          viewState.dirty[tab] = false;
          view.populate(viewState.latestData);
        } else {
          // Tab is selected but not dirty. We're done here.
          populated[tab] = true;
          window.emit(EVENTS.TAB_UPDATED);

          if (NetMonitorController.isConnected()) {
            NetMonitorView.RequestsMenu.ensureSelectedItemIsVisible();
          }
        }
      } else if (viewState.dirty[tab]) {
        // Tab is dirty but no longer selected. Don't refresh it now, it'll be
        // done if the tab is shown again.
        viewState.dirty[tab] = false;
      }
    }, e => console.error(e));
  },

  /**
   * Sets the network request summary shown in this view.
   *
   * @param object data
   *        The data source (this should be the attachment of a request item).
   */
  _setSummary: function (data) {
    if (data.url) {
      let unicodeUrl = NetworkHelper.convertToUnicode(unescape(data.url));
      $("#headers-summary-url-value").setAttribute("value", unicodeUrl);
      $("#headers-summary-url-value").setAttribute("tooltiptext", unicodeUrl);
      $("#headers-summary-url").removeAttribute("hidden");
    } else {
      $("#headers-summary-url").setAttribute("hidden", "true");
    }

    if (data.method) {
      $("#headers-summary-method-value").setAttribute("value", data.method);
      $("#headers-summary-method").removeAttribute("hidden");
    } else {
      $("#headers-summary-method").setAttribute("hidden", "true");
    }

    if (data.remoteAddress) {
      let address = data.remoteAddress;
      if (address.indexOf(":") != -1) {
        address = `[${address}]`;
      }
      if (data.remotePort) {
        address += `:${data.remotePort}`;
      }
      $("#headers-summary-address-value").setAttribute("value", address);
      $("#headers-summary-address-value").setAttribute("tooltiptext", address);
      $("#headers-summary-address").removeAttribute("hidden");
    } else {
      $("#headers-summary-address").setAttribute("hidden", "true");
    }

    if (data.status) {
      // "code" attribute is only used by css to determine the icon color
      let code;
      if (data.fromCache) {
        code = "cached";
      } else if (data.fromServiceWorker) {
        code = "service worker";
      } else {
        code = data.status;
      }
      $("#headers-summary-status-circle").setAttribute("code", code);
      $("#headers-summary-status-value").setAttribute("value",
        data.status + " " + data.statusText);
      $("#headers-summary-status").removeAttribute("hidden");
    } else {
      $("#headers-summary-status").setAttribute("hidden", "true");
    }

    if (data.httpVersion) {
      $("#headers-summary-version-value").setAttribute("value",
        data.httpVersion);
      $("#headers-summary-version").removeAttribute("hidden");
    } else {
      $("#headers-summary-version").setAttribute("hidden", "true");
    }
  },

  /**
   * Sets the network request headers shown in this view.
   *
   * @param object headers
   *        The "requestHeaders" message received from the server.
   * @param object uploadHeaders
   *        The "requestHeadersFromUploadStream" inferred from the POST payload.
   * @return object
   *        A promise that resolves when request headers are set.
   */
  _setRequestHeaders: Task.async(function* (headers, uploadHeaders) {
    if (headers && headers.headers.length) {
      yield this._addHeaders(this._requestHeaders, headers);
    }
    if (uploadHeaders && uploadHeaders.headers.length) {
      yield this._addHeaders(this._requestHeadersFromUpload, uploadHeaders);
    }
  }),

  /**
   * Sets the network response headers shown in this view.
   *
   * @param object response
   *        The message received from the server.
   * @return object
   *        A promise that resolves when response headers are set.
   */
  _setResponseHeaders: Task.async(function* (response) {
    if (response && response.headers.length) {
      response.headers.sort((a, b) => a.name > b.name);
      yield this._addHeaders(this._responseHeaders, response);
    }
  }),

  /**
   * Populates the headers container in this view with the specified data.
   *
   * @param string name
   *        The type of headers to populate (request or response).
   * @param object response
   *        The message received from the server.
   * @return object
   *        A promise that resolves when headers are added.
   */
  _addHeaders: Task.async(function* (name, response) {
    let kb = response.headersSize / 1024;
    let size = L10N.numberWithDecimals(kb, HEADERS_SIZE_DECIMALS);
    let text = L10N.getFormatStr("networkMenu.sizeKB", size);

    let headersScope = this._headers.addScope(name + " (" + text + ")");
    headersScope.expanded = true;

    for (let header of response.headers) {
      let headerVar = headersScope.addItem(header.name, {}, {relaxed: true});
      let headerValue = yield gNetwork.getString(header.value);
      headerVar.setGrip(headerValue);
    }
  }),

  /**
   * Sets the network request cookies shown in this view.
   *
   * @param object response
   *        The message received from the server.
   * @return object
   *        A promise that is resolved when the request cookies are set.
   */
  _setRequestCookies: Task.async(function* (response) {
    if (response && response.cookies.length) {
      response.cookies.sort((a, b) => a.name > b.name);
      yield this._addCookies(this._requestCookies, response);
    }
  }),

  /**
   * Sets the network response cookies shown in this view.
   *
   * @param object response
   *        The message received from the server.
   * @return object
   *        A promise that is resolved when the response cookies are set.
   */
  _setResponseCookies: Task.async(function* (response) {
    if (response && response.cookies.length) {
      yield this._addCookies(this._responseCookies, response);
    }
  }),

  /**
   * Populates the cookies container in this view with the specified data.
   *
   * @param string name
   *        The type of cookies to populate (request or response).
   * @param object response
   *        The message received from the server.
   * @return object
   *        Returns a promise that resolves upon the adding of cookies.
   */
  _addCookies: Task.async(function* (name, response) {
    let cookiesScope = this._cookies.addScope(name);
    cookiesScope.expanded = true;

    for (let cookie of response.cookies) {
      let cookieVar = cookiesScope.addItem(cookie.name, {}, {relaxed: true});
      let cookieValue = yield gNetwork.getString(cookie.value);
      cookieVar.setGrip(cookieValue);

      // By default the cookie name and value are shown. If this is the only
      // information available, then nothing else is to be displayed.
      let cookieProps = Object.keys(cookie);
      if (cookieProps.length == 2) {
        continue;
      }

      // Display any other information other than the cookie name and value
      // which may be available.
      let rawObject = Object.create(null);
      let otherProps = cookieProps.filter(e => e != "name" && e != "value");
      for (let prop of otherProps) {
        rawObject[prop] = cookie[prop];
      }
      cookieVar.populate(rawObject);
      cookieVar.twisty = true;
      cookieVar.expanded = true;
    }
  }),

  /**
   * Sets the network request get params shown in this view.
   *
   * @param string url
   *        The request's url.
   */
  _setRequestGetParams: function (url) {
    let query = NetworkHelper.nsIURL(url).query;
    if (query) {
      this._addParams(this._paramsQueryString, query);
    }
  },

  /**
   * Sets the network request post params shown in this view.
   *
   * @param object headers
   *        The "requestHeaders" message received from the server.
   * @param object uploadHeaders
   *        The "requestHeadersFromUploadStream" inferred from the POST payload.
   * @param object postData
   *        The "requestPostData" message received from the server.
   * @return object
   *        A promise that is resolved when the request post params are set.
   */
  _setRequestPostParams: Task.async(function* (headers, uploadHeaders,
    postData) {
    if (!headers || !uploadHeaders || !postData) {
      return;
    }

    let formDataSections = yield getFormDataSections(
      headers,
      uploadHeaders,
      postData,
      gNetwork.getString.bind(gNetwork));

    this._params.onlyEnumVisible = false;

    // Handle urlencoded form data sections (e.g. "?foo=bar&baz=42").
    if (formDataSections.length > 0) {
      formDataSections.forEach(section => {
        this._addParams(this._paramsFormData, section);
      });
    } else {
      // Handle JSON and actual forms ("multipart/form-data" content type).
      let postDataLongString = postData.postData.text;
      let text = yield gNetwork.getString(postDataLongString);
      let jsonVal = null;
      try {
        jsonVal = JSON.parse(text);
      } catch (ex) { // eslint-disable-line
      }

      if (jsonVal) {
        this._params.onlyEnumVisible = true;
        let jsonScopeName = L10N.getStr("jsonScopeName");
        let jsonScope = this._params.addScope(jsonScopeName);
        jsonScope.expanded = true;
        let jsonItem = jsonScope.addItem("", { enumerable: true });
        jsonItem.populate(jsonVal, { sorted: true });
      } else {
        // This is really awkward, but hey, it works. Let's show an empty
        // scope in the params view and place the source editor containing
        // the raw post data directly underneath.
        $("#request-params-box").removeAttribute("flex");
        let paramsScope = this._params.addScope(this._paramsPostPayload);
        paramsScope.expanded = true;
        paramsScope.locked = true;

        $("#request-post-data-textarea-box").hidden = false;
        let editor = yield NetMonitorView.editor("#request-post-data-textarea");
        editor.setMode(Editor.modes.text);
        editor.setText(text);
      }
    }

    window.emit(EVENTS.REQUEST_POST_PARAMS_DISPLAYED);
  }),

  /**
   * Populates the params container in this view with the specified data.
   *
   * @param string name
   *        The type of params to populate (get or post).
   * @param string queryString
   *        A query string of params (e.g. "?foo=bar&baz=42").
   */
  _addParams: function (name, queryString) {
    let paramsArray = NetworkHelper.parseQueryString(queryString);
    if (!paramsArray) {
      return;
    }
    let paramsScope = this._params.addScope(name);
    paramsScope.expanded = true;

    for (let param of paramsArray) {
      let paramVar = paramsScope.addItem(param.name, {}, {relaxed: true});
      paramVar.setGrip(param.value);
    }
  },

  /**
   * Sets the network response body shown in this view.
   *
   * @param string url
   *        The request's url.
   * @param object response
   *        The message received from the server.
   * @return object
   *         A promise that is resolved when the response body is set.
   */
  _setResponseBody: Task.async(function* (url, response) {
    if (!response) {
      return;
    }
    let { mimeType, text, encoding } = response.content;
    let responseBody = yield gNetwork.getString(text);

    // Handle json, which we tentatively identify by checking the MIME type
    // for "json" after any word boundary. This works for the standard
    // "application/json", and also for custom types like "x-bigcorp-json".
    // Additionally, we also directly parse the response text content to
    // verify whether it's json or not, to handle responses incorrectly
    // labeled as text/plain instead.
    let jsonMimeType, jsonObject, jsonObjectParseError;
    try {
      jsonMimeType = /\bjson/.test(mimeType);
      jsonObject = JSON.parse(responseBody);
    } catch (e) {
      jsonObjectParseError = e;
    }
    if (jsonMimeType || jsonObject) {
      // Extract the actual json substring in case this might be a "JSONP".
      // This regex basically parses a function call and captures the
      // function name and arguments in two separate groups.
      let jsonpRegex = /^\s*([\w$]+)\s*\(\s*([^]*)\s*\)\s*;?\s*$/;
      let [_, callbackPadding, jsonpString] = // eslint-disable-line
        responseBody.match(jsonpRegex) || [];

      // Make sure this is a valid JSON object first. If so, nicely display
      // the parsing results in a variables view. Otherwise, simply show
      // the contents as plain text.
      if (callbackPadding && jsonpString) {
        try {
          jsonObject = JSON.parse(jsonpString);
        } catch (e) {
          jsonObjectParseError = e;
        }
      }

      // Valid JSON or JSONP.
      if (jsonObject) {
        $("#response-content-json-box").hidden = false;
        let jsonScopeName = callbackPadding
          ? L10N.getFormatStr("jsonpScopeName", callbackPadding)
          : L10N.getStr("jsonScopeName");

        let jsonVar = { label: jsonScopeName, rawObject: jsonObject };
        yield this._json.controller.setSingleVariable(jsonVar).expanded;
      } else {
        // Malformed JSON.
        $("#response-content-textarea-box").hidden = false;
        let infoHeader = $("#response-content-info-header");
        infoHeader.setAttribute("value", jsonObjectParseError);
        infoHeader.setAttribute("tooltiptext", jsonObjectParseError);
        infoHeader.hidden = false;

        let editor = yield NetMonitorView.editor("#response-content-textarea");
        editor.setMode(Editor.modes.js);
        editor.setText(responseBody);
      }
    } else if (mimeType.includes("image/")) {
      // Handle images.
      $("#response-content-image-box").setAttribute("align", "center");
      $("#response-content-image-box").setAttribute("pack", "center");
      $("#response-content-image-box").hidden = false;
      $("#response-content-image").src = formDataURI(mimeType, encoding, responseBody);

      // Immediately display additional information about the image:
      // file name, mime type and encoding.
      $("#response-content-image-name-value").setAttribute("value",
        NetworkHelper.nsIURL(url).fileName);
      $("#response-content-image-mime-value").setAttribute("value", mimeType);

      // Wait for the image to load in order to display the width and height.
      $("#response-content-image").onload = e => {
        // XUL images are majestic so they don't bother storing their dimensions
        // in width and height attributes like the rest of the folk. Hack around
        // this by getting the bounding client rect and subtracting the margins.
        let { width, height } = e.target.getBoundingClientRect();
        let dimensions = (width - 2) + " \u00D7 " + (height - 2);
        $("#response-content-image-dimensions-value").setAttribute("value",
          dimensions);
      };
    } else {
      $("#response-content-textarea-box").hidden = false;
      let editor = yield NetMonitorView.editor("#response-content-textarea");
      editor.setMode(Editor.modes.text);
      editor.setText(responseBody);

      // Maybe set a more appropriate mode in the Source Editor if possible,
      // but avoid doing this for very large files.
      if (responseBody.length < SOURCE_SYNTAX_HIGHLIGHT_MAX_FILE_SIZE) {
        let mapping = Object.keys(CONTENT_MIME_TYPE_MAPPINGS).find(key => {
          return mimeType.includes(key);
        });

        if (mapping) {
          editor.setMode(CONTENT_MIME_TYPE_MAPPINGS[mapping]);
        }
      }
    }

    window.emit(EVENTS.RESPONSE_BODY_DISPLAYED);
  }),

  /**
   * Sets the timings information shown in this view.
   *
   * @param object response
   *        The message received from the server.
   */
  _setTimingsInformation: function (response) {
    if (!response) {
      return;
    }
    let { blocked, dns, connect, send, wait, receive } = response.timings;

    let tabboxWidth = $("#details-pane").getAttribute("width");

    // Other nodes also take some space.
    let availableWidth = tabboxWidth / 2;
    let scale = (response.totalTime > 0 ?
                 Math.max(availableWidth / response.totalTime, 0) :
                 0);

    $("#timings-summary-blocked .requests-menu-timings-box")
      .setAttribute("width", blocked * scale);
    $("#timings-summary-blocked .requests-menu-timings-total")
      .setAttribute("value", L10N.getFormatStr("networkMenu.totalMS", blocked));

    $("#timings-summary-dns .requests-menu-timings-box")
      .setAttribute("width", dns * scale);
    $("#timings-summary-dns .requests-menu-timings-total")
      .setAttribute("value", L10N.getFormatStr("networkMenu.totalMS", dns));

    $("#timings-summary-connect .requests-menu-timings-box")
      .setAttribute("width", connect * scale);
    $("#timings-summary-connect .requests-menu-timings-total")
      .setAttribute("value", L10N.getFormatStr("networkMenu.totalMS", connect));

    $("#timings-summary-send .requests-menu-timings-box")
      .setAttribute("width", send * scale);
    $("#timings-summary-send .requests-menu-timings-total")
      .setAttribute("value", L10N.getFormatStr("networkMenu.totalMS", send));

    $("#timings-summary-wait .requests-menu-timings-box")
      .setAttribute("width", wait * scale);
    $("#timings-summary-wait .requests-menu-timings-total")
      .setAttribute("value", L10N.getFormatStr("networkMenu.totalMS", wait));

    $("#timings-summary-receive .requests-menu-timings-box")
      .setAttribute("width", receive * scale);
    $("#timings-summary-receive .requests-menu-timings-total")
      .setAttribute("value", L10N.getFormatStr("networkMenu.totalMS", receive));

    $("#timings-summary-dns .requests-menu-timings-box")
      .style.transform = "translateX(" + (scale * blocked) + "px)";
    $("#timings-summary-connect .requests-menu-timings-box")
      .style.transform = "translateX(" + (scale * (blocked + dns)) + "px)";
    $("#timings-summary-send .requests-menu-timings-box")
      .style.transform =
        "translateX(" + (scale * (blocked + dns + connect)) + "px)";
    $("#timings-summary-wait .requests-menu-timings-box")
      .style.transform =
        "translateX(" + (scale * (blocked + dns + connect + send)) + "px)";
    $("#timings-summary-receive .requests-menu-timings-box")
      .style.transform =
        "translateX(" + (scale * (blocked + dns + connect + send + wait)) +
          "px)";

    $("#timings-summary-dns .requests-menu-timings-total")
      .style.transform = "translateX(" + (scale * blocked) + "px)";
    $("#timings-summary-connect .requests-menu-timings-total")
      .style.transform = "translateX(" + (scale * (blocked + dns)) + "px)";
    $("#timings-summary-send .requests-menu-timings-total")
      .style.transform =
        "translateX(" + (scale * (blocked + dns + connect)) + "px)";
    $("#timings-summary-wait .requests-menu-timings-total")
      .style.transform =
        "translateX(" + (scale * (blocked + dns + connect + send)) + "px)";
    $("#timings-summary-receive .requests-menu-timings-total")
      .style.transform =
        "translateX(" + (scale * (blocked + dns + connect + send + wait)) +
         "px)";
  },

  /**
   * Sets the preview for HTML responses shown in this view.
   *
   * @param object response
   *        The message received from the server.
   * @return object
   *        A promise that is resolved when the html preview is rendered.
   */
  _setHtmlPreview: Task.async(function* (response) {
    if (!response) {
      return promise.resolve();
    }
    let { text } = response.content;
    let responseBody = yield gNetwork.getString(text);

    // Always disable JS when previewing HTML responses.
    let iframe = $("#response-preview");
    iframe.contentDocument.docShell.allowJavascript = false;
    iframe.contentDocument.documentElement.innerHTML = responseBody;

    window.emit(EVENTS.RESPONSE_HTML_PREVIEW_DISPLAYED);
    return undefined;
  }),

  /**
   * Sets the security information shown in this view.
   *
   * @param object securityInfo
   *        The data received from server
   * @param string url
   *        The URL of this request
   * @return object
   *        A promise that is resolved when the security info is rendered.
   */
  _setSecurityInfo: Task.async(function* (securityInfo, url) {
    if (!securityInfo) {
      // We don't have security info. This could mean one of two things:
      // 1) This connection is not secure and this tab is not visible and thus
      //    we shouldn't be here.
      // 2) We have already received securityState and the tab is visible BUT
      //    the rest of the information is still on its way. Once it arrives
      //    this method is called again.
      return;
    }

    /**
     * A helper that sets value and tooltiptext attributes of an element to
     * specified value.
     *
     * @param string selector
     *        A selector for the element.
     * @param string value
     *        The value to set. If this evaluates to false a placeholder string
     *        <Not Available> is used instead.
     */
    function setValue(selector, value) {
      let label = $(selector);
      if (!value) {
        label.setAttribute("value", L10N.getStr(
          "netmonitor.security.notAvailable"));
        label.setAttribute("tooltiptext", label.getAttribute("value"));
      } else {
        label.setAttribute("value", value);
        label.setAttribute("tooltiptext", value);
      }
    }

    let errorbox = $("#security-error");
    let infobox = $("#security-information");

    if (securityInfo.state === "secure" || securityInfo.state === "weak") {
      infobox.hidden = false;
      errorbox.hidden = true;

      // Warning icons
      let cipher = $("#security-warning-cipher");

      if (securityInfo.state === "weak") {
        cipher.hidden = securityInfo.weaknessReasons.indexOf("cipher") === -1;
      } else {
        cipher.hidden = true;
      }

      let enabledLabel = L10N.getStr("netmonitor.security.enabled");
      let disabledLabel = L10N.getStr("netmonitor.security.disabled");

      // Connection parameters
      setValue("#security-protocol-version-value",
        securityInfo.protocolVersion);
      setValue("#security-ciphersuite-value", securityInfo.cipherSuite);

      // Host header
      let domain = NetMonitorView.RequestsMenu._getUriHostPort(url);
      let hostHeader = L10N.getFormatStr("netmonitor.security.hostHeader",
        domain);
      setValue("#security-info-host-header", hostHeader);

      // Parameters related to the domain
      setValue("#security-http-strict-transport-security-value",
                securityInfo.hsts ? enabledLabel : disabledLabel);

      setValue("#security-public-key-pinning-value",
                securityInfo.hpkp ? enabledLabel : disabledLabel);

      // Certificate parameters
      let cert = securityInfo.cert;
      setValue("#security-cert-subject-cn", cert.subject.commonName);
      setValue("#security-cert-subject-o", cert.subject.organization);
      setValue("#security-cert-subject-ou", cert.subject.organizationalUnit);

      setValue("#security-cert-issuer-cn", cert.issuer.commonName);
      setValue("#security-cert-issuer-o", cert.issuer.organization);
      setValue("#security-cert-issuer-ou", cert.issuer.organizationalUnit);

      setValue("#security-cert-validity-begins", cert.validity.start);
      setValue("#security-cert-validity-expires", cert.validity.end);

      setValue("#security-cert-sha1-fingerprint", cert.fingerprint.sha1);
      setValue("#security-cert-sha256-fingerprint", cert.fingerprint.sha256);
    } else {
      infobox.hidden = true;
      errorbox.hidden = false;

      // Strip any HTML from the message.
      let plain = new DOMParser().parseFromString(securityInfo.errorMessage,
        "text/html");
      setValue("#security-error-message", plain.body.textContent);
    }
  }),

  _dataSrc: null,
  _headers: null,
  _cookies: null,
  _params: null,
  _json: null,
  _paramsQueryString: "",
  _paramsFormData: "",
  _paramsPostPayload: "",
  _requestHeaders: "",
  _responseHeaders: "",
  _requestCookies: "",
  _responseCookies: ""
};

/**
 * Functions handling the performance statistics view.
 */
function PerformanceStatisticsView() {
}

PerformanceStatisticsView.prototype = {
  /**
   * Initializes and displays empty charts in this container.
   */
  displayPlaceholderCharts: function () {
    this._createChart({
      id: "#primed-cache-chart",
      title: "charts.cacheEnabled"
    });
    this._createChart({
      id: "#empty-cache-chart",
      title: "charts.cacheDisabled"
    });
    window.emit(EVENTS.PLACEHOLDER_CHARTS_DISPLAYED);
  },

  /**
   * Populates and displays the primed cache chart in this container.
   *
   * @param array items
   *        @see this._sanitizeChartDataSource
   */
  createPrimedCacheChart: function (items) {
    this._createChart({
      id: "#primed-cache-chart",
      title: "charts.cacheEnabled",
      data: this._sanitizeChartDataSource(items),
      strings: this._commonChartStrings,
      totals: this._commonChartTotals,
      sorted: true
    });
    window.emit(EVENTS.PRIMED_CACHE_CHART_DISPLAYED);
  },

  /**
   * Populates and displays the empty cache chart in this container.
   *
   * @param array items
   *        @see this._sanitizeChartDataSource
   */
  createEmptyCacheChart: function (items) {
    this._createChart({
      id: "#empty-cache-chart",
      title: "charts.cacheDisabled",
      data: this._sanitizeChartDataSource(items, true),
      strings: this._commonChartStrings,
      totals: this._commonChartTotals,
      sorted: true
    });
    window.emit(EVENTS.EMPTY_CACHE_CHART_DISPLAYED);
  },

  /**
   * Common stringifier predicates used for items and totals in both the
   * "primed" and "empty" cache charts.
   */
  _commonChartStrings: {
    size: value => {
      let string = L10N.numberWithDecimals(value / 1024, CONTENT_SIZE_DECIMALS);
      return L10N.getFormatStr("charts.sizeKB", string);
    },
    time: value => {
      let string = L10N.numberWithDecimals(value / 1000, REQUEST_TIME_DECIMALS);
      return L10N.getFormatStr("charts.totalS", string);
    }
  },
  _commonChartTotals: {
    size: total => {
      let string = L10N.numberWithDecimals(total / 1024, CONTENT_SIZE_DECIMALS);
      return L10N.getFormatStr("charts.totalSize", string);
    },
    time: total => {
      let seconds = total / 1000;
      let string = L10N.numberWithDecimals(seconds, REQUEST_TIME_DECIMALS);
      return PluralForm.get(seconds,
        L10N.getStr("charts.totalSeconds")).replace("#1", string);
    },
    cached: total => {
      return L10N.getFormatStr("charts.totalCached", total);
    },
    count: total => {
      return L10N.getFormatStr("charts.totalCount", total);
    }
  },

  /**
   * Adds a specific chart to this container.
   *
   * @param object
   *        An object containing all or some the following properties:
   *          - id: either "#primed-cache-chart" or "#empty-cache-chart"
   *          - title/data/strings/totals/sorted: @see Chart.jsm for details
   */
  _createChart: function ({ id, title, data, strings, totals, sorted }) {
    let container = $(id);

    // Nuke all existing charts of the specified type.
    while (container.hasChildNodes()) {
      container.firstChild.remove();
    }

    // Create a new chart.
    let chart = Chart.PieTable(document, {
      diameter: NETWORK_ANALYSIS_PIE_CHART_DIAMETER,
      title: L10N.getStr(title),
      data: data,
      strings: strings,
      totals: totals,
      sorted: sorted
    });

    chart.on("click", (_, item) => {
      NetMonitorView.RequestsMenu.filterOnlyOn(item.label);
      NetMonitorView.showNetworkInspectorView();
    });

    container.appendChild(chart.node);
  },

  /**
   * Sanitizes the data source used for creating charts, to follow the
   * data format spec defined in Chart.jsm.
   *
   * @param array items
   *        A collection of request items used as the data source for the chart.
   * @param boolean emptyCache
   *        True if the cache is considered enabled, false for disabled.
   */
  _sanitizeChartDataSource: function (items, emptyCache) {
    let data = [
      "html", "css", "js", "xhr", "fonts", "images", "media", "flash", "ws", "other"
    ].map(e => ({
      cached: 0,
      count: 0,
      label: e,
      size: 0,
      time: 0
    }));

    for (let requestItem of items) {
      let details = requestItem.attachment;
      let type;

      if (Filters.html(details)) {
        // "html"
        type = 0;
      } else if (Filters.css(details)) {
        // "css"
        type = 1;
      } else if (Filters.js(details)) {
        // "js"
        type = 2;
      } else if (Filters.fonts(details)) {
        // "fonts"
        type = 4;
      } else if (Filters.images(details)) {
        // "images"
        type = 5;
      } else if (Filters.media(details)) {
        // "media"
        type = 6;
      } else if (Filters.flash(details)) {
        // "flash"
        type = 7;
      } else if (Filters.ws(details)) {
        // "ws"
        type = 8;
      } else if (Filters.xhr(details)) {
        // Verify XHR last, to categorize other mime types in their own blobs.
        // "xhr"
        type = 3;
      } else {
        // "other"
        type = 9;
      }

      if (emptyCache || !responseIsFresh(details)) {
        data[type].time += details.totalTime || 0;
        data[type].size += details.contentSize || 0;
      } else {
        data[type].cached++;
      }
      data[type].count++;
    }

    return data.filter(e => e.count > 0);
  },
};

/**
 * DOM query helper.
 */
var $ = (selector, target = document) => target.querySelector(selector);
var $all = (selector, target = document) => target.querySelectorAll(selector);

/**
 * Parse text representation of multiple HTTP headers.
 *
 * @param string text
 *        Text of headers
 * @return array
 *         Array of headers info {name, value}
 */
function parseHeadersText(text) {
  return parseRequestText(text, "\\S+?", ":");
}

/**
 * Parse readable text list of a query string.
 *
 * @param string text
 *        Text of query string represetation
 * @return array
 *         Array of query params {name, value}
 */
function parseQueryText(text) {
  return parseRequestText(text, ".+?", "=");
}

/**
 * Parse a text representation of a name[divider]value list with
 * the given name regex and divider character.
 *
 * @param string text
 *        Text of list
 * @return array
 *         Array of headers info {name, value}
 */
function parseRequestText(text, namereg, divider) {
  let regex = new RegExp("(" + namereg + ")\\" + divider + "\\s*(.+)");
  let pairs = [];

  for (let line of text.split("\n")) {
    let matches;
    if (matches = regex.exec(line)) { // eslint-disable-line
      let [, name, value] = matches;
      pairs.push({name: name, value: value});
    }
  }
  return pairs;
}

/**
 * Write out a list of query params into a chunk of text
 *
 * @param array params
 *        Array of query params {name, value}
 * @return string
 *         List of query params in text format
 */
function writeQueryText(params) {
  return params.map(({name, value}) => name + "=" + value).join("\n");
}

/**
 * Write out a list of query params into a query string
 *
 * @param array params
 *        Array of query  params {name, value}
 * @return string
 *         Query string that can be appended to a url.
 */
function writeQueryString(params) {
  return params.map(({name, value}) => name + "=" + value).join("&");
}

/**
 * Checks if the "Expiration Calculations" defined in section 13.2.4 of the
 * "HTTP/1.1: Caching in HTTP" spec holds true for a collection of headers.
 *
 * @param object
 *        An object containing the { responseHeaders, status } properties.
 * @return boolean
 *         True if the response is fresh and loaded from cache.
 */
function responseIsFresh({ responseHeaders, status }) {
  // Check for a "304 Not Modified" status and response headers availability.
  if (status != 304 || !responseHeaders) {
    return false;
  }

  let list = responseHeaders.headers;
  let cacheControl = list.filter(e => {
    return e.name.toLowerCase() == "cache-control";
  })[0];

  let expires = list.filter(e => e.name.toLowerCase() == "expires")[0];

  // Check the "Cache-Control" header for a maximum age value.
  if (cacheControl) {
    let maxAgeMatch =
      cacheControl.value.match(/s-maxage\s*=\s*(\d+)/) ||
      cacheControl.value.match(/max-age\s*=\s*(\d+)/);

    if (maxAgeMatch && maxAgeMatch.pop() > 0) {
      return true;
    }
  }

  // Check the "Expires" header for a valid date.
  if (expires && Date.parse(expires.value)) {
    return true;
  }

  return false;
}

/**
 * Makes sure certain properties are available on all objects in a data store.
 *
 * @param array dataStore
 *        A list of objects for which to check the availability of properties.
 * @param array mandatoryFields
 *        A list of strings representing properties of objects in dataStore.
 * @return object
 *         A promise resolved when all objects in dataStore contain the
 *         properties defined in mandatoryFields.
 */
function whenDataAvailable(dataStore, mandatoryFields) {
  let deferred = promise.defer();

  let interval = setInterval(() => {
    if (dataStore.every(item => {
      return mandatoryFields.every(field => field in item);
    })) {
      clearInterval(interval);
      clearTimeout(timer);
      deferred.resolve();
    }
  }, WDA_DEFAULT_VERIFY_INTERVAL);

  let timer = setTimeout(() => {
    clearInterval(interval);
    deferred.reject(new Error("Timed out while waiting for data"));
  }, WDA_DEFAULT_GIVE_UP_TIMEOUT);

  return deferred.promise;
}

/**
 * Preliminary setup for the NetMonitorView object.
 */
NetMonitorView.Toolbar = new ToolbarView();
NetMonitorView.RequestsMenu = new RequestsMenuView();
NetMonitorView.Sidebar = new SidebarView();
NetMonitorView.CustomRequest = new CustomRequestView();
NetMonitorView.NetworkDetails = new NetworkDetailsView();
NetMonitorView.PerformanceStatistics = new PerformanceStatisticsView();
