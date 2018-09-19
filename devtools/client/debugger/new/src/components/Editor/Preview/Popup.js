"use strict";

Object.defineProperty(exports, "__esModule", {
  value: true
});
exports.Popup = undefined;

var _react = require("devtools/client/shared/vendor/react");

var _react2 = _interopRequireDefault(_react);

var _reactRedux = require("devtools/client/shared/vendor/react-redux");

var _devtoolsReps = require("devtools/client/shared/components/reps/reps.js");

var _devtoolsReps2 = _interopRequireDefault(_devtoolsReps);

var _actions = require("../../../actions/index");

var _actions2 = _interopRequireDefault(_actions);

var _selectors = require("../../../selectors/index");

var _Popover = require("../../shared/Popover");

var _Popover2 = _interopRequireDefault(_Popover);

var _PreviewFunction = require("../../shared/PreviewFunction");

var _PreviewFunction2 = _interopRequireDefault(_PreviewFunction);

var _preview = require("../../../utils/preview");

var _Svg = require("devtools/client/debugger/new/dist/vendors").vendored["Svg"];

var _Svg2 = _interopRequireDefault(_Svg);

var _firefox = require("../../../client/firefox");

function _interopRequireDefault(obj) { return obj && obj.__esModule ? obj : { default: obj }; }

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at <http://mozilla.org/MPL/2.0/>. */
const {
  REPS: {
    Rep
  },
  MODE,
  ObjectInspector,
  ObjectInspectorUtils
} = _devtoolsReps2.default;
const {
  createNode,
  getChildren,
  getValue,
  nodeIsPrimitive,
  NODE_TYPES
} = ObjectInspectorUtils.node;
const {
  loadItemProperties
} = ObjectInspectorUtils.loadProperties;

function inPreview(event) {
  const relatedTarget = event.relatedTarget;

  if (!relatedTarget || relatedTarget.classList && relatedTarget.classList.contains("preview-expression")) {
    return true;
  } // $FlowIgnore


  const inPreviewSelection = document.elementsFromPoint(event.clientX, event.clientY).some(el => el.classList.contains("preview-selection"));
  return inPreviewSelection;
}

class Popup extends _react.Component {
  constructor(props) {
    super(props);

    this.onMouseLeave = e => {
      const relatedTarget = e.relatedTarget;

      if (!relatedTarget) {
        return this.props.onClose();
      }

      if (!inPreview(e)) {
        this.props.onClose();
      }
    };

    this.onKeyDown = e => {
      if (e.key === "Escape") {
        this.props.onClose();
      }
    };

    this.calculateMaxHeight = () => {
      const {
        editorRef
      } = this.props;

      if (!editorRef) {
        return "auto";
      }

      return editorRef.getBoundingClientRect().height - this.state.top;
    };

    this.onPopoverCoords = coords => {
      this.setState({
        top: coords.top
      });
    };

    this.state = {
      top: 0
    };
  }

  async componentWillMount() {
    const {
      value,
      setPopupObjectProperties,
      popupObjectProperties
    } = this.props;
    const root = this.getRoot();

    if (!nodeIsPrimitive(root) && value && value.actor && !popupObjectProperties[value.actor]) {
      const onLoadItemProperties = loadItemProperties(root, _firefox.createObjectClient);

      if (onLoadItemProperties !== null) {
        const properties = await onLoadItemProperties;
        setPopupObjectProperties(root.contents.value, properties);
      }
    }
  }

  getRoot() {
    const {
      expression,
      value,
      extra
    } = this.props;
    let rootValue = value;

    if (extra.immutable) {
      rootValue = extra.immutable.entries;
    }

    return createNode({
      name: expression,
      path: expression,
      contents: {
        value: rootValue
      }
    });
  }

  getObjectProperties() {
    const {
      popupObjectProperties
    } = this.props;
    const root = this.getRoot();
    const value = getValue(root);

    if (!value) {
      return null;
    }

    return popupObjectProperties[value.actor];
  }

  getChildren() {
    const properties = this.getObjectProperties();
    const root = this.getRoot();

    if (!properties) {
      return null;
    }

    const children = getChildren({
      item: root,
      loadedProperties: new Map([[root.path, properties]])
    });

    if (children.length > 0) {
      return children;
    }

    return null;
  }

