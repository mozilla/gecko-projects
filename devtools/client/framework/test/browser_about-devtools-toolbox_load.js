/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Test that about:devtools-toolbox shows error an page when opened with invalid
 * paramters
 */
add_task(async function() {
  // test that error is shown when missing `type` param
  let { document, tab } = await openAboutToolbox({ invalid: "invalid" });
  await assertErrorIsShown(document);
  await removeTab(tab);
  // test that error is shown if `id` is not provided
  ({ document, tab } = await openAboutToolbox({ type: "tab" }));
  await assertErrorIsShown(document);
  await removeTab(tab);
  // test that error is shown if `remoteId` refers to an unexisting target
  ({ document, tab } = await openAboutToolbox({ type: "tab", remoteId: "13371337" }));
  await assertErrorIsShown(document);
  await removeTab(tab);

  async function assertErrorIsShown(doc) {
    await waitUntil(() => doc.querySelector(".js-error-page"));
    ok(doc.querySelector(".js-error-page"), "Error page is rendered");
  }
});

async function openAboutToolbox(params) {
  info("opening about:devtools-toolbox");
  const querystring = new URLSearchParams();
  Object.keys(params).forEach(x => querystring.append(x, params[x]));

  const tab = await addTab(`about:devtools-toolbox?${querystring}`);
  const browser = tab.linkedBrowser;

  return {
    tab,
    document: browser.contentDocument,
  };
}
