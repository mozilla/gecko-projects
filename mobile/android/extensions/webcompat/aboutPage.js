/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

/* global ExtensionAPI, Services, XPCOMUtils */

ChromeUtils.defineModuleGetter(this, "AppConstants",
                               "resource://gre/modules/AppConstants.jsm");

ChromeUtils.defineModuleGetter(this, "Services",
                               "resource://gre/modules/Services.jsm");

XPCOMUtils.defineLazyServiceGetter(this, "resProto",
                                   "@mozilla.org/network/protocol;1?name=resource",
                                   "nsISubstitutingProtocolHandler");

const ResourceSubstitution = "webcompat";
const ProcessScriptURL = "resource://webcompat/aboutPageProcessScript.js";

const ShouldStart = ["default", "nightly", "nightly-try"].includes(AppConstants.MOZ_UPDATE_CHANNEL);

this.aboutPage = class extends ExtensionAPI {
  onStartup() {
    if (!ShouldStart) {
      return;
    }

    const {rootURI} = this.extension;

    resProto.setSubstitution(ResourceSubstitution,
                             Services.io.newURI("chrome/res/", null, rootURI));

    Services.ppmm.loadProcessScript(ProcessScriptURL, true);
  }

  onShutdown() {
    if (!ShouldStart) {
      return;
    }

    resProto.setSubstitution(ResourceSubstitution, null);

    Services.ppmm.removeDelayedProcessScript(ProcessScriptURL);
  }
};
