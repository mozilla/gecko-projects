add_task(async function oopProcessSwap() {
  const FILE = fileURL("dummy_page.html");
  const WEB = httpURL("file_postmsg_parent.html");

  let win = await BrowserTestUtils.openNewBrowserWindow({fission: true});

  await BrowserTestUtils.withNewTab({gBrowser: win.gBrowser, url: FILE}, async browser => {
    is(browser.browsingContext.getChildren().length, 0);

    info("creating an in-process frame");
    let frameId = await ContentTask.spawn(browser, {FILE}, async ({FILE}) => {
      let iframe = content.document.createElement("iframe");
      iframe.setAttribute("src", FILE);
      content.document.body.appendChild(iframe);

      // The nested URI should be same-process
      ok(iframe.browsingContext.docShell, "Should be in-process");

      return iframe.browsingContext.id;
    });

    is(browser.browsingContext.getChildren().length, 1);

    info("navigating to x-process frame");
    let oopinfo = await ContentTask.spawn(browser, {WEB}, async ({WEB}) => {
      let iframe = content.document.querySelector("iframe");

      iframe.contentWindow.location = WEB;

      let data = await new Promise(resolve => {
        content.window.addEventListener("message", function(evt) {
          info("oop iframe loaded");
          is(evt.source, iframe.contentWindow);
          resolve(evt.data);
        }, {once: true});
      });

      is(iframe.browsingContext.docShell, null, "Should be out-of-process");
      is(iframe.browsingContext.embedderElement, iframe, "correct embedder");

      return {
        location: data.location,
        browsingContextId: iframe.browsingContext.id,
      };
    });

    is(browser.browsingContext.getChildren().length, 1);
    todo(frameId == oopinfo.browsingContextId, "BrowsingContext should not have changed");
    is(oopinfo.location, WEB, "correct location");
  });

  await BrowserTestUtils.closeWindow(win);
});
