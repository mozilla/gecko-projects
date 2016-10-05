/* -*- indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim: set ft=javascript ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

// React & Redux
const {
  createClass,
  createFactory,
  DOM: dom,
  PropTypes
} = require("devtools/client/shared/vendor/react");
const actions = require("devtools/client/webconsole/new-console-output/actions/index");
const CollapseButton = createFactory(require("devtools/client/webconsole/new-console-output/components/collapse-button"));
const MessageIcon = createFactory(require("devtools/client/webconsole/new-console-output/components/message-icon"));
const MessageRepeat = createFactory(require("devtools/client/webconsole/new-console-output/components/message-repeat"));
const FrameView = createFactory(require("devtools/client/shared/components/frame"));
const StackTrace = createFactory(require("devtools/client/shared/components/stack-trace"));

const Message = createClass({
  displayName: "Message",

  propTypes: {
    open: PropTypes.bool,
    source: PropTypes.string.isRequired,
    type: PropTypes.string.isRequired,
    level: PropTypes.string.isRequired,
    topLevelClasses: PropTypes.array.isRequired,
    messageBody: PropTypes.any.isRequired,
    repeat: PropTypes.any,
    frame: PropTypes.any,
    attachment: PropTypes.any,
    stacktrace: PropTypes.any,
    messageId: PropTypes.string,
    scrollToMessage: PropTypes.bool,
    serviceContainer: PropTypes.shape({
      emitNewMessage: PropTypes.func.isRequired,
      onViewSourceInDebugger: PropTypes.func.isRequired,
      sourceMapService: PropTypes.any,
    }),
  },

  componentDidMount() {
    if (this.messageNode) {
      if (this.props.scrollToMessage) {
        this.messageNode.scrollIntoView();
      }
      // Event used in tests. Some message types don't pass it in because existing tests
      // did not emit for them.
      if (this.props.serviceContainer) {
        this.props.serviceContainer.emitNewMessage(this.messageNode, this.props.messageId);
      }
    }
  },

  render() {
    const {
      messageId,
      open,
      source,
      type,
      level,
      topLevelClasses,
      messageBody,
      frame,
      stacktrace,
      serviceContainer,
      dispatch,
    } = this.props;

    topLevelClasses.push("message", source, type, level);
    if (open) {
      topLevelClasses.push("open");
    }

    const icon = MessageIcon({level});

    // Figure out if there is an expandable part to the message.
    let attachment = null;
    if (this.props.attachment) {
      attachment = this.props.attachment;
    } else if (stacktrace) {
      const child = open ? StackTrace({
        stacktrace: stacktrace,
        onViewSourceInDebugger: serviceContainer.onViewSourceInDebugger
      }) : null;
      attachment = dom.div({ className: "stacktrace devtools-monospace" }, child);
    }

    // If there is an expandable part, make it collapsible.
    let collapse = null;
    if (attachment) {
      collapse = CollapseButton({
        open,
        onClick: function () {
          if (open) {
            dispatch(actions.messageClose(messageId));
          } else {
            dispatch(actions.messageOpen(messageId));
          }
        },
      });
    }

    const repeat = this.props.repeat ? MessageRepeat({repeat: this.props.repeat}) : null;

    // Configure the location.
    const shouldRenderFrame = frame && frame.source !== "debugger eval code";
    const location = dom.span({ className: "message-location devtools-monospace" },
      shouldRenderFrame ? FrameView({
        frame,
        onClick: serviceContainer.onViewSourceInDebugger,
        showEmptyPathAsHost: true,
        sourceMapService: serviceContainer.sourceMapService
      }) : null
    );

    return dom.div({
      className: topLevelClasses.join(" "),
      ref: node => {
        this.messageNode = node;
      }
    },
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
});

module.exports = Message;
