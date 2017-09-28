/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

var Cu = Components.utils;
const {require} = Cu.import("resource://devtools/shared/Loader.jsm", {});
const Services = require("Services");
const {gDevTools} = require("devtools/client/framework/devtools");
const {GetAvailableAddons, ForgetAddonsList} = require("devtools/client/webide/modules/addons");
const Strings = Services.strings.createBundle("chrome://devtools/locale/webide.properties");

window.addEventListener("load", function () {
  document.querySelector("#aboutaddons").onclick = function () {
    let browserWin = Services.wm.getMostRecentWindow(gDevTools.chromeWindowType);
    if (browserWin && browserWin.BrowserOpenAddonsMgr) {
      browserWin.BrowserOpenAddonsMgr("addons://list/extension");
    }
  };
  document.querySelector("#close").onclick = CloseUI;
  BuildUI(GetAvailableAddons());
}, {capture: true, once: true});

window.addEventListener("unload", function () {
  ForgetAddonsList();
}, {capture: true, once: true});

function CloseUI() {
  window.parent.UI.openProject();
}

function BuildUI(addons) {
  BuildItem(addons.adb, "adb");
}

function BuildItem(addon, type) {

  function onAddonUpdate(event, arg) {
    switch (event) {
      case "update":
        progress.removeAttribute("value");
        li.setAttribute("status", addon.status);
        status.textContent = Strings.GetStringFromName("addons_status_" + addon.status);
        break;
      case "failure":
        window.parent.UI.reportError("error_operationFail", arg);
        break;
      case "progress":
        if (arg == -1) {
          progress.removeAttribute("value");
        } else {
          progress.value = arg;
        }
        break;
    }
  }

  let events = ["update", "failure", "progress"];
  for (let e of events) {
    addon.on(e, onAddonUpdate);
  }
  window.addEventListener("unload", function () {
    for (let e of events) {
      addon.off(e, onAddonUpdate);
    }
  }, {once: true});

  let li = document.createElement("li");
  li.setAttribute("status", addon.status);

  let name = document.createElement("span");
  name.className = "name";

  switch (type) {
    case "adb":
      li.setAttribute("addon", type);
      name.textContent = Strings.GetStringFromName("addons_adb_label");
      break;
  }

  li.appendChild(name);

  let status = document.createElement("span");
  status.className = "status";
  status.textContent = Strings.GetStringFromName("addons_status_" + addon.status);
  li.appendChild(status);

  let installButton = document.createElement("button");
  installButton.className = "install-button";
  installButton.onclick = () => addon.install();
  installButton.textContent = Strings.GetStringFromName("addons_install_button");
  li.appendChild(installButton);

  let uninstallButton = document.createElement("button");
  uninstallButton.className = "uninstall-button";
  uninstallButton.onclick = () => addon.uninstall();
  uninstallButton.textContent = Strings.GetStringFromName("addons_uninstall_button");
  li.appendChild(uninstallButton);

  let progress = document.createElement("progress");
  li.appendChild(progress);

  if (type == "adb") {
    let warning = document.createElement("p");
    warning.textContent = Strings.GetStringFromName("addons_adb_warning");
    warning.className = "warning";
    li.appendChild(warning);
  }

  document.querySelector("ul").appendChild(li);
}
