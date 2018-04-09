/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */
"use strict";

const { Component, createFactory } = require("devtools/client/shared/vendor/react");
const dom = require("devtools/client/shared/vendor/react-dom-factories");
const PropTypes = require("devtools/client/shared/vendor/react-prop-types");
const {div, button} = dom;
const {openLink} = require("devtools/client/shared/link");

const Menu = require("devtools/client/framework/menu");
const MenuItem = require("devtools/client/framework/menu-item");
const ToolboxTab = createFactory(require("devtools/client/framework/components/toolbox-tab"));
const ToolboxTabs = createFactory(require("devtools/client/framework/components/toolbox-tabs"));

/**
 * This is the overall component for the toolbox toolbar. It is designed to not know how
 * the state is being managed, and attempts to be as pure as possible. The
 * ToolboxController component controls the changing state, and passes in everything as
 * props.
 */
class ToolboxToolbar extends Component {
  static get propTypes() {
    return {
      // The currently focused item (for arrow keyboard navigation)
      // This ID determines the tabindex being 0 or -1.
      focusedButton: PropTypes.string,
      // List of command button definitions.
      toolboxButtons: PropTypes.array,
      // The id of the currently selected tool, e.g. "inspector"
      currentToolId: PropTypes.string,
      // An optionally highlighted tools, e.g. "inspector" (used by ToolboxTabs
      // component).
      highlightedTools: PropTypes.instanceOf(Set),
      // List of tool panel definitions (used by ToolboxTabs component).
      panelDefinitions: PropTypes.array,
      // List of possible docking options.
      hostTypes: PropTypes.arrayOf(PropTypes.shape({
        position: PropTypes.string.isRequired,
        switchHost: PropTypes.func.isRequired,
      })),
      // Should the docking options be enabled? They are disabled in some
      // contexts such as WebIDE.
      areDockButtonsEnabled: PropTypes.bool,
      // Do we need to add UI for closing the toolbox? We don't when the
      // toolbox is undocked, for example.
      canCloseToolbox: PropTypes.bool,
      // Is the split console currently visible?
      isSplitConsoleActive: PropTypes.bool,
      // Are we disabling the behavior where pop-ups are automatically closed
      // when clicking outside them?
      //
      // This is a tri-state value that may be true/false or undefined where
      // undefined means that the option is not relevant in this context
      // (i.e. we're not in a browser toolbox).
      disableAutohide: PropTypes.bool,
      // Function to select a tool based on its id.
      selectTool: PropTypes.func,
      // Function to turn the split console on / off.
      toggleSplitConsole: PropTypes.func,
      // Function to turn the disable pop-up autohide behavior on / off.
      toggleNoAutohide: PropTypes.func,
      // Function to completely close the toolbox.
      closeToolbox: PropTypes.func,
      // Keep a record of what button is focused.
      focusButton: PropTypes.func,
      // Hold off displaying the toolbar until enough information is ready for
      // it to render nicely.
      canRender: PropTypes.bool,
      // Localization interface.
      L10N: PropTypes.object,
      // The devtools toolbox
      toolbox: PropTypes.object,
    };
  }

  /**
   * The render function is kept fairly short for maintainability. See the individual
   * render functions for how each of the sections is rendered.
   */
  render() {
    const containerProps = {className: "devtools-tabbar"};
    return this.props.canRender
      ? (
        div(
          containerProps,
          renderToolboxButtonsStart(this.props),
          ToolboxTabs(this.props),
          renderToolboxButtonsEnd(this.props),
          renderToolboxControls(this.props)
        )
      )
      : div(containerProps);
  }
}

module.exports = ToolboxToolbar;

/**
 * A little helper function to call renderToolboxButtons for buttons at the start
 * of the toolbox.
 */
function renderToolboxButtonsStart(props) {
  return renderToolboxButtons(props, true);
}

/**
* A little helper function to call renderToolboxButtons for buttons at the end
* of the toolbox.
 */
function renderToolboxButtonsEnd(props) {
  return renderToolboxButtons(props, false);
}

/**
 * Render all of the tabs, this takes in a list of toolbox button states. These are plain
 * objects that have all of the relevant information needed to render the button.
 * See Toolbox.prototype._createButtonState in devtools/client/framework/toolbox.js for
 * documentation on this object.
 *
 * @param {String} focusedButton - The id of the focused button.
 * @param {Array} toolboxButtons - Array of objects that define the command buttons.
 * @param {Function} focusButton - Keep a record of the currently focused button.
 * @param {boolean} isStart - Render either the starting buttons, or ending buttons.
 */
