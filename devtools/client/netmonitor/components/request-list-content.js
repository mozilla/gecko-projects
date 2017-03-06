/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { KeyCodes } = require("devtools/client/shared/keycodes");
const {
  createClass,
  createFactory,
  DOM,
  PropTypes,
} = require("devtools/client/shared/vendor/react");
const { connect } = require("devtools/client/shared/vendor/react-redux");
const { HTMLTooltip } = require("devtools/client/shared/widgets/tooltip/HTMLTooltip");
const Actions = require("../actions/index");
const {
  setTooltipImageContent,
  setTooltipStackTraceContent,
} = require("../request-list-tooltip");
const {
  getDisplayedRequests,
  getWaterfallScale,
} = require("../selectors/index");

// Components
const RequestListItem = createFactory(require("./request-list-item"));
const RequestListContextMenu = require("../request-list-context-menu");

const { div } = DOM;

// tooltip show/hide delay in ms
const REQUESTS_TOOLTIP_TOGGLE_DELAY = 500;

/**
 * Renders the actual contents of the request list.
 */
const RequestListContent = createClass({
  displayName: "RequestListContent",

  propTypes: {
    dispatch: PropTypes.func.isRequired,
    displayedRequests: PropTypes.object.isRequired,
    firstRequestStartedMillis: PropTypes.number.isRequired,
    onItemMouseDown: PropTypes.func.isRequired,
    onSecurityIconClick: PropTypes.func.isRequired,
    onSelectDelta: PropTypes.func.isRequired,
    scale: PropTypes.number,
    selectedRequestId: PropTypes.string,
  },

  componentWillMount() {
    const { dispatch } = this.props;
    this.contextMenu = new RequestListContextMenu({
      cloneSelectedRequest: () => dispatch(Actions.cloneSelectedRequest()),
      openStatistics: (open) => dispatch(Actions.openStatistics(open)),
    });
    this.tooltip = new HTMLTooltip(window.parent.document, { type: "arrow" });
  },

  componentDidMount() {
    // Set the CSS variables for waterfall scaling
    this.setScalingStyles();

    // Install event handler for displaying a tooltip
    this.tooltip.startTogglingOnHover(this.refs.contentEl, this.onHover, {
      toggleDelay: REQUESTS_TOOLTIP_TOGGLE_DELAY,
      interactive: true
    });

    // Install event handler to hide the tooltip on scroll
    this.refs.contentEl.addEventListener("scroll", this.onScroll, true);
  },

  componentWillUpdate(nextProps) {
    // Check if the list is scrolled to bottom before the UI update.
    // The scroll is ever needed only if new rows are added to the list.
    const delta = nextProps.displayedRequests.size - this.props.displayedRequests.size;
    this.shouldScrollBottom = delta > 0 && this.isScrolledToBottom();
  },

  componentDidUpdate(prevProps) {
    // Update the CSS variables for waterfall scaling after props change
    this.setScalingStyles(prevProps);

    // Keep the list scrolled to bottom if a new row was added
    if (this.shouldScrollBottom) {
      let node = this.refs.contentEl;
      node.scrollTop = node.scrollHeight;
    }
  },

  componentWillUnmount() {
    this.refs.contentEl.removeEventListener("scroll", this.onScroll, true);

    // Uninstall the tooltip event handler
    this.tooltip.stopTogglingOnHover();
  },

  /**
   * Set the CSS variables for waterfall scaling. If React supported setting CSS
   * variables as part of the "style" property of a DOM element, we would use that.
   *
   * However, React doesn't support this, so we need to use a hack and update the
   * DOM element directly: https://github.com/facebook/react/issues/6411
   */
  setScalingStyles(prevProps) {
    const { scale } = this.props;
    if (prevProps && prevProps.scale === scale) {
      return;
    }

    const { style } = this.refs.contentEl;
    style.removeProperty("--timings-scale");
    style.removeProperty("--timings-rev-scale");
    style.setProperty("--timings-scale", scale);
    style.setProperty("--timings-rev-scale", 1 / scale);
  },

  isScrolledToBottom() {
    const { contentEl } = this.refs;
    const lastChildEl = contentEl.lastElementChild;

    if (!lastChildEl) {
      return false;
    }

    let lastChildRect = lastChildEl.getBoundingClientRect();
    let contentRect = contentEl.getBoundingClientRect();

    return (lastChildRect.height + lastChildRect.top) <= contentRect.bottom;
  },

  /**
   * The predicate used when deciding whether a popup should be shown
   * over a request item or not.
   *
   * @param nsIDOMNode target
   *        The element node currently being hovered.
   * @param object tooltip
   *        The current tooltip instance.
   * @return {Promise}
   */
  onHover(target, tooltip) {
    let itemEl = target.closest(".request-list-item");
    if (!itemEl) {
      return false;
    }
    let itemId = itemEl.dataset.id;
    if (!itemId) {
      return false;
    }
    let requestItem = this.props.displayedRequests.find(r => r.id == itemId);
    if (!requestItem) {
      return false;
    }

    if (requestItem.responseContent && target.closest(".requests-list-icon-and-file")) {
      return setTooltipImageContent(tooltip, itemEl, requestItem);
    } else if (requestItem.cause && target.closest(".requests-list-cause-stack")) {
      return setTooltipStackTraceContent(tooltip, requestItem);
    }

    return false;
  },

  /**
   * Scroll listener for the requests menu view.
   */
  onScroll() {
    this.tooltip.hide();
  },

  /**
   * Handler for keyboard events. For arrow up/down, page up/down, home/end,
   * move the selection up or down.
   */
  onKeyDown(e) {
    let delta;

    switch (e.keyCode) {
      case KeyCodes.DOM_VK_UP:
      case KeyCodes.DOM_VK_LEFT:
        delta = -1;
        break;
      case KeyCodes.DOM_VK_DOWN:
      case KeyCodes.DOM_VK_RIGHT:
        delta = +1;
        break;
      case KeyCodes.DOM_VK_PAGE_UP:
        delta = "PAGE_UP";
        break;
      case KeyCodes.DOM_VK_PAGE_DOWN:
        delta = "PAGE_DOWN";
        break;
      case KeyCodes.DOM_VK_HOME:
        delta = -Infinity;
        break;
      case KeyCodes.DOM_VK_END:
        delta = +Infinity;
        break;
    }

    if (delta) {
      // Prevent scrolling when pressing navigation keys.
      e.preventDefault();
      e.stopPropagation();
      this.props.onSelectDelta(delta);
    }
  },

  onContextMenu(evt) {
    evt.preventDefault();
    this.contextMenu.open(evt);
  },

  /**
   * If selection has just changed (by keyboard navigation), don't keep the list
   * scrolled to bottom, but allow scrolling up with the selection.
   */
  onFocusedNodeChange() {
    this.shouldScrollBottom = false;
  },

  render() {
    const {
      displayedRequests,
      firstRequestStartedMillis,
      selectedRequestId,
      onItemMouseDown,
      onSecurityIconClick,
    } = this.props;

    return (
      div({
        ref: "contentEl",
        className: "requests-list-contents",
        tabIndex: 0,
        onKeyDown: this.onKeyDown,
      },
        displayedRequests.map((item, index) => RequestListItem({
          firstRequestStartedMillis,
          item,
          index,
          isSelected: item.id === selectedRequestId,
          key: item.id,
          onContextMenu: this.onContextMenu,
          onFocusedNodeChange: this.onFocusedNodeChange,
          onMouseDown: () => onItemMouseDown(item.id),
          onSecurityIconClick: () => onSecurityIconClick(item.securityState),
        }))
      )
    );
  },
});

module.exports = connect(
  (state) => ({
    displayedRequests: getDisplayedRequests(state),
    firstRequestStartedMillis: state.requests.firstStartedMillis,
    selectedRequestId: state.requests.selectedId,
    scale: getWaterfallScale(state),
  }),
  (dispatch) => ({
    dispatch,
    onItemMouseDown: (id) => dispatch(Actions.selectRequest(id)),
    /**
     * A handler that opens the security tab in the details view if secure or
     * broken security indicator is clicked.
     */
    onSecurityIconClick: (securityState) => {
      if (securityState && securityState !== "insecure") {
        dispatch(Actions.selectDetailsPanelTab("security"));
      }
    },
    onSelectDelta: (delta) => dispatch(Actions.selectDelta(delta)),
  }),
)(RequestListContent);
