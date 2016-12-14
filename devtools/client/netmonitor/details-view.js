/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* eslint-disable mozilla/reject-some-requires */
/* globals window, dumpn, $, NetMonitorView, gNetwork */

"use strict";

const promise = require("promise");
const EventEmitter = require("devtools/shared/event-emitter");
const Editor = require("devtools/client/sourceeditor/editor");
const { Heritage } = require("devtools/client/shared/widgets/view-helpers");
const { Task } = require("devtools/shared/task");
const { ToolSidebar } = require("devtools/client/framework/sidebar");
const { VariablesView } = require("resource://devtools/client/shared/widgets/VariablesView.jsm");
const { VariablesViewController } = require("resource://devtools/client/shared/widgets/VariablesViewController.jsm");
const { EVENTS } = require("./events");
const { L10N } = require("./l10n");
const { Filters } = require("./filter-predicates");
const {
  decodeUnicodeUrl,
  formDataURI,
  getFormDataSections,
  getUrlBaseName,
  getUrlQuery,
  getUrlHost,
  parseQueryString,
} = require("./request-utils");
const { createFactory } = require("devtools/client/shared/vendor/react");
const ReactDOM = require("devtools/client/shared/vendor/react-dom");
const Provider = createFactory(require("devtools/client/shared/vendor/react-redux").Provider);
const TimingsPanel = createFactory(require("./shared/components/timings-panel"));

// 100 KB in bytes
const SOURCE_SYNTAX_HIGHLIGHT_MAX_FILE_SIZE = 102400;
const HEADERS_SIZE_DECIMALS = 3;
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

/**
 * Functions handling the requests details view.
 */
function DetailsView() {
  dumpn("DetailsView was instantiated");

  // The ToolSidebar requires the panel object to be able to emit events.
  EventEmitter.decorate(this);

  this._onTabSelect = this._onTabSelect.bind(this);
}

DetailsView.prototype = {
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
  initialize: function (store) {
    dumpn("Initializing the DetailsView");

    this._timingsPanelNode = $("#react-timings-tabpanel-hook");

    ReactDOM.render(Provider(
      { store },
      TimingsPanel()
    ), this._timingsPanelNode);

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
    dumpn("Destroying the DetailsView");
    ReactDOM.unmountComponentAtNode(this._timingsPanelNode);
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
      let unicodeUrl = decodeUnicodeUrl(data.url);
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
      $("#headers-summary-status-circle").setAttribute("data-code", code);
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
    let query = getUrlQuery(url);
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
        let jsonItem = jsonScope.addItem(undefined, { enumerable: true });
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
    let paramsArray = parseQueryString(queryString);
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
        getUrlBaseName(url));
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
      let domain = getUrlHost(url);
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

exports.DetailsView = DetailsView;