function renderToolboxButtons({focusedButton, toolboxButtons, focusButton}, isStart) {
  const visibleButtons = toolboxButtons.filter(command => {
    const {isVisible, isInStartContainer} = command;
    return isVisible && (isStart ? isInStartContainer : !isInStartContainer);
  });

  if (visibleButtons.length === 0) {
    return null;
  }

  // The RDM button, if present, should always go last
  const rdmIndex = visibleButtons.findIndex(
    button => button.id === "command-button-responsive"
  );
  if (rdmIndex !== -1 && rdmIndex !== visibleButtons.length - 1) {
    const rdm = visibleButtons.splice(rdmIndex, 1)[0];
    visibleButtons.push(rdm);
  }

  const renderedButtons =
    visibleButtons.map(command => {
      const {
        id,
        description,
        disabled,
        onClick,
        isChecked,
        className: buttonClass,
        onKeyDown
      } = command;
      return button({
        id,
        title: description,
        disabled,
        className: (
          "command-button devtools-button "
          + buttonClass + (isChecked ? " checked" : "")
        ),
        onClick: (event) => {
          onClick(event);
          focusButton(id);
        },
        onFocus: () => focusButton(id),
        tabIndex: id === focusedButton ? "0" : "-1",
        onKeyDown: (event) => {
          onKeyDown(event);
        }
      });
    });

  // Add the appropriate separator, if needed.
  let children = renderedButtons;
  if (renderedButtons.length) {
    // For the end group we add a separator *before* the RDM button if it
    // exists.
    if (rdmIndex !== -1) {
      children.splice(
        children.length - 1,
        0,
        renderSeparator()
      );
    } else {
      children.push(renderSeparator());
    }
  }

  return div({id: `toolbox-buttons-${isStart ? "start" : "end"}`}, ...children);
}

/**
 * Render a separator.
 */
function renderSeparator() {
  return div({className: "devtools-separator"});
}

/**
 * Render the toolbox control buttons. The following props are expected:
 *
 * @param {string} focusedButton
 *        The id of the focused button.
 * @param {Object[]} hostTypes
 *        Array of host type objects.
 * @param {string} hostTypes[].position
 *        Position name.
 * @param {Function} hostTypes[].switchHost
 *        Function to switch the host.
 * @param {boolean} areDockOptionsEnabled
 *        They are not enabled in certain situations like when they are in the
 *        WebIDE.
 * @param {boolean} canCloseToolbox
 *        Do we need to add UI for closing the toolbox? We don't when the
 *        toolbox is undocked, for example.
 * @param {boolean} isSplitConsoleActive
 *         Is the split console currently visible?
 *        toolbox is undocked, for example.
 * @param {boolean|undefined} disableAutohide
 *        Are we disabling the behavior where pop-ups are automatically
 *        closed when clicking outside them?
 *        (Only defined for the browser toolbox.)
 * @param {Function} selectTool
 *        Function to select a tool based on its id.
 * @param {Function} toggleSplitConsole
 *        Function to turn the split console on / off.
 * @param {Function} toggleNoAutohide
 *        Function to turn the disable pop-up autohide behavior on / off.
 * @param {Function} closeToolbox
 *        Completely close the toolbox.
 * @param {Function} focusButton
 *        Keep a record of the currently focused button.
 * @param {Object} L10N
 *        Localization interface.
 */
function renderToolboxControls(props) {
  const {
    focusedButton,
    closeToolbox,
    hostTypes,
    focusButton,
    L10N,
    areDockOptionsEnabled,
    canCloseToolbox,
  } = props;

  const meatballMenuButtonId = "toolbox-meatball-menu-button";

  const meatballMenuButton = button({
    id: meatballMenuButtonId,
    onFocus: () => focusButton(meatballMenuButtonId),
    className: "devtools-button",
    title: L10N.getStr("toolbox.meatballMenu.button.tooltip"),
    onClick: evt => {
      showMeatballMenu(evt.target, {
        ...props,
        hostTypes: areDockOptionsEnabled ? hostTypes : [],
      });
    },
    tabIndex: focusedButton === meatballMenuButtonId ? "0" : "-1",
  });

  const closeButtonId = "toolbox-close";

  const closeButton = canCloseToolbox
    ? button({
      id: closeButtonId,
      onFocus: () => focusButton(closeButtonId),
      className: "devtools-button",
      title: L10N.getStr("toolbox.closebutton.tooltip"),
      onClick: () => {
        closeToolbox();
      },
      tabIndex: focusedButton === "toolbox-close" ? "0" : "-1",
    })
    : null;

  return div({id: "toolbox-controls"},
    meatballMenuButton,
    closeButton
  );
}

