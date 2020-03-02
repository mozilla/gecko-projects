/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
"use strict";

const Services = require("Services");
const { L10nRegistry } = require("resource://gre/modules/L10nRegistry.jsm");

const EventEmitter = require("devtools/shared/event-emitter");

const Telemetry = require("devtools/client/shared/telemetry");

const { Picker } = require("devtools/client/accessibility/picker");
const {
  A11Y_SERVICE_DURATION,
} = require("devtools/client/accessibility/constants");

// The panel's window global is an EventEmitter firing the following events:
const EVENTS = {
  // When the accessibility inspector has a new accessible front selected.
  NEW_ACCESSIBLE_FRONT_SELECTED: "Accessibility:NewAccessibleFrontSelected",
  // When the accessibility inspector has a new accessible front highlighted.
  NEW_ACCESSIBLE_FRONT_HIGHLIGHTED:
    "Accessibility:NewAccessibleFrontHighlighted",
  // When the accessibility inspector has a new accessible front inspected.
  NEW_ACCESSIBLE_FRONT_INSPECTED: "Accessibility:NewAccessibleFrontInspected",
  // When the accessibility inspector is updated.
  ACCESSIBILITY_INSPECTOR_UPDATED:
    "Accessibility:AccessibilityInspectorUpdated",
};

const {
  accessibility: { AUDIT_TYPE },
} = require("devtools/shared/constants");
const { FILTERS } = require("devtools/client/accessibility/constants");

/**
 * This object represents Accessibility panel. It's responsibility is to
 * render Accessibility Tree of the current debugger target and the sidebar that
 * displays current relevant accessible details.
 */
function AccessibilityPanel(iframeWindow, toolbox, startup) {
  this.panelWin = iframeWindow;
  this._toolbox = toolbox;
  this.startup = startup;

  this.onTabNavigated = this.onTabNavigated.bind(this);
  this.onPanelVisibilityChange = this.onPanelVisibilityChange.bind(this);
  this.onNewAccessibleFrontSelected = this.onNewAccessibleFrontSelected.bind(
    this
  );
  this.onAccessibilityInspectorUpdated = this.onAccessibilityInspectorUpdated.bind(
    this
  );
  this.updateA11YServiceDurationTimer = this.updateA11YServiceDurationTimer.bind(
    this
  );
  this.forceUpdatePickerButton = this.forceUpdatePickerButton.bind(this);
  this.getAccessibilityTreeRoot = this.getAccessibilityTreeRoot.bind(this);
  this.startListeningForAccessibilityEvents = this.startListeningForAccessibilityEvents.bind(
    this
  );
  this.stopListeningForAccessibilityEvents = this.stopListeningForAccessibilityEvents.bind(
    this
  );
  this.audit = this.audit.bind(this);
  this.simulate = this.simulate.bind(this);

  EventEmitter.decorate(this);
}

