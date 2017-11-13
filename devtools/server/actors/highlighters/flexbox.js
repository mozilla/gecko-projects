/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { AutoRefreshHighlighter } = require("./auto-refresh");
const {
  CANVAS_SIZE,
  DEFAULT_COLOR,
  drawRect,
  getCurrentMatrix,
  updateCanvasElement,
  updateCanvasPosition,
} = require("./utils/canvas");
const {
  CanvasFrameAnonymousContentHelper,
  createNode,
} = require("./utils/markup");
const {
  getAdjustedQuads,
  getDisplayPixelRatio,
  setIgnoreLayoutChanges,
} = require("devtools/shared/layout/utils");

const FLEXBOX_LINES_PROPERTIES = {
  "edge": {
    lineDash: [0, 0],
    alpha: 1,
  },
  "item": {
    lineDash: [2, 2],
    alpha: 1,
  },
};

class FlexboxHighlighter extends AutoRefreshHighlighter {
  constructor(highlighterEnv) {
    super(highlighterEnv);

    this.ID_CLASS_PREFIX = "flexbox-";

    this.markup = new CanvasFrameAnonymousContentHelper(this.highlighterEnv,
      this._buildMarkup.bind(this));

    this.onPageHide = this.onPageHide.bind(this);
    this.onWillNavigate = this.onWillNavigate.bind(this);

    this.highlighterEnv.on("will-navigate", this.onWillNavigate);

    let { pageListenerTarget } = highlighterEnv;
    pageListenerTarget.addEventListener("pagehide", this.onPageHide);

    // Initialize the <canvas> position to the top left corner of the page
    this._canvasPosition = {
      x: 0,
      y: 0
    };

    // Calling `updateCanvasPosition` anyway since the highlighter could be initialized
    // on a page that has scrolled already.
    updateCanvasPosition(this._canvasPosition, this._scroll, this.win,
      this._winDimensions);
  }

  _buildMarkup() {
    let container = createNode(this.win, {
      attributes: {
        "class": "highlighter-container"
      }
    });

    let root = createNode(this.win, {
      parent: container,
      attributes: {
        "id": "root",
        "class": "root"
      },
      prefix: this.ID_CLASS_PREFIX
    });

    // We use a <canvas> element because there is an arbitrary number of items and texts
    // to draw which wouldn't be possible with HTML or SVG without having to insert and
    // remove the whole markup on every update.
    createNode(this.win, {
      parent: root,
      nodeType: "canvas",
      attributes: {
        "id": "canvas",
        "class": "canvas",
        "hidden": "true",
        "width": CANVAS_SIZE,
        "height": CANVAS_SIZE
      },
      prefix: this.ID_CLASS_PREFIX
    });

    return container;
  }

  destroy() {
    let { highlighterEnv } = this;
    highlighterEnv.off("will-navigate", this.onWillNavigate);

    let { pageListenerTarget } = highlighterEnv;
    if (pageListenerTarget) {
      pageListenerTarget.removeEventListener("pagehide", this.onPageHide);
    }

    this.markup.destroy();

    AutoRefreshHighlighter.prototype.destroy.call(this);
  }

  get canvas() {
    return this.getElement("canvas");
  }

  get ctx() {
    return this.canvas.getCanvasContext("2d");
  }

  getElement(id) {
    return this.markup.getElement(this.ID_CLASS_PREFIX + id);
  }

  /**
   * The AutoRefreshHighlighter's _hasMoved method returns true only if the
   * element's quads have changed. Override it so it also returns true if the
   * element and its flex items have changed.
   */
  _hasMoved() {
    let hasMoved = AutoRefreshHighlighter.prototype._hasMoved.call(this);

    // TODO: Implement a check of old and new flex container and flex items to react
    // to any alignment and size changes. This is blocked on Bug 1414920 that implements
    // a platform API to retrieve the flex container and flex item information.

    return hasMoved;
  }

  _hide() {
    setIgnoreLayoutChanges(true);
    this._hideFlexbox();
    setIgnoreLayoutChanges(false, this.highlighterEnv.document.documentElement);
  }

  _hideFlexbox() {
    this.getElement("canvas").setAttribute("hidden", "true");
  }

  /**
   * The <canvas>'s position needs to be updated if the page scrolls too much, in order
   * to give the illusion that it always covers the viewport.
   */
  _scrollUpdate() {
    let hasUpdated = updateCanvasPosition(this._canvasPosition, this._scroll, this.win,
      this._winDimensions);

    if (hasUpdated) {
      this._update();
    }
  }

  _show() {
    this._hide();
    return this._update();
  }

