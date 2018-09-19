/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { PureComponent } = require("devtools/client/shared/vendor/react");
const dom = require("devtools/client/shared/vendor/react-dom-factories");
const PropTypes = require("devtools/client/shared/vendor/react-prop-types");

const Actions = require("../../actions/index");

/**
 * This component displays an item of the Sidebar component.
 */
class SidebarItem extends PureComponent {
  static get propTypes() {
    return {
      connectComponent: PropTypes.any,
      dispatch: PropTypes.func.isRequired,
      icon: PropTypes.string.isRequired,
      id: PropTypes.string.isRequired,
      isSelected: PropTypes.bool.isRequired,
      name: PropTypes.string.isRequired,
      runtimeId: PropTypes.string,
      selectable: PropTypes.bool.isRequired,
      type: PropTypes.string.isRequired,
    };
  }

  onItemClick() {
    const { id, dispatch, runtimeId } = this.props;
    dispatch(Actions.selectPage(id, runtimeId));
  }

  render() {
    const { connectComponent, icon, isSelected, name, selectable } = this.props;

    return dom.li(
      {
        className: "sidebar-item js-sidebar-item" +
                   (isSelected ?
                      " sidebar-item--selected js-sidebar-item-selected" :
                      ""
                   ) +
                   (selectable ? " sidebar-item--selectable" : ""),
        onClick: selectable ? () => this.onItemClick() : null
      },
      dom.img({
        className: "sidebar-item__icon",
        src: icon,
      }),
      dom.span(
        {
          className: "ellipsis-text",
          title: name,
        },
        name),
      connectComponent ? connectComponent : null
    );
  }
}

module.exports = SidebarItem;
