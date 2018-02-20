/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

/* global pref */

// Marionette is the remote protocol that lets OOP programs communicate
// with, instrument, and control Gecko.

// Controls whether the Marionette component is enabled.
pref("marionette.enabled", false);

// Delay server startup until a modal dialogue has been clicked to
// allow time for user to set breakpoints in Browser Toolbox.
pref("marionette.debugging.clicktostart", false);

// Marionette logging verbosity.  Allowed values are "fatal", "error",
// "warn", "info", "config", "debug", and "trace".
pref("marionette.log.level", "info");

// Port to start Marionette server on.
pref("marionette.port", 2828);

// Sets recommended automation preferences when Marionette is started.
pref("marionette.prefs.recommended", true);
