/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* globals window, dumpn, $, gNetwork, NetMonitorController, NetMonitorView */

"use strict";

const { Task } = require("devtools/shared/task");
const { HTMLTooltip } = require("devtools/client/shared/widgets/tooltip/HTMLTooltip");
const { setNamedTimeout } = require("devtools/client/shared/widgets/view-helpers");
const { CurlUtils } = require("devtools/client/shared/curl");
const { L10N } = require("./l10n");
const { EVENTS } = require("./events");
const { createElement, createFactory } = require("devtools/client/shared/vendor/react");
const ReactDOM = require("devtools/client/shared/vendor/react-dom");
const { Provider } = require("devtools/client/shared/vendor/react-redux");
const RequestList = createFactory(require("./components/request-list"));
const RequestListContextMenu = require("./request-list-context-menu");
const Actions = require("./actions/index");
const { Prefs } = require("./prefs");

const {
  fetchHeaders,
  formDataURI,
  getFormDataSections,
} = require("./request-utils");

const {
  getActiveFilters,
  getDisplayedRequests,
  getRequestById,
  getSelectedRequest,
  getSortedRequests,
} = require("./selectors/index");

// ms
const RESIZE_REFRESH_RATE = 50;

// A smart store watcher to notify store changes as necessary
function storeWatcher(initialValue, reduceValue, onChange) {
  let currentValue = initialValue;

  return () => {
    const oldValue = currentValue;
    const newValue = reduceValue(currentValue);
    if (newValue !== oldValue) {
      currentValue = newValue;
      onChange(newValue, oldValue);
    }
  };
}

/**
 * Functions handling the requests menu (containing details about each request,
 * like status, method, file, domain, as well as a waterfall representing
 * timing information).
 */
function RequestsMenuView() {
  dumpn("RequestsMenuView was instantiated");
}