AccessibilityPanel.prototype = {
  /**
   * Open is effectively an asynchronous constructor.
   */
  async open() {
    if (this._opening) {
      await this._opening;
      return this._opening;
    }

    let resolver;
    this._opening = new Promise(resolve => {
      resolver = resolve;
    });

    this._telemetry = new Telemetry();
    this.panelWin.gTelemetry = this._telemetry;

    this.target.on("navigate", this.onTabNavigated);
    this._toolbox.on("select", this.onPanelVisibilityChange);

    this.panelWin.EVENTS = EVENTS;
    EventEmitter.decorate(this.panelWin);
    this.panelWin.on(
      EVENTS.NEW_ACCESSIBLE_FRONT_SELECTED,
      this.onNewAccessibleFrontSelected
    );
    this.panelWin.on(
      EVENTS.ACCESSIBILITY_INSPECTOR_UPDATED,
      this.onAccessibilityInspectorUpdated
    );

    this.shouldRefresh = true;

    await this.startup.initAccessibility();
    this.picker = new Picker(this);
    this.fluentBundles = await this.createFluentBundles();

    this.updateA11YServiceDurationTimer();
    this.front.on("init", this.updateA11YServiceDurationTimer);
    this.front.on("shutdown", this.updateA11YServiceDurationTimer);

    this.front.on("init", this.forceUpdatePickerButton);
    this.front.on("shutdown", this.forceUpdatePickerButton);

    this.isReady = true;
    this.emit("ready");
    resolver(this);
    return this._opening;
  },

  /**
   * Retrieve message contexts for the current locales, and return them as an
   * array of FluentBundles elements.
   */
  async createFluentBundles() {
    const locales = Services.locale.appLocalesAsBCP47;
    const generator = L10nRegistry.generateBundles(locales, [
      "devtools/client/accessibility.ftl",
    ]);

    // Return value of generateBundles is a generator and should be converted to
    // a sync iterable before using it with React.
    const contexts = [];
    for await (const message of generator) {
      contexts.push(message);
    }

    return contexts;
  },

  onNewAccessibleFrontSelected(selected) {
    this.emit("new-accessible-front-selected", selected);
  },

  onAccessibilityInspectorUpdated() {
    this.emit("accessibility-inspector-updated");
  },

  /**
   * Make sure the panel is refreshed when the page is reloaded. The panel is
   * refreshed immediatelly if it's currently selected or lazily when the user
   * actually selects it.
   */
  onTabNavigated() {
    this.shouldRefresh = true;
    this._opening.then(() => this.refresh());
  },

  /**
   * Make sure the panel is refreshed (if needed) when it's selected.
   */
  onPanelVisibilityChange() {
    this._opening.then(() => this.refresh());
  },

  refresh() {
    this.cancelPicker();

    if (!this.isVisible) {
      // Do not refresh if the panel isn't visible.
      return;
    }

    // Do not refresh if it isn't necessary.
    if (!this.shouldRefresh) {
      return;
    }
    // Alright reset the flag we are about to refresh the panel.
    this.shouldRefresh = false;
    this.postContentMessage("initialize", {
      front: this.front,
      supports: this.supports,
      fluentBundles: this.fluentBundles,
      toolbox: this._toolbox,
      getAccessibilityTreeRoot: this.getAccessibilityTreeRoot,
      startListeningForAccessibilityEvents: this
        .startListeningForAccessibilityEvents,
      stopListeningForAccessibilityEvents: this
        .stopListeningForAccessibilityEvents,
      audit: this.audit,
      simulate: this.startup.simulator && this.simulate,
    });
  },

  updateA11YServiceDurationTimer() {
    if (this.front.enabled) {
      this._telemetry.start(A11Y_SERVICE_DURATION, this);
    } else {
      this._telemetry.finish(A11Y_SERVICE_DURATION, this, true);
    }
  },

  selectAccessible(accessibleFront) {
    this.postContentMessage("selectAccessible", accessibleFront);
  },

  selectAccessibleForNode(nodeFront, reason) {
    if (reason) {
      this._telemetry.keyedScalarAdd(
        "devtools.accessibility.select_accessible_for_node",
        reason,
        1
      );
    }

    this.postContentMessage("selectNodeAccessible", nodeFront);
  },

  highlightAccessible(accessibleFront) {
    this.postContentMessage("highlightAccessible", accessibleFront);
  },

  postContentMessage(type, ...args) {
    const event = new this.panelWin.MessageEvent("devtools/chrome/message", {
      bubbles: true,
      cancelable: true,
      data: { type, args },
    });

    this.panelWin.dispatchEvent(event);
  },

  updatePickerButton() {
    this.picker && this.picker.updateButton();
  },

  forceUpdatePickerButton() {
    // Only update picker button when the panel is selected.
    if (!this.isVisible) {
      return;
    }

    this.updatePickerButton();
    // Calling setToolboxButtons to make sure toolbar is forced to re-render.
    this._toolbox.component.setToolboxButtons(this._toolbox.toolbarButtons);
  },

  togglePicker(focus) {
    this.picker && this.picker.toggle();
  },

  cancelPicker() {
    this.picker && this.picker.cancel();
  },

  stopPicker() {
    this.picker && this.picker.stop();
  },

  /**
   * Stop picking and remove all walker listeners.
   */
  async cancelPick(onHovered, onPicked, onPreviewed, onCanceled) {
    await this.walker.cancelPick();
    this.walker.off("picker-accessible-hovered", onHovered);
    this.walker.off("picker-accessible-picked", onPicked);
    this.walker.off("picker-accessible-previewed", onPreviewed);
    this.walker.off("picker-accessible-canceled", onCanceled);
  },

  /**
   * Start picking and add walker listeners.
   * @param  {Boolean} doFocus
   *         If true, move keyboard focus into content.
   */
  async pick(doFocus, onHovered, onPicked, onPreviewed, onCanceled) {
    this.walker.on("picker-accessible-hovered", onHovered);
    this.walker.on("picker-accessible-picked", onPicked);
    this.walker.on("picker-accessible-previewed", onPreviewed);
    this.walker.on("picker-accessible-canceled", onCanceled);
    await this.walker.pick(doFocus);
  },

  /**
   * Return the topmost level accessibility walker to be used as the root of
   * the accessibility tree view.
   *
   * @return {Object}
   *         Topmost accessibility walker.
   */
  getAccessibilityTreeRoot() {
    return this.walker;
  },

  startListeningForAccessibilityEvents(eventMap) {
    for (const [type, listener] of Object.entries(eventMap)) {
      this.walker.on(type, listener);
    }
  },

  stopListeningForAccessibilityEvents(eventMap) {
    for (const [type, listener] of Object.entries(eventMap)) {
      this.walker.off(type, listener);
    }
  },

  /**
   * Perform an audit for a given filter.
   *
   * @param  {Object} this.walker
   *         Accessibility walker to be used for accessibility audit.
   * @param  {String} filter
   *         Type of an audit to perform.
   * @param  {Function} onError
   *         Audit error callback.
   * @param  {Function} onProgress
   *         Audit progress callback.
   * @param  {Function} onCompleted
   *         Audit completion callback.
   *
   * @return {Promise}
   *         Resolves when the audit for a top document, that the walker
   *         traverses, completes.
   */
  audit(filter, onError, onProgress, onCompleted) {
    return new Promise(resolve => {
      const types =
        filter === FILTERS.ALL ? Object.values(AUDIT_TYPE) : [filter];
      const auditEventHandler = ({ type, ancestries, progress }) => {
        switch (type) {
          case "error":
            this.walker.off("audit-event", auditEventHandler);
            onError();
            resolve();
            break;
          case "completed":
            this.walker.off("audit-event", auditEventHandler);
            onCompleted(ancestries);
            resolve();
            break;
          case "progress":
            onProgress(progress);
            break;
          default:
            break;
        }
      };

      this.walker.on("audit-event", auditEventHandler);
      this.walker.startAudit({ types });
    });
  },

  simulate(types) {
    return this.startup.simulator.simulate({ types });
  },

  get front() {
    return this.startup.accessibility;
  },

  get walker() {
    return this.startup.walker;
  },

  get supports() {
    return this.startup._supports;
  },

  /**
   * Return true if the Accessibility panel is currently selected.
   */
  get isVisible() {
    return this._toolbox.currentToolId === "accessibility";
  },

  get target() {
    return this._toolbox.target;
  },

  destroy() {
    if (this._destroyed) {
      return;
    }
    this._destroyed = true;

    this.postContentMessage("destroy");

    this.target.off("navigate", this.onTabNavigated);
    this._toolbox.off("select", this.onPanelVisibilityChange);

    this.panelWin.off(
      EVENTS.NEW_ACCESSIBLE_FRONT_SELECTED,
      this.onNewAccessibleFrontSelected
    );
    this.panelWin.off(
      EVENTS.ACCESSIBILITY_INSPECTOR_UPDATED,
      this.onAccessibilityInspectorUpdated
    );

    // Older versions of devtools server do not support picker functionality.
    if (this.picker) {
      this.picker.release();
      this.picker = null;
    }

    if (this.front) {
      this.front.off("init", this.updateA11YServiceDurationTimer);
      this.front.off("shutdown", this.updateA11YServiceDurationTimer);

      this.front.off("init", this.forceUpdatePickerButton);
      this.front.off("shutdown", this.forceUpdatePickerButton);
    }

    this._telemetry = null;
    this.panelWin.gTelemetry = null;

    this.emit("destroyed");
  },
};

// Exports from this module
exports.AccessibilityPanel = AccessibilityPanel;
