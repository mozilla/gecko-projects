/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {
  createFactory,
  DOM,
  PropTypes,
} = require("devtools/client/shared/vendor/react");
const { connect } = require("devtools/client/shared/vendor/react-redux");
const { PluralForm } = require("devtools/shared/plural-form");
const Actions = require("../actions/index");
const { L10N } = require("../l10n");
const {
  getDisplayedRequestsSummary,
  isSidebarToggleButtonDisabled,
} = require("../selectors/index");
const {
  getSizeWithDecimals,
  getTimeWithDecimals,
} = require("../utils/format-utils");
const { FILTER_SEARCH_DELAY } = require("../constants");

// Components
const SearchBox = createFactory(require("devtools/client/shared/components/search-box"));

const { button, div, span } = DOM;

const COLLPASE_DETAILS_PANE = L10N.getStr("collapseDetailsPane");
const EXPAND_DETAILS_PANE = L10N.getStr("expandDetailsPane");
const SEARCH_KEY_SHORTCUT = L10N.getStr("netmonitor.toolbar.filterFreetext.key");
const SEARCH_PLACE_HOLDER = L10N.getStr("netmonitor.toolbar.filterFreetext.label");
const TOOLBAR_CLEAR = L10N.getStr("netmonitor.toolbar.clear");

/*
 * Network monitor toolbar component
 * Toolbar contains a set of useful tools to control network requests
 */
function Toolbar({
  clearRequests,
  openStatistics,
  requestFilterTypes,
  setRequestFilterText,
  sidebarToggleDisabled,
  sidebarOpen,
  summary,
  toggleRequestFilterType,
  toggleSidebar,
}) {
  let toggleButtonClassName = ["devtools-button"];
  if (!sidebarOpen) {
    toggleButtonClassName.push("pane-collapsed");
  }

  let { count, bytes, millis } = summary;
  const text = (count === 0) ? L10N.getStr("networkMenu.empty") :
    PluralForm.get(count, L10N.getStr("networkMenu.summary"))
    .replace("#1", count)
    .replace("#2", getSizeWithDecimals(bytes / 1024))
    .replace("#3", getTimeWithDecimals(millis / 1000));

  const buttons = requestFilterTypes.entrySeq().map(([type, checked]) => {
    let classList = ["menu-filter-button"];
    checked && classList.push("checked");

    return (
      button({
        id: `requests-menu-filter-${type}-button`,
        key: type,
        className: classList.join(" "),
        "data-key": type,
        onClick: toggleRequestFilterType,
        onKeyDown: toggleRequestFilterType,
      },
        L10N.getStr(`netmonitor.toolbar.filter.${type}`)
      )
    );
  }).toArray();

  return (
    span({ className: "devtools-toolbar devtools-toolbar-container" },
      span({ className: "devtools-toolbar-group" },
        button({
          id: "requests-menu-clear-button",
          className: "devtools-button devtools-clear-icon",
          title: TOOLBAR_CLEAR,
          onClick: clearRequests,
        }),
        div({ id: "requests-menu-filter-buttons" }, buttons),
      ),
      span({ className: "devtools-toolbar-group" },
        button({
          id: "requests-menu-network-summary-button",
          className: "devtools-button",
          title: count ? text : L10N.getStr("netmonitor.toolbar.perf"),
          onClick: openStatistics,
        },
          span({ className: "summary-info-icon" }),
          span({ className: "summary-info-text" }, text),
        ),
        SearchBox({
          delay: FILTER_SEARCH_DELAY,
          keyShortcut: SEARCH_KEY_SHORTCUT,
          placeholder: SEARCH_PLACE_HOLDER,
          type: "filter",
          onChange: setRequestFilterText,
        }),
        button({
          id: "details-pane-toggle",
          className: toggleButtonClassName.join(" "),
          title: sidebarOpen ? COLLPASE_DETAILS_PANE : EXPAND_DETAILS_PANE,
          disabled: sidebarToggleDisabled,
          tabIndex: "0",
          onMouseDown: toggleSidebar,
        }),
      )
    )
  );
}

Toolbar.displayName = "Toolbar";

Toolbar.propTypes = {
  clearRequests: PropTypes.func.isRequired,
  openStatistics: PropTypes.func.isRequired,
  requestFilterTypes: PropTypes.object.isRequired,
  setRequestFilterText: PropTypes.func.isRequired,
  sidebarToggleDisabled: PropTypes.bool.isRequired,
  sidebarOpen: PropTypes.bool.isRequired,
  summary: PropTypes.object.isRequired,
  toggleRequestFilterType: PropTypes.func.isRequired,
  toggleSidebar: PropTypes.func.isRequired,
};

module.exports = connect(
  (state) => ({
    sidebarToggleDisabled: isSidebarToggleButtonDisabled(state),
    sidebarOpen: state.ui.sidebarOpen,
    requestFilterTypes: state.filters.requestFilterTypes,
    summary: getDisplayedRequestsSummary(state),
  }),
  (dispatch) => ({
    clearRequests: () => dispatch(Actions.clearRequests()),
    openStatistics: () => dispatch(Actions.openStatistics(true)),
    setRequestFilterText: (text) => dispatch(Actions.setRequestFilterText(text)),
    toggleRequestFilterType: (evt) => {
      if (evt.type === "keydown" && (evt.key !== "" || evt.key !== "Enter")) {
        return;
      }
      dispatch(Actions.toggleRequestFilterType(evt.target.dataset.key));
    },
    toggleSidebar: () => dispatch(Actions.toggleSidebar()),
  })
)(Toolbar);
