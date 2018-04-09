/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const Services = require("Services");
const { gDevTools } = require("devtools/client/framework/devtools");

/**
 * Opens given request in a new tab.
 */
function openRequestInTab(url, requestPostData) {
  let win = Services.wm.getMostRecentWindow(gDevTools.chromeWindowType);
  if (!requestPostData) {
    win.openUILinkIn(url, "tab", {relatedToCurrent: true});
  } else {
    openPostRequestInTabHelper({
      url,
      data: requestPostData.postData
    });
  }
}

function openPostRequestInTabHelper({url, data}) {
  let form = document.createElement("form");
  form.target = "_blank";
  form.action = url;
  form.method = "post";

  if (data) {
    for (let key in data) {
      let input = document.createElement("input");
      input.name = key;
      input.value = data[key];
      form.appendChild(input);
    }
  }

  form.hidden = true;
  document.body.appendChild(form);
  form.submit();
  form.remove();
}

module.exports = {
  openRequestInTab,
};