/**
 * Display the "..." menu (affectionately known as the meatball menu).
 *
 * @param {Object} menuButton
 *        The <button> element from which the menu should pop out. The geometry
 *        of this element is used to position the menu.
 * @param {Object} props
 *        Properties as described below.
 * @param {string} props.currentToolId
 *        The id of the currently selected tool.
 * @param {Object[]} props.hostTypes
 *        Array of host type objects.
 * @param {string} props.hostTypes[].position
 *        Position name.
 * @param {Function} props.hostTypes[].switchHost
 *        Function to switch the host.
 *        This array will be empty if we shouldn't shouldn't show any dock
 *        options.
 * @param {boolean} isSplitConsoleActive
 *        Is the split console currently visible?
 * @param {boolean|undefined} disableAutohide
 *        Are we disabling the behavior where pop-ups are automatically
 *        closed when clicking outside them.
 *        (Only defined for the browser toolbox.)
 * @param {Function} props.selectTool
 *        Function to select a tool based on its id.
 * @param {Function} toggleSplitConsole
 *        Function to turn the split console on / off.
 * @param {Function} toggleNoAutohide
 *        Function to turn the disable pop-up autohide behavior on / off.
 * @param {Object} props.L10N
 *        Localization interface.
 * @param {Object} props.toolbox
 *        The devtools toolbox. Used by the Menu component to determine which
 *        document to use.
 */
function showMeatballMenu(
  menuButton,
  {
    currentToolId,
    hostTypes,
    isSplitConsoleActive,
    disableAutohide,
    selectTool,
    toggleSplitConsole,
    toggleNoAutohide,
    L10N,
    toolbox,
  }
) {
  const menu = new Menu({ id: "toolbox-meatball-menu" });

  // Dock options
  for (const hostType of hostTypes) {
    menu.append(new MenuItem({
      id: `toolbox-meatball-menu-dock-${hostType.position}`,
      label: L10N.getStr(
        `toolbox.meatballMenu.dock.${hostType.position}.label`
      ),
      click: () => hostType.switchHost(),
    }));
  }

  // Split console
  if (currentToolId !== "webconsole") {
    menu.append(new MenuItem({
      id: "toolbox-meatball-menu-splitconsole",
      label: L10N.getStr(
        `toolbox.meatballMenu.${
          isSplitConsoleActive ? "hideconsole" : "splitconsole"
        }.label`
      ),
      accelerator: "Esc",
      click: toggleSplitConsole,
    }));
  }

  // Disable pop-up autohide
  //
  // If |disableAutohide| is undefined, it means this feature is not available
  // in this context.
  if (typeof disableAutohide !== "undefined") {
    menu.append(new MenuItem({
      id: "toolbox-meatball-menu-noautohide",
      label: L10N.getStr("toolbox.meatballMenu.noautohide.label"),
      type: "checkbox",
      checked: disableAutohide,
      click: toggleNoAutohide,
    }));
  }

  if (menu.items.length) {
    menu.append(new MenuItem({ type: "separator" }));
  }

  // Settings
  menu.append(new MenuItem({
    id: "toolbox-meatball-menu-settings",
    label: L10N.getStr("toolbox.meatballMenu.settings.label"),
    accelerator: L10N.getStr("toolbox.help.key"),
    click: () => selectTool("options"),
  }));

  if (menu.items.length) {
    menu.append(new MenuItem({ type: "separator" }));
  }

  // Getting started
  menu.append(new MenuItem({
    id: "toolbox-meatball-menu-gettingstarted",
    label: L10N.getStr("toolbox.meatballMenu.gettingStarted.label"),
    click: () => {
      openLink("https://developer.mozilla.org/docs/Tools", toolbox);
    },
  }));

  // Give feedback
  menu.append(new MenuItem({
    id: "toolbox-meatball-menu-feedback",
    label: L10N.getStr("toolbox.meatballMenu.giveFeedback.label"),
    click: () => {
      openLink("https://discourse.mozilla.org/c/devtools", toolbox);
    },
  }));

  const rect = menuButton.getBoundingClientRect();
  const screenX = menuButton.ownerDocument.defaultView.mozInnerScreenX;
  const screenY = menuButton.ownerDocument.defaultView.mozInnerScreenY;

  // Display the popup below the button.
  menu.popup(rect.left + screenX, rect.bottom + screenY, toolbox);
}
