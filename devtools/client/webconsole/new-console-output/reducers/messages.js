/* -*- indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim: set ft=javascript ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
"use strict";

const Immutable = require("devtools/client/shared/vendor/immutable");

const {
  isGroupType,
  l10n,
} = require("devtools/client/webconsole/new-console-output/utils/messages");

const constants = require("devtools/client/webconsole/new-console-output/constants");
const {
  DEFAULT_FILTERS,
  FILTERS,
  MESSAGE_TYPE,
  MESSAGE_SOURCE,
} = constants;
const { getGripPreviewItems } = require("devtools/client/shared/components/reps/reps");
const { getSourceNames } = require("devtools/client/shared/source-utils");

const {
  UPDATE_REQUEST,
} = require("devtools/client/netmonitor/src/constants");

const {
  processNetworkUpdates,
} = require("devtools/client/netmonitor/src/utils/request-utils");

const MessageState = Immutable.Record({
  // List of all the messages added to the console.
  messagesById: Immutable.OrderedMap(),
  // Array of the visible messages.
  visibleMessages: [],
  // Object for the filtered messages.
  filteredMessagesCount: getDefaultFiltersCounter(),
  // List of the message ids which are opened.
  messagesUiById: Immutable.List(),
  // Map of the form {messageId : tableData}, which represent the data passed
  // as an argument in console.table calls.
  messagesTableDataById: Immutable.Map(),
  // Map of the form {groupMessageId : groupArray},
  // where groupArray is the list of of all the parent groups' ids of the groupMessageId.
  groupsById: Immutable.Map(),
  // Message id of the current group (no corresponding console.groupEnd yet).
  currentGroup: null,
  // Array of removed actors (i.e. actors logged in removed messages) we keep track of
  // in order to properly release them.
  // This array is not supposed to be consumed by any UI component.
  removedActors: [],
  // Map of the form {messageId : numberOfRepeat}
  repeatById: {},
  // Map of the form {messageId : networkInformation}
  // `networkInformation` holds request, response, totalTime, ...
  networkMessagesUpdateById: {},
});

function addMessage(state, filtersState, prefsState, newMessage) {
  const {
    messagesById,
    messagesUiById,
    groupsById,
    currentGroup,
    repeatById,
    visibleMessages,
    filteredMessagesCount,
  } = state;

  if (newMessage.type === constants.MESSAGE_TYPE.NULL_MESSAGE) {
    // When the message has a NULL type, we don't add it.
    return state;
  }

  if (newMessage.type === constants.MESSAGE_TYPE.END_GROUP) {
    // Compute the new current group.
    return state.set("currentGroup", getNewCurrentGroup(currentGroup, groupsById));
  }

  if (newMessage.allowRepeating && messagesById.size > 0) {
    let lastMessage = messagesById.last();
    if (
      lastMessage.repeatId === newMessage.repeatId
      && lastMessage.groupId === currentGroup
    ) {
      return state.set(
        "repeatById",
        Object.assign({}, repeatById, {
          [lastMessage.id]: (repeatById[lastMessage.id] || 1) + 1
        })
      );
    }
  }

  return state.withMutations(function (record) {
    // Add the new message with a reference to the parent group.
    let parentGroups = getParentGroups(currentGroup, groupsById);
    newMessage.groupId = currentGroup;
    newMessage.indent = parentGroups.length;

    const addedMessage = Object.freeze(newMessage);
    record.set(
      "messagesById",
      messagesById.set(newMessage.id, addedMessage)
    );

    if (newMessage.type === "trace") {
      // We want the stacktrace to be open by default.
      record.set("messagesUiById", messagesUiById.push(newMessage.id));
    } else if (isGroupType(newMessage.type)) {
      record.set("currentGroup", newMessage.id);
      record.set("groupsById", groupsById.set(newMessage.id, parentGroups));

      if (newMessage.type === constants.MESSAGE_TYPE.START_GROUP) {
        // We want the group to be open by default.
        record.set("messagesUiById", messagesUiById.push(newMessage.id));
      }
    }

    const {
      visible,
      cause
    } = getMessageVisibility(addedMessage, record, filtersState);

    if (visible) {
      record.set("visibleMessages", [...visibleMessages, newMessage.id]);
    } else if (DEFAULT_FILTERS.includes(cause)) {
      record.set("filteredMessagesCount", Object.assign({}, filteredMessagesCount, {
        global: filteredMessagesCount.global + 1,
        [cause]: filteredMessagesCount[cause] + 1
      }));
    }
  });
}

function messages(state = new MessageState(), action, filtersState, prefsState) {
  const {
    messagesById,
    messagesUiById,
    messagesTableDataById,
    networkMessagesUpdateById,
    groupsById,
    visibleMessages,
  } = state;

  const {logLimit} = prefsState;

  let newState;
  switch (action.type) {
    case constants.MESSAGES_ADD:
      newState = state;

      // Preemptively remove messages that will never be rendered
      let list = [];
      let prunableCount = 0;
      let lastMessageRepeatId = -1;
      for (let i = action.messages.length - 1; i >= 0; i--) {
        let message = action.messages[i];
        if (!message.groupId && !isGroupType(message.type) &&
            message.type !== MESSAGE_TYPE.END_GROUP) {
          prunableCount++;
          // Once we've added the max number of messages that can be added, stop.
          // Except for repeated messages, where we keep adding over the limit.
          if (prunableCount <= logLimit || message.repeatId == lastMessageRepeatId) {
            list.unshift(action.messages[i]);
          } else {
            break;
          }
        } else {
          list.unshift(message);
        }
        lastMessageRepeatId = message.repeatId;
      }

      list.forEach(message => {
        newState = addMessage(newState, filtersState, prefsState, message);
      });

      return limitTopLevelMessageCount(newState, logLimit);

    case constants.MESSAGE_ADD:
      newState = addMessage(state, filtersState, prefsState, action.message);
      return limitTopLevelMessageCount(newState, logLimit);

    case constants.MESSAGES_CLEAR:
      return new MessageState({
        // Store all actors from removed messages. This array is used by
        // `releaseActorsEnhancer` to release all of those backend actors.
        "removedActors": [...state.messagesById].reduce((res, [id, msg]) => {
          res.push(...getAllActorsInMessage(msg, state));
          return res;
        }, [])
      });

    case constants.MESSAGE_OPEN:
      return state.withMutations(function (record) {
        record.set("messagesUiById", messagesUiById.push(action.id));

        let currMessage = messagesById.get(action.id);

        // If the message is a group
        if (isGroupType(currMessage.type)) {
          // We want to make its children visible
          const messagesToShow = [...messagesById].reduce((res, [id, message]) => {
            if (
              !visibleMessages.includes(message.id)
              && getParentGroups(message.groupId, groupsById).includes(action.id)
              && getMessageVisibility(
                message,
                record,
                filtersState,
                // We want to check if the message is in an open group
                // only if it is not a direct child of the group we're opening.
                message.groupId !== action.id
              ).visible
            ) {
              res.push(id);
            }
            return res;
          }, []);

          // We can then insert the messages ids right after the one of the group.
          const insertIndex = visibleMessages.indexOf(action.id) + 1;
          record.set("visibleMessages", [
            ...visibleMessages.slice(0, insertIndex),
            ...messagesToShow,
            ...visibleMessages.slice(insertIndex),
          ]);
        }

        // If the current message is a network event, mark it as opened-once,
        // so HTTP details are not fetched again the next time the user
        // opens the log.
        if (currMessage.source == "network") {
          record.set("messagesById",
            messagesById.set(
              action.id, Object.assign({},
                currMessage, {
                  openedOnce: true
                }
              )
            )
          );
        }
      });

    case constants.MESSAGE_CLOSE:
      return state.withMutations(function (record) {
        let messageId = action.id;
        let index = record.messagesUiById.indexOf(messageId);
        record.deleteIn(["messagesUiById", index]);

        // If the message is a group
        if (isGroupType(messagesById.get(messageId).type)) {
          // Hide all its children
          record.set(
            "visibleMessages",
            [...visibleMessages].filter(id => getParentGroups(
                messagesById.get(id).groupId,
                groupsById
              ).includes(messageId) === false
            )
          );
        }
      });

    case constants.MESSAGE_TABLE_RECEIVE:
      const {id, data} = action;
      return state.set("messagesTableDataById", messagesTableDataById.set(id, data));

    case constants.NETWORK_MESSAGE_UPDATE:
      return state.set(
        "networkMessagesUpdateById",
        Object.assign({}, networkMessagesUpdateById, {
          [action.message.id]: action.message
        })
      );

    case UPDATE_REQUEST:
    case constants.NETWORK_UPDATE_REQUEST: {
      let request = networkMessagesUpdateById[action.id];
      if (!request) {
        return state;
      }

      let values = processNetworkUpdates(action.data);
      newState = state.set(
        "networkMessagesUpdateById",
        Object.assign({}, networkMessagesUpdateById, {
          [action.id]: Object.assign({}, request, values)
        })
      );

      return newState;
    }

    case constants.REMOVED_ACTORS_CLEAR:
      return state.set("removedActors", []);

    case constants.FILTER_TOGGLE:
    case constants.FILTER_TEXT_SET:
    case constants.FILTERS_CLEAR:
    case constants.DEFAULT_FILTERS_RESET:
      return state.withMutations(function (record) {
        const messagesToShow = [];
        const filtered = getDefaultFiltersCounter();
        messagesById.forEach((message, messageId) => {
          const {
            visible,
            cause
          } = getMessageVisibility(message, state, filtersState);
          if (visible) {
            messagesToShow.push(messageId);
          } else if (DEFAULT_FILTERS.includes(cause)) {
            filtered.global = filtered.global + 1;
            filtered[cause] = filtered[cause] + 1;
          }
        });

        record.set("visibleMessages", messagesToShow);
        record.set("filteredMessagesCount", filtered);
      });
  }

  return state;
}

function getNewCurrentGroup(currentGoup, groupsById) {
  let newCurrentGroup = null;
  if (currentGoup) {
    // Retrieve the parent groups of the current group.
    let parents = groupsById.get(currentGoup);
    if (Array.isArray(parents) && parents.length > 0) {
      // If there's at least one parent, make the first one the new currentGroup.
      newCurrentGroup = parents[0];
    }
  }
  return newCurrentGroup;
}

function getParentGroups(currentGroup, groupsById) {
  let groups = [];
  if (currentGroup) {
    // If there is a current group, we add it as a parent
    groups = [currentGroup];

    // As well as all its parents, if it has some.
    let parentGroups = groupsById.get(currentGroup);
    if (Array.isArray(parentGroups) && parentGroups.length > 0) {
      groups = groups.concat(parentGroups);
    }
  }

  return groups;
}

/**
 * Remove all top level messages that exceeds message limit.
 * Also populate an array of all backend actors associated with these
 * messages so they can be released.
 */
