"use strict";

Object.defineProperty(exports, "__esModule", {
  value: true
});

var _devtoolsSourceMap = require("devtools/client/shared/source-map/index.js");

var _react = require("devtools/client/shared/vendor/react");

var _react2 = _interopRequireDefault(_react);

var _reactRedux = require("devtools/client/shared/vendor/react-redux");

var _classnames = require("devtools/client/debugger/new/dist/vendors").vendored["classnames"];

var _classnames2 = _interopRequireDefault(_classnames);

var _devtoolsContextmenu = require("devtools/client/debugger/new/dist/vendors").vendored["devtools-contextmenu"];

var _SourceIcon = require("../shared/SourceIcon");

var _SourceIcon2 = _interopRequireDefault(_SourceIcon);

var _Svg = require("devtools/client/debugger/new/dist/vendors").vendored["Svg"];

var _Svg2 = _interopRequireDefault(_Svg);

var _selectors = require("../../selectors/index");

var _actions = require("../../actions/index");

var _actions2 = _interopRequireDefault(_actions);

var _sourcesTree = require("../../utils/sources-tree/index");

var _clipboard = require("../../utils/clipboard");

var _prefs = require("../../utils/prefs");

function _interopRequireDefault(obj) { return obj && obj.__esModule ? obj : { default: obj }; }

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at <http://mozilla.org/MPL/2.0/>. */
class SourceTreeItem extends _react.Component {
  constructor(...args) {
    var _temp;

    return _temp = super(...args), this.onClick = e => {
      const {
        expanded,
        item,
        focusItem,
        setExpanded,
        selectItem
      } = this.props;
      focusItem(item);

      if ((0, _sourcesTree.isDirectory)(item)) {
        setExpanded(item, !!expanded, e.altKey);
      } else {
        selectItem(item);
      }
    }, this.onContextMenu = (event, item) => {
      const copySourceUri2Label = L10N.getStr("copySourceUri2");
      const copySourceUri2Key = L10N.getStr("copySourceUri2.accesskey");
      const setDirectoryRootLabel = L10N.getStr("setDirectoryRoot.label");
      const setDirectoryRootKey = L10N.getStr("setDirectoryRoot.accesskey");
      const removeDirectoryRootLabel = L10N.getStr("removeDirectoryRoot.label");
      event.stopPropagation();
      event.preventDefault();
      const menuOptions = [];

      if (!(0, _sourcesTree.isDirectory)(item)) {
        // Flow requires some extra handling to ensure the value of contents.
        const {
          contents
        } = item;

        if (!Array.isArray(contents)) {
          const copySourceUri2 = {
            id: "node-menu-copy-source",
            label: copySourceUri2Label,
            accesskey: copySourceUri2Key,
            disabled: false,
            click: () => (0, _clipboard.copyToTheClipboard)(contents.url)
          };
          menuOptions.push(copySourceUri2);
        }
      }

      if ((0, _sourcesTree.isDirectory)(item) && _prefs.features.root) {
        const {
          path
        } = item;
        const {
          projectRoot
        } = this.props;

        if (projectRoot.endsWith(path)) {
          menuOptions.push({
            id: "node-remove-directory-root",
            label: removeDirectoryRootLabel,
            disabled: false,
            click: () => this.props.clearProjectDirectoryRoot()
          });
        } else {
          menuOptions.push({
            id: "node-set-directory-root",
            label: setDirectoryRootLabel,
            accesskey: setDirectoryRootKey,
            disabled: false,
            click: () => this.props.setProjectDirectoryRoot(path)
          });
        }
      }

      (0, _devtoolsContextmenu.showMenu)(event, menuOptions);
    }, _temp;
  }

  getIcon(item, depth) {
    const {
      debuggeeUrl,
      projectRoot,
      source
    } = this.props;

    if (item.path === "webpack://") {
      return _react2.default.createElement(_Svg2.default, {
        name: "webpack"
      });
    } else if (item.path === "ng://") {
      return _react2.default.createElement(_Svg2.default, {
        name: "angular"
      });
    } else if (item.path.startsWith("moz-extension://") && depth === 0) {
      return _react2.default.createElement("img", {
        className: "extension"
      });
    }

    if (depth === 0 && projectRoot === "") {
      return _react2.default.createElement("img", {
        className: (0, _classnames2.default)("domain", {
          debuggee: debuggeeUrl && debuggeeUrl.includes(item.name)
        })
      });
    }

    if ((0, _sourcesTree.isDirectory)(item)) {
      return _react2.default.createElement("img", {
        className: "folder"
      });
    }

    if (source) {
      return _react2.default.createElement(_SourceIcon2.default, {
        source: source
      });
    }

    return null;
  }

  renderItemArrow() {
    const {
      item,
      expanded
    } = this.props;
    return (0, _sourcesTree.isDirectory)(item) ? _react2.default.createElement("img", {
      className: (0, _classnames2.default)("arrow", {
        expanded
      })
    }) : _react2.default.createElement("i", {
      className: "no-arrow"
    });
  }

  renderItemName() {
    const {
      item
    } = this.props;

    switch (item.name) {
      case "ng://":
        return "Angular";

      case "webpack://":
        return "Webpack";

      default:
        return `${item.name}`;
    }
  }

  render() {
    const {
      item,
      depth,
      focused,
      hasMatchingGeneratedSource
    } = this.props;
    const suffix = hasMatchingGeneratedSource ? _react2.default.createElement("span", {
      className: "suffix"
    }, L10N.getStr("sourceFooter.mappedSuffix")) : null;
    return _react2.default.createElement("div", {
      className: (0, _classnames2.default)("node", {
        focused
      }),
      key: item.path,
      onClick: this.onClick,
      onContextMenu: e => this.onContextMenu(e, item)
    }, this.renderItemArrow(), this.getIcon(item, depth), _react2.default.createElement("span", {
      className: "label"
    }, " ", this.renderItemName(), " ", suffix));
  }

}

function getHasMatchingGeneratedSource(state, source) {
  if (!source) {
    return false;
  }

  const sources = (0, _selectors.getSourcesByURL)(state, source.url);
  return (0, _devtoolsSourceMap.isOriginalId)(source.id) && sources.length > 1;
}

const mapStateToProps = (state, props) => {
  const {
    source
  } = props;
  return {
    hasMatchingGeneratedSource: getHasMatchingGeneratedSource(state, source)
  };
};

exports.default = (0, _reactRedux.connect)(mapStateToProps, {
  setProjectDirectoryRoot: _actions2.default.setProjectDirectoryRoot,
  clearProjectDirectoryRoot: _actions2.default.clearProjectDirectoryRoot
})(SourceTreeItem);