  renderFunctionPreview() {
    const {
      selectSourceURL,
      value
    } = this.props;

    if (!value) {
      return null;
    }

    const {
      location
    } = value;
    return _react2.default.createElement("div", {
      className: "preview-popup",
      onClick: () => selectSourceURL(location.url, {
        line: location.line
      })
    }, _react2.default.createElement(_PreviewFunction2.default, {
      func: value
    }));
  }

  renderReact(react) {
    const reactHeader = react.displayName || "React Component";
    return _react2.default.createElement("div", {
      className: "header-container"
    }, _react2.default.createElement(_Svg2.default, {
      name: "react",
      className: "logo"
    }), _react2.default.createElement("h3", null, reactHeader));
  }

  renderImmutable(immutable) {
    const immutableHeader = immutable.type || "Immutable";
    return _react2.default.createElement("div", {
      className: "header-container"
    }, _react2.default.createElement(_Svg2.default, {
      name: "immutable",
      className: "logo"
    }), _react2.default.createElement("h3", null, immutableHeader));
  }

  renderObjectPreview() {
    const {
      extra
    } = this.props;
    const root = this.getRoot();

    if (nodeIsPrimitive(root)) {
      return null;
    }

    let roots = this.getChildren();

    if (!Array.isArray(roots) || roots.length === 0) {
      return null;
    }

    let header = null;

    if ((0, _preview.isImmutable)(this.getObjectProperties())) {
      header = this.renderImmutable(extra.immutable);
      roots = roots.filter(r => r.type != NODE_TYPES.PROTOTYPE);
    }

    if (extra.react && (0, _preview.isReactComponent)(this.getObjectProperties())) {
      header = this.renderReact(extra.react);
      roots = roots.filter(r => ["state", "props"].includes(r.name));
    }

    return _react2.default.createElement("div", {
      className: "preview-popup",
      style: {
        maxHeight: this.calculateMaxHeight()
      }
    }, header, this.renderObjectInspector(roots));
  }

  renderSimplePreview(value) {
    const {
      openLink
    } = this.props;
    return _react2.default.createElement("div", {
      className: "preview-popup"
    }, Rep({
      object: value,
      mode: MODE.LONG,
      openLink
    }));
  }

  renderObjectInspector(roots) {
    const {
      openLink
    } = this.props;
    return _react2.default.createElement(ObjectInspector, {
      roots: roots,
      autoExpandDepth: 0,
      disableWrap: true,
      focusable: false,
      openLink: openLink,
      createObjectClient: grip => (0, _firefox.createObjectClient)(grip)
    });
  }

  renderPreview() {
    // We don't have to check and
    // return on `false`, `""`, `0`, `undefined` etc,
    // these falsy simple typed value because we want to
    // do `renderSimplePreview` on these values below.
    const {
      value
    } = this.props;

    if (value && value.class === "Function") {
      return this.renderFunctionPreview();
    }

    if (value && value.type === "object") {
      return _react2.default.createElement("div", null, this.renderObjectPreview());
    }

    return this.renderSimplePreview(value);
  }

  getPreviewType(value) {
    if (typeof value == "number" || typeof value == "boolean" || typeof value == "string" && value.length < 10 || typeof value == "number" && value.toString().length < 10 || value.type == "null" || value.type == "undefined" || value.class === "Function") {
      return "tooltip";
    }

    return "popover";
  }

  render() {
    const {
      popoverPos,
      value,
      editorRef
    } = this.props;
    const type = this.getPreviewType(value);

    if (value && value.type === "object" && !this.getChildren()) {
      return null;
    }

    return _react2.default.createElement(_Popover2.default, {
      targetPosition: popoverPos,
      onMouseLeave: this.onMouseLeave,
      onKeyDown: this.onKeyDown,
      type: type,
      onPopoverCoords: this.onPopoverCoords,
      editorRef: editorRef
    }, this.renderPreview());
  }

}

exports.Popup = Popup;

const mapStateToProps = state => ({
  popupObjectProperties: (0, _selectors.getAllPopupObjectProperties)(state)
});

const {
  addExpression,
  selectSourceURL,
  selectLocation,
  setPopupObjectProperties,
  openLink
} = _actions2.default;
const mapDispatchToProps = {
  addExpression,
  selectSourceURL,
  selectLocation,
  setPopupObjectProperties,
  openLink
};
exports.default = (0, _reactRedux.connect)(mapStateToProps, mapDispatchToProps)(Popup);