function limitTopLevelMessageCount(state, logLimit) {
  return state.withMutations(function (record) {
    let topLevelCount = record.groupsById.size === 0
      ? record.messagesById.size
      : getToplevelMessageCount(record);

    if (topLevelCount <= logLimit) {
      return;
    }

    const removedMessagesId = [];
    const removedActors = [];
    let visibleMessages = [...record.visibleMessages];

    let cleaningGroup = false;
    record.messagesById.forEach((message, id) => {
      // If we were cleaning a group and the current message does not have
      // a groupId, we're done cleaning.
      if (cleaningGroup === true && !message.groupId) {
        cleaningGroup = false;
      }

      // If we're not cleaning a group and the message count is below the logLimit,
      // we exit the forEach iteration.
      if (cleaningGroup === false && topLevelCount <= logLimit) {
        return false;
      }

      // If we're not currently cleaning a group, and the current message is identified
      // as a group, set the cleaning flag to true.
      if (cleaningGroup === false && record.groupsById.has(id)) {
        cleaningGroup = true;
      }

      if (!message.groupId) {
        topLevelCount--;
      }

      removedMessagesId.push(id);
      removedActors.push(...getAllActorsInMessage(message, record));

      const index = visibleMessages.indexOf(id);
      if (index > -1) {
        visibleMessages.splice(index, 1);
      }

      return true;
    });

    if (removedActors.length > 0) {
      record.set("removedActors", record.removedActors.concat(removedActors));
    }

    if (record.visibleMessages.length > visibleMessages.length) {
      record.set("visibleMessages", visibleMessages);
    }

    const isInRemovedId = id => removedMessagesId.includes(id);
    const mapHasRemovedIdKey = map => map.findKey((value, id) => isInRemovedId(id));
    const objectHasRemovedIdKey = obj => Object.keys(obj).findIndex(isInRemovedId) !== -1;
    const cleanUpCollection = map => removedMessagesId.forEach(id => map.remove(id));
    const cleanUpList = list => list.filter(id => {
      return isInRemovedId(id) === false;
    });
    const cleanUpObject = object => [...Object.entries(object)]
      .reduce((res, [id, value]) => {
        if (!isInRemovedId(id)) {
          res[id] = value;
        }
        return res;
      }, {});

    record.set("messagesById", record.messagesById.withMutations(cleanUpCollection));

    if (record.messagesUiById.find(isInRemovedId)) {
      record.set("messagesUiById", cleanUpList(record.messagesUiById));
    }
    if (mapHasRemovedIdKey(record.messagesTableDataById)) {
      record.set("messagesTableDataById",
        record.messagesTableDataById.withMutations(cleanUpCollection));
    }
    if (mapHasRemovedIdKey(record.groupsById)) {
      record.set("groupsById", record.groupsById.withMutations(cleanUpCollection));
    }
    if (objectHasRemovedIdKey(record.repeatById)) {
      record.set("repeatById", cleanUpObject(record.repeatById));
    }

    if (objectHasRemovedIdKey(record.networkMessagesUpdateById)) {
      record.set("networkMessagesUpdateById",
        cleanUpObject(record.networkMessagesUpdateById));
    }
  });
}

