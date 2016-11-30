/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { addons, createClass, DOM: dom, PropTypes } =
  require("devtools/client/shared/vendor/react");

const Types = require("../types");
const { getStr } = require("../utils/l10n");

module.exports = createClass({

  displayName: "GridDisplaySettings",

  propTypes: {
    highlighterSettings: PropTypes.shape(Types.highlighterSettings).isRequired,
    onToggleShowGridLineNumbers: PropTypes.func.isRequired,
    onToggleShowInfiniteLines: PropTypes.func.isRequired,
  },

  mixins: [ addons.PureRenderMixin ],

  onShowGridLineNumbersCheckboxClick() {
    let {
      highlighterSettings,
      onToggleShowGridLineNumbers,
    } = this.props;

    onToggleShowGridLineNumbers(!highlighterSettings.showGridLineNumbers);
  },

  onShowInfiniteLinesCheckboxClick() {
    let {
      highlighterSettings,
      onToggleShowInfiniteLines,
    } = this.props;

    onToggleShowInfiniteLines(!highlighterSettings.showInfiniteLines);
  },

  render() {
    let {
      highlighterSettings,
    } = this.props;

    return dom.div(
      {
        className: "grid-container",
      },
      dom.span(
        {},
        getStr("layout.gridDisplaySettings")
      ),
      dom.ul(
        {},
        dom.li(
          {},
          dom.label(
            {},
            dom.input(
              {
                type: "checkbox",
                checked: highlighterSettings.showInfiniteLines,
                onChange: this.onShowInfiniteLinesCheckboxClick,
              }
            ),
            getStr("layout.extendGridLinesInfinitely")
          )
        ),
        dom.li(
          {},
          dom.label(
            {},
            dom.input(
              {
                type: "checkbox",
                checked: highlighterSettings.showGridLineNumbers,
                onChange: this.onShowGridLineNumbersCheckboxClick,
              }
            ),
            getStr("layout.displayNumbersOnLines")
          )
        )
      )
    );
  },

});