  _showFlexbox() {
    this.getElement("canvas").removeAttribute("hidden");
  }

  /**
   * If a page hide event is triggered for current window's highlighter, hide the
   * highlighter.
   */
  onPageHide({ target }) {
    if (target.defaultView === this.win) {
      this.hide();
    }
  }

  /**
   * Called when the page will-navigate. Used to hide the flexbox highlighter and clear
   * the cached gap patterns and avoid using DeadWrapper obejcts as gap patterns the
   * next time.
   */
  onWillNavigate({ isTopLevel }) {
    if (isTopLevel) {
      this.hide();
    }
  }

  renderFlexContainer() {
    if (!this.currentQuads.content || !this.currentQuads.content[0]) {
      return;
    }

    let { devicePixelRatio } = this.win;
    let lineWidth = getDisplayPixelRatio(this.win);
    let offset = (lineWidth / 2) % 1;

    let canvasX = Math.round(this._canvasPosition.x * devicePixelRatio);
    let canvasY = Math.round(this._canvasPosition.y * devicePixelRatio);

    this.ctx.save();
    this.ctx.translate(offset - canvasX, offset - canvasY);
    this.ctx.setLineDash(FLEXBOX_LINES_PROPERTIES.edge.lineDash);
    this.ctx.globalAlpha = FLEXBOX_LINES_PROPERTIES.edge.alpha;
    this.ctx.lineWidth = lineWidth;
    this.ctx.strokeStyle = DEFAULT_COLOR;

    let { bounds } = this.currentQuads.content[0];

    drawRect(this.ctx, 0, 0, bounds.width, bounds.height, this.currentMatrix);

    this.ctx.stroke();
    this.ctx.restore();
  }

  renderFlexItems() {
    if (!this.currentQuads.content || !this.currentQuads.content[0]) {
      return;
    }

    let { devicePixelRatio } = this.win;
    let lineWidth = getDisplayPixelRatio(this.win);
    let offset = (lineWidth / 2) % 1;

    let canvasX = Math.round(this._canvasPosition.x * devicePixelRatio);
    let canvasY = Math.round(this._canvasPosition.y * devicePixelRatio);

    this.ctx.save();
    this.ctx.translate(offset - canvasX, offset - canvasY);
    this.ctx.setLineDash(FLEXBOX_LINES_PROPERTIES.item.lineDash);
    this.ctx.globalAlpha = FLEXBOX_LINES_PROPERTIES.item.alpha;
    this.ctx.lineWidth = lineWidth;
    this.ctx.strokeStyle = DEFAULT_COLOR;

    let { bounds } = this.currentQuads.content[0];
    let flexItems = this.currentNode.children;

    // TODO: Utilize the platform API that will be implemented in Bug 1414290 to
    // retrieve the flex item properties.
    for (let flexItem of flexItems) {
      let quads = getAdjustedQuads(this.win, flexItem, "content");
      if (!quads.length) {
        continue;
      }

      // Adjust the flex item bounds relative to the current quads.
      let { bounds: flexItemBounds } = quads[0];
      let left = flexItemBounds.left - bounds.left;
      let top = flexItemBounds.top - bounds.top;
      let right = flexItemBounds.right - bounds.left;
      let bottom = flexItemBounds.bottom - bounds.top;

      drawRect(this.ctx, left, top, right, bottom, this.currentMatrix);
      this.ctx.stroke();
    }

    this.ctx.restore();
  }

  _update() {
    setIgnoreLayoutChanges(true);

    let root = this.getElement("root");

    // Hide the root element and force the reflow in order to get the proper window's
    // dimensions without increasing them.
    root.setAttribute("style", "display: none");
    this.win.document.documentElement.offsetWidth;

    let { width, height } = this._winDimensions;

    // Updates the <canvas> element's position and size.
    // It also clear the <canvas>'s drawing context.
    updateCanvasElement(this.canvas, this._canvasPosition, this.win.devicePixelRatio);

    // Update the current matrix used in our canvas' rendering
    let { currentMatrix, hasNodeTransformations } = getCurrentMatrix(this.currentNode,
      this.win);
    this.currentMatrix = currentMatrix;
    this.hasNodeTransformations = hasNodeTransformations;

    this.renderFlexContainer();
    this.renderFlexItems();

    this._showFlexbox();

    root.setAttribute("style",
      `position: absolute; width: ${width}px; height: ${height}px; overflow: hidden`);

    setIgnoreLayoutChanges(false, this.highlighterEnv.document.documentElement);
    return true;
  }
}

exports.FlexboxHighlighter = FlexboxHighlighter;
