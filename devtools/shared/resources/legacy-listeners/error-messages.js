/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

module.exports = async function({
  targetList,
  targetFront,
  isFissionEnabledOnContentToolbox,
  onAvailable,
}) {
  // Allow the top level target unconditionnally.
  // Also allow frame, but only in content toolbox, when the fission/content toolbox pref is
  // set. i.e. still ignore them in the content of the browser toolbox as we inspect
  // messages via the process targets
  // Also ignore workers as they are not supported yet. (see bug 1592584)
  const isContentToolbox = targetList.targetFront.isLocalTab;
  const listenForFrames = isContentToolbox && isFissionEnabledOnContentToolbox;
  const isAllowed =
    targetFront.isTopLevel ||
    targetFront.targetType === targetList.TYPES.PROCESS ||
    (targetFront.targetType === targetList.TYPES.FRAME && listenForFrames);

  if (!isAllowed) {
    return;
  }

  const webConsoleFront = await targetFront.getFront("console");

  // Request notifying about new messages. Here the "PageError" type start listening for
  // both actual PageErrors (emitted as "pageError" events) as well as LogMessages (
  // emitted as "logMessage" events). This function only set up the listener on the
  // webConsoleFront for "pageError".
  await webConsoleFront.startListeners(["PageError"]);

  // Fetch already existing messages
  // /!\ The actor implementation requires to call startListeners("PageError") first /!\
  const { messages } = await webConsoleFront.getCachedMessages(["PageError"]);

  for (let message of messages) {
    // On older server (< v77), we're also getting LogMessage cached messages, so we need
    // to ignore those.
    if (
      !webConsoleFront.traits.newCacheStructure &&
      message._type &&
      message._type !== "PageError"
    ) {
      continue;
    }

    // Handling cached messages for servers older than Firefox 78.
    if (message._type) {
      // Wrap the message into a `pageError` attribute, to match `pageError` behavior
      message = {
        pageError: message,
        type: "pageError",
      };
    }

    // Cached messages don't have the same shape as live messages,
    // so we need to transform them.
    onAvailable(message);
  }

  webConsoleFront.on("pageError", onAvailable);
};
