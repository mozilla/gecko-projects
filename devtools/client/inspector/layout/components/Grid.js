/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { addons, createClass, createFactory, DOM: dom, PropTypes } =
  require("devtools/client/shared/vendor/react");

const GridDisplaySettings = createFactory(require("./GridDisplaySettings"));
const GridList = createFactory(require("./GridList"));

const Types = require("../types");
const { getStr } = require("../utils/l10n");

module.exports = createClass({

  displayName: "Grid",

  propTypes: {
    getSwatchColorPickerTooltip: PropTypes.func.isRequired,
    grids: PropTypes.arrayOf(PropTypes.shape(Types.grid)).isRequired,
    highlighterSettings: PropTypes.shape(Types.highlighterSettings).isRequired,
    onSetGridOverlayColor: PropTypes.func.isRequired,
    onToggleGridHighlighter: PropTypes.func.isRequired,
    onToggleShowGridLineNumbers: PropTypes.func.isRequired,
    onToggleShowInfiniteLines: PropTypes.func.isRequired,
  },

  mixins: [ addons.PureRenderMixin ],

  render() {
    let {
      getSwatchColorPickerTooltip,
      grids,
      highlighterSettings,
      onSetGridOverlayColor,
      onToggleGridHighlighter,
      onToggleShowGridLineNumbers,
      onToggleShowInfiniteLines,
    } = this.props;

    return grids.length ?
      dom.div(
        {
          id: "layout-grid-container",
        },
        GridList({
          getSwatchColorPickerTooltip,
          grids,
          onSetGridOverlayColor,
          onToggleGridHighlighter,
        }),
        GridDisplaySettings({
          highlighterSettings,
          onToggleShowGridLineNumbers,
          onToggleShowInfiniteLines,
        })
      )
      :
      dom.div(
        {
          className: "layout-no-grids",
        },
        getStr("layout.noGrids")
      );
  },

});
