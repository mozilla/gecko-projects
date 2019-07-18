/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const Services = require("Services");
const {
  Component,
  createRef,
  createFactory,
} = require("devtools/client/shared/vendor/react");
const dom = require("devtools/client/shared/vendor/react-dom-factories");
const { div } = dom;
const PropTypes = require("devtools/client/shared/vendor/react-prop-types");
const {
  connect,
} = require("devtools/client/shared/redux/visibility-handler-connect");
const Actions = require("../../actions/index");

const {
  getSelectedFrame,
  isSelectedFrameVisible,
} = require("../../selectors/index");

// Components
const SplitBox = createFactory(
  require("devtools/client/shared/components/splitter/SplitBox")
);
const FrameListContent = createFactory(require("./FrameListContent"));
const Toolbar = createFactory(require("./Toolbar"));
const StatusBar = createFactory(require("./StatusBar"));

loader.lazyGetter(this, "FramePayload", function() {
  return createFactory(require("./FramePayload"));
});

/**
 * Renders a list of WebSocket frames in table view.
 * Full payload is separated using a SplitBox.
 */
class WebSocketsPanel extends Component {
  static get propTypes() {
    return {
      connector: PropTypes.object.isRequired,
      selectedFrame: PropTypes.object,
      frameDetailsOpen: PropTypes.bool.isRequired,
      openFrameDetailsTab: PropTypes.func.isRequired,
      selectedFrameVisible: PropTypes.bool.isRequired,
      channelId: PropTypes.number,
    };
  }

  constructor(props) {
    super(props);

    this.searchboxRef = createRef();
    this.clearFilterText = this.clearFilterText.bind(this);
  }

  componentDidUpdate(nextProps) {
    const { selectedFrameVisible, openFrameDetailsTab, channelId } = this.props;

    // If a new WebSocket connection is selected, clear the filter text
    if (channelId !== nextProps.channelId) {
      this.clearFilterText();
    }

    if (!selectedFrameVisible) {
      openFrameDetailsTab(false);
    }
  }

  // Reset the filter text
  clearFilterText() {
    if (this.searchboxRef) {
      this.searchboxRef.current.onClearButtonClick();
    }
  }

  render() {
    const { frameDetailsOpen, connector, selectedFrame } = this.props;

    const initialWidth = Services.prefs.getIntPref(
      "devtools.netmonitor.ws.payload-preview-width"
    );
    const initialHeight = Services.prefs.getIntPref(
      "devtools.netmonitor.ws.payload-preview-height"
    );

    return div(
      { className: "monitor-panel" },
      Toolbar({
        searchboxRef: this.searchboxRef,
      }),
      SplitBox({
        className: "devtools-responsive-container",
        initialWidth: initialWidth,
        initialHeight: initialHeight,
        minSize: "50px",
        maxSize: "50%",
        splitterSize: frameDetailsOpen ? 1 : 0,
        startPanel: FrameListContent({ connector }),
        endPanel:
          frameDetailsOpen &&
          FramePayload({
            connector,
            selectedFrame,
          }),
        endPanelCollapsed: !frameDetailsOpen,
        endPanelControl: true,
        vert: false,
      }),
      StatusBar()
    );
  }
}

module.exports = connect(
  state => ({
    channelId: state.webSockets.currentChannelId,
    frameDetailsOpen: state.webSockets.frameDetailsOpen,
    selectedFrame: getSelectedFrame(state),
    selectedFrameVisible: isSelectedFrameVisible(state),
  }),
  dispatch => ({
    openFrameDetailsTab: open => dispatch(Actions.openFrameDetails(open)),
  })
)(WebSocketsPanel);
