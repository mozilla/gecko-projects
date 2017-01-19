/* -*- indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const promise = require("promise");
const {Task} = require("devtools/shared/task");
const EventEmitter = require("devtools/shared/event-emitter");
const { VIEW_NODE_VALUE_TYPE } = require("devtools/client/inspector/shared/node-types");

/**
 * Highlighters overlay is a singleton managing all highlighters in the Inspector.
 *
 * @param  {Inspector} inspector
 *         Inspector toolbox panel.
 */
function HighlightersOverlay(inspector) {
  this.inspector = inspector;
  this.highlighters = {};
  this.highlighterUtils = this.inspector.toolbox.highlighterUtils;

  // Only initialize the overlay if at least one of the highlighter types is supported.
  this.supportsHighlighters = this.highlighterUtils.supportsCustomHighlighters();

  // NodeFront of the grid container that is highlighted.
  this.gridHighlighterShown = null;
  // Name of the highlighter shown on mouse hover.
  this.hoveredHighlighterShown = null;
  // Name of the selector highlighter shown.
  this.selectorHighlighterShown = null;

  this.onClick = this.onClick.bind(this);
  this.onMouseMove = this.onMouseMove.bind(this);
  this.onMouseOut = this.onMouseOut.bind(this);
  this.onWillNavigate = this.onWillNavigate.bind(this);

  EventEmitter.decorate(this);
}

