/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */
"use strict";

const { PureComponent } = require("devtools/client/shared/vendor/react");
const dom = require("devtools/client/shared/vendor/react-dom-factories");
const PropTypes = require("devtools/client/shared/vendor/react-prop-types");
const { CONNECTION_TYPES, DEBUG_TARGET_TYPES } =
  require("devtools/client/shared/remote-debugging/constants");

/**
 * This is header that should be displayed on top of the toolbox when using
 * about:devtools-toolbox.
 */
class DebugTargetInfo extends PureComponent {
  static get propTypes() {
    return {
      debugTargetData: PropTypes.shape({
        connectionType: PropTypes.oneOf(Object.values(CONNECTION_TYPES)).isRequired,
        deviceDescription: PropTypes.shape({
          brandName: PropTypes.string.isRequired,
          channel: PropTypes.string.isRequired,
          deviceName: PropTypes.string,
          version: PropTypes.string.isRequired,
        }).isRequired,
        targetType: PropTypes.oneOf(Object.values(DEBUG_TARGET_TYPES)).isRequired,
      }).isRequired,
      L10N: PropTypes.object.isRequired,
      toolbox: PropTypes.object.isRequired,
    };
  }

  getRuntimeText() {
    const { debugTargetData, L10N } = this.props;
    const { brandName, version } = debugTargetData.deviceDescription;
    const { connectionType } = debugTargetData;

    return (connectionType === CONNECTION_TYPES.THIS_FIREFOX)
      ? L10N.getFormatStr("toolbox.debugTargetInfo.runtimeLabel.thisFirefox", version)
      : L10N.getFormatStr("toolbox.debugTargetInfo.runtimeLabel", brandName, version);
  }

  getAssetsForConnectionType() {
    const { connectionType } = this.props.debugTargetData;

    switch (connectionType) {
      case CONNECTION_TYPES.USB:
        return {
          image: "chrome://devtools/skin/images/aboutdebugging-usb-icon.svg",
          l10nId: "toolbox.debugTargetInfo.connection.usb",
        };
      case CONNECTION_TYPES.NETWORK:
        return {
          image: "chrome://devtools/skin/images/aboutdebugging-globe-icon.svg",
          l10nId: "toolbox.debugTargetInfo.connection.network",
        };
    }
  }

  getAssetsForDebugTargetType() {
    const { targetType } = this.props.debugTargetData;

    // TODO: https://bugzilla.mozilla.org/show_bug.cgi?id=1520723
    //       Show actual favicon (currently toolbox.target.activeTab.favicon
    //       is unpopulated)
    const favicon = "chrome://devtools/skin/images/globe.svg";

    switch (targetType) {
      case DEBUG_TARGET_TYPES.EXTENSION:
        return {
          image: "chrome://devtools/skin/images/debugging-addons.svg",
          l10nId: "toolbox.debugTargetInfo.targetType.extension",
        };
      case DEBUG_TARGET_TYPES.PROCESS:
        return {
          image: "chrome://devtools/skin/images/settings.svg",
          l10nId: "toolbox.debugTargetInfo.targetType.process",
        };
      case DEBUG_TARGET_TYPES.TAB:
        return {
          image: favicon,
          l10nId: "toolbox.debugTargetInfo.targetType.tab",
        };
      case DEBUG_TARGET_TYPES.WORKER:
        return {
          image: "chrome://devtools/skin/images/debugging-workers.svg",
          l10nId: "toolbox.debugTargetInfo.targetType.worker",
        };
    }
  }

  shallRenderConnection() {
    const { connectionType } = this.props.debugTargetData;
    const renderableTypes = [
      CONNECTION_TYPES.USB,
      CONNECTION_TYPES.NETWORK,
    ];

    return renderableTypes.includes(connectionType);
  }

  renderConnection() {
    const { connectionType } = this.props.debugTargetData;
    const { image, l10nId } = this.getAssetsForConnectionType();

    return dom.span(
      {
        className: "iconized-label js-connection-info",
      },
      dom.img({ src: image, alt: `${connectionType} icon`}),
      this.props.L10N.getStr(l10nId),
    );
  }

  renderRuntime() {
    const { channel, deviceName } = this.props.debugTargetData.deviceDescription;

    const channelIcon =
      (channel === "release" || channel === "beta" || channel === "aurora") ?
      `chrome://devtools/skin/images/aboutdebugging-firefox-${ channel }.svg` :
      "chrome://devtools/skin/images/aboutdebugging-firefox-nightly.svg";

    return dom.span(
      {
        className: "iconized-label",
      },
      dom.img({ src: channelIcon, className: "channel-icon" }),
      dom.b({ className: "devtools-ellipsis-text" }, this.getRuntimeText()),
      dom.span({ className: "devtools-ellipsis-text" }, deviceName),
    );
  }

  renderTarget() {
    const title = this.props.toolbox.target.name;
    const url = this.props.toolbox.target.url;

    const { image, l10nId } = this.getAssetsForDebugTargetType();

    return dom.span(
      {
        className: "iconized-label",
      },
      dom.img({ src: image, alt: this.props.L10N.getStr(l10nId)}),
      title ? dom.b({ className: "devtools-ellipsis-text js-target-title"}, title) : null,
      dom.span({ className: "devtools-ellipsis-text" }, url),
    );
  }

  render() {
    return dom.header(
      {
        className: "debug-target-info js-debug-target-info",
      },
      this.shallRenderConnection() ? this.renderConnection() : null,
      this.renderRuntime(),
      this.renderTarget(),
    );
  }
}

module.exports = DebugTargetInfo;
