/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { createFactory, PureComponent } = require("devtools/client/shared/vendor/react");
const dom = require("devtools/client/shared/vendor/react-dom-factories");
const PropTypes = require("devtools/client/shared/vendor/react-prop-types");

const FluentReact = require("devtools/client/shared/vendor/fluent-react");
const Localized = createFactory(FluentReact.Localized);

const Message = createFactory(require("../shared/Message"));
const SidebarItem = createFactory(require("./SidebarItem"));
const Actions = require("../../actions/index");
const { MESSAGE_LEVEL } = require("../../constants");

/**
 * This component displays a runtime item of the Sidebar component.
 */
class SidebarRuntimeItem extends PureComponent {
  static get propTypes() {
    return {
      deviceName: PropTypes.string,
      dispatch: PropTypes.func.isRequired,
      // Provided by wrapping the component with FluentReact.withLocalization.
      getString: PropTypes.func.isRequired,
      icon: PropTypes.string.isRequired,
      isConnected: PropTypes.bool.isRequired,
      isConnecting: PropTypes.bool.isRequired,
      isConnectionFailed: PropTypes.bool.isRequired,
      isConnectionNotResponding: PropTypes.bool.isRequired,
      isSelected: PropTypes.bool.isRequired,
      isUnavailable: PropTypes.bool.isRequired,
      isUnplugged: PropTypes.bool.isRequired,
      name: PropTypes.string.isRequired,
      runtimeId: PropTypes.string.isRequired,
    };
  }

  renderConnectButton() {
    const { isConnecting } = this.props;
    const localizationId = isConnecting
                             ? "about-debugging-sidebar-item-connect-button-connecting"
                             : "about-debugging-sidebar-item-connect-button";
    return Localized(
      {
        id: localizationId,
      },
      dom.button(
        {
          className: "default-button default-button--micro js-connect-button",
          disabled: isConnecting,
          onClick: () => {
            const { dispatch, runtimeId } = this.props;
            dispatch(Actions.connectRuntime(runtimeId));
          },
        },
        localizationId
      )
    );
  }

  renderConnectionError() {
    const { isConnectionFailed } = this.props;

    if (!isConnectionFailed) {
      return null;
    }

    const localizationId =
      "about-debugging-sidebar-item-connect-button-connection-failed";

    return Message(
      {
        level: MESSAGE_LEVEL.ERROR,
        key: "connection-error",
        className: "qa-connection-error",
      },
      Localized(
        {
          id: localizationId,
        },
        dom.p({ className: "word-wrap-anywhere" }, localizationId)
      )
    );
  }

  renderConnectionNotResponding() {
    const { isConnectionNotResponding } = this.props;

    if (!isConnectionNotResponding) {
      return null;
    }

    const localizationId =
      "about-debugging-sidebar-item-connect-button-connection-not-responding";

    return Message(
      {
        level: MESSAGE_LEVEL.WARNING,
        key: "connection-not-responding",
        className: "qa-connection-not-responding",
      },
      Localized(
        {
          id: localizationId,
        },
        dom.p({ className: "word-wrap-anywhere" }, localizationId)
      )
    );
  }

  renderName() {
    const { deviceName, getString, isUnavailable, isUnplugged, name } = this.props;

    let displayName, qaClassName;
    if (isUnplugged) {
      displayName = getString("about-debugging-sidebar-runtime-item-unplugged");
      qaClassName = "qa-runtime-item-unplugged";
    } else if (isUnavailable) {
      displayName = getString("about-debugging-sidebar-runtime-item-waiting-for-browser");
      qaClassName = "qa-runtime-item-waiting-for-browser";
    } else {
      displayName = name;
      qaClassName = "qa-runtime-item-standard";
    }

    const localizationId = deviceName
      ? "about-debugging-sidebar-runtime-item-name"
      : "about-debugging-sidebar-runtime-item-name-no-device";

    const className = "ellipsis-text sidebar-runtime-item__runtime";

    function renderWithDevice() {
      return dom.span(
        {
          className,
          title: localizationId,
        },
        deviceName,
        dom.br({}),
        dom.span(
          {
            className: `sidebar-runtime-item__runtime__details ${qaClassName}`,
          },
          displayName,
        ),
      );
    }

    function renderNoDevice() {
      return dom.span(
        {
          className,
          title: localizationId,
        },
        displayName,
      );
    }

    return Localized(
      {
        id: localizationId,
        attrs: { title: true },
        $deviceName: deviceName,
        $displayName: displayName,
      },
      deviceName ? renderWithDevice() : renderNoDevice(),
    );
  }

  render() {
    const {
      getString,
      icon,
      isConnected,
      isSelected,
      isUnavailable,
      runtimeId,
    } = this.props;

    const connectionStatus = isConnected ?
      getString("aboutdebugging-sidebar-runtime-connection-status-connected") :
      getString("aboutdebugging-sidebar-runtime-connection-status-disconnected");

    return [
      SidebarItem(
        {
          className: "sidebar-item--tall",
          key: "sidebar-item",
          isSelected,
          to: isConnected ? `/runtime/${encodeURIComponent(runtimeId)}` : null,
        },
        dom.section(
          {
            className: "sidebar-runtime-item__container",
          },
          dom.img(
            {
              className: "sidebar-runtime-item__icon ",
              src: icon,
              alt: connectionStatus,
              title: connectionStatus,
            }
          ),
          this.renderName(),
          !isUnavailable && !isConnected ? this.renderConnectButton() : null
        ),
      ),
      this.renderConnectionError(),
      this.renderConnectionNotResponding(),
    ];
  }
}

module.exports = FluentReact.withLocalization(SidebarRuntimeItem);
