/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {
  CLEAR_FLEXBOX,
  TOGGLE_FLEX_ITEM_SHOWN,
  UPDATE_FLEXBOX,
  UPDATE_FLEXBOX_COLOR,
  UPDATE_FLEXBOX_HIGHLIGHTED,
} = require("../actions/index");

const INITIAL_FLEXBOX = {
  // The actor ID of the flex container.
  actorID: null,
  // The color of the flexbox highlighter overlay.
  color: "",
  // An array of flex items belonging to the current flex container.
  flexItems: [],
  // Whether or not the flexbox highlighter is highlighting the flex container.
  highlighted: false,
  // The NodeFront of the flex container.
  nodeFront: null,
  // The computed style properties of the flex container.
  properties: {},
};

const reducers = {

  [CLEAR_FLEXBOX](flexbox, _) {
    return INITIAL_FLEXBOX;
  },

  [TOGGLE_FLEX_ITEM_SHOWN](flexbox, { nodeFront }) {
    return Object.assign({}, flexbox, {
      flexItems: flexbox.flexItems.map(flexItem => {
        if (flexItem.nodeFront !== nodeFront) {
          return Object.assign({}, flexItem, {
            shown: false,
          });
        }

        return Object.assign({}, flexItem, {
          shown: !flexItem.shown,
        });
      })
    });
  },

  [UPDATE_FLEXBOX](_, { flexbox }) {
    return flexbox;
  },

  [UPDATE_FLEXBOX_COLOR](flexbox, { color }) {
    return Object.assign({}, flexbox, {
      color,
    });
  },

  [UPDATE_FLEXBOX_HIGHLIGHTED](flexbox, { highlighted }) {
    return Object.assign({}, flexbox, {
      highlighted,
    });
  },

};

module.exports = function(flexbox = INITIAL_FLEXBOX, action) {
  const reducer = reducers[action.type];
  if (!reducer) {
    return flexbox;
  }
  return reducer(flexbox, action);
};
