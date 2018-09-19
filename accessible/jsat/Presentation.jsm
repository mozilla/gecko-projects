/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

/* exported Presentation */

"use strict";

ChromeUtils.import("resource://gre/modules/accessibility/Utils.jsm");
ChromeUtils.defineModuleGetter(this, "PivotContext", // jshint ignore:line
  "resource://gre/modules/accessibility/Utils.jsm");
ChromeUtils.defineModuleGetter(this, "UtteranceGenerator", // jshint ignore:line
  "resource://gre/modules/accessibility/OutputGenerator.jsm");
ChromeUtils.defineModuleGetter(this, "States", // jshint ignore:line
  "resource://gre/modules/accessibility/Constants.jsm");
ChromeUtils.defineModuleGetter(this, "Roles", // jshint ignore:line
  "resource://gre/modules/accessibility/Constants.jsm");
ChromeUtils.defineModuleGetter(this, "AndroidEvents", // jshint ignore:line
  "resource://gre/modules/accessibility/Constants.jsm");

var EXPORTED_SYMBOLS = ["Presentation"]; // jshint ignore:line

const EDIT_TEXT_ROLES = new Set([
  Roles.SPINBUTTON, Roles.PASSWORD_TEXT,
  Roles.AUTOCOMPLETE, Roles.ENTRY, Roles.EDITCOMBOBOX]);

class AndroidPresentor {
  constructor() {
    this.type = "Android";
    this.displayedAccessibles = new WeakMap();
  }

  /**
   * The virtual cursor's position changed.
   * @param {PivotContext} aContext the context object for the new pivot
   *   position.
   * @param {int} aReason the reason for the pivot change.
   *   See nsIAccessiblePivot.
   * @param {bool} aBoundaryType the boundary type for the text movement
   * or NO_BOUNDARY if it was not a text movement. See nsIAccessiblePivot.
   */
  pivotChanged(aPosition, aOldPosition, aStartOffset, aEndOffset, aReason, aBoundaryType) {
    let context = new PivotContext(
      aPosition, aOldPosition, aStartOffset, aEndOffset);
    if (!context.accessible) {
      return null;
    }

    let androidEvents = [];

    const isExploreByTouch = aReason == Ci.nsIAccessiblePivot.REASON_POINT;

    if (isExploreByTouch) {
      // This isn't really used by TalkBack so this is a half-hearted attempt
      // for now.
      androidEvents.push({eventType: AndroidEvents.VIEW_HOVER_EXIT, text: []});
    }

    if (aPosition != aOldPosition) {
      let info = this._infoFromContext(context);
      let eventType = isExploreByTouch ?
        AndroidEvents.VIEW_HOVER_ENTER :
        AndroidEvents.VIEW_ACCESSIBILITY_FOCUSED;
      androidEvents.push({...info, eventType});

      try {
        context.accessibleForBounds.scrollTo(
          Ci.nsIAccessibleScrollType.SCROLL_TYPE_ANYWHERE);
      } catch (e) {}
    }

    if (aBoundaryType != Ci.nsIAccessiblePivot.NO_BOUNDARY) {
      const adjustedText = context.textAndAdjustedOffsets;

      androidEvents.push({
        eventType: AndroidEvents.VIEW_TEXT_TRAVERSED_AT_MOVEMENT_GRANULARITY,
        text: [adjustedText.text],
        fromIndex: adjustedText.startOffset,
        toIndex: adjustedText.endOffset
      });

      aPosition.QueryInterface(Ci.nsIAccessibleText).scrollSubstringTo(
        aStartOffset, aEndOffset,
        Ci.nsIAccessibleScrollType.SCROLL_TYPE_ANYWHERE);
    }

    if (context.accessible) {
      this.displayedAccessibles.set(context.accessible.document.window, context);
    }

    return androidEvents;
  }

  focused(aObject) {
    let info = this._infoFromContext(
      new PivotContext(aObject, null, -1, -1, true, false));
    return [{ eventType: AndroidEvents.VIEW_FOCUSED, ...info }];
  }

  /**
   * An object's check action has been invoked.
   * Note: Checkable objects use TalkBack's text derived from the event state, so we don't
   * populate the text here.
   * @param {nsIAccessible} aAccessible the object that has been invoked.
   */
  checked(aAccessible) {
    return [{
      eventType: AndroidEvents.VIEW_CLICKED,
      checked: Utils.getState(aAccessible).contains(States.CHECKED)
    }];
  }

  /**
   * An object's select action has been invoked.
   * @param {nsIAccessible} aAccessible the object that has been invoked.
   */
  selected(aAccessible) {
    return [{
      eventType: AndroidEvents.VIEW_SELECTED,
      selected: Utils.getState(aAccessible).contains(States.SELECTED)
    }];
  }

  /**
   * An object's action has been invoked.
   */
  actionInvoked() {
    return [{ eventType: AndroidEvents.VIEW_CLICKED }];
  }

  /**
   * Text has changed, either by the user or by the system. TODO.
   */
  textChanged(aAccessible, aIsInserted, aStart, aLength, aText, aModifiedText) {
    let androidEvent = {
      eventType: AndroidEvents.VIEW_TEXT_CHANGED,
      text: [aText],
      fromIndex: aStart,
      removedCount: 0,
      addedCount: 0
    };

    if (aIsInserted) {
      androidEvent.addedCount = aLength;
      androidEvent.beforeText =
        aText.substring(0, aStart) + aText.substring(aStart + aLength);
    } else {
      androidEvent.removedCount = aLength;
      androidEvent.beforeText =
        aText.substring(0, aStart) + aModifiedText + aText.substring(aStart);
    }

    return [androidEvent];
  }

