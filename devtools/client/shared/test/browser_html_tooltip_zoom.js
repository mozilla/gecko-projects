/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */
/* import-globals-from helper_html_tooltip.js */

"use strict";

/**
 * Test the HTMLTooltip is displayed correct position if content is zoomed in.
 */

const HTML_NS = "http://www.w3.org/1999/xhtml";
const TEST_URI = CHROME_URL_ROOT + "doc_html_tooltip.xul";

const {HTMLTooltip} = require("devtools/client/shared/widgets/tooltip/HTMLTooltip");

function getTooltipContent(doc) {
  let div = doc.createElementNS(HTML_NS, "div");
  div.style.height = "50px";
  div.style.boxSizing = "border-box";
  div.style.backgroundColor = "red";
  div.textContent = "tooltip";
  return div;
}

add_task(async function() {
  let [host,, doc] = await createHost("window", TEST_URI);
  const zoom = 1.5;
  await pushPref("devtools.toolbox.zoomValue", zoom.toString(10));

  // Change this xul zoom to the x1.5 since this test doesn't use the toolbox preferences.
  let contentViewer = host.frame.docShell.contentViewer;
  contentViewer.fullZoom = zoom;
  let tooltip = new HTMLTooltip(doc, {useXulWrapper: true});

  info("Set tooltip content");
  tooltip.setContent(getTooltipContent(doc), {width: 100, height: 50});

  is(tooltip.isVisible(), false, "Tooltip is not visible");

  info("Show the tooltip and check the expected events are fired.");
  let onShown = tooltip.once("shown");
  tooltip.show(doc.getElementById("box1"));
  await onShown;

  let menuRect =
      doc.querySelector(".tooltip-xul-wrapper").getBoxQuads({relativeTo: doc})[0].bounds;
  let anchorRect = doc.getElementById("box1").getBoxQuads({relativeTo: doc})[0].bounds;
  let xDelta = Math.abs(menuRect.left - anchorRect.left);
  let yDelta = Math.abs(menuRect.top - anchorRect.bottom);

  // This test allow the rounded error and platform's offset.
  // For detail, see the devtools/client/framework/test/browser_toolbox_zoom_popup.js
  ok(xDelta < 2, "xDelta is lower than 2: " + xDelta + ".");
  ok(yDelta < 6, "yDelta is lower than 6: " + yDelta + ".");

  info("Hide the tooltip and check the expected events are fired.");

  let onPopupHidden = tooltip.once("hidden");
  tooltip.hide();
  await onPopupHidden;

  tooltip.destroy();
  await host.destroy();
});