RequestsMenuView.prototype = {
  /**
   * Initialization function, called when the network monitor is started.
   */
  initialize: function (store) {
    dumpn("Initializing the RequestsMenuView");

    this.store = store;

    this.contextMenu = new RequestListContextMenu();
    this.contextMenu.initialize(store);

    Prefs.filters.forEach(type => store.dispatch(Actions.toggleRequestFilterType(type)));

    // Watch selection changes
    this.store.subscribe(storeWatcher(
      null,
      () => getSelectedRequest(this.store.getState()),
      (newSelected, oldSelected) => this.onSelectionUpdate(newSelected, oldSelected)
    ));

    // Watch the sidebar status and resize the waterfall column on change
    this.store.subscribe(storeWatcher(
      false,
      () => this.store.getState().ui.sidebarOpen,
      () => this.onResize()
    ));

    // Watch the requestHeaders, requestHeadersFromUploadStream and requestPostData
    // in order to update formDataSections for composing form data
    this.store.subscribe(storeWatcher(
      false,
      (currentRequest) => {
        const request = getSelectedRequest(this.store.getState());
        if (!request) {
          return {};
        }

        const isChanged = request.requestHeaders !== currentRequest.requestHeaders ||
        request.requestHeadersFromUploadStream !==
        currentRequest.requestHeadersFromUploadStream ||
        request.requestPostData !== currentRequest.requestPostData;

        if (isChanged) {
          return {
            id: request.id,
            requestHeaders: request.requestHeaders,
            requestHeadersFromUploadStream: request.requestHeadersFromUploadStream,
            requestPostData: request.requestPostData,
          };
        }

        return currentRequest;
      },
      (newRequest) => {
        const {
          id,
          requestHeaders,
          requestHeadersFromUploadStream,
          requestPostData,
        } = newRequest;

        if (requestHeaders && requestHeadersFromUploadStream && requestPostData) {
          getFormDataSections(
            requestHeaders,
            requestHeadersFromUploadStream,
            requestPostData,
            gNetwork.getString.bind(gNetwork),
          ).then((formDataSections) => {
            this.store.dispatch(Actions.updateRequest(
              id,
              { formDataSections },
              true,
            ));
          });
        }
      },
    ));

    this.sendCustomRequestEvent = this.sendCustomRequest.bind(this);
    this.closeCustomRequestEvent = this.closeCustomRequest.bind(this);

    this._summary = $("#requests-menu-network-summary-button");
    this._summary.setAttribute("label", L10N.getStr("networkMenu.empty"));

    this.onResize = this.onResize.bind(this);
    this._splitter = $("#network-inspector-view-splitter");
    this._splitter.addEventListener("mouseup", this.onResize);
    window.addEventListener("resize", this.onResize);

    this.tooltip = new HTMLTooltip(NetMonitorController._toolbox.doc, { type: "arrow" });

    this.mountPoint = $("#network-table");
    ReactDOM.render(createElement(Provider,
      { store: this.store },
      RequestList()
    ), this.mountPoint);

    window.once("connected", this._onConnect.bind(this));
  },

  _onConnect() {
    if (NetMonitorController.supportsCustomRequest) {
      $("#custom-request-send-button")
        .addEventListener("click", this.sendCustomRequestEvent);
      $("#custom-request-close-button")
        .addEventListener("click", this.closeCustomRequestEvent);
    }
  },

  /**
   * Destruction function, called when the network monitor is closed.
   */
  destroy() {
    dumpn("Destroying the RequestsMenuView");

    Prefs.filters = getActiveFilters(this.store.getState());

    // this.flushRequestsTask.disarm();

    $("#custom-request-send-button")
      .removeEventListener("click", this.sendCustomRequestEvent);
    $("#custom-request-close-button")
      .removeEventListener("click", this.closeCustomRequestEvent);

    this._splitter.removeEventListener("mouseup", this.onResize);
    window.removeEventListener("resize", this.onResize);

    this.tooltip.destroy();

    ReactDOM.unmountComponentAtNode(this.mountPoint);
  },

  /**
   * Resets this container (removes all the networking information).
   */
  reset() {
    this.store.dispatch(Actions.batchReset());
    this.store.dispatch(Actions.clearRequests());
  },

  /**
   * Removes all network requests and closes the sidebar if open.
   */
  clear() {
    this.store.dispatch(Actions.clearRequests());
  },

  addRequest(id, data) {
    let { method, url, isXHR, cause, startedDateTime, fromCache,
          fromServiceWorker } = data;

    // Convert the received date/time string to a unix timestamp.
    let startedMillis = Date.parse(startedDateTime);

    const action = Actions.addRequest(
      id,
      {
        startedMillis,
        method,
        url,
        isXHR,
        cause,
        fromCache,
        fromServiceWorker,
      },
      true
    );

    this.store.dispatch(action).then(() => window.emit(EVENTS.REQUEST_ADDED, action.id));
  },

  updateRequest: Task.async(function* (id, data) {
    const action = Actions.updateRequest(id, data, true);
    yield this.store.dispatch(action);
    let {
      responseContent,
      responseCookies,
      responseHeaders,
      requestCookies,
      requestHeaders,
      requestPostData,
    } = action.data;
    let request = getRequestById(this.store.getState(), action.id);

    if (requestHeaders && requestHeaders.headers && requestHeaders.headers.length) {
      let headers = yield fetchHeaders(
        requestHeaders, gNetwork.getString.bind(gNetwork));
      if (headers) {
        yield this.store.dispatch(Actions.updateRequest(
          action.id,
          { requestHeaders: headers },
          true,
        ));
      }
    }

    if (responseHeaders && responseHeaders.headers && responseHeaders.headers.length) {
      let headers = yield fetchHeaders(
        responseHeaders, gNetwork.getString.bind(gNetwork));
      if (headers) {
        yield this.store.dispatch(Actions.updateRequest(
          action.id,
          { responseHeaders: headers },
          true,
        ));
      }
    }

    if (request && responseContent && responseContent.content) {
      let { mimeType } = request;
      let { text, encoding } = responseContent.content;
      let response = yield gNetwork.getString(text);
      let payload = {};

      if (mimeType.includes("image/")) {
        payload.responseContentDataUri = formDataURI(mimeType, encoding, response);
      }

      responseContent.content.text = response;
      payload.responseContent = responseContent;

      yield this.store.dispatch(Actions.updateRequest(action.id, payload, true));

      if (mimeType.includes("image/")) {
        window.emit(EVENTS.RESPONSE_IMAGE_THUMBNAIL_DISPLAYED);
      }
    }

    // Search the POST data upload stream for request headers and add
    // them as a separate property, different from the classic headers.
    if (requestPostData && requestPostData.postData) {
      let { text } = requestPostData.postData;
      let postData = yield gNetwork.getString(text);
      const headers = CurlUtils.getHeadersFromMultipartText(postData);
      const headersSize = headers.reduce((acc, { name, value }) => {
        return acc + name.length + value.length + 2;
      }, 0);
      let payload = {};
      requestPostData.postData.text = postData;
      payload.requestPostData = Object.assign({}, requestPostData);
      payload.requestHeadersFromUploadStream = { headers, headersSize };

      yield this.store.dispatch(Actions.updateRequest(action.id, payload, true));
    }

    // Fetch request and response cookies long value.
    // Actor does not provide full sized cookie value when the value is too long
    // To display values correctly, we need fetch them in each request.
    if (requestCookies) {
      let reqCookies = [];
      // request store cookies in requestCookies or requestCookies.cookies
      let cookies = requestCookies.cookies ?
        requestCookies.cookies : requestCookies;
      // make sure cookies is iterable
      if (typeof cookies[Symbol.iterator] === "function") {
        for (let cookie of cookies) {
          reqCookies.push(Object.assign({}, cookie, {
            value: yield gNetwork.getString(cookie.value),
          }));
        }
        if (reqCookies.length) {
          yield this.store.dispatch(Actions.updateRequest(
            action.id,
            { requestCookies: reqCookies },
            true));
        }
      }
    }

    if (responseCookies) {
      let resCookies = [];
      // response store cookies in responseCookies or responseCookies.cookies
      let cookies = responseCookies.cookies ?
        responseCookies.cookies : responseCookies;
      // make sure cookies is iterable
      if (typeof cookies[Symbol.iterator] === "function") {
        for (let cookie of cookies) {
          resCookies.push(Object.assign({}, cookie, {
            value: yield gNetwork.getString(cookie.value),
          }));
        }
        if (resCookies.length) {
          yield this.store.dispatch(Actions.updateRequest(
            action.id,
            { responseCookies: resCookies },
            true));
        }
      }
    }
  }),

  /**
   * Disable batched updates. Used by tests.
   */
  set lazyUpdate(value) {
    this.store.dispatch(Actions.batchEnable(value));
  },

  get items() {
    return getSortedRequests(this.store.getState());
  },

  get visibleItems() {
    return getDisplayedRequests(this.store.getState());
  },

  get itemCount() {
    return this.store.getState().requests.requests.size;
  },

  getItemAtIndex(index) {
    return getSortedRequests(this.store.getState()).get(index);
  },

  get selectedIndex() {
    const state = this.store.getState();
    if (!state.requests.selectedId) {
      return -1;
    }
    return getSortedRequests(state).findIndex(r => r.id === state.requests.selectedId);
  },

  set selectedIndex(index) {
    const requests = getSortedRequests(this.store.getState());
    let itemId = null;
    if (index >= 0 && index < requests.size) {
      itemId = requests.get(index).id;
    }
    this.store.dispatch(Actions.selectRequest(itemId));
  },

  get selectedItem() {
    return getSelectedRequest(this.store.getState());
  },

  set selectedItem(item) {
    this.store.dispatch(Actions.selectRequest(item ? item.id : null));
  },

  /**
   * Updates the sidebar status when something about the selection changes
   */
  onSelectionUpdate(newSelected, oldSelected) {
    if (newSelected && oldSelected && newSelected.id === oldSelected.id) {
      // The same item is still selected, its data only got updated
      NetMonitorView.NetworkDetails.populate(newSelected);
    } else if (newSelected) {
      // Another item just got selected
      NetMonitorView.Sidebar.populate(newSelected);
      NetMonitorView.Sidebar.toggle(true);
    } else {
      // Selection just got empty
      NetMonitorView.Sidebar.toggle(false);
    }
  },

  /**
   * The resize listener for this container's window.
   */
  onResize() {
    // Allow requests to settle down first.
    setNamedTimeout("resize-events", RESIZE_REFRESH_RATE, () => {
      const waterfallHeaderEl = $("#requests-menu-waterfall-header-box");
      if (waterfallHeaderEl) {
        const { width } = waterfallHeaderEl.getBoundingClientRect();
        this.store.dispatch(Actions.resizeWaterfall(width));
      }
    });
  },

  /**
   * Create a new custom request form populated with the data from
   * the currently selected request.
   */
  cloneSelectedRequest() {
    this.store.dispatch(Actions.cloneSelectedRequest());
  },

  /**
   * Send a new HTTP request using the data in the custom request form.
   */
  sendCustomRequest: function () {
    let selected = getSelectedRequest(this.store.getState());

    let data = {
      url: selected.url,
      method: selected.method,
      httpVersion: selected.httpVersion,
    };
    if (selected.requestHeaders) {
      data.headers = selected.requestHeaders.headers;
    }
    if (selected.requestPostData) {
      data.body = selected.requestPostData.postData.text;
    }

    NetMonitorController.webConsoleClient.sendHTTPRequest(data, response => {
      let id = response.eventActor.actor;
      this.store.dispatch(Actions.preselectRequest(id));
    });

    this.closeCustomRequest();
  },

  /**
   * Remove the currently selected custom request.
   */
  closeCustomRequest() {
    this.store.dispatch(Actions.removeSelectedCustomRequest());
  },
};

exports.RequestsMenuView = RequestsMenuView;