  /**
   * Text selection has changed. TODO.
   */
  textSelectionChanged(aText, aStart, aEnd, aOldStart, aOldEnd, aIsFromUserInput) {
    let androidEvents = [];

    if (aIsFromUserInput) {
      let [from, to] = aOldStart < aStart ?
        [aOldStart, aStart] : [aStart, aOldStart];
      androidEvents.push({
        eventType: AndroidEvents.VIEW_TEXT_TRAVERSED_AT_MOVEMENT_GRANULARITY,
        text: [aText],
        fromIndex: from,
        toIndex: to
      });
    } else {
      androidEvents.push({
        eventType: AndroidEvents.VIEW_TEXT_SELECTION_CHANGED,
        text: [aText],
        fromIndex: aStart,
        toIndex: aEnd,
        itemCount: aText.length
      });
    }

    return androidEvents;
  }

  /**
   * Selection has changed.
   * XXX: Implement android event?
   * @param {nsIAccessible} aObject the object that has been selected.
   */
  selectionChanged(aObject) {
    return ["todo.selection-changed"];
  }

  /**
   * Name has changed.
   * XXX: Implement android event?
   * @param {nsIAccessible} aAccessible the object whose value has changed.
   */
  nameChanged(aAccessible) {
    return ["todo.name-changed"];
  }

  /**
   * Value has changed.
   * XXX: Implement android event?
   * @param {nsIAccessible} aAccessible the object whose value has changed.
   */
  valueChanged(aAccessible) {
    return ["todo.value-changed"];
  }

  /**
   * The tab, or the tab's document state has changed.
   * @param {nsIAccessible} aDocObj the tab document accessible that has had its
   *    state changed, or null if the tab has no associated document yet.
   * @param {string} aPageState the state name for the tab, valid states are:
   *    'newtab', 'loading', 'newdoc', 'loaded', 'stopped', and 'reload'.
   */
  tabStateChanged(aDocObj, aPageState) {
    return this.announce(
      UtteranceGenerator.genForTabStateChange(aDocObj, aPageState));
  }

  /**
   * The viewport has changed because of scroll.
   * @param {Window} aWindow window of viewport that changed.
   */
  viewportScrolled(aWindow) {
    const { windowUtils, devicePixelRatio } = aWindow;
    const resolution = { value: 1 };
    windowUtils.getResolution(resolution);
    const scale = devicePixelRatio * resolution.value;
    return [{
      eventType: AndroidEvents.VIEW_SCROLLED,
      scrollX: aWindow.scrollX * scale,
      scrollY: aWindow.scrollY * scale,
      maxScrollX: aWindow.scrollMaxX * scale,
      maxScrollY: aWindow.scrollMaxY * scale,
    }];
  }

  /**
   * The viewport has changed, either a pan, zoom, or landscape/portrait toggle.
   * @param {Window} aWindow window of viewport that changed.
   */
  viewportChanged(aWindow) {
    const currentContext = this.displayedAccessibles.get(aWindow);
    if (!currentContext) {
      return;
    }

    const currentAcc = currentContext.accessibleForBounds;
    if (Utils.isAliveAndVisible(currentAcc)) {
      return [{
        eventType: AndroidEvents.WINDOW_CONTENT_CHANGED,
        bounds: Utils.getBounds(currentAcc)
      }];
    }
  }

  /**
   * Announce something. Typically an app state change.
   */
  announce(aAnnouncement) {
    let localizedAnnouncement = Utils.localize(aAnnouncement).join(" ");
    return [{
      eventType: AndroidEvents.ANNOUNCEMENT,
      text: [localizedAnnouncement],
      addedCount: localizedAnnouncement.length,
      removedCount: 0,
      fromIndex: 0
    }];
  }


  /**
   * User tried to move cursor forward or backward with no success.
   * @param {string} aMoveMethod move method that was used (eg. 'moveNext').
   */
  noMove(aMoveMethod) {
    return [{
      eventType: AndroidEvents.VIEW_ACCESSIBILITY_FOCUSED,
      exitView: aMoveMethod,
      text: [""]
    }];
  }

  /**
   * Announce a live region.
   * @param  {PivotContext} aContext context object for an accessible.
   * @param  {boolean} aIsPolite A politeness level for a live region.
   * @param  {boolean} aIsHide An indicator of hide/remove event.
   * @param  {string} aModifiedText Optional modified text.
   */
  liveRegion(aAccessible, aIsPolite, aIsHide, aModifiedText) {
    let context = !aModifiedText ?
      new PivotContext(aAccessible, null, -1, -1, true, !!aIsHide) : null;
    return this.announce(
      UtteranceGenerator.genForLiveRegion(context, aIsHide, aModifiedText));
  }

  _infoFromContext(aContext) {
    const state = Utils.getState(aContext.accessible);
    const info = {
      bounds: aContext.bounds,
      focusable: state.contains(States.FOCUSABLE),
      focused: state.contains(States.FOCUSED),
      clickable: aContext.accessible.actionCount > 0,
      checkable: state.contains(States.CHECKABLE),
      checked: state.contains(States.CHECKED),
      editable: state.contains(States.EDITABLE),
      selected: state.contains(States.SELECTED)
    };

    if (EDIT_TEXT_ROLES.has(aContext.accessible.role)) {
      let textAcc = aContext.accessible.QueryInterface(Ci.nsIAccessibleText);
      return {
        ...info,
        className: "android.widget.EditText",
        hint: aContext.accessible.name,
        text: [textAcc.getText(0, -1)]
      };
    }

    return {
      ...info,
      className: "android.view.View",
      text: Utils.localize(UtteranceGenerator.genForContext(aContext)),
    };
  }
}

const Presentation = new AndroidPresentor();
