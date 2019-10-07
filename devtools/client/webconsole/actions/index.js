/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const actionModules = [
  require("./autocomplete"),
  require("./filters"),
  require("./input"),
  require("./messages"),
  require("./ui"),
  require("./notifications"),
  require("./object"),
  require("./toolbox"),
  require("./history"),
];

const actions = Object.assign({}, ...actionModules);

module.exports = actions;
