/* -*- indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim: set ft=javascript ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

// React & Redux
const {
  createFactory,
  DOM: dom,
  PropTypes
} = require("devtools/client/shared/vendor/react");
const FrameView = createFactory(require("devtools/client/shared/components/frame"));
const StackTrace = createFactory(require("devtools/client/shared/components/stack-trace"));
const GripMessageBody = createFactory(require("devtools/client/webconsole/new-console-output/components/grip-message-body").GripMessageBody);
const MessageRepeat = createFactory(require("devtools/client/webconsole/new-console-output/components/message-repeat").MessageRepeat);
const MessageIcon = createFactory(require("devtools/client/webconsole/new-console-output/components/message-icon").MessageIcon);
const CollapseButton = createFactory(require("devtools/client/webconsole/new-console-output/components/collapse-button").CollapseButton);
const ConsoleTable = createFactory(require("devtools/client/webconsole/new-console-output/components/console-table").ConsoleTable);
const actions = require("devtools/client/webconsole/new-console-output/actions/index");

ConsoleApiCall.displayName = "ConsoleApiCall";

ConsoleApiCall.propTypes = {
  message: PropTypes.object.isRequired,
  sourceMapService: PropTypes.object,
  onViewSourceInDebugger: PropTypes.func.isRequired,
  open: PropTypes.bool,
  hudProxyClient: PropTypes.object.isRequired,
};

ConsoleApiCall.defaultProps = {
  open: false
};

function ConsoleApiCall(props) {
  const {
    dispatch,
    message,
    sourceMapService,
    onViewSourceInDebugger,
    open,
    hudProxyClient,
    tableData
  } = props;
  const {source, level, stacktrace, type, frame, parameters } = message;

  let messageBody;
  if (type === "trace") {
    messageBody = dom.span({className: "cm-variable"}, "console.trace()");
  } else if (type === "assert") {
    let reps = formatReps(parameters);
    messageBody = dom.span({ className: "cm-variable" }, "Assertion failed: ", reps);
  } else if (type === "table") {
    // TODO: Chrome does not output anything, see if we want to keep this
    messageBody = dom.span({className: "cm-variable"}, "console.table()");
  } else if (parameters) {
    messageBody = formatReps(parameters);
  } else {
    messageBody = message.messageText;
  }

  const icon = MessageIcon({ level });
  const repeat = MessageRepeat({ repeat: message.repeat });
  const shouldRenderFrame = frame && frame.source !== "debugger eval code";
  const location = dom.span({ className: "message-location devtools-monospace" },
    shouldRenderFrame ? FrameView({
      frame,
      onClick: onViewSourceInDebugger,
      showEmptyPathAsHost: true,
      sourceMapService
    }) : null
  );

  let collapse = "";
  let attachment = "";
  if (stacktrace) {
    if (open) {
      attachment = dom.div({ className: "stacktrace devtools-monospace" },
        StackTrace({
          stacktrace: stacktrace,
          onViewSourceInDebugger: onViewSourceInDebugger
        })
      );
    }

    collapse = CollapseButton({
      open,
      onClick: function () {
        if (open) {
          dispatch(actions.messageClose(message.id));
        } else {
          dispatch(actions.messageOpen(message.id));
        }
      },
    });
  } else if (type === "table") {
    attachment = ConsoleTable({
      dispatch,
      id: message.id,
      hudProxyClient,
      parameters: message.parameters,
      tableData
    });
  }

  const classes = ["message", "cm-s-mozilla"];

  classes.push(source);
  classes.push(type);
  classes.push(level);

  if (open === true) {
    classes.push("open");
  }

  return dom.div({ className: classes.join(" ") },
    // @TODO add timestamp
    // @TODO add indent if necessary
    icon,
    collapse,
    dom.span({ className: "message-body-wrapper" },
      dom.span({ className: "message-flex-body" },
        dom.span({ className: "message-body devtools-monospace" },
          messageBody
        ),
        repeat,
        location
      ),
      attachment
    )
  );
}

function formatReps(parameters) {
  return (
    parameters
      // Get all the grips.
      .map((grip, key) => GripMessageBody({ grip, key }))
      // Interleave spaces.
      .reduce((arr, v, i) => {
        return i + 1 < parameters.length
          ? arr.concat(v, dom.span({}, " "))
          : arr.concat(v);
      }, [])
  );
}

module.exports.ConsoleApiCall = ConsoleApiCall;
