/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {
  APPLY_FONT_VARIATION_INSTANCE,
  RESET_EDITOR,
  UPDATE_AXIS_VALUE,
  UPDATE_CUSTOM_INSTANCE,
  UPDATE_EDITOR_STATE,
  UPDATE_PROPERTY_VALUE,
  UPDATE_WARNING_MESSAGE,
} = require("./index");

module.exports = {

  resetFontEditor() {
    return {
      type: RESET_EDITOR,
    };
  },

  applyInstance(name, values) {
    return {
      type: APPLY_FONT_VARIATION_INSTANCE,
      name,
      values,
    };
  },

  updateCustomInstance() {
    return {
      type: UPDATE_CUSTOM_INSTANCE,
    };
  },

  updateAxis(axis, value) {
    return {
      type: UPDATE_AXIS_VALUE,
      axis,
      value,
    };
  },

  updateFontEditor(fonts, properties = {}, id = "") {
    return {
      type: UPDATE_EDITOR_STATE,
      fonts,
      properties,
      id,
    };
  },

  updateFontProperty(property, value) {
    return {
      type: UPDATE_PROPERTY_VALUE,
      property,
      value,
    };
  },

  updateWarningMessage(warning) {
    return {
      type: UPDATE_WARNING_MESSAGE,
      warning,
    };
  },

};