/**
 * Get an array of all the actors logged in a specific message.
 *
 * @param {Message} message: The message to get actors from.
 * @param {Record} state: The redux state.
 * @return {Array} An array containing all the actors logged in a message.
 */
function getAllActorsInMessage(message, state) {
  const {
    parameters,
    messageText,
  } = message;

  let actors = [];
  if (Array.isArray(parameters)) {
    message.parameters.forEach(parameter => {
      if (parameter.actor) {
        actors.push(parameter.actor);
      }
    });
  }

  if (messageText && messageText.actor) {
    actors.push(messageText.actor);
  }

  return actors;
}

/**
 * Returns total count of top level messages (those which are not
 * within a group).
 */
function getToplevelMessageCount(record) {
  return record.messagesById.count(message => !message.groupId);
}

/**
 * Check if a message should be visible in the console output, and if not, what
 * causes it to be hidden.
 *
 * @return {Object} An object of the following form:
 *         - visible {Boolean}: true if the message should be visible
 *         - cause {String}: if visible is false, what causes the message to be hidden.
 */
function getMessageVisibility(message, messagesState, filtersState, checkGroup = true) {
  // Do not display the message if it's in closed group.
  if (
    checkGroup
    && !isInOpenedGroup(message, messagesState.groupsById, messagesState.messagesUiById)
  ) {
    return {
      visible: false,
      cause: "closedGroup"
    };
  }

  // Some messages can't be filtered out (e.g. groups).
  // So, always return visible: true for those.
  if (isUnfilterable(message)) {
    return {
      visible: true
    };
  }

  if (!passSearchFilters(message, filtersState)) {
    return {
      visible: false,
      cause: FILTERS.TEXT
    };
  }

  // Let's check all level filters (error, warn, log, …) and return visible: false
  // and the message level as a cause if the function returns false.
  if (!passLevelFilters(message, filtersState)) {
    return {
      visible: false,
      cause: message.level
    };
  }

  if (!passCssFilters(message, filtersState)) {
    return {
      visible: false,
      cause: FILTERS.CSS
    };
  }

  if (!passNetworkFilter(message, filtersState)) {
    return {
      visible: false,
      cause: FILTERS.NET
    };
  }

  if (!passXhrFilter(message, filtersState)) {
    return {
      visible: false,
      cause: FILTERS.NETXHR
    };
  }

  return {
    visible: true
  };
}

