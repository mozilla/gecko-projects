/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { createEnum } = require("devtools/client/shared/enum");

createEnum([
  // Update the list of animation.
  "UPDATE_ANIMATIONS",
  // Update state of the picker enabled.
  "UPDATE_ELEMENT_PICKER_ENABLED",
], module.exports);
