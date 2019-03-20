/* -*- indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {arg, DebuggerClient} = require("devtools/shared/client/debugger-client");
const eventSource = require("devtools/shared/client/event-source");
const {ThreadStateTypes} = require("devtools/shared/client/constants");

loader.lazyRequireGetter(this, "ArrayBufferClient", "devtools/shared/client/array-buffer-client");
loader.lazyRequireGetter(this, "LongStringClient", "devtools/shared/client/long-string-client");
loader.lazyRequireGetter(this, "ObjectClient", "devtools/shared/client/object-client");
loader.lazyRequireGetter(this, "SourceClient", "devtools/shared/client/source-client");

/**
 * Creates a thread client for the remote debugging protocol server. This client
 * is a front to the thread actor created in the server side, hiding the
 * protocol details in a traditional JavaScript API.
 *
 * @param client DebuggerClient
 * @param actor string
 *        The actor ID for this thread.
 */
function ThreadClient(client, actor) {
  this.client = client;
  this._actor = actor;
  this._pauseGrips = {};
  this._threadGrips = {};
  this.request = this.client.request;
}

ThreadClient.prototype = {
  _state: "paused",
  get state() {
    return this._state;
  },
  get paused() {
    return this._state === "paused";
  },

  _actor: null,

  get actor() {
    return this._actor;
  },

  get _transport() {
    return this.client._transport;
  },

  _assertPaused: function(command) {
    if (!this.paused) {
      throw Error(command + " command sent while not paused. Currently " + this._state);
    }
  },

  /**
   * Resume a paused thread. If the optional limit parameter is present, then
   * the thread will also pause when that limit is reached.
   *
   * @param [optional] object limit
   *        An object with a type property set to the appropriate limit (next,
   *        step, or finish) per the remote debugging protocol specification.
   *        Use null to specify no limit.
   * @param bool aRewind
   *        Whether execution should rewind until the limit is reached, rather
   *        than proceeding forwards. This parameter has no effect if the
   *        server does not support rewinding.
   * @param function onResponse
   *        Called with the response packet.
   */
  _doResume: DebuggerClient.requester({
    type: "resume",
    resumeLimit: arg(0),
    rewind: arg(1),
  }, {
    before: function(packet) {
      this._assertPaused("resume");

      // Put the client in a tentative "resuming" state so we can prevent
      // further requests that should only be sent in the paused state.
      this._previousState = this._state;
      this._state = "resuming";

      return packet;
    },
    after: function(response) {
      if (response.error && this._state == "resuming") {
        // There was an error resuming, update the state to the new one
        // reported by the server, if given (only on wrongState), otherwise
        // reset back to the previous state.
        if (response.state) {
          this._state = ThreadStateTypes[response.state];
        } else {
          this._state = this._previousState;
        }
      }
      delete this._previousState;
      return response;
    },
  }),

  /**
   * Reconfigure the thread actor.
   *
   * @param object options
   *        A dictionary object of the new options to use in the thread actor.
   * @param function onResponse
   *        Called with the response packet.
   */
  reconfigure: DebuggerClient.requester({
    type: "reconfigure",
    options: arg(0),
  }),

  /**
   * Resume a paused thread.
   */
  resume: function(onResponse) {
    return this._doResume(null, false, onResponse);
  },

  /**
   * Resume then pause without stepping.
   *
   * @param function onResponse
   *        Called with the response packet.
   */
  resumeThenPause: function(onResponse) {
    return this._doResume({ type: "break" }, false, onResponse);
  },

  /**
   * Rewind a thread until a breakpoint is hit.
   *
   * @param function aOnResponse
   *        Called with the response packet.
   */
  rewind: function(onResponse) {
    this._doResume(null, true, onResponse);
  },

  /**
   * Step over a function call.
   *
   * @param function onResponse
   *        Called with the response packet.
   */
  stepOver: function(onResponse) {
    return this._doResume({ type: "next" }, false, onResponse);
  },

  /**
   * Step into a function call.
   *
   * @param function onResponse
   *        Called with the response packet.
   */
  stepIn: function(onResponse) {
    return this._doResume({ type: "step" }, false, onResponse);
  },

  /**
   * Step out of a function call.
   *
   * @param function onResponse
   *        Called with the response packet.
   */
  stepOut: function(onResponse) {
    return this._doResume({ type: "finish" }, false, onResponse);
  },

  /**
   * Rewind step over a function call.
   *
   * @param function aOnResponse
   *        Called with the response packet.
   */
  reverseStepOver: function(onResponse) {
    return this._doResume({ type: "next" }, true, onResponse);
  },

  /**
   * Rewind step into a function call.
   *
   * @param function aOnResponse
   *        Called with the response packet.
   */
  reverseStepIn: function(onResponse) {
    return this._doResume({ type: "step" }, true, onResponse);
  },

  /**
   * Rewind step out of a function call.
   *
   * @param function aOnResponse
   *        Called with the response packet.
   */
  reverseStepOut: function(onResponse) {
    return this._doResume({ type: "finish" }, true, onResponse);
  },

  /**
   * Immediately interrupt a running thread.
   *
   * @param function onResponse
   *        Called with the response packet.
   */
  interrupt: function(onResponse) {
    return this._doInterrupt(null, onResponse);
  },

  /**
   * Pause execution right before the next JavaScript bytecode is executed.
   *
   * @param function onResponse
   *        Called with the response packet.
   */
  breakOnNext: function(onResponse) {
    return this._doInterrupt("onNext", onResponse);
  },

  /**
   * Warp through time to an execution point in the past or future.
   *
   * @param object aTarget
   *        Description of the warp destination.
   * @param function aOnResponse
   *        Called with the response packet.
   */
  timeWarp: function(target, onResponse) {
    const warp = () => {
      this._doResume({ type: "warp", target }, true, onResponse);
    };
    if (this.paused) {
      warp();
    } else {
      this.interrupt(warp);
    }
  },

  /**
   * Interrupt a running thread.
   *
   * @param function onResponse
   *        Called with the response packet.
   */
  _doInterrupt: DebuggerClient.requester({
    type: "interrupt",
    when: arg(0),
  }),

  /**
   * Enable or disable pausing when an exception is thrown.
   *
   * @param boolean pauseOnExceptions
   *        Enables pausing if true, disables otherwise.
   * @param boolean ignoreCaughtExceptions
   *        Whether to ignore caught exceptions
   * @param function onResponse
   *        Called with the response packet.
   */
  pauseOnExceptions: DebuggerClient.requester({
    type: "pauseOnExceptions",
    pauseOnExceptions: arg(0),
    ignoreCaughtExceptions: arg(1),
  }),

  /**
   * Detach from the thread actor.
   *
   * @param function onResponse
   *        Called with the response packet.
   */
  detach: DebuggerClient.requester({
    type: "detach",
  }, {
    after: function(response) {
      this.client.unregisterClient(this);
      return response;
    },
  }),

  /**
   * Promote multiple pause-lifetime object actors to thread-lifetime ones.
   *
   * @param array actors
   *        An array with actor IDs to promote.
   */
  threadGrips: DebuggerClient.requester({
    type: "threadGrips",
    actors: arg(0),
  }),

  /**
   * Request the loaded sources for the current thread.
   *
   * @param onResponse Function
   *        Called with the thread's response.
   */
  getSources: DebuggerClient.requester({
    type: "sources",
  }),

  /**
   * Request frames from the callstack for the current thread.
   *
   * @param start integer
   *        The number of the youngest stack frame to return (the youngest
   *        frame is 0).
   * @param count integer
   *        The maximum number of frames to return, or null to return all
   *        frames.
   * @param onResponse function
   *        Called with the thread's response.
   */
  getFrames: DebuggerClient.requester({
    type: "frames",
    start: arg(0),
    count: arg(1),
  }),

  /**
   * Toggle pausing via breakpoints in the server.
   *
   * @param skip boolean
   *        Whether the server should skip pausing via breakpoints
   */
  skipBreakpoints: DebuggerClient.requester({
    type: "skipBreakpoints",
    skip: arg(0),
  }),

  /**
   * Request the frame environment.
   *
   * @param frameId string
   */
  getEnvironment: function(frameId) {
    return this.request({ to: frameId, type: "getEnvironment" });
  },

  /**
   * Return a ObjectClient object for the given object grip.
   *
   * @param grip object
   *        A pause-lifetime object grip returned by the protocol.
   */
  pauseGrip: function(grip) {
    if (grip.actor in this._pauseGrips) {
      return this._pauseGrips[grip.actor];
    }

    const client = new ObjectClient(this.client, grip);
    this._pauseGrips[grip.actor] = client;
    return client;
  },

  /**
   * Get or create a long string client, checking the grip client cache if it
   * already exists.
   *
   * @param grip Object
   *        The long string grip returned by the protocol.
   * @param gripCacheName String
   *        The property name of the grip client cache to check for existing
   *        clients in.
   */
  _longString: function(grip, gripCacheName) {
    if (grip.actor in this[gripCacheName]) {
      return this[gripCacheName][grip.actor];
    }

    const client = new LongStringClient(this.client, grip);
    this[gripCacheName][grip.actor] = client;
    return client;
  },

  /**
   * Return an instance of LongStringClient for the given long string grip that
   * is scoped to the current pause.
   *
   * @param grip Object
   *        The long string grip returned by the protocol.
   */
  pauseLongString: function(grip) {
    return this._longString(grip, "_pauseGrips");
  },

  /**
   * Return an instance of LongStringClient for the given long string grip that
   * is scoped to the thread lifetime.
   *
   * @param grip Object
   *        The long string grip returned by the protocol.
   */
  threadLongString: function(grip) {
    return this._longString(grip, "_threadGrips");
  },

  /**
   * Get or create an ArrayBuffer client, checking the grip client cache if it
   * already exists.
   *
   * @param grip Object
   *        The ArrayBuffer grip returned by the protocol.
   * @param gripCacheName String
   *        The property name of the grip client cache to check for existing
   *        clients in.
   */
  _arrayBuffer: function(grip, gripCacheName) {
    if (grip.actor in this[gripCacheName]) {
      return this[gripCacheName][grip.actor];
    }

    const client = new ArrayBufferClient(this.client, grip);
    this[gripCacheName][grip.actor] = client;
    return client;
  },

  /**
   * Return an instance of ArrayBufferClient for the given ArrayBuffer grip that
   * is scoped to the thread lifetime.
   *
   * @param grip Object
   *        The ArrayBuffer grip returned by the protocol.
   */
  threadArrayBuffer: function(grip) {
    return this._arrayBuffer(grip, "_threadGrips");
  },

  /**
   * Clear and invalidate all the grip clients from the given cache.
   *
   * @param gripCacheName
   *        The property name of the grip cache we want to clear.
   */
  _clearObjectClients: function(gripCacheName) {
    for (const id in this[gripCacheName]) {
      this[gripCacheName][id].valid = false;
    }
    this[gripCacheName] = {};
  },

  /**
   * Invalidate pause-lifetime grip clients and clear the list of current grip
   * clients.
   */
  _clearPauseGrips: function() {
    this._clearObjectClients("_pauseGrips");
  },

  /**
   * Invalidate thread-lifetime grip clients and clear the list of current grip
   * clients.
   */
  _clearThreadGrips: function() {
    this._clearObjectClients("_threadGrips");
  },

  /**
   * Handle thread state change by doing necessary cleanup and notifying all
   * registered listeners.
   */
  _onThreadState: function(packet) {
    this._state = ThreadStateTypes[packet.type];
    // The debugger UI may not be initialized yet so we want to keep
    // the packet around so it knows what to pause state to display
    // when it's initialized
    this._lastPausePacket = packet.type === "resumed" ? null : packet;
    this._clearPauseGrips();
    packet.type === ThreadStateTypes.detached && this._clearThreadGrips();
    this.client._eventsEnabled && this.emit(packet.type, packet);
  },

  getLastPausePacket: function() {
    return this._lastPausePacket;
  },

  setBreakpoint: DebuggerClient.requester({
    type: "setBreakpoint",
    location: arg(0),
    options: arg(1),
  }),

  removeBreakpoint: DebuggerClient.requester({
    type: "removeBreakpoint",
    location: arg(0),
  }),

  /**
   * Requests to set XHR breakpoint
   * @param string path
   *        pause when url contains `path`
   * @param string method
   *        pause when method of request is `method`
   */
  setXHRBreakpoint: DebuggerClient.requester({
    type: "setXHRBreakpoint",
    path: arg(0),
    method: arg(1),
  }),

  /**
   * Request to remove XHR breakpoint
   * @param string path
   * @param string method
   */
  removeXHRBreakpoint: DebuggerClient.requester({
    type: "removeXHRBreakpoint",
    path: arg(0),
    method: arg(1),
  }),

  /**
   * Request to get the set of available event breakpoints.
   */
  getAvailableEventBreakpoints: DebuggerClient.requester({
    type: "getAvailableEventBreakpoints",
  }),

  /**
   * Request to get the IDs of the active event breakpoints.
   */
  getActiveEventBreakpoints: DebuggerClient.requester({
    type: "getActiveEventBreakpoints",
  }),

  /**
   * Request to set the IDs of the active event breakpoints.
   */
  setActiveEventBreakpoints: DebuggerClient.requester({
    type: "setActiveEventBreakpoints",
    ids: arg(0),
  }),

  /**
   * Return an instance of SourceClient for the given source actor form.
   */
  source: function(form) {
    if (form.actor in this._threadGrips) {
      return this._threadGrips[form.actor];
    }

    this._threadGrips[form.actor] = new SourceClient(this, form);
    return this._threadGrips[form.actor];
  },

  events: ["newSource", "progress"],
};

eventSource(ThreadClient.prototype);

module.exports = ThreadClient;