function isUnfilterable(message) {
  return [
    MESSAGE_TYPE.COMMAND,
    MESSAGE_TYPE.RESULT,
    MESSAGE_TYPE.START_GROUP,
    MESSAGE_TYPE.START_GROUP_COLLAPSED,
  ].includes(message.type);
}

function isInOpenedGroup(message, groupsById, messagesUI) {
  return !message.groupId
    || (
      !isGroupClosed(message.groupId, messagesUI)
      && !hasClosedParentGroup(groupsById.get(message.groupId), messagesUI)
    );
}

function hasClosedParentGroup(group, messagesUI) {
  return group.some(groupId => isGroupClosed(groupId, messagesUI));
}

function isGroupClosed(groupId, messagesUI) {
  return messagesUI.includes(groupId) === false;
}

/**
 * Returns true if the message shouldn't be hidden because of the network filter state.
 *
 * @param {Object} message - The message to check the filter against.
 * @param {FilterState} filters - redux "filters" state.
 * @returns {Boolean}
 */
function passNetworkFilter(message, filters) {
  // The message passes the filter if it is not a network message,
  // or if it is an xhr one,
  // or if the network filter is on.
  return (
    message.source !== MESSAGE_SOURCE.NETWORK ||
    message.isXHR === true ||
    filters.get(FILTERS.NET) === true
  );
}

