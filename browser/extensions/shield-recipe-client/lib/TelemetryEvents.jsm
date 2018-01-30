/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
"use strict";

const {utils: Cu, interfaces: Ci} = Components;
Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource://gre/modules/XPCOMUtils.jsm");

this.EXPORTED_SYMBOLS = ["TelemetryEvents"];

const TELEMETRY_CATEGORY = "normandy";

const TelemetryEvents = {
  init() {
    Services.telemetry.registerEvents(TELEMETRY_CATEGORY, {
      enroll: {
        methods: ["enroll"],
        objects: ["preference_study", "addon_study"],
        extra_keys: ["experimentType", "branch", "addonId", "addonVersion"],
        record_on_release: true,
      },

      unenroll: {
        methods: ["unenroll"],
        objects: ["preference_study", "addon_study"],
        extra_keys: ["reason", "didResetValue", "addonId", "addonVersion"],
        record_on_release: true,
      },
    });
  },

  sendEvent(method, object, value, extra) {
    Services.telemetry.recordEvent(TELEMETRY_CATEGORY, method, object, value, extra);
  },
};
