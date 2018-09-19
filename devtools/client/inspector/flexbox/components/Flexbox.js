/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { createFactory, PureComponent } = require("devtools/client/shared/vendor/react");
const dom = require("devtools/client/shared/vendor/react-dom-factories");
const PropTypes = require("devtools/client/shared/vendor/react-prop-types");
const { getStr } = require("devtools/client/inspector/layout/utils/l10n");

loader.lazyGetter(this, "FlexContainerProperties", function() {
  return createFactory(require("./FlexContainerProperties"));
});
loader.lazyGetter(this, "FlexItemList", function() {
  return createFactory(require("./FlexItemList"));
});
loader.lazyGetter(this, "FlexItemSizingProperties", function() {
  return createFactory(require("./FlexItemSizingProperties"));
});
loader.lazyGetter(this, "Header", function() {
  return createFactory(require("./Header"));
});

const Types = require("../types");

class Flexbox extends PureComponent {
  static get propTypes() {
    return {
      flexbox: PropTypes.shape(Types.flexbox).isRequired,
      getSwatchColorPickerTooltip: PropTypes.func.isRequired,
      onHideBoxModelHighlighter: PropTypes.func.isRequired,
      onSetFlexboxOverlayColor: PropTypes.func.isRequired,
      onShowBoxModelHighlighterForNode: PropTypes.func.isRequired,
      onToggleFlexboxHighlighter: PropTypes.func.isRequired,
      onToggleFlexItemShown: PropTypes.func.isRequired,
      setSelectedNode: PropTypes.func.isRequired,
    };
  }

  renderFlexItemList() {
    const {
      flexbox,
      onToggleFlexItemShown,
    } = this.props;
    const {
      flexItems,
      flexItemShown,
    } = flexbox;

    if (flexItemShown || !flexItems.length) {
      return null;
    }

    return FlexItemList({
      flexItems,
      onToggleFlexItemShown,
    });
  }

  renderFlexItemSizingProperties() {
    const { flexbox } = this.props;
    const {
      flexItems,
      flexItemShown,
    } = flexbox;

    if (!flexItemShown) {
      return null;
    }

    return FlexItemSizingProperties({
      flexDirection: flexbox.properties["flex-direction"],
      flexItem: flexItems.find(item => item.nodeFront.actorID === flexItemShown),
    });
  }

  render() {
    const {
      flexbox,
      getSwatchColorPickerTooltip,
      onHideBoxModelHighlighter,
      onSetFlexboxOverlayColor,
      onShowBoxModelHighlighterForNode,
      onToggleFlexboxHighlighter,
      onToggleFlexItemShown,
      setSelectedNode,
    } = this.props;

    if (!flexbox.actorID) {
      return (
        dom.div({ className: "devtools-sidepanel-no-result" },
          getStr("flexbox.noFlexboxeOnThisPage")
        )
      );
    }

    return (
      dom.div({ id: "layout-flexbox-container" },
        Header({
          flexbox,
          getSwatchColorPickerTooltip,
          onHideBoxModelHighlighter,
          onSetFlexboxOverlayColor,
          onShowBoxModelHighlighterForNode,
          onToggleFlexboxHighlighter,
          onToggleFlexItemShown,
          setSelectedNode,
        }),
        this.renderFlexItemList(),
        this.renderFlexItemSizingProperties(),
        FlexContainerProperties({
          properties: flexbox.properties,
        })
      )
    );
  }
}

module.exports = Flexbox;