/**
 * Returns true if the message shouldn't be hidden because of the xhr filter state.
 *
 * @param {Object} message - The message to check the filter against.
 * @param {FilterState} filters - redux "filters" state.
 * @returns {Boolean}
 */
function passXhrFilter(message, filters) {
  // The message passes the filter if it is not a network message,
  // or if it is a non-xhr one,
  // or if the xhr filter is on.
  return (
    message.source !== MESSAGE_SOURCE.NETWORK ||
    message.isXHR === false ||
    filters.get(FILTERS.NETXHR) === true
  );
}

/**
 * Returns true if the message shouldn't be hidden because of levels filter state.
 *
 * @param {Object} message - The message to check the filter against.
 * @param {FilterState} filters - redux "filters" state.
 * @returns {Boolean}
 */
function passLevelFilters(message, filters) {
  // The message passes the filter if it is not a console call,
  // or if its level matches the state of the corresponding filter.
  return (
    (message.source !== MESSAGE_SOURCE.CONSOLE_API &&
    message.source !== MESSAGE_SOURCE.JAVASCRIPT) ||
    filters.get(message.level) === true
  );
}

/**
 * Returns true if the message shouldn't be hidden because of the CSS filter state.
 *
 * @param {Object} message - The message to check the filter against.
 * @param {FilterState} filters - redux "filters" state.
 * @returns {Boolean}
 */
function passCssFilters(message, filters) {
  // The message passes the filter if it is not a CSS message,
  // or if the CSS filter is on.
  return (
    message.source !== MESSAGE_SOURCE.CSS ||
    filters.get("css") === true
  );
}

/**
 * Returns true if the message shouldn't be hidden because of search filter state.
 *
 * @param {Object} message - The message to check the filter against.
 * @param {FilterState} filters - redux "filters" state.
 * @returns {Boolean}
 */
