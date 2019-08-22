/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const TEST_URI =
  "http://example.com/browser/devtools/client/webconsole/" +
  "test/browser/test-console.html";

add_task(async function() {
  const hud = await openNewTabAndConsole(TEST_URI);

  info("console.log with a string argument");
  const receivedMessages = waitForMessages({
    hud,
    messages: [
      {
        // Test that the output does not include quotes.
        text: "stringLog",
      },
    ],
  });

  await ContentTask.spawn(gBrowser.selectedBrowser, {}, function() {
    content.wrappedJSObject.stringLog();
  });

  await receivedMessages;

  info("evaluating a string constant");
  const msg = await executeAndWaitForMessage(
    hud,
    '"string\\nconstant"',
    "constant",
    ".result"
  );
  const body = msg.node.querySelector(".message-body");
  // On the other hand, a string constant result should be quoted, but
  // newlines should be let through.
  ok(
    body.textContent.includes('"string\nconstant"'),
    `found expected text - "${body.textContent}"`
  );
});
