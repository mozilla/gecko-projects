/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

this.EXPORTED_SYMBOLS = ["FinderHighlighter"];

const { interfaces: Ci, utils: Cu } = Components;

Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource://gre/modules/Task.jsm");
Cu.import("resource://gre/modules/XPCOMUtils.jsm");

XPCOMUtils.defineLazyModuleGetter(this, "Color", "resource://gre/modules/Color.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "Rect", "resource://gre/modules/Geometry.jsm");
XPCOMUtils.defineLazyGetter(this, "kDebug", () => {
  const kDebugPref = "findbar.modalHighlight.debug";
  return Services.prefs.getPrefType(kDebugPref) && Services.prefs.getBoolPref(kDebugPref);
});

const kContentChangeThresholdPx = 5;
const kModalHighlightRepaintFreqMs = 200;
const kHighlightAllPref = "findbar.highlightAll";
const kModalHighlightPref = "findbar.modalHighlight";
const kFontPropsCSS = ["color", "font-family", "font-kerning", "font-size",
  "font-size-adjust", "font-stretch", "font-variant", "font-weight", "line-height",
  "letter-spacing", "text-emphasis", "text-orientation", "text-transform", "word-spacing"];
const kFontPropsCamelCase = kFontPropsCSS.map(prop => {
  let parts = prop.split("-");
  return parts.shift() + parts.map(part => part.charAt(0).toUpperCase() + part.slice(1)).join("");
});
const kRGBRE = /^rgba?\s*\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*/i
// This uuid is used to prefix HTML element IDs and classNames in order to make
// them unique and hard to clash with IDs and classNames content authors come up
// with, since the stylesheet for modal highlighting is inserted as an agent-sheet
// in the active HTML document.
const kModalIdPrefix = "cedee4d0-74c5-4f2d-ab43-4d37c0f9d463";
const kModalOutlineId = kModalIdPrefix + "-findbar-modalHighlight-outline";
const kModalStyle = `
.findbar-modalHighlight-outline {
  position: absolute;
  background: #ffc535;
  border-radius: 3px;
  box-shadow: 0 2px 0 0 rgba(0,0,0,.1);
  color: #000;
  display: -moz-box;
  margin: -2px 0 0 -2px !important;
  padding: 2px !important;
  pointer-events: none;
  z-index: 2;
}

.findbar-modalHighlight-outline.findbar-debug {
  z-index: 2147483647;
}

.findbar-modalHighlight-outline[grow] {
  animation-name: findbar-modalHighlight-outlineAnim;
}

@keyframes findbar-modalHighlight-outlineAnim {
  from {
    transform: scaleX(0) scaleY(0);
  }
  50% {
    transform: scaleX(1.5) scaleY(1.5);
  }
  to {
    transform: scaleX(0) scaleY(0);
  }
}

.findbar-modalHighlight-outline[hidden] {
  opacity: 0;
}

.findbar-modalHighlight-outline:not([disable-transitions]) {
  transition-property: opacity, transform, top, left;
  transition-duration: 50ms;
  transition-timing-function: linear;
}

.findbar-modalHighlight-outline-text {
  margin: 0 !important;
  padding: 0 !important;
  vertical-align: top !important;
}

.findbar-modalHighlight-outlineMask {
  background: #000;
  mix-blend-mode: multiply;
  opacity: .35;
  pointer-events: none;
  position: absolute;
  z-index: 1;
}

.findbar-modalHighlight-outlineMask.findbar-debug {
  z-index: 2147483646;
  top: 0;
  left: 0;
}

.findbar-modalHighlight-outlineMask[brighttext] {
  background: #fff;
}

.findbar-modalHighlight-rect {
  background: #fff;
  margin: -1px 0 0 -1px !important;
  padding: 0 1px 2px 1px !important;
  position: absolute;
}

.findbar-modalHighlight-outlineMask[brighttext] > .findbar-modalHighlight-rect {
  background: #000;
}`;

function mockAnonymousContentNode(domNode) {
  return {
    setTextContentForElement(id, text) {
      (domNode.querySelector("#" + id) || domNode).textContent = text;
    },
    getAttributeForElement(id, attrName) {
      let node = domNode.querySelector("#" + id) || domNode;
      if (!node.hasAttribute(attrName))
        return undefined;
      return node.getAttribute(attrName);
    },
    setAttributeForElement(id, attrName, attrValue) {
      (domNode.querySelector("#" + id) || domNode).setAttribute(attrName, attrValue);
    },
    removeAttributeForElement(id, attrName) {
      let node = domNode.querySelector("#" + id) || domNode;
      if (!node.hasAttribute(attrName))
        return;
      node.removeAttribute(attrName);
    },
    remove() {
      try {
        domNode.parentNode.removeChild(domNode);
      } catch (ex) {}
    }
  };
}

let gWindows = new Map();

/**
 * FinderHighlighter class that is used by Finder.jsm to take care of the
 * 'Highlight All' feature, which can highlight all find occurrences in a page.
 *
 * @param {Finder} finder Finder.jsm instance
 */
function FinderHighlighter(finder) {
  this._highlightAll = Services.prefs.getBoolPref(kHighlightAllPref);
  this._modal = Services.prefs.getBoolPref(kModalHighlightPref);
  this.finder = finder;
}

FinderHighlighter.prototype = {
  get iterator() {
    if (this._iterator)
      return this._iterator;
    this._iterator = Cu.import("resource://gre/modules/FinderIterator.jsm", null).FinderIterator;
    return this._iterator;
  },

  get modalStyleSheet() {
    if (!this._modalStyleSheet) {
      this._modalStyleSheet = kModalStyle.replace(/findbar-/g,
        kModalIdPrefix + "-findbar-");
    }
    return this._modalStyleSheet;
  },

  get modalStyleSheetURI() {
    if (!this._modalStyleSheetURI) {
      this._modalStyleSheetURI = "data:text/css;charset=utf-8," +
        encodeURIComponent(this.modalStyleSheet.replace(/[\n]+/g, " "));
    }
    return this._modalStyleSheetURI;
  },

  /**
   * Each window is unique, globally, and the relation between an active
   * highlighting session and a window is 1:1.
   * For each window we track a number of properties which _at least_ consist of
   *  - {Set}     dynamicRangesSet       Set of ranges that may move around, depending
   *                                     on page layout changes and user input
   *  - {Map}     frames                 Collection of frames that were encountered
   *                                     when inspecting the found ranges
   *  - {Boolean} installedSheet         Whether the modal stylesheet was loaded
   *                                     already
   *  - {Map}     modalHighlightRectsMap Collection of ranges and their corresponding
   *                                     Rects
   *
   * @param  {nsIDOMWindow} window
   * @return {Object}
   */
  getForWindow(window, propName = null) {
    if (!gWindows.has(window)) {
      gWindows.set(window, {
        dynamicRangesSet: new Set(),
        frames: new Map(),
        installedSheet: false,
        modalHighlightRectsMap: new Map()
      });
    }
    return gWindows.get(window);
  },

  /**
   * Notify all registered listeners that the 'Highlight All' operation finished.
   *
   * @param {Boolean} highlight Whether highlighting was turned on
   */
  notifyFinished(highlight) {
    for (let l of this.finder._listeners) {
      try {
        l.onHighlightFinished(highlight);
      } catch (ex) {}
    }
  },

  /**
   * Toggle highlighting all occurrences of a word in a page. This method will
   * be called recursively for each (i)frame inside a page.
   *
   * @param {Booolean} highlight Whether highlighting should be turned on
   * @param {String}   word      Needle to search for and highlight when found
   * @param {Boolean}  linksOnly Only consider nodes that are links for the search
   * @yield {Promise}  that resolves once the operation has finished
   */
  highlight: Task.async(function* (highlight, word, linksOnly) {
    let window = this.finder._getWindow();
    let dict = this.getForWindow(window);
    let controller = this.finder._getSelectionController(window);
    let doc = window.document;
    this._found = false;

    if (!controller || !doc || !doc.documentElement) {
      // Without the selection controller,
      // we are unable to (un)highlight any matches
      return this._found;
    }

    if (highlight) {
      let params = {
        allowDistance: 1,
        caseSensitive: this.finder._fastFind.caseSensitive,
        entireWord: this.finder._fastFind.entireWord,
        linksOnly, word,
        finder: this.finder,
        listener: this,
        useCache: true
      };
      if (this.iterator._areParamsEqual(params, dict.lastIteratorParams))
        return this._found;
      if (params) {
        yield this.iterator.start(params);
        if (this._found)
          this.finder._outlineLink(true);
      }
    } else {
      this.hide(window);

      // Removing the highlighting always succeeds, so return true.
      this._found = true;
    }

    return this._found;
  }),

  // FinderIterator listener implementation

  onIteratorRangeFound(range) {
    this.highlightRange(range);
    this._found = true;
  },

  onIteratorReset() {
    this.clear(this.finder._getWindow());
  },

  onIteratorRestart() {},

  onIteratorStart(params) {
    let window = this.finder._getWindow();
    let dict = this.getForWindow(window);
    // Save a clean params set for use later in the `update()` method.
    dict.lastIteratorParams = params;
    this.clear(window);
    if (!this._modal)
      this.hide(window, this.finder._fastFind.getFoundRange());
  },

  /**
   * Add a range to the find selection, i.e. highlight it, and if it's inside an
   * editable node, track it.
   *
   * @param {nsIDOMRange} range Range object to be highlighted
   */
  highlightRange(range) {
    let node = range.startContainer;
    let editableNode = this._getEditableNode(node);
    let window = node.ownerDocument.defaultView;
    let controller = this.finder._getSelectionController(window);
    if (editableNode) {
      controller = editableNode.editor.selectionController;
    }

    if (this._modal) {
      this._modalHighlight(range, controller, window);
    } else {
      let findSelection = controller.getSelection(Ci.nsISelectionController.SELECTION_FIND);
      findSelection.addRange(range);
    }

    if (editableNode) {
      // Highlighting added, so cache this editor, and hook up listeners
      // to ensure we deal properly with edits within the highlighting
      this._addEditorListeners(editableNode.editor);
    }
  },

  /**
   * If modal highlighting is enabled, show the dimmed background that will overlay
   * the page.
   *
   * @param {nsIDOMWindow} window The dimmed background will overlay this window.
   *                              Optional, defaults to the finder window.
   */
  show(window = null) {
    window = (window || this.finder._getWindow()).top;
    let dict = this.getForWindow(window);
    if (!this._modal || dict.visible)
      return;

    dict.visible = true;

    this._maybeCreateModalHighlightNodes(window);
    this._addModalHighlightListeners(window);
  },

  /**
   * Clear all highlighted matches. If modal highlighting is enabled and
   * the outline + dimmed background is currently visible, both will be hidden.
   *
   * @param {nsIDOMWindow} window    The dimmed background will overlay this window.
   *                                 Optional, defaults to the finder window.
   * @param {nsIDOMRange}  skipRange A range that should not be removed from the
   *                                 find selection.
   * @param {nsIDOMEvent}  event     When called from an event handler, this will
   *                                 be the triggering event.
   */
  hide(window = null, skipRange = null, event = null) {
    // Do not hide on anything but a left-click.
    if (event && event.type == "click" && event.button !== 0)
      return;

    window = (window || this.finder._getWindow()).top;
    let dict = this.getForWindow(window);

    this._clearSelection(this.finder._getSelectionController(window), skipRange);
    for (let frame of dict.frames)
      this._clearSelection(this.finder._getSelectionController(frame), skipRange);

    // Next, check our editor cache, for editors belonging to this
    // document
    if (this._editors) {
      let doc = window.document;
      for (let x = this._editors.length - 1; x >= 0; --x) {
        if (this._editors[x].document == doc) {
          this._clearSelection(this._editors[x].selectionController, skipRange);
          // We don't need to listen to this editor any more
          this._unhookListenersAtIndex(x);
        }
      }
    }

    if (dict.modalRepaintScheduler) {
      window.clearTimeout(dict.modalRepaintScheduler);
      dict.modalRepaintScheduler = null;
    }
    dict.lastWindowDimensions = null;

    if (dict.modalHighlightOutline)
      dict.modalHighlightOutline.setAttributeForElement(kModalOutlineId, "hidden", "true");

    this._removeHighlightAllMask(window);
    this._removeModalHighlightListeners(window);
    delete dict.brightText;

    dict.visible = false;
  },

  /**
   * Called by the Finder after a find result comes in; update the position and
   * content of the outline to the newly found occurrence.
   * To make sure that the outline covers the found range completely, all the
   * CSS styles that influence the text are copied and applied to the outline.
   *
   * @param {Object} data Dictionary coming from Finder that contains the
   *                      following properties:
   *   {Number}  result        One of the nsITypeAheadFind.FIND_* constants
   *                           indicating the result of a search operation.
   *   {Boolean} findBackwards If TRUE, the search was performed backwards,
   *                           FALSE if forwards.
   *   {Boolean} findAgain     If TRUE, the search was performed using the same
   *                           search string as before.
   *   {String}  linkURL       If a link was hit, this will contain a URL string.
   *   {Rect}    rect          An object with top, left, width and height
   *                           coordinates of the current selection.
   *   {String}  searchString  The string the search was performed with.
   *   {Boolean} storeResult   Indicator if the search string should be stored
   *                           by the consumer of the Finder.
   */
  update(data) {
    let window = this.finder._getWindow();
    let dict = this.getForWindow(window);
    let foundRange = this.finder._fastFind.getFoundRange();
    if (!this._modal) {
      if (this._highlightAll) {
        dict.currentFoundRange = foundRange;
        let params = this.iterator.params;
        if (this.iterator._areParamsEqual(params, dict.lastIteratorParams))
          return;
        if (params)
          this.highlight(true, params.word, params.linksOnly);
      }
      return;
    }

    // Place the match placeholder on top of the current found range.
    if (data.result == Ci.nsITypeAheadFind.FIND_NOTFOUND || !foundRange) {
      this.hide();
      return;
    }

    let outlineNode;
    if (foundRange !== dict.currentFoundRange || data.findAgain) {
      dict.currentFoundRange = foundRange;

      let textContent = this._getRangeContentArray(foundRange);
      if (!textContent.length) {
        this.hide(window);
        return;
      }

      let fontStyle = this._getRangeFontStyle(foundRange);
      if (typeof dict.brightText == "undefined") {
        dict.brightText = this._isColorBright(fontStyle.color);
      }

      if (!dict.visible)
        this.show(window);
      else
        this._maybeCreateModalHighlightNodes(window);

      this._updateRangeOutline(dict, textContent, fontStyle);
    }

    outlineNode = dict.modalHighlightOutline;
    try {
      outlineNode.removeAttributeForElement(kModalOutlineId, "grow");
    } catch (ex) {}
    window.requestAnimationFrame(() => {
      outlineNode.setAttributeForElement(kModalOutlineId, "grow", true);
    });
  },

  /**
   * Invalidates the list by clearing the map of highglighted ranges that we
   * keep to build the mask for.
   */
  clear(window = null) {
    if (!window) {
      // Reset the Map, because no range references a node anymore.
      gWindows.clear();
      return;
    }

    let dict = this.getForWindow(window.top);
    dict.currentFoundRange = null;
    dict.dynamicRangesSet.clear();
    dict.frames.clear();
    dict.modalHighlightRectsMap.clear();
  },

  /**
   * When the current page is refreshed or navigated away from, the CanvasFrame
   * contents is not valid anymore, i.e. all anonymous content is destroyed.
   * We need to clear the references we keep, which'll make sure we redraw
   * everything when the user starts to find in page again.
   */
  onLocationChange() {
    let window = this.finder._getWindow();
    let dict = this.getForWindow(window);
    this.clear(window);

    if (!dict.modalHighlightOutline)
      return;

    if (kDebug) {
      dict.modalHighlightOutline.remove();
    } else {
      try {
        window.document.removeAnonymousContent(dict.modalHighlightOutline);
      } catch (ex) {}
    }

    dict.modalHighlightOutline = null;
  },

  /**
   * When `kModalHighlightPref` pref changed during a session, this callback is
   * invoked. When modal highlighting is turned off, we hide the CanvasFrame
   * contents.
   *
   * @param {Boolean} useModalHighlight
   */
  onModalHighlightChange(useModalHighlight) {
    if (this._modal && !useModalHighlight) {
      this.hide();
      this.clear();
    }
    this._modal = useModalHighlight;
  },

  /**
   * When 'Highlight All' is toggled during a session, this callback is invoked
   * and when it's turned off, the found occurrences will be removed from the mask.
   *
   * @param {Boolean} highlightAll
   */
  onHighlightAllChange(highlightAll) {
    this._highlightAll = highlightAll;
    if (this._modal && !highlightAll) {
      this.clear();
      this._scheduleRepaintOfMask(this.finder._getWindow());
    }
  },

  /**
   * Utility; removes all ranges from the find selection that belongs to a
   * controller. Optionally skips a specific range.
   *
   * @param  {nsISelectionController} controller
   * @param  {nsIDOMRange}            restoreRange
   */
  _clearSelection(controller, restoreRange = null) {
    if (!controller)
      return;
    let sel = controller.getSelection(Ci.nsISelectionController.SELECTION_FIND);
    sel.removeAllRanges();
    if (restoreRange) {
      sel = controller.getSelection(Ci.nsISelectionController.SELECTION_NORMAL);
      sel.addRange(restoreRange);
      controller.setDisplaySelection(Ci.nsISelectionController.SELECTION_ATTENTION);
      controller.repaintSelection(Ci.nsISelectionController.SELECTION_NORMAL);
    }
  },

  /**
   * Utility; get the nsIDOMWindowUtils for a window.
   *
   * @param  {nsIDOMWindow} window Optional, defaults to the finder window.
   * @return {nsIDOMWindowUtils}
   */
  _getDWU(window = null) {
    return (window || this.finder._getWindow())
      .QueryInterface(Ci.nsIInterfaceRequestor)
      .getInterface(Ci.nsIDOMWindowUtils);
  },

  /**
   * Utility; returns the bounds of the page relative to the viewport.
   * If the pages is part of a frameset or inside an iframe of any kind, its
   * offset is accounted for.
   * Geometry.jsm takes care of the DOMRect calculations.
   *
   * @param  {nsIDOMWindow} window
   * @return {Rect}
   */
  _getRootBounds(window) {
    let dwu = this._getDWU(window);
    let cssPageRect = Rect.fromRect(dwu.getRootBounds());

    let scrollX = {};
    let scrollY = {};
    dwu.getScrollXY(false, scrollX, scrollY);
    cssPageRect.translate(scrollX.value, scrollY.value);

    // If we're in a frame, update the position of the rect (top/ left).
    let currWin = window;
    while (currWin != window.top) {
      // Since the frame is an element inside a parent window, we'd like to
      // learn its position relative to it.
      let el = this._getDWU(currWin).containerElement;
      currWin = window.parent;
      dwu = this._getDWU(currWin);
      let parentRect = Rect.fromRect(dwu.getBoundsWithoutFlushing(el));

      // Always take the scroll position into account.
      dwu.getScrollXY(false, scrollX, scrollY);
      parentRect.translate(scrollX.value, scrollY.value);

      cssPageRect.translate(parentRect.left, parentRect.top);
    }

    return cssPageRect;
  },

  /**
   * Utility; fetch the full width and height of the current window, excluding
   * scrollbars.
   *
   * @param  {nsiDOMWindow} window The current finder window.
   * @return {Object} The current full page dimensions with `width` and `height`
   *                  properties
   */
  _getWindowDimensions(window) {
    // First we'll try without flushing layout, because it's way faster.
    let dwu = this._getDWU(window);
    let { width, height } = dwu.getRootBounds();

    if (!width || !height) {
      // We need a flush after all :'(
      width = window.innerWidth + window.scrollMaxX - window.scrollMinX;
      height = window.innerHeight + window.scrollMaxY - window.scrollMinY;
    }

    let scrollbarHeight = {};
    let scrollbarWidth = {};
    dwu.getScrollbarSize(false, scrollbarWidth, scrollbarHeight);
    width -= scrollbarWidth.value;
    height -= scrollbarHeight.value;

    return { width, height };
  },

  /**
   * Utility; fetch the current text contents of a given range.
   *
   * @param  {nsIDOMRange} range Range object to extract the contents from.
   * @return {Array} Snippets of text.
   */
  _getRangeContentArray(range) {
    let content = range.cloneContents();
    let t, textContent = [];
    for (let node of content.childNodes) {
      t = node.textContent || node.nodeValue;
      //if (t && t.trim())
        textContent.push(t);
    }
    return textContent;
  },

  /**
   * Utility; get all available font styles as applied to the content of a given
   * range. The CSS properties we look for can be found in `kFontPropsCSS`.
   *
   * @param  {nsIDOMRange} range Range to fetch style info from.
   * @return {Object} Dictionary consisting of the styles that were found.
   */
  _getRangeFontStyle(range) {
    let node = range.startContainer;
    while (node.nodeType != 1)
      node = node.parentNode;
    let style = node.ownerDocument.defaultView.getComputedStyle(node, "");
    let props = {};
    for (let prop of kFontPropsCamelCase) {
      if (prop in style && style[prop])
        props[prop] = style[prop];
    }
    return props;
  },

  /**
   * Utility; transform a dictionary object as returned by `_getRangeFontStyle`
   * above into a HTML style attribute value.
   *
   * @param  {Object} fontStyle
   * @return {String}
   */
  _getHTMLFontStyle(fontStyle) {
    let style = [];
    for (let prop of Object.getOwnPropertyNames(fontStyle)) {
      let idx = kFontPropsCamelCase.indexOf(prop);
      if (idx == -1)
        continue;
      style.push(`${kFontPropsCSS[idx]}: ${fontStyle[prop]};`);
    }
    return style.join(" ");
  },

  /**
   * Checks whether a CSS RGB color value can be classified as being 'bright'.
   *
   * @param  {String} cssColor RGB color value in the default format rgb[a](r,g,b)
   * @return {Boolean}
   */
  _isColorBright(cssColor) {
    cssColor = cssColor.match(kRGBRE);
    if (!cssColor || !cssColor.length)
      return false;
    cssColor.shift();
    return new Color(...cssColor).isBright;
  },

  /**
   * Checks if a range is inside a DOM node that's positioned in a way that it
   * doesn't scroll along when the document is scrolled and/ or zoomed. This
   * is the case for 'fixed' and 'sticky' positioned elements and elements inside
   * (i)frames.
   *
   * @param  {nsIDOMRange} range Range that be enclosed in a fixed container
   * @return {Boolean}
   */
  _isInDynamicContainer(range) {
    const kFixed = new Set(["fixed", "sticky"]);
    let node = range.startContainer;
    while (node.nodeType != 1)
      node = node.parentNode;
    let document = node.ownerDocument;
    let window = document.defaultView;
    let dict = this.getForWindow(window.top);

    // Check if we're in a frameset (including iframes).
    if (window != window.top) {
      if (!dict.frames.has(window))
        dict.frames.set(window, null);
      return true;
    }

    do {
      if (kFixed.has(window.getComputedStyle(node, null).position))
        return true;
      node = node.parentNode;
    } while (node && node != document.documentElement)

    return false;
  },

  /**
   * Read and store the rectangles that encompass the entire region of a range
   * for use by the drawing function of the highlighter.
   *
   * @param {nsIDOMRange} range            Range to fetch the rectangles from
   * @param {Boolean}     [checkIfDynamic] Whether we should check if the range
   *                                       is dynamic as per the rules in
   *                                       `_isInDynamicContainer()`. Optional,
   *                                       defaults to `true`
   * @param {Object}     [dict]            Dictionary of properties belonging to
   *                                       the currently active window
   */
  _updateRangeRects(range, checkIfDynamic = true, dict = null) {
    let window = range.startContainer.ownerDocument.defaultView;
    let bounds;
    // If the window is part of a frameset, try to cache the bounds query.
    if (dict && dict.frames.has(window)) {
      bounds = dict.frames.get(window);
      if (!bounds) {
        bounds = this._getRootBounds(window);
        dict.frames.set(window, bounds);
      }
    } else
      bounds = this._getRootBounds(window);

    let rects = new Set();
    // A range may consist of multiple rectangles, we can also do these kind of
    // precise cut-outs. range.getBoundingClientRect() returns the fully
    // encompassing rectangle, which is too much for our purpose here.
    for (let dims of range.getClientRects()) {
      rects.add({
        height: dims.bottom - dims.top,
        width: dims.right - dims.left,
        y: dims.top + bounds.top,
        x: dims.left + bounds.left
      });
    }

    dict = dict || this.getForWindow(window.top);
    dict.modalHighlightRectsMap.set(range, rects);
    if (checkIfDynamic && this._isInDynamicContainer(range))
      dict.dynamicRangesSet.add(range);
  },

  /**
   * Re-read the rectangles of the ranges that we keep track of separately,
   * because they're enclosed by a position: fixed container DOM node.
   *
   * @param {Object} dict Dictionary of properties belonging to the currently
   *                      active window
   */
  _updateFixedRangesRects(dict) {
    for (let range of dict.dynamicRangesSet)
      this._updateRangeRects(range, false, dict);
    // Reset the frame bounds cache.
    for (let frame of dict.frames.keys())
      dict.frames.set(frame, null);
  },

  /**
   * Update the content, position and style of the yellow current found range
   * outline the floats atop the mask with the dimmed background.
   *
   * @param {Object} dict          Dictionary of properties belonging to the
   *                               currently active window
   * @param {Array}  [textContent] Array of text that's inside the range. Optional,
   *                               defaults to an empty array
   * @param {Object} [fontStyle]   Dictionary of CSS styles in camelCase as
   *                               returned by `_getRangeFontStyle()`. Optional
   */
  _updateRangeOutline(dict, textContent = [], fontStyle = null) {
    let outlineNode = dict.modalHighlightOutline;
    let range = dict.currentFoundRange;
    if (!outlineNode || !range)
      return;
    let rect = range.getClientRects()[0];
    if (!rect)
      return;

    if (!fontStyle)
      fontStyle = this._getRangeFontStyle(range);
    // Text color in the outline is determined by our stylesheet.
    delete fontStyle.color;

    if (textContent.length)
      outlineNode.setTextContentForElement(kModalOutlineId + "-text", textContent.join(" "));
    // Correct the line-height to align the text in the middle of the box.
    fontStyle.lineHeight = rect.height + "px";
    outlineNode.setAttributeForElement(kModalOutlineId + "-text", "style",
      this._getHTMLFontStyle(fontStyle));

    if (typeof outlineNode.getAttributeForElement(kModalOutlineId, "hidden") == "string")
      outlineNode.removeAttributeForElement(kModalOutlineId, "hidden");

    let window = range.startContainer.ownerDocument.defaultView;
    let { left, top } = this._getRootBounds(window);
    outlineNode.setAttributeForElement(kModalOutlineId, "style",
      `top: ${top + rect.top}px; left: ${left + rect.left}px;
      height: ${rect.height}px; width: ${rect.width}px;`);
  },

  /**
   * Add a range to the list of ranges to highlight on, or cut out of, the dimmed
   * background.
   *
   * @param {nsIDOMRange}  range  Range object that should be inspected
   * @param {nsIDOMWindow} window Window object, whose DOM tree is being traversed
   */
  _modalHighlight(range, controller, window) {
    if (!this._getRangeContentArray(range).length)
      return;

    this._updateRangeRects(range);

    this.show(window);
    // We don't repaint the mask right away, but pass it off to a render loop of
    // sorts.
    this._scheduleRepaintOfMask(window);
  },

  /**
   * Lazily insert the nodes we need as anonymous content into the CanvasFrame
   * of a window.
   *
   * @param {nsIDOMWindow} window Window to draw in.
   */
  _maybeCreateModalHighlightNodes(window) {
    window = window.top;
    let dict = this.getForWindow(window);
    if (dict.modalHighlightOutline) {
      if (!dict.modalHighlightAllMask) {
        // Make sure to at least show the dimmed background.
        this._repaintHighlightAllMask(window, false);
        this._scheduleRepaintOfMask(window);
      }
      return;
    }

    let document = window.document;
    // A hidden document doesn't accept insertAnonymousContent calls yet.
    if (document.hidden) {
      let onVisibilityChange = () => {
        document.removeEventListener("visibilitychange", onVisibilityChange);
        this._maybeCreateModalHighlightNodes(window);
      };
      document.addEventListener("visibilitychange", onVisibilityChange);
      return;
    }

    this._maybeInstallStyleSheet(window);

    // The outline needs to be sitting inside a container, otherwise the anonymous
    // content API won't find it by its ID later...
    let container = document.createElement("div");

    // Create the main (yellow) highlight outline box.
    let outlineBox = document.createElement("div");
    outlineBox.setAttribute("id", kModalOutlineId);
    outlineBox.className = kModalOutlineId + (kDebug ? ` ${kModalIdPrefix}-findbar-debug` : "");
    let outlineBoxText = document.createElement("span");
    let attrValue = kModalOutlineId + "-text";
    outlineBoxText.setAttribute("id", attrValue);
    outlineBoxText.setAttribute("class", attrValue);
    outlineBox.appendChild(outlineBoxText);

    container.appendChild(outlineBox);
    dict.modalHighlightOutline = kDebug ?
      mockAnonymousContentNode(document.body.appendChild(container.firstChild)) :
      document.insertAnonymousContent(container);

    // Make sure to at least show the dimmed background.
    this._repaintHighlightAllMask(window, false);
  },

  /**
   * Build and draw the mask that takes care of the dimmed background that
   * overlays the current page and the mask that cuts out all the rectangles of
   * the ranges that were found.
   *
   * @param {nsIDOMWindow} window Window to draw in.
   * @param {Boolean} [paintContent]
   */
  _repaintHighlightAllMask(window, paintContent = true) {
    window = window.top;
    let dict = this.getForWindow(window);
    let document = window.document;

    const kMaskId = kModalIdPrefix + "-findbar-modalHighlight-outlineMask";
    let maskNode = document.createElement("div");

    // Make sure the dimmed mask node takes the full width and height that's available.
    let {width, height} = this._getWindowDimensions(window);
    dict.lastWindowDimensions = { width, height };
    maskNode.setAttribute("id", kMaskId);
    maskNode.setAttribute("class", kMaskId + (kDebug ? ` ${kModalIdPrefix}-findbar-debug` : ""));
    maskNode.setAttribute("style", `width: ${width}px; height: ${height}px;`);
    if (dict.brightText)
      maskNode.setAttribute("brighttext", "true");

    if (paintContent || dict.modalHighlightAllMask) {
      this._updateRangeOutline(dict);
      this._updateFixedRangesRects(dict);
      // Create a DOM node for each rectangle representing the ranges we found.
      let maskContent = [];
      const kRectClassName = kModalIdPrefix + "-findbar-modalHighlight-rect";
      for (let [range, rects] of dict.modalHighlightRectsMap) {
        for (let rect of rects) {
          maskContent.push(`<div class="${kRectClassName}" style="top: ${rect.y}px;
            left: ${rect.x}px; height: ${rect.height}px; width: ${rect.width}px;"></div>`);
        }
      }
      maskNode.innerHTML = maskContent.join("");
    }

    // Always remove the current mask and insert it a-fresh, because we're not
    // free to alter DOM nodes inside the CanvasFrame.
    this._removeHighlightAllMask(window);

    dict.modalHighlightAllMask = kDebug ?
      mockAnonymousContentNode(document.body.appendChild(maskNode)) :
      document.insertAnonymousContent(maskNode);
  },

  /**
   * Safely remove the mask AnoymousContent node from the CanvasFrame.
   *
   * @param {nsIDOMWindow} window
   */
  _removeHighlightAllMask(window) {
    window = window.top;
    let dict = this.getForWindow(window);
    if (!dict.modalHighlightAllMask)
      return;

    // If the current window isn't the one the content was inserted into, this
    // will fail, but that's fine.
    if (kDebug) {
      dict.modalHighlightAllMask.remove();
    } else {
      try {
        window.document.removeAnonymousContent(dict.modalHighlightAllMask);
      } catch (ex) {}
    }
    dict.modalHighlightAllMask = null;
  },

  /**
   * Doing a full repaint each time a range is delivered by the highlight iterator
   * is way too costly, thus we pipe the frequency down to every
   * `kModalHighlightRepaintFreqMs` milliseconds.
   *
   * @param {nsIDOMWindow} window
   * @param {Object}       options Dictionary of painter hints that contains the
   *                               following properties:
   *   {Boolean} contentChanged Whether the documents' content changed in the
   *                            meantime. This happens when the DOM is updated
   *                            whilst the page is loaded.
   *   {Boolean} scrollOnly     TRUE when the page has scrolled in the meantime,
   *                            which means that the fixed positioned elements
   *                            need to be repainted.
   */
  _scheduleRepaintOfMask(window, { contentChanged, scrollOnly } = { contentChanged: false, scrollOnly: false }) {
    window = window.top;
    let dict = this.getForWindow(window);
    let repaintFixedNodes = (scrollOnly && !!dict.dynamicRangesSet.size);

    // When we request to repaint unconditionally, we mean to call
    // `_repaintHighlightAllMask()` right after the timeout.
    if (!dict.unconditionalRepaintRequested)
      dict.unconditionalRepaintRequested = !contentChanged || repaintFixedNodes;

    if (dict.modalRepaintScheduler)
      return;

    dict.modalRepaintScheduler = window.setTimeout(() => {
      dict.modalRepaintScheduler = null;

      if (dict.unconditionalRepaintRequested) {
        dict.unconditionalRepaintRequested = false;
        this._repaintHighlightAllMask(window);
        return;
      }

      let { width, height } = this._getWindowDimensions(window);
      if (!dict.modalHighlightRectsMap.size ||
          (Math.abs(dict.lastWindowDimensions.width - width) < kContentChangeThresholdPx &&
           Math.abs(dict.lastWindowDimensions.height - height) < kContentChangeThresholdPx)) {
        return;
      }

      this.iterator.restart(this.finder);
      dict.lastWindowDimensions = { width, height };
      this._repaintHighlightAllMask(window);
    }, kModalHighlightRepaintFreqMs);
  },

  /**
   * The outline that shows/ highlights the current found range is styled and
   * animated using CSS. This style can be found in `kModalStyle`, but to have it
   * applied on any DOM node we insert using the AnonymousContent API we need to
   * inject an agent sheet into the document.
   *
   * @param {nsIDOMWindow} window
   */
  _maybeInstallStyleSheet(window) {
    window = window.top;
    let dict = this.getForWindow(window);
    if (dict.installedSheet)
      return;

    let dwu = this._getDWU(window);
    let uri = this.modalStyleSheetURI;
    try {
      dwu.loadSheetUsingURIString(uri, dwu.AGENT_SHEET);
    } catch (e) {}
    dict.installedSheet = true;
  },

  /**
   * Add event listeners to the content which will cause the modal highlight
   * AnonymousContent to be re-painted or hidden.
   *
   * @param {nsIDOMWindow} window
   */
  _addModalHighlightListeners(window) {
    window = window.top;
    let dict = this.getForWindow(window);
    if (dict.highlightListeners)
      return;

    window = window.top;
    dict.highlightListeners = [
      this._scheduleRepaintOfMask.bind(this, window, { contentChanged: true }),
      this._scheduleRepaintOfMask.bind(this, window, { scrollOnly: true }),
      this.hide.bind(this, window, null)
    ];
    let target = this.iterator._getDocShell(window).chromeEventHandler;
    target.addEventListener("MozAfterPaint", dict.highlightListeners[0]);
    target.addEventListener("DOMMouseScroll", dict.highlightListeners[1]);
    target.addEventListener("mousewheel", dict.highlightListeners[1]);
    target.addEventListener("click", dict.highlightListeners[2]);
  },

  /**
   * Remove event listeners from content.
   *
   * @param {nsIDOMWindow} window
   */
  _removeModalHighlightListeners(window) {
    window = window.top;
    let dict = this.getForWindow(window);
    if (!dict.highlightListeners)
      return;

    let target = this.iterator._getDocShell(window).chromeEventHandler;
    target.removeEventListener("MozAfterPaint", dict.highlightListeners[0]);
    target.removeEventListener("DOMMouseScroll", dict.highlightListeners[1]);
    target.removeEventListener("mousewheel", dict.highlightListeners[1]);
    target.removeEventListener("click", dict.highlightListeners[2]);

    dict.highlightListeners = null;
  },

  /**
   * For a given node returns its editable parent or null if there is none.
   * It's enough to check if node is a text node and its parent's parent is
   * instance of nsIDOMNSEditableElement.
   *
   * @param node the node we want to check
   * @returns the first node in the parent chain that is editable,
   *          null if there is no such node
   */
  _getEditableNode(node) {
    if (node.nodeType === node.TEXT_NODE && node.parentNode && node.parentNode.parentNode &&
        node.parentNode.parentNode instanceof Ci.nsIDOMNSEditableElement) {
      return node.parentNode.parentNode;
    }
    return null;
  },

  /**
   * Add ourselves as an nsIEditActionListener and nsIDocumentStateListener for
   * a given editor
   *
   * @param editor the editor we'd like to listen to
   */
  _addEditorListeners(editor) {
    if (!this._editors) {
      this._editors = [];
      this._stateListeners = [];
    }

    let existingIndex = this._editors.indexOf(editor);
    if (existingIndex == -1) {
      let x = this._editors.length;
      this._editors[x] = editor;
      this._stateListeners[x] = this._createStateListener();
      this._editors[x].addEditActionListener(this);
      this._editors[x].addDocumentStateListener(this._stateListeners[x]);
    }
  },

  /**
   * Helper method to unhook listeners, remove cached editors
   * and keep the relevant arrays in sync
   *
   * @param idx the index into the array of editors/state listeners
   *        we wish to remove
   */
  _unhookListenersAtIndex(idx) {
    this._editors[idx].removeEditActionListener(this);
    this._editors[idx]
        .removeDocumentStateListener(this._stateListeners[idx]);
    this._editors.splice(idx, 1);
    this._stateListeners.splice(idx, 1);
    if (!this._editors.length) {
      delete this._editors;
      delete this._stateListeners;
    }
  },

  /**
   * Remove ourselves as an nsIEditActionListener and
   * nsIDocumentStateListener from a given cached editor
   *
   * @param editor the editor we no longer wish to listen to
   */
  _removeEditorListeners(editor) {
    // editor is an editor that we listen to, so therefore must be
    // cached. Find the index of this editor
    let idx = this._editors.indexOf(editor);
    if (idx == -1) {
      return;
    }
    // Now unhook ourselves, and remove our cached copy
    this._unhookListenersAtIndex(idx);
  },

  /*
   * nsIEditActionListener logic follows
   *
   * We implement this interface to allow us to catch the case where
   * the findbar found a match in a HTML <input> or <textarea>. If the
   * user adjusts the text in some way, it will no longer match, so we
   * want to remove the highlight, rather than have it expand/contract
   * when letters are added or removed.
   */

  /**
   * Helper method used to check whether a selection intersects with
   * some highlighting
   *
   * @param selectionRange the range from the selection to check
   * @param findRange the highlighted range to check against
   * @returns true if they intersect, false otherwise
   */
  _checkOverlap(selectionRange, findRange) {
    // The ranges overlap if one of the following is true:
    // 1) At least one of the endpoints of the deleted selection
    //    is in the find selection
    // 2) At least one of the endpoints of the find selection
    //    is in the deleted selection
    if (findRange.isPointInRange(selectionRange.startContainer,
                                 selectionRange.startOffset))
      return true;
    if (findRange.isPointInRange(selectionRange.endContainer,
                                 selectionRange.endOffset))
      return true;
    if (selectionRange.isPointInRange(findRange.startContainer,
                                      findRange.startOffset))
      return true;
    if (selectionRange.isPointInRange(findRange.endContainer,
                                      findRange.endOffset))
      return true;

    return false;
  },

  /**
   * Helper method to determine if an edit occurred within a highlight
   *
   * @param selection the selection we wish to check
   * @param node the node we want to check is contained in selection
   * @param offset the offset into node that we want to check
   * @returns the range containing (node, offset) or null if no ranges
   *          in the selection contain it
   */
  _findRange(selection, node, offset) {
    let rangeCount = selection.rangeCount;
    let rangeidx = 0;
    let foundContainingRange = false;
    let range = null;

    // Check to see if this node is inside one of the selection's ranges
    while (!foundContainingRange && rangeidx < rangeCount) {
      range = selection.getRangeAt(rangeidx);
      if (range.isPointInRange(node, offset)) {
        foundContainingRange = true;
        break;
      }
      rangeidx++;
    }

    if (foundContainingRange) {
      return range;
    }

    return null;
  },

  // Start of nsIEditActionListener implementations

  WillDeleteText(textNode, offset, length) {
    let editor = this._getEditableNode(textNode).editor;
    let controller = editor.selectionController;
    let fSelection = controller.getSelection(Ci.nsISelectionController.SELECTION_FIND);
    let range = this._findRange(fSelection, textNode, offset);

    if (range) {
      // Don't remove the highlighting if the deleted text is at the
      // end of the range
      if (textNode != range.endContainer ||
          offset != range.endOffset) {
        // Text within the highlight is being removed - the text can
        // no longer be a match, so remove the highlighting
        fSelection.removeRange(range);
        if (fSelection.rangeCount == 0) {
          this._removeEditorListeners(editor);
        }
      }
    }
  },

  DidInsertText(textNode, offset, aString) {
    let editor = this._getEditableNode(textNode).editor;
    let controller = editor.selectionController;
    let fSelection = controller.getSelection(Ci.nsISelectionController.SELECTION_FIND);
    let range = this._findRange(fSelection, textNode, offset);

    if (range) {
      // If the text was inserted before the highlight
      // adjust the highlight's bounds accordingly
      if (textNode == range.startContainer &&
          offset == range.startOffset) {
        range.setStart(range.startContainer,
                       range.startOffset+aString.length);
      } else if (textNode != range.endContainer ||
                 offset != range.endOffset) {
        // The edit occurred within the highlight - any addition of text
        // will result in the text no longer being a match,
        // so remove the highlighting
        fSelection.removeRange(range);
        if (fSelection.rangeCount == 0) {
          this._removeEditorListeners(editor);
        }
      }
    }
  },

  WillDeleteSelection(selection) {
    let editor = this._getEditableNode(selection.getRangeAt(0)
                                                 .startContainer).editor;
    let controller = editor.selectionController;
    let fSelection = controller.getSelection(Ci.nsISelectionController.SELECTION_FIND);

    let selectionIndex = 0;
    let findSelectionIndex = 0;
    let shouldDelete = {};
    let numberOfDeletedSelections = 0;
    let numberOfMatches = fSelection.rangeCount;

    // We need to test if any ranges in the deleted selection (selection)
    // are in any of the ranges of the find selection
    // Usually both selections will only contain one range, however
    // either may contain more than one.

    for (let fIndex = 0; fIndex < numberOfMatches; fIndex++) {
      shouldDelete[fIndex] = false;
      let fRange = fSelection.getRangeAt(fIndex);

      for (let index = 0; index < selection.rangeCount; index++) {
        if (shouldDelete[fIndex]) {
          continue;
        }

        let selRange = selection.getRangeAt(index);
        let doesOverlap = this._checkOverlap(selRange, fRange);
        if (doesOverlap) {
          shouldDelete[fIndex] = true;
          numberOfDeletedSelections++;
        }
      }
    }

    // OK, so now we know what matches (if any) are in the selection
    // that is being deleted. Time to remove them.
    if (!numberOfDeletedSelections) {
      return;
    }

    for (let i = numberOfMatches - 1; i >= 0; i--) {
      if (shouldDelete[i])
        fSelection.removeRange(fSelection.getRangeAt(i));
    }

    // Remove listeners if no more highlights left
    if (!fSelection.rangeCount) {
      this._removeEditorListeners(editor);
    }
  },

  /*
   * nsIDocumentStateListener logic follows
   *
   * When attaching nsIEditActionListeners, there are no guarantees
   * as to whether the findbar or the documents in the browser will get
   * destructed first. This leads to the potential to either leak, or to
   * hold on to a reference an editable element's editor for too long,
   * preventing it from being destructed.
   *
   * However, when an editor's owning node is being destroyed, the editor
   * sends out a DocumentWillBeDestroyed notification. We can use this to
   * clean up our references to the object, to allow it to be destroyed in a
   * timely fashion.
   */

  /**
   * Unhook ourselves when one of our state listeners has been called.
   * This can happen in 4 cases:
   *  1) The document the editor belongs to is navigated away from, and
   *     the document is not being cached
   *
   *  2) The document the editor belongs to is expired from the cache
   *
   *  3) The tab containing the owning document is closed
   *
   *  4) The <input> or <textarea> that owns the editor is explicitly
   *     removed from the DOM
   *
   * @param the listener that was invoked
   */
  _onEditorDestruction(aListener) {
    // First find the index of the editor the given listener listens to.
    // The listeners and editors arrays must always be in sync.
    // The listener will be in our array of cached listeners, as this
    // method could not have been called otherwise.
    let idx = 0;
    while (this._stateListeners[idx] != aListener) {
      idx++;
    }

    // Unhook both listeners
    this._unhookListenersAtIndex(idx);
  },

  /**
   * Creates a unique document state listener for an editor.
   *
   * It is not possible to simply have the findbar implement the
   * listener interface itself, as it wouldn't have sufficient information
   * to work out which editor was being destroyed. Therefore, we create new
   * listeners on the fly, and cache them in sync with the editors they
   * listen to.
   */
  _createStateListener() {
    return {
      findbar: this,

      QueryInterface: function(iid) {
        if (iid.equals(Ci.nsIDocumentStateListener) ||
            iid.equals(Ci.nsISupports))
          return this;

        throw Components.results.NS_ERROR_NO_INTERFACE;
      },

      NotifyDocumentWillBeDestroyed: function() {
        this.findbar._onEditorDestruction(this);
      },

      // Unimplemented
      notifyDocumentCreated: function() {},
      notifyDocumentStateChanged: function(aDirty) {}
    };
  }
};