function passSearchFilters(message, filters) {
  let text = (filters.get("text") || "").trim();

  // If there is no search, the message passes the filter.
  if (!text) {
    return true;
  }

  return (
    // Look for a match in parameters.
    isTextInParameters(text, message.parameters)
    // Look for a match in location.
    || isTextInFrame(text, message.frame)
    // Look for a match in net events.
    || isTextInNetEvent(text, message.request)
    // Look for a match in stack-trace.
    || isTextInStackTrace(text, message.stacktrace)
    // Look for a match in messageText.
    || isTextInMessageText(text, message.messageText)
    // Look for a match in notes.
    || isTextInNotes(text, message.notes)
  );
}

/**
* Returns true if given text is included in provided stack frame.
*/
function isTextInFrame(text, frame) {
  if (!frame) {
    return false;
  }

  const {
    functionName,
    line,
    column,
    source
  } = frame;
  const { short } = getSourceNames(source);

  return `${functionName ? functionName + " " : ""}${short}:${line}:${column}`
    .toLocaleLowerCase()
    .includes(text.toLocaleLowerCase());
}

/**
* Returns true if given text is included in provided parameters.
*/
function isTextInParameters(text, parameters) {
  if (!parameters) {
    return false;
  }

  text = text.toLocaleLowerCase();
  return getAllProps(parameters).some(prop =>
    (prop + "").toLocaleLowerCase().includes(text)
  );
}

/**
* Returns true if given text is included in provided net event grip.
*/
function isTextInNetEvent(text, request) {
  if (!request) {
    return false;
  }

  text = text.toLocaleLowerCase();

  let method = request.method.toLocaleLowerCase();
  let url = request.url.toLocaleLowerCase();
  return method.includes(text) || url.includes(text);
}

/**
* Returns true if given text is included in provided stack trace.
*/
function isTextInStackTrace(text, stacktrace) {
  if (!Array.isArray(stacktrace)) {
    return false;
  }

  // isTextInFrame expect the properties of the frame object to be in the same
  // order they are rendered in the Frame component.
  return stacktrace.some(frame => isTextInFrame(text, {
    functionName: frame.functionName || l10n.getStr("stacktrace.anonymousFunction"),
    source: frame.filename,
    lineNumber: frame.lineNumber,
    columnNumber: frame.columnNumber
  }));
}

/**
* Returns true if given text is included in `messageText` field.
*/
function isTextInMessageText(text, messageText) {
  if (!messageText) {
    return false;
  }

  return messageText.toLocaleLowerCase().includes(text.toLocaleLowerCase());
}

/**
* Returns true if given text is included in notes.
*/
function isTextInNotes(text, notes) {
  if (!Array.isArray(notes)) {
    return false;
  }

  return notes.some(note =>
    // Look for a match in location.
    isTextInFrame(text, note.frame) ||
    // Look for a match in messageBody.
    (
      note.messageBody &&
      note.messageBody.toLocaleLowerCase().includes(text.toLocaleLowerCase())
    )
  );
}

/**
 * Get a flat array of all the grips and their properties.
 *
 * @param {Array} Grips
 * @return {Array} Flat array of the grips and their properties.
 */
function getAllProps(grips) {
  let result = grips.reduce((res, grip) => {
    let previewItems = getGripPreviewItems(grip);
    let allProps = previewItems.length > 0 ? getAllProps(previewItems) : [];
    return [...res, grip, grip.class, ...allProps];
  }, []);

  // We are interested only in primitive props (to search for)
  // not in objects and undefined previews.
  result = result.filter(grip =>
    typeof grip != "object" &&
    typeof grip != "undefined"
  );

  return [...new Set(result)];
}

function getDefaultFiltersCounter() {
  const count = DEFAULT_FILTERS.reduce((res, filter) => {
    res[filter] = 0;
    return res;
  }, {});
  count.global = 0;
  return count;
}

exports.messages = messages;
