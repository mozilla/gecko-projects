/* -*- Mode: indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim: set sts=2 sw=2 et tw=80: */
"use strict";

XPCOMUtils.defineLazyModuleGetter(this, "gDevTools",
                                  "resource://devtools/client/framework/gDevTools.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "devtools",
                                  "resource://devtools/shared/Loader.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "ContentTaskUtils",
                                  "resource://testing-common/ContentTaskUtils.jsm");

function isActiveSidebarTabTitle(inspector, expectedTabTitle, message) {
  const actualTabTitle = inspector.panelDoc.querySelector(".tabs-menu-item.is-active").innerText;
  is(actualTabTitle, expectedTabTitle, message);
}

add_task(async function test_devtools_panels_elements_sidebar() {
  let tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, "http://mochi.test:8888/");

  async function devtools_page() {
    const sidebar1 = await browser.devtools.panels.elements.createSidebarPane("Test Sidebar 1");
    const sidebar2 = await browser.devtools.panels.elements.createSidebarPane("Test Sidebar 2");
    const sidebar3 = await browser.devtools.panels.elements.createSidebarPane("Test Sidebar 3");

    const onShownListener = (event, sidebarInstance) => {
      browser.test.sendMessage(`devtools_sidebar_${event}`, sidebarInstance);
    };

    sidebar1.onShown.addListener(() => onShownListener("shown", "sidebar1"));
    sidebar2.onShown.addListener(() => onShownListener("shown", "sidebar2"));
    sidebar3.onShown.addListener(() => onShownListener("shown", "sidebar3"));

    sidebar1.onHidden.addListener(() => onShownListener("hidden", "sidebar1"));
    sidebar2.onHidden.addListener(() => onShownListener("hidden", "sidebar2"));
    sidebar3.onHidden.addListener(() => onShownListener("hidden", "sidebar3"));

    sidebar1.setObject({propertyName: "propertyValue"}, "Optional Root Object Title");
    sidebar2.setObject({anotherPropertyName: 123});

    // Refresh the sidebar content on every inspector selection.
    browser.devtools.panels.elements.onSelectionChanged.addListener(() => {
      sidebar3.setExpression("$0 && $0.tagName", "Selected Element tagName");
    });

    browser.test.sendMessage("devtools_page_loaded");
  }

  let extension = ExtensionTestUtils.loadExtension({
    manifest: {
      devtools_page: "devtools_page.html",
    },
    files: {
      "devtools_page.html": `<!DOCTYPE html>
      <html>
       <head>
         <meta charset="utf-8">
       </head>
       <body>
         <script src="devtools_page.js"></script>
       </body>
      </html>`,
      "devtools_page.js": devtools_page,
    },
  });

  await extension.startup();

  let target = devtools.TargetFactory.forTab(tab);

  const toolbox = await gDevTools.showToolbox(target, "webconsole");
  info("developer toolbox opened");

  await extension.awaitMessage("devtools_page_loaded");

  const waitInspector = toolbox.once("inspector-selected");
  toolbox.selectTool("inspector");
  await waitInspector;

  const sidebarIds = Array.from(toolbox._inspectorExtensionSidebars.keys());

  const inspector = await toolbox.getPanel("inspector");

  inspector.sidebar.show(sidebarIds[0]);

  const shownSidebarInstance = await extension.awaitMessage("devtools_sidebar_shown");

  is(shownSidebarInstance, "sidebar1", "Got the shown event on the first extension sidebar");

  isActiveSidebarTabTitle(inspector, "Test Sidebar 1",
                          "Got the expected title on the active sidebar tab");

  const sidebarPanel1 = inspector.sidebar.getTabPanel(sidebarIds[0]);

  ok(sidebarPanel1, "Got a rendered sidebar panel for the first registered extension sidebar");

  is(sidebarPanel1.querySelectorAll("table.treeTable").length, 1,
     "The first sidebar panel contains a rendered TreeView component");

  is(sidebarPanel1.querySelectorAll("table.treeTable .stringCell").length, 1,
     "The TreeView component contains the expected number of string cells.");

  const sidebarPanel1Tree = sidebarPanel1.querySelector("table.treeTable");
  ok(
    sidebarPanel1Tree.innerText.includes("Optional Root Object Title"),
    "The optional root object title has been included in the object tree"
  );

  inspector.sidebar.show(sidebarIds[1]);

  const shownSidebarInstance2 = await extension.awaitMessage("devtools_sidebar_shown");
  const hiddenSidebarInstance1 = await extension.awaitMessage("devtools_sidebar_hidden");

  is(shownSidebarInstance2, "sidebar2", "Got the shown event on the second extension sidebar");
  is(hiddenSidebarInstance1, "sidebar1", "Got the hidden event on the first extension sidebar");

  isActiveSidebarTabTitle(inspector, "Test Sidebar 2",
                          "Got the expected title on the active sidebar tab");

  const sidebarPanel2 = inspector.sidebar.getTabPanel(sidebarIds[1]);

  ok(sidebarPanel2, "Got a rendered sidebar panel for the second registered extension sidebar");

  is(sidebarPanel2.querySelectorAll("table.treeTable").length, 1,
     "The second sidebar panel contains a rendered TreeView component");

  is(sidebarPanel2.querySelectorAll("table.treeTable .numberCell").length, 1,
     "The TreeView component contains the expected a cell of type number.");

  inspector.sidebar.show(sidebarIds[2]);

  const shownSidebarInstance3 = await extension.awaitMessage("devtools_sidebar_shown");
  const hiddenSidebarInstance2 = await extension.awaitMessage("devtools_sidebar_hidden");

  is(shownSidebarInstance3, "sidebar3", "Got the shown event on the third extension sidebar");
  is(hiddenSidebarInstance2, "sidebar2", "Got the hidden event on the second extension sidebar");

  isActiveSidebarTabTitle(inspector, "Test Sidebar 3",
                          "Got the expected title on the active sidebar tab");

  const sidebarPanel3 = inspector.sidebar.getTabPanel(sidebarIds[2]);

  ok(sidebarPanel3, "Got a rendered sidebar panel for the third registered extension sidebar");

  info("Waiting for the third panel to be rendered");
  await ContentTaskUtils.waitForCondition(() => {
    return sidebarPanel3.querySelectorAll("table.treeTable").length > 0;
  });

  is(sidebarPanel3.querySelectorAll("table.treeTable").length, 1,
     "The third sidebar panel contains a rendered TreeView component");

  const treeViewStringValues = sidebarPanel3.querySelectorAll("table.treeTable .stringCell");

  is(treeViewStringValues.length, 1,
     "The TreeView component contains the expected content of type string.");

  is(treeViewStringValues[0].innerText, "\"BODY\"",
     "Got the expected content in the sidebar.setExpression rendered TreeView");

  await extension.unload();

  is(Array.from(toolbox._inspectorExtensionSidebars.keys()).length, 0,
     "All the registered sidebars have been unregistered on extension unload");

  is(inspector.sidebar.getTabPanel(sidebarIds[0]), undefined,
     "The first registered sidebar has been removed");

  is(inspector.sidebar.getTabPanel(sidebarIds[1]), undefined,
     "The second registered sidebar has been removed");

  is(inspector.sidebar.getTabPanel(sidebarIds[2]), undefined,
     "The third registered sidebar has been removed");

  await gDevTools.closeToolbox(target);

  await target.destroy();

  await BrowserTestUtils.removeTab(tab);
});