HighlightersOverlay.prototype = {
  get isRuleView() {
    return this.inspector.sidebar.getCurrentTabID() == "ruleview";
  },

  /**
   * Add the highlighters overlay to the view. This will start tracking mouse events
   * and display highlighters when needed.
   *
   * @param  {CssRuleView|CssComputedView|LayoutView} view
   *         Either the rule-view or computed-view panel to add the highlighters overlay.
   *
   */
  addToView: function (view) {
    if (!this.supportsHighlighters) {
      return;
    }

    let el = view.element;
    el.addEventListener("click", this.onClick, true);
    el.addEventListener("mousemove", this.onMouseMove);
    el.addEventListener("mouseout", this.onMouseOut);
    el.ownerDocument.defaultView.addEventListener("mouseout", this.onMouseOut);

    this.inspector.target.on("will-navigate", this.onWillNavigate);
  },

  /**
   * Remove the overlay from the given view. This will stop tracking mouse movement and
   * showing highlighters.
   *
   * @param  {CssRuleView|CssComputedView|LayoutView} view
   *         Either the rule-view or computed-view panel to remove the highlighters
   *         overlay.
   */
  removeFromView: function (view) {
    if (!this.supportsHighlighters) {
      return;
    }

    let el = view.element;
    el.removeEventListener("click", this.onClick, true);
    el.removeEventListener("mousemove", this.onMouseMove);
    el.removeEventListener("mouseout", this.onMouseOut);

    this.inspector.target.off("will-navigate", this.onWillNavigate);
  },

  /**
   * Toggle the grid highlighter for the given grid container element.
   *
   * @param  {NodeFront} node
   *         The NodeFront of the grid container element to highlight.
   * @param  {Object} options
   *         Object used for passing options to the grid highlighter.
   */
  toggleGridHighlighter: Task.async(function* (node, options = {}) {
    if (node == this.gridHighlighterShown) {
      yield this.hideGridHighlighter(node);
      return;
    }

    yield this.showGridHighlighter(node, options);
  }),

  /**
   * Show the grid highlighter for the given grid container element.
   *
   * @param  {NodeFront} node
   *         The NodeFront of the grid container element to highlight.
   * @param  {Object} options
   *         Object used for passing options to the grid highlighter.
   */
  showGridHighlighter: Task.async(function* (node, options) {
    let highlighter = yield this._getHighlighter("CssGridHighlighter");
    if (!highlighter) {
      return;
    }

    let isShown = yield highlighter.show(node, options);
    if (!isShown) {
      return;
    }

    this._toggleRuleViewGridIcon(node, true);

    // Emit the NodeFront of the grid container element that the grid highlighter was
    // shown for.
    this.emit("grid-highlighter-shown", node);
    this.gridHighlighterShown = node;
  }),

  /**
   * Hide the grid highlighter for the given grid container element.
   *
   * @param  {NodeFront} node
   *         The NodeFront of the grid container element to unhighlight.
   */
  hideGridHighlighter: Task.async(function* (node) {
    if (!this.gridHighlighterShown || !this.highlighters.CssGridHighlighter) {
      return;
    }

    this._toggleRuleViewGridIcon(node, false);

    yield this.highlighters.CssGridHighlighter.hide();

    // Emit the NodeFront of the grid container element that the grid highlighter was
    // hidden for.
    this.emit("grid-highlighter-hidden", this.gridHighlighterShown);
    this.gridHighlighterShown = null;
  }),

  /**
   * Get a highlighter front given a type. It will only be initialized once.
   *
   * @param  {String} type
   *         The highlighter type. One of this.highlighters.
   * @return {Promise} that resolves to the highlighter
   */
  _getHighlighter: function (type) {
    let utils = this.highlighterUtils;

    if (this.highlighters[type]) {
      return promise.resolve(this.highlighters[type]);
    }

    return utils.getHighlighterByType(type).then(highlighter => {
      this.highlighters[type] = highlighter;
      return highlighter;
    });
  },

  /**
   * Toggle all the grid icons in the rule view if the current inspector selection is the
   * highlighted node.
   *
   * @param  {NodeFront} node
   *         The NodeFront of the grid container element to highlight.
   * @param  {Boolean} active
   *         Whether or not the grid icon should be active.
   */
  _toggleRuleViewGridIcon: function (node, active) {
    if (this.inspector.selection.nodeFront != node) {
      return;
    }

    let ruleViewEl = this.inspector.ruleview.view.element;

    for (let gridIcon of ruleViewEl.querySelectorAll(".ruleview-grid")) {
      gridIcon.classList.toggle("active", active);
    }
  },

  /**
   * Hide the currently shown hovered highlighter.
   */
  _hideHoveredHighlighter: function () {
    if (!this.hoveredHighlighterShown ||
        !this.highlighters[this.hoveredHighlighterShown]) {
      return;
    }

    // For some reason, the call to highlighter.hide doesn't always return a
    // promise. This causes some tests to fail when trying to install a
    // rejection handler on the result of the call. To avoid this, check
    // whether the result is truthy before installing the handler.
    let onHidden = this.highlighters[this.hoveredHighlighterShown].hide();
    if (onHidden) {
      onHidden.then(null, e => console.error(e));
    }

    this.hoveredHighlighterShown = null;
    this.emit("highlighter-hidden");
  },

  /**
   * Is the current hovered node a css transform property value in the
   * computed-view.
   *
   * @param  {Object} nodeInfo
   * @return {Boolean}
   */
  _isComputedViewTransform: function (nodeInfo) {
    let isTransform = nodeInfo.type === VIEW_NODE_VALUE_TYPE &&
                      nodeInfo.value.property === "transform";
    return !this.isRuleView && isTransform;
  },

  /**
   * Is the current clicked node a grid display property value in the
   * rule-view.
   *
   * @param  {DOMNode} node
   * @return {Boolean}
   */
  _isRuleViewDisplayGrid: function (node) {
    return this.isRuleView && node.classList.contains("ruleview-grid");
  },

  /**
   * Is the current hovered node a css transform property value in the rule-view.
   *
   * @param  {Object} nodeInfo
   * @return {Boolean}
   */
  _isRuleViewTransform: function (nodeInfo) {
    let isTransform = nodeInfo.type === VIEW_NODE_VALUE_TYPE &&
                      nodeInfo.value.property === "transform";
    let isEnabled = nodeInfo.value.enabled &&
                    !nodeInfo.value.overridden &&
                    !nodeInfo.value.pseudoElement;
    return this.isRuleView && isTransform && isEnabled;
  },

  onClick: function (event) {
    // Bail out if the target is not a grid property value.
    if (!this._isRuleViewDisplayGrid(event.target)) {
      return;
    }

    event.stopPropagation();
    this.toggleGridHighlighter(this.inspector.selection.nodeFront);
  },

  onMouseMove: function (event) {
    // Bail out if the target is the same as for the last mousemove.
    if (event.target === this._lastHovered) {
      return;
    }

    // Only one highlighter can be displayed at a time, hide the currently shown.
    this._hideHoveredHighlighter();

    this._lastHovered = event.target;

    let view = this.isRuleView ?
      this.inspector.ruleview.view : this.inspector.computedview.computedView;
    let nodeInfo = view.getNodeInfo(event.target);
    if (!nodeInfo) {
      return;
    }

    // Choose the type of highlighter required for the hovered node.
    let type;
    if (this._isRuleViewTransform(nodeInfo) ||
        this._isComputedViewTransform(nodeInfo)) {
      type = "CssTransformHighlighter";
    }

    if (type) {
      this.hoveredHighlighterShown = type;
      let node = this.inspector.selection.nodeFront;
      this._getHighlighter(type)
          .then(highlighter => highlighter.show(node))
          .then(shown => {
            if (shown) {
              this.emit("highlighter-shown");
            }
          });
    }
  },

  onMouseOut: function (event) {
    // Only hide the highlighter if the mouse leaves the currently hovered node.
    if (!this._lastHovered ||
        (event && this._lastHovered.contains(event.relatedTarget))) {
      return;
    }

    // Otherwise, hide the highlighter.
    this._lastHovered = null;
    this._hideHoveredHighlighter();
  },

  /**
   * Clear saved highlighter shown properties on will-navigate.
   */
  onWillNavigate: function () {
    this.gridHighlighterShown = null;
    this.hoveredHighlighterShown = null;
    this.selectorHighlighterShown = null;
  },

  /**
   * Destroy this overlay instance, removing it from the view and destroying
   * all initialized highlighters.
   */
  destroy: function () {
    for (let type in this.highlighters) {
      if (this.highlighters[type]) {
        this.highlighters[type].finalize();
        this.highlighters[type] = null;
      }
    }

    this._lastHovered = null;

    this.inspector = null;
    this.highlighters = null;
    this.highlighterUtils = null;
    this.supportsHighlighters = null;
    this.gridHighlighterShown = null;
    this.hoveredHighlighterShown = null;
    this.selectorHighlighterShown = null;
  }
};

module.exports = HighlightersOverlay;
