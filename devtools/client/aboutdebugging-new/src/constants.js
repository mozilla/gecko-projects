/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const actionTypes = {
  PAGE_SELECTED: "PAGE_SELECTED",
};

const PAGES = {
  THIS_FIREFOX: "this-firefox",
  CONNECT: "connect",
};

// flatten constants
module.exports = Object.assign({}, { PAGES }, actionTypes);
