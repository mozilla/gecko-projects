/* -*- js-indent-level: 2; indent-tabs-mode: nil -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
"use strict";

const Services = require("Services");
const { createElement, createFactory } = require("devtools/client/shared/vendor/react");
const ReactDOM = require("devtools/client/shared/vendor/react-dom");
const { Provider } = require("devtools/client/shared/vendor/react-redux");

const actions = require("devtools/client/webconsole/actions/index");
const { createEditContextMenu } = require("devtools/client/framework/toolbox-context-menu");
const { createContextMenu } = require("devtools/client/webconsole/utils/context-menu");
const { configureStore } = require("devtools/client/webconsole/store");

const { isPacketPrivate } = require("devtools/client/webconsole/utils/messages");
const { getAllMessagesById, getMessage } = require("devtools/client/webconsole/selectors/messages");
const Telemetry = require("devtools/client/shared/telemetry");

const EventEmitter = require("devtools/shared/event-emitter");
const App = createFactory(require("devtools/client/webconsole/components/App"));
const ObjectClient = require("devtools/shared/client/object-client");
const LongStringClient = require("devtools/shared/client/long-string-client");
loader.lazyRequireGetter(this, "Constants", "devtools/client/webconsole/constants");
loader.lazyRequireGetter(this, "getElementText", "devtools/client/webconsole/utils/clipboard", true);

let store = null;

class WebConsoleWrapper {
  /**
   *
   * @param {HTMLElement} parentNode
   * @param {WebConsoleUI} webConsoleUI
   * @param {Toolbox} toolbox
   * @param {Document} document
   */
  constructor(parentNode, webConsoleUI, toolbox, document) {
    EventEmitter.decorate(this);

    this.parentNode = parentNode;
    this.webConsoleUI = webConsoleUI;
    this.toolbox = toolbox;
    this.hud = this.webConsoleUI.hud;
    this.document = document;

    this.init = this.init.bind(this);

    this.queuedMessageAdds = [];
    this.queuedMessageUpdates = [];
    this.queuedRequestUpdates = [];
    this.throttledDispatchPromise = null;

    this.telemetry = new Telemetry();
  }

  init() {
    return new Promise((resolve) => {
      const attachRefToWebConsoleUI = (id, node) => {
        this.webConsoleUI[id] = node;
      };
      const { webConsoleUI } = this;
      const debuggerClient = this.hud.target.client;

      const serviceContainer = {
        attachRefToWebConsoleUI,
        emitNewMessage: (node, messageId, timeStamp) => {
          webConsoleUI.emit("new-messages", new Set([{
            node,
            messageId,
            timeStamp,
          }]));
        },
        proxy: webConsoleUI.proxy,
        openLink: (url, e) => {
          webConsoleUI.hud.openLink(url, e);
        },
        canRewind: () => {
          if (!(
            webConsoleUI.hud
            && webConsoleUI.hud.target
            && webConsoleUI.hud.target.traits
          )) {
            return false;
          }

          return webConsoleUI.hud.target.traits.canRewind;
        },
        createElement: nodename => {
          return this.document.createElement(nodename);
        },
        getLongString: (grip) => {
          return webConsoleUI.proxy.webConsoleClient.getString(grip);
        },
        requestData(id, type) {
          return webConsoleUI.proxy.networkDataProvider.requestData(id, type);
        },
        onViewSource(frame) {
          if (webConsoleUI && webConsoleUI.hud && webConsoleUI.hud.viewSource) {
            webConsoleUI.hud.viewSource(frame.url, frame.line);
          }
        },
        recordTelemetryEvent: (eventName, extra = {}) => {
          this.telemetry.recordEvent(eventName, "webconsole", null, {
            ...extra,
            "session_id": this.toolbox && this.toolbox.sessionId || -1,
          });
        },
        createObjectClient: (object) => {
          return new ObjectClient(debuggerClient, object);
        },

        createLongStringClient: (object) => {
          return new LongStringClient(debuggerClient, object);
        },

        releaseActor: (actor) => {
          if (!actor) {
            return null;
          }

          return debuggerClient.release(actor);
        },

        getWebConsoleClient: () => {
          return webConsoleUI.webConsoleClient;
        },

        /**
         * Retrieve the FrameActor ID given a frame depth, or the selected one if no
         * frame depth given.
         *
         * @param {Number} frame: optional frame depth.
         * @return {String|null}: The FrameActor ID for the given frame depth (or the
         *                        selected frame if it exists).
         */
        getFrameActor: (frame = null) => {
          const state = this.hud.getDebuggerFrames();
          if (!state) {
            return null;
          }

          const grip = Number.isInteger(frame)
            ? state.frames[frame]
            : state.frames[state.selected];
          return grip ? grip.actor : null;
        },

        inputHasSelection: () => {
          const {editor, inputNode} = webConsoleUI.jsterm || {};
          return editor
            ? !!editor.getSelection()
            : (inputNode && inputNode.selectionStart !== inputNode.selectionEnd);
        },

        getInputValue: () => {
          return this.hud.getInputValue();
        },

        setInputValue: (value) => {
          this.hud.setInputValue(value);
        },

        focusInput: () => {
          return webConsoleUI.jsterm && webConsoleUI.jsterm.focus();
        },

        evaluateInput: (expression) => {
          return webConsoleUI.jsterm && webConsoleUI.jsterm.execute(expression);
        },

        getInputCursor: () => {
          return webConsoleUI.jsterm && webConsoleUI.jsterm.getSelectionStart();
        },

        getSelectedNodeActor: () => {
          const inspectorSelection = this.hud.getInspectorSelection();
          if (inspectorSelection && inspectorSelection.nodeFront) {
            return inspectorSelection.nodeFront.actorID;
          }
          return null;
        },

        getJsTermTooltipAnchor: () => {
          if (jstermCodeMirror) {
            return webConsoleUI.jsterm.node.querySelector(".CodeMirror-cursor");
          }
          return webConsoleUI.jsterm.completeNode;
        },
      };

      // Set `openContextMenu` this way so, `serviceContainer` variable
      // is available in the current scope and we can pass it into
      // `createContextMenu` method.
      serviceContainer.openContextMenu = (e, message) => {
        const { screenX, screenY, target } = e;

        const messageEl = target.closest(".message");
        const clipboardText = getElementText(messageEl);

        const linkEl = target.closest("a[href]");
        const url = linkEl && linkEl.href;

        const messageVariable = target.closest(".objectBox");
        // Ensure that console.group and console.groupCollapsed commands are not captured
        const variableText = (messageVariable
          && !(messageEl.classList.contains("startGroup"))
          && !(messageEl.classList.contains("startGroupCollapsed")))
            ? messageVariable.textContent : null;

        // Retrieve closes actor id from the DOM.
        const actorEl = target.closest("[data-link-actor-id]") ||
                      target.querySelector("[data-link-actor-id]");
        const actor = actorEl ? actorEl.dataset.linkActorId : null;

        const rootObjectInspector = target.closest(".object-inspector");
        const rootActor = rootObjectInspector ?
                        rootObjectInspector.querySelector("[data-link-actor-id]") : null;
        const rootActorId = rootActor ? rootActor.dataset.linkActorId : null;

        const sidebarTogglePref = store.getState().prefs.sidebarToggle;
        const openSidebar = sidebarTogglePref ? (messageId) => {
          store.dispatch(actions.showMessageObjectInSidebar(rootActorId, messageId));
        } : null;

        const messageData = getMessage(store.getState(), message.messageId);
        const executionPoint = messageData && messageData.executionPoint;

        const menu = createContextMenu(this.webConsoleUI, this.parentNode, {
          actor,
          clipboardText,
          variableText,
          message,
          serviceContainer,
          openSidebar,
          rootActorId,
          executionPoint,
          toolbox: this.toolbox,
          url,
        });

        // Emit the "menu-open" event for testing.
        menu.once("open", () => this.emit("menu-open"));
        menu.popup(screenX, screenY, { doc: this.hud.chromeWindow.document });

        return menu;
      };

      serviceContainer.openEditContextMenu = (e) => {
        const { screenX, screenY } = e;
        const menu = createEditContextMenu(window, "webconsole-menu");
        // Emit the "menu-open" event for testing.
        menu.once("open", () => this.emit("menu-open"));
        menu.popup(screenX, screenY, { doc: this.hud.chromeWindow.document });

        return menu;
      };

      if (this.toolbox) {
        this.toolbox.threadClient.addListener("paused", this.dispatchPaused.bind(this));
        this.toolbox.threadClient.addListener(
          "progress", this.dispatchProgress.bind(this));

        Object.assign(serviceContainer, {
          onViewSourceInDebugger: frame => {
            this.toolbox.viewSourceInDebugger(
              frame.url, frame.line, frame.sourceId
            ).then(() => {
              this.telemetry.recordEvent(
                "jump_to_source", "webconsole",
                null, { "session_id": this.toolbox.sessionId }
              );
              this.webConsoleUI.emit("source-in-debugger-opened");
            });
          },
          onViewSourceInScratchpad: frame => this.toolbox.viewSourceInScratchpad(
            frame.url,
            frame.line
          ).then(() => {
            this.telemetry.recordEvent("jump_to_source", "webconsole",
                                       null, { "session_id": this.toolbox.sessionId }
            );
          }),
          onViewSourceInStyleEditor: frame => this.toolbox.viewSourceInStyleEditor(
            frame.url,
            frame.line,
            frame.column
          ).then(() => {
            this.telemetry.recordEvent("jump_to_source", "webconsole",
                                       null, { "session_id": this.toolbox.sessionId }
            );
          }),
          openNetworkPanel: (requestId) => {
            return this.toolbox.selectTool("netmonitor").then((panel) => {
              return panel.panelWin.Netmonitor.inspectRequest(requestId);
            });
          },
          sourceMapService: this.toolbox ? this.toolbox.sourceMapURLService : null,
          highlightDomElement: async (grip, options = {}) => {
            await this.toolbox.initInspector();
            if (!this.toolbox.highlighter) {
              return null;
            }
            const nodeFront = await this.toolbox.walker.gripToNodeFront(grip);
            return this.toolbox.highlighter.highlight(nodeFront, options);
          },
          unHighlightDomElement: (forceHide = false) => {
            return this.toolbox.highlighter
              ? this.toolbox.highlighter.unhighlight(forceHide)
              : null;
          },
          openNodeInInspector: async (grip) => {
            await this.toolbox.initInspector();
            const onSelectInspector = this.toolbox.selectTool("inspector", "inspect_dom");
            const onGripNodeToFront = this.toolbox.walker.gripToNodeFront(grip);
            const [
              front,
              inspector,
            ] = await Promise.all([onGripNodeToFront, onSelectInspector]);

            const onInspectorUpdated = inspector.once("inspector-updated");
            const onNodeFrontSet = this.toolbox.selection
              .setNodeFront(front, { reason: "console" });

            return Promise.all([onNodeFrontSet, onInspectorUpdated]);
          },
          jumpToExecutionPoint: executionPoint =>
            this.toolbox.threadClient.timeWarp(executionPoint),

          onMessageHover: (type, messageId) => {
            const message = getMessage(store.getState(), messageId);
            this.webConsoleUI.emit("message-hover", type, message);
          },
        });
      }

      store = configureStore(this.webConsoleUI, {
        // We may not have access to the toolbox (e.g. in the browser console).
        sessionId: this.toolbox && this.toolbox.sessionId || -1,
        telemetry: this.telemetry,
        services: serviceContainer,
      });

      const {prefs} = store.getState();
      const jstermCodeMirror = prefs.jstermCodeMirror
        && !Services.appinfo.accessibilityEnabled;

      const app = App({
        attachRefToWebConsoleUI,
        serviceContainer,
        webConsoleUI,
        onFirstMeaningfulPaint: resolve,
        closeSplitConsole: this.closeSplitConsole.bind(this),
        jstermCodeMirror,
      });

      // Render the root Application component.
      if (this.parentNode) {
        const provider = createElement(Provider, { store }, app);
        this.body = ReactDOM.render(provider, this.parentNode);
      } else {
        // If there's no parentNode, we are in a test. So we can resolve immediately.
        resolve();
      }
    });
  }

  dispatchMessageAdd(packet, waitForResponse) {
    // Wait for the message to render to resolve with the DOM node.
    // This is just for backwards compatibility with old tests, and should
    // be removed once it's not needed anymore.
    // Can only wait for response if the action contains a valid message.
    let promise;
    // Also, do not expect any update while the panel is in background.
    if (waitForResponse && document.visibilityState === "visible") {
      const timeStampToMatch = packet.message
        ? packet.message.timeStamp
        : packet.timestamp;

      promise = new Promise(resolve => {
        this.webConsoleUI.on("new-messages", function onThisMessage(messages) {
          for (const m of messages) {
            if (m.timeStamp === timeStampToMatch) {
              resolve(m.node);
              this.webConsoleUI.off("new-messages", onThisMessage);
              return;
            }
          }
        }.bind(this));
      });
    } else {
      promise = Promise.resolve();
    }

    this.batchedMessageAdd(packet);
    return promise;
  }

  dispatchMessagesAdd(messages) {
    this.batchedMessagesAdd(messages);
  }

  dispatchMessagesClear() {
    // We might still have pending message additions and updates when the clear action is
    // triggered, so we need to flush them to make sure we don't have unexpected behavior
    // in the ConsoleOutput.
    this.queuedMessageAdds = [];
    this.queuedMessageUpdates = [];
    this.queuedRequestUpdates = [];
    store.dispatch(actions.messagesClear());
    this.webConsoleUI.emit("messages-cleared");
  }

  dispatchPrivateMessagesClear() {
    // We might still have pending private message additions when the private messages
    // clear action is triggered. We need to remove any private-window-issued packets from
    // the queue so they won't appear in the output.

    // For (network) message updates, we need to check both messages queue and the state
    // since we can receive updates even if the message isn't rendered yet.
    const messages = [...getAllMessagesById(store.getState()).values()];
    this.queuedMessageUpdates = this.queuedMessageUpdates.filter(({networkInfo}) => {
      const { actor } = networkInfo;

      const queuedNetworkMessage = this.queuedMessageAdds.find(p => p.actor === actor);
      if (queuedNetworkMessage && isPacketPrivate(queuedNetworkMessage)) {
        return false;
      }

      const requestMessage = messages.find(message => actor === message.actor);
      if (requestMessage && requestMessage.private === true) {
        return false;
      }

      return true;
    });

    // For (network) requests updates, we can check only the state, since there must be a
    // user interaction to get an update (i.e. the network message is displayed and thus
    // in the state).
    this.queuedRequestUpdates = this.queuedRequestUpdates.filter(({id}) => {
      const requestMessage = getMessage(store.getState(), id);
      if (requestMessage && requestMessage.private === true) {
        return false;
      }

      return true;
    });

    // Finally we clear the messages queue. This needs to be done here since we use it to
    // clean the other queues.
    this.queuedMessageAdds = this.queuedMessageAdds.filter(p => !isPacketPrivate(p));

    store.dispatch(actions.privateMessagesClear());
  }

  dispatchTimestampsToggle(enabled) {
    store.dispatch(actions.timestampsToggle(enabled));
  }

  dispatchPaused(_, packet) {
    if (packet.executionPoint) {
      store.dispatch(actions.setPauseExecutionPoint(packet.executionPoint));
    }
  }

  dispatchProgress(_, packet) {
    const {executionPoint, recording} = packet;
    const point = recording ? null : executionPoint;
    store.dispatch(actions.setPauseExecutionPoint(point));
  }

  dispatchMessageUpdate(message, res) {
    // network-message-updated will emit when all the update message arrives.
    // Since we can't ensure the order of the network update, we check
    // that networkInfo.updates has all we need.
    // Note that 'requestPostData' is sent only for POST requests, so we need
    // to count with that.
    // 'fetchCacheDescriptor' will also cause a network update and increment
    // the number of networkInfo.updates
    const NUMBER_OF_NETWORK_UPDATE = 8;

    let expectedLength = NUMBER_OF_NETWORK_UPDATE;
    if (this.webConsoleUI.webConsoleClient.traits.fetchCacheDescriptor
      && res.networkInfo.updates.includes("responseCache")) {
      expectedLength++;
    }
    if (res.networkInfo.updates.includes("requestPostData")) {
      expectedLength++;
    }

    if (res.networkInfo.updates.length === expectedLength) {
      this.batchedMessageUpdates({ res, message });
    }
  }

  dispatchRequestUpdate(id, data) {
    this.batchedRequestUpdates({ id, data });
  }

  dispatchSidebarClose() {
    store.dispatch(actions.sidebarClose());
  }

  dispatchSplitConsoleCloseButtonToggle() {
    store.dispatch(actions.splitConsoleCloseButtonToggle(
      this.toolbox && this.toolbox.currentToolId !== "webconsole"));
  }

  dispatchTabWillNavigate(packet) {
    const { ui } = store.getState();

    // For the browser console, we receive tab navigation
    // when the original top level window we attached to is closed,
    // but we don't want to reset console history and just switch to
    // the next available window.
    if (ui.persistLogs || this.webConsoleUI.isBrowserConsole) {
      // Add a type in order for this event packet to be identified by
      // utils/messages.js's `transformPacket`
      packet.type = "will-navigate";
      this.dispatchMessageAdd(packet);
    } else {
      this.webConsoleUI.webConsoleClient.clearNetworkRequests();
      this.dispatchMessagesClear();
      store.dispatch({
        type: Constants.WILL_NAVIGATE,
      });
    }
  }

  batchedMessageUpdates(info) {
    this.queuedMessageUpdates.push(info);
    this.setTimeoutIfNeeded();
  }

  batchedRequestUpdates(message) {
    this.queuedRequestUpdates.push(message);
    this.setTimeoutIfNeeded();
  }

  batchedMessageAdd(message) {
    this.queuedMessageAdds.push(message);
    this.setTimeoutIfNeeded();
  }

  batchedMessagesAdd(messages) {
    this.queuedMessageAdds = this.queuedMessageAdds.concat(messages);
    this.setTimeoutIfNeeded();
  }

  dispatchClearLogpointMessages(logpointId) {
    store.dispatch(actions.messagesClearLogpoint(logpointId));
  }

  dispatchClearHistory() {
    store.dispatch(actions.clearHistory());
  }

  /**
   * Returns a Promise that resolves once any async dispatch is finally dispatched.
   */
  waitAsyncDispatches() {
    if (!this.throttledDispatchPromise) {
      return Promise.resolve();
    }
    return this.throttledDispatchPromise;
  }

  setTimeoutIfNeeded() {
    if (this.throttledDispatchPromise) {
      return;
    }

    this.throttledDispatchPromise = new Promise(done => {
      setTimeout(() => {
        this.throttledDispatchPromise = null;

        if (!store) {
          // The store is not initialized yet, we can call setTimeoutIfNeeded so the
          // messages will be handled in the next timeout when the store is ready.
          this.setTimeoutIfNeeded();
          return;
        }

        store.dispatch(actions.messagesAdd(this.queuedMessageAdds));

        const length = this.queuedMessageAdds.length;

        // This telemetry event is only useful when we have a toolbox so only
        // send it when we have one.
        if (this.toolbox) {
          this.telemetry.addEventProperty(
            this.toolbox, "enter", "webconsole", null, "message_count", length);
        }

        this.queuedMessageAdds = [];

        if (this.queuedMessageUpdates.length > 0) {
          this.queuedMessageUpdates.forEach(({ message, res }) => {
            store.dispatch(actions.networkMessageUpdate(message, null, res));
            this.webConsoleUI.emit("network-message-updated", res);
          });
          this.queuedMessageUpdates = [];
        }
        if (this.queuedRequestUpdates.length > 0) {
          this.queuedRequestUpdates.forEach(({ id, data}) => {
            store.dispatch(actions.networkUpdateRequest(id, data));
          });
          this.queuedRequestUpdates = [];

          // Fire an event indicating that all data fetched from
          // the backend has been received. This is based on
          // 'FirefoxDataProvider.isQueuePayloadReady', see more
          // comments in that method.
          // (netmonitor/src/connector/firefox-data-provider).
          // This event might be utilized in tests to find the right
          // time when to finish.
          this.webConsoleUI.emit("network-request-payload-ready");
        }
        done();
      }, 50);
    });
  }

  // Should be used for test purpose only.
  getStore() {
    return store;
  }

  subscribeToStore(callback) {
    store.subscribe(() => callback(store.getState()));
  }

  // Called by pushing close button.
  closeSplitConsole() {
    this.toolbox.closeSplitConsole();
  }
}

// Exports from this module
module.exports = WebConsoleWrapper;
