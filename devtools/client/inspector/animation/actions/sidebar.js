/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { UPDATE_SIDEBAR_SIZE } = require("./index");

module.exports = {
  /**
   * Update the sidebar size.
   */
  updateSidebarSize(size) {
    return {
      type: UPDATE_SIDEBAR_SIZE,
      size,
    };
  }
};
