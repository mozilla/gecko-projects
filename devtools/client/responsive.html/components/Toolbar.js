/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { PureComponent, createFactory } = require("devtools/client/shared/vendor/react");
const dom = require("devtools/client/shared/vendor/react-dom-factories");
const PropTypes = require("devtools/client/shared/vendor/react-prop-types");
const { connect } = require("devtools/client/shared/vendor/react-redux");

const DevicePixelRatioMenu = createFactory(require("./DevicePixelRatioMenu"));
const DeviceSelector = createFactory(require("./DeviceSelector"));
const NetworkThrottlingMenu = createFactory(require("devtools/client/shared/components/throttling/NetworkThrottlingMenu"));
const SettingsMenu = createFactory(require("./SettingsMenu"));
const ViewportDimension = createFactory(require("./ViewportDimension"));

const { getStr } = require("../utils/l10n");
const Types = require("../types");

class Toolbar extends PureComponent {
  static get propTypes() {
    return {
      devices: PropTypes.shape(Types.devices).isRequired,
      displayPixelRatio: Types.pixelRatio.value.isRequired,
      leftAlignmentEnabled: PropTypes.bool.isRequired,
      networkThrottling: PropTypes.shape(Types.networkThrottling).isRequired,
      onChangeDevice: PropTypes.func.isRequired,
      onChangeNetworkThrottling: PropTypes.func.isRequired,
      onChangePixelRatio: PropTypes.func.isRequired,
      onChangeReloadCondition: PropTypes.func.isRequired,
      onChangeTouchSimulation: PropTypes.func.isRequired,
      onExit: PropTypes.func.isRequired,
      onRemoveDeviceAssociation: PropTypes.func.isRequired,
      onResizeViewport: PropTypes.func.isRequired,
      onRotateViewport: PropTypes.func.isRequired,
      onScreenshot: PropTypes.func.isRequired,
      onToggleLeftAlignment: PropTypes.func.isRequired,
      onUpdateDeviceModal: PropTypes.func.isRequired,
      reloadConditions: PropTypes.shape(Types.reloadConditions).isRequired,
      screenshot: PropTypes.shape(Types.screenshot).isRequired,
      selectedDevice: PropTypes.string.isRequired,
      selectedPixelRatio: PropTypes.shape(Types.pixelRatio).isRequired,
      touchSimulation: PropTypes.shape(Types.touchSimulation).isRequired,
      viewport: PropTypes.shape(Types.viewport).isRequired,
    };
  }

  render() {
    const {
      devices,
      displayPixelRatio,
      leftAlignmentEnabled,
      networkThrottling,
      onChangeDevice,
      onChangeNetworkThrottling,
      onChangePixelRatio,
      onChangeReloadCondition,
      onChangeTouchSimulation,
      onExit,
      onRemoveDeviceAssociation,
      onResizeViewport,
      onRotateViewport,
      onScreenshot,
      onToggleLeftAlignment,
      onUpdateDeviceModal,
      reloadConditions,
      screenshot,
      selectedDevice,
      selectedPixelRatio,
      touchSimulation,
      viewport,
    } = this.props;

    return (
      dom.header(
        {
          id: "toolbar",
          className: leftAlignmentEnabled ? "left-aligned" : "",
        },
        DeviceSelector({
          devices,
          onChangeDevice,
          onResizeViewport,
          onUpdateDeviceModal,
          selectedDevice,
          viewportId: viewport.id,
        }),
        leftAlignmentEnabled ?
          dom.div({ className: "devtools-separator" })
          :
          null,
        dom.div(
          { id: "toolbar-center-controls" },
          ViewportDimension({
            onRemoveDeviceAssociation,
            onResizeViewport,
            viewport,
          }),
          dom.button({
            id: "rotate-button",
            className: "devtools-button",
            onClick: () => onRotateViewport(viewport.id),
            title: getStr("responsive.rotate"),
          }),
          dom.div({ className: "devtools-separator" }),
          DevicePixelRatioMenu({
            devices,
            displayPixelRatio,
            onChangePixelRatio,
            selectedDevice,
            selectedPixelRatio,
          }),
          dom.div({ className: "devtools-separator" }),
          NetworkThrottlingMenu({
            networkThrottling,
            onChangeNetworkThrottling,
            useTopLevelWindow: true,
          }),
          dom.div({ className: "devtools-separator" }),
          dom.button({
            id: "touch-simulation-button",
            className: "devtools-button" +
                       (touchSimulation.enabled ? " checked" : ""),
            title: (touchSimulation.enabled ?
              getStr("responsive.disableTouch") : getStr("responsive.enableTouch")),
            onClick: () => onChangeTouchSimulation(!touchSimulation.enabled),
          })
        ),
        dom.div(
          { id: "toolbar-end-controls" },
          dom.button({
            id: "screenshot-button",
            className: "devtools-button",
            title: getStr("responsive.screenshot"),
            onClick: onScreenshot,
            disabled: screenshot.isCapturing,
          }),
          SettingsMenu({
            reloadConditions,
            onChangeReloadCondition,
            onToggleLeftAlignment,
          }),
          dom.div({ className: "devtools-separator" }),
          dom.button({
            id: "exit-button",
            className: "devtools-button",
            title: getStr("responsive.exit"),
            onClick: onExit,
          })
        )
      )
    );
  }
}

const mapStateToProps = state => {
  return {
    leftAlignmentEnabled: state.ui.leftAlignmentEnabled,
  };
};

module.exports = connect(mapStateToProps)(Toolbar);
