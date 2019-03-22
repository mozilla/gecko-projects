/* -*- indent-tabs-mode: nil; js-indent-level: 2; js-indent-level: 2 -*- */
/* vim: set ft=javascript ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const Services = require("Services");
const { Cr, Ci } = require("chrome");
const { ActorPool } = require("devtools/server/actors/common");
const { createValueGrip } = require("devtools/server/actors/object/utils");
const { longStringGrip } = require("devtools/server/actors/object/long-string");
const { ActorClassWithSpec, Actor } = require("devtools/shared/protocol");
const DevToolsUtils = require("devtools/shared/DevToolsUtils");
const { assert, dumpn } = DevToolsUtils;
const { threadSpec } = require("devtools/shared/specs/script");
const {
  getAvailableEventBreakpoints,
} = require("devtools/server/actors/utils/event-breakpoints");

loader.lazyRequireGetter(this, "EnvironmentActor", "devtools/server/actors/environment", true);
loader.lazyRequireGetter(this, "BreakpointActorMap", "devtools/server/actors/utils/breakpoint-actor-map", true);
loader.lazyRequireGetter(this, "PauseScopedObjectActor", "devtools/server/actors/pause-scoped", true);
loader.lazyRequireGetter(this, "EventLoopStack", "devtools/server/actors/utils/event-loop", true);
loader.lazyRequireGetter(this, "FrameActor", "devtools/server/actors/frame", true);
loader.lazyRequireGetter(this, "throttle", "devtools/shared/throttle", true);

/**
 * JSD2 actors.
 */

/**
 * Creates a ThreadActor.
 *
 * ThreadActors manage a JSInspector object and manage execution/inspection
 * of debuggees.
 *
 * @param aParent object
 *        This |ThreadActor|'s parent actor. It must implement the following
 *        properties:
 *          - url: The URL string of the debuggee.
 *          - window: The global window object.
 *          - preNest: Function called before entering a nested event loop.
 *          - postNest: Function called after exiting a nested event loop.
 *          - makeDebugger: A function that takes no arguments and instantiates
 *            a Debugger that manages its globals on its own.
 * @param aGlobal object [optional]
 *        An optional (for content debugging only) reference to the content
 *        window.
 */
const ThreadActor = ActorClassWithSpec(threadSpec, {
  initialize: function(parent, global) {
    Actor.prototype.initialize.call(this, parent.conn);
    this._state = "detached";
    this._frameActors = [];
    this._parent = parent;
    this._dbg = null;
    this._gripDepth = 0;
    this._threadLifetimePool = null;
    this._parentClosed = false;
    this._scripts = null;
    this._xhrBreakpoints = [];
    this._observingNetwork = false;
    this._eventBreakpoints = [];

    this._options = {
      autoBlackBox: false,
    };

    this.breakpointActorMap = new BreakpointActorMap(this);

    this._debuggerSourcesSeen = null;

    // A Set of URLs string to watch for when new sources are found by
    // the debugger instance.
    this._onLoadBreakpointURLs = new Set();

    this.global = global;

    this.onNewSourceEvent = this.onNewSourceEvent.bind(this);
    this.onUpdatedSourceEvent = this.onUpdatedSourceEvent.bind(this);

    this.uncaughtExceptionHook = this.uncaughtExceptionHook.bind(this);
    this.createCompletionGrip = this.createCompletionGrip.bind(this);
    this.onDebuggerStatement = this.onDebuggerStatement.bind(this);
    this.onNewScript = this.onNewScript.bind(this);
    this.objectGrip = this.objectGrip.bind(this);
    this.pauseObjectGrip = this.pauseObjectGrip.bind(this);
    this._onOpeningRequest = this._onOpeningRequest.bind(this);

    if (Services.obs) {
      // Set a wrappedJSObject property so |this| can be sent via the observer svc
      // for the xpcshell harness.
      this.wrappedJSObject = this;
      Services.obs.notifyObservers(this, "devtools-thread-instantiated");
    }
  },

  // Used by the ObjectActor to keep track of the depth of grip() calls.
  _gripDepth: null,

  get dbg() {
    if (!this._dbg) {
      this._dbg = this._parent.makeDebugger();
      this._dbg.uncaughtExceptionHook = this.uncaughtExceptionHook;
      this._dbg.onDebuggerStatement = this.onDebuggerStatement;
      this._dbg.onNewScript = this.onNewScript;
      if (this._dbg.replaying) {
        this._dbg.replayingOnForcedPause = this.replayingOnForcedPause.bind(this);
        const sendProgress = throttle((recording, executionPoint) => {
          if (this.attached) {
            this.conn.send({ type: "progress", from: this.actorID,
                             recording, executionPoint });
          }
        }, 100);
        this._dbg.replayingOnPositionChange =
          this.replayingOnPositionChange.bind(this, sendProgress);
      }
      // Keep the debugger disabled until a client attaches.
      this._dbg.enabled = this._state != "detached";
    }
    return this._dbg;
  },

  get globalDebugObject() {
    if (!this._parent.window || this.dbg.replaying) {
      return null;
    }
    return this.dbg.makeGlobalObjectReference(this._parent.window);
  },

  get state() {
    return this._state;
  },

  get attached() {
    return this.state == "attached" ||
           this.state == "running" ||
           this.state == "paused";
  },

  get threadLifetimePool() {
    if (!this._threadLifetimePool) {
      this._threadLifetimePool = new ActorPool(this.conn);
      this.conn.addActorPool(this._threadLifetimePool);
      this._threadLifetimePool.objectActors = new WeakMap();
    }
    return this._threadLifetimePool;
  },

  get sources() {
    return this._parent.sources;
  },

  get breakpoints() {
    return this._parent.breakpoints;
  },

  get youngestFrame() {
    if (this.state != "paused") {
      return null;
    }
    return this.dbg.getNewestFrame();
  },

  /**
   * Keep track of all of the nested event loops we use to pause the debuggee
   * when we hit a breakpoint/debugger statement/etc in one place so we can
   * resolve them when we get resume packets. We have more than one (and keep
   * them in a stack) because we can pause within client evals.
   */
  _threadPauseEventLoops: null,
  _pushThreadPause: function() {
    if (this.dbg.replaying) {
      this.dbg.replayPushThreadPause();
    }
    if (!this._threadPauseEventLoops) {
      this._threadPauseEventLoops = [];
    }
    const eventLoop = this._nestedEventLoops.push();
    this._threadPauseEventLoops.push(eventLoop);
    eventLoop.enter();
  },
  _popThreadPause: function() {
    const eventLoop = this._threadPauseEventLoops.pop();
    assert(eventLoop, "Should have an event loop.");
    eventLoop.resolve();
    if (this.dbg.replaying) {
      this.dbg.replayPopThreadPause();
    }
  },

  /**
   * Remove all debuggees and clear out the thread's sources.
   */
  clearDebuggees: function() {
    if (this._dbg) {
      this.dbg.removeAllDebuggees();
    }
    this._sources = null;
    this._scripts = null;
  },

  /**
   * Clean up listeners, debuggees and clear actor pools associated with
   * the lifetime of this actor. This does not destroy the thread actor,
   * it resets it. This is used in methods `onDetatch` and
   * `exit`. The actor is truely destroyed in the `exit method`.
   */
  destroy: function() {
    dumpn("in ThreadActor.prototype.destroy");
    if (this._state == "paused") {
      this.onResume();
    }

    this._xhrBreakpoints = [];
    this._updateNetworkObserver();

    this.sources.off("newSource", this.onNewSourceEvent);
    this.sources.off("updatedSource", this.onUpdatedSourceEvent);
    this.clearDebuggees();
    this.conn.removeActorPool(this._threadLifetimePool);
    this._threadLifetimePool = null;

    if (!this._dbg) {
      return;
    }
    this._dbg.enabled = false;
    this._dbg = null;
  },

  /**
   * destroy the debugger and put the actor in the exited state.
   */
  exit: function() {
    this.destroy();
    this._state = "exited";
    // This actor has a slightly different destroy behavior than other
    // actors using Protocol.js. The thread actor may detach but still
    // be in use, however detach calls the destroy method, even though it
    // expects the actor to still be alive. Therefore, we are calling
    // `Actor.prototype.destroy` in the `exit` method, after its state has
    // been set to "exited", where we are certain that the actor is no
    // longer in use.
    Actor.prototype.destroy.call(this);
  },

  // Request handlers
  onAttach: function(request) {
    if (this.state === "exited") {
      return { type: "exited" };
    }

    if (this.state !== "detached") {
      return { error: "wrongState",
               message: "Current state is " + this.state };
    }

    this._state = "attached";
    this._debuggerSourcesSeen = new WeakSet();

    Object.assign(this._options, request.options || {});
    this.sources.setOptions(this._options);
    this.sources.on("newSource", this.onNewSourceEvent);
    this.sources.on("updatedSource", this.onUpdatedSourceEvent);

    // Initialize an event loop stack. This can't be done in the constructor,
    // because this.conn is not yet initialized by the actor pool at that time.
    this._nestedEventLoops = new EventLoopStack({
      hooks: this._parent,
      connection: this.conn,
      thread: this,
    });

    if (request.options.breakpoints) {
      for (const { location, options } of Object.values(request.options.breakpoints)) {
        this.setBreakpoint(location, options);
      }
    }

    this.dbg.addDebuggees();
    this.dbg.enabled = true;

    if ("observeAsmJS" in this._options) {
      this.dbg.allowUnobservedAsmJS = !this._options.observeAsmJS;
    }

    // Notify the parent that we've finished attaching. If this is a worker
    // thread which was paused until attaching, this will allow content to
    // begin executing.
    if (this._parent.onThreadAttached) {
      this._parent.onThreadAttached();
    }

    try {
      // Put ourselves in the paused state.
      const packet = this._paused();
      if (!packet) {
        return { error: "notAttached" };
      }
      packet.why = { type: "attached" };

      // Send the response to the attach request now (rather than
      // returning it), because we're going to start a nested event loop
      // here.
      this.conn.send(packet);

      // Start a nested event loop.
      this._pushThreadPause();

      // We already sent a response to this request, don't send one
      // now.
      return null;
    } catch (e) {
      reportError(e);
      return { error: "notAttached", message: e.toString() };
    }
  },

  /**
   * Tell the thread to automatically add a breakpoint on the first line of
   * a given file, when it is first loaded.
   *
   * This is currently only used by the xpcshell test harness, and unless
   * we decide to expand the scope of this feature, we should keep it that way.
   */
  setBreakpointOnLoad(urls) {
    this._onLoadBreakpointURLs = new Set(urls);
  },

  _findXHRBreakpointIndex(p, m) {
    return this._xhrBreakpoints.findIndex(
      ({ path, method }) => path === p && method === m);
  },

  setBreakpoint(location, options) {
    const actor = this.breakpointActorMap.getOrCreateBreakpointActor(location);
    actor.setOptions(options);

    if (location.sourceUrl) {
      // There can be multiple source actors for a URL if there are multiple
      // inline sources on an HTML page.
      const sourceActors = this.sources.getSourceActorsByURL(location.sourceUrl);
      sourceActors.map(sourceActor => sourceActor.applyBreakpoint(actor));
    } else {
      const sourceActor = this.sources.getSourceActorById(location.sourceId);
      if (sourceActor) {
        sourceActor.applyBreakpoint(actor);
      }
    }
  },

  removeBreakpoint(location) {
    const actor = this.breakpointActorMap.getOrCreateBreakpointActor(location);
    actor.delete();
  },

  removeXHRBreakpoint: function(path, method) {
    const index = this._findXHRBreakpointIndex(path, method);

    if (index >= 0) {
      this._xhrBreakpoints.splice(index, 1);
    }
    return this._updateNetworkObserver();
  },

  setXHRBreakpoint: function(path, method) {
    // request.path is a string,
    // If requested url contains the path, then we pause.
    const index = this._findXHRBreakpointIndex(path, method);

    if (index === -1) {
      this._xhrBreakpoints.push({ path, method });
    }
    return this._updateNetworkObserver();
  },

  getAvailableEventBreakpoints: function() {
    return getAvailableEventBreakpoints();
  },
  getActiveEventBreakpoints: function() {
    return this._eventBreakpoints;
  },
  setActiveEventBreakpoints: function(ids) {
    this._eventBreakpoints = ids;
  },

  _updateNetworkObserver() {
    // Workers don't have access to `Services` and even if they did, network
    // requests are all dispatched to the main thread, so there would be
    // nothing here to listen for. We'll need to revisit implementing
    // XHR breakpoints for workers.
    if (isWorker) {
      return false;
    }

    if (this._xhrBreakpoints.length > 0 && !this._observingNetwork) {
      this._observingNetwork = true;
      Services.obs.addObserver(this._onOpeningRequest, "http-on-opening-request");
    } else if (this._xhrBreakpoints.length === 0 && this._observingNetwork) {
      this._observingNetwork = false;
      Services.obs.removeObserver(this._onOpeningRequest, "http-on-opening-request");
    }

    return true;
  },

  _onOpeningRequest: function(subject) {
    if (this.skipBreakpoints) {
      return;
    }

    const channel = subject.QueryInterface(Ci.nsIHttpChannel);
    const url = channel.URI.asciiSpec;
    const requestMethod = channel.requestMethod;

    let causeType = Ci.nsIContentPolicy.TYPE_OTHER;
    if (channel.loadInfo) {
      causeType = channel.loadInfo.externalContentPolicyType;
    }

    const isXHR = (
      causeType === Ci.nsIContentPolicy.TYPE_XMLHTTPREQUEST ||
      causeType === Ci.nsIContentPolicy.TYPE_FETCH
    );

    if (!isXHR) {
      // We currently break only if the request is either fetch or xhr
      return;
    }

    let shouldPause = false;
    for (const { path, method } of this._xhrBreakpoints) {
      if (method !== "ANY" && method !== requestMethod) {
        continue;
      }
      if (url.includes(path)) {
        shouldPause = true;
        break;
      }
    }

    if (shouldPause) {
      const frame = this.dbg.getNewestFrame();

      // If there is no frame, this request was dispatched by logic that isn't
      // primarily JS, so pausing the event loop wouldn't make sense.
      // This covers background requests like loading the initial page document,
      // or loading favicons. This also includes requests dispatched indirectly
      // from workers. We'll need to handle them separately in the future.
      if (frame) {
        this._pauseAndRespond(frame, { type: "XHR" });
      }
    }
  },

  onDetach: function(request) {
    this.destroy();
    this._state = "detached";
    this._debuggerSourcesSeen = null;

    dumpn("ThreadActor.prototype.onDetach: returning 'detached' packet");
    return {
      type: "detached",
    };
  },

  onReconfigure: function(request) {
    if (this.state == "exited") {
      return { error: "wrongState" };
    }
    const options = request.options || {};

    if ("observeAsmJS" in options) {
      this.dbg.allowUnobservedAsmJS = !options.observeAsmJS;
    }

    if ("skipBreakpoints" in options) {
      this.skipBreakpoints = options.skipBreakpoints;
    }

    if ("pauseWorkersUntilAttach" in options) {
      if (this._parent.pauseWorkersUntilAttach) {
        this._parent.pauseWorkersUntilAttach(options.pauseWorkersUntilAttach);
      }
    }

    Object.assign(this._options, options);

    // Update the global source store
    this.sources.setOptions(options);

    return {};
  },

  /**
   * Pause the debuggee, by entering a nested event loop, and return a 'paused'
   * packet to the client.
   *
   * @param Debugger.Frame frame
   *        The newest debuggee frame in the stack.
   * @param object reason
   *        An object with a 'type' property containing the reason for the pause.
   * @param function onPacket
   *        Hook to modify the packet before it is sent. Feel free to return a
   *        promise.
   */
  _pauseAndRespond: function(frame, reason, onPacket = k => k) {
    try {
      const packet = this._paused(frame);
      if (!packet) {
        return undefined;
      }
      packet.why = reason;

      const {
        generatedSourceActor,
        generatedLine,
        generatedColumn,
      } = this.sources.getFrameLocation(frame);

      if (!generatedSourceActor) {
        // If the frame location is in a source that not pass the 'allowSource'
        // check and thus has no actor, we do not bother pausing.
        return undefined;
      }

      packet.frame.where = {
        actor: generatedSourceActor.actorID,
        line: generatedLine,
        column: generatedColumn,
      };
      const pkt = onPacket(packet);

      this.conn.send(pkt);
    } catch (error) {
      reportError(error);
      this.conn.send({
        error: "unknownError",
        message: error.message + "\n" + error.stack,
      });
      return undefined;
    }

    try {
      this._pushThreadPause();
    } catch (e) {
      reportError(e, "Got an exception during TA__pauseAndRespond: ");
    }

    // If the parent actor has been closed, terminate the debuggee script
    // instead of continuing. Executing JS after the content window is gone is
    // a bad idea.
    return this._parentClosed ? null : undefined;
  },

  _makeOnEnterFrame: function({ pauseAndRespond, rewinding }) {
    return frame => {
      const { generatedSourceActor } = this.sources.getFrameLocation(frame);

      const url = generatedSourceActor.url;

      // When rewinding into a frame, we end up at the point when it is being popped.
      if (rewinding) {
        frame.reportedPop = true;
      }

      if (this.sources.isBlackBoxed(url)) {
        return undefined;
      }

      return pauseAndRespond(frame);
    };
  },

  _makeOnPop: function({ thread, pauseAndRespond, startLocation, steppingType }) {
    const result = function(completion) {
      // onPop is called with 'this' set to the current frame.
      const generatedLocation = thread.sources.getFrameLocation(this);

      const { generatedSourceActor } = generatedLocation;
      const url = generatedSourceActor.url;

      if (thread.sources.isBlackBoxed(url)) {
        return undefined;
      }

      // Note that we're popping this frame; we need to watch for
      // subsequent step events on its caller.
      this.reportedPop = true;

      if (steppingType == "finish") {
        const parentFrame = thread._getNextStepFrame(this);
        if (parentFrame && parentFrame.script) {
          // We can't use the completion value in stepping hooks if we're
          // replaying, as we can't use its contents after resuming.
          const ncompletion = thread.dbg.replaying ? null : completion;
          const { onStep, onPop } = thread._makeSteppingHooks(
            generatedLocation, "next", false, ncompletion
          );
          if (thread.dbg.replaying) {
            const parentGeneratedLocation = thread.sources.getFrameLocation(parentFrame);
            const offsets =
              thread._findReplayingStepOffsets(parentGeneratedLocation, parentFrame,
                                               /* rewinding = */ false);
            parentFrame.setReplayingOnStep(onStep, offsets);
          } else {
            parentFrame.onStep = onStep;
          }
          // We need the onPop alongside the onStep because it is possible that
          // the parent frame won't have any steppable offsets, and we want to
          // make sure that we always pause in the parent _somewhere_.
          parentFrame.onPop = onPop;
          return undefined;
        }
      }

      return pauseAndRespond(this, packet => {
        if (completion) {
          thread.createCompletionGrip(packet, completion);
        } else {
          packet.why.frameFinished = {
            terminated: true,
          };
        }
        return packet;
      });
    };

    // When stepping out, we don't want to stop at a breakpoint that
    // happened to be set exactly at the spot where we stepped out.
    // See bug 970469.  We record the generated location here and check
    // it when a breakpoint is hit.  Furthermore we store this on the
    // function because, while we could store it directly on the
    // frame, if we did we'd also have to find the appropriate spot to
    // clear it.
    result.generatedLocation = startLocation;

    return result;
  },

  // Return whether reaching a script offset should be considered a distinct
  // "step" from another location.
  _intraFrameLocationIsStepTarget: function(startLocation, script, offset) {
    // Only allow stepping stops at entry points for the line.
    if (!script.getOffsetMetadata(offset).isBreakpoint) {
      return false;
    }

    const generatedLocation = this.sources.getScriptOffsetLocation(script, offset);

    if (startLocation.generatedUrl !== generatedLocation.generatedUrl) {
      return true;
    }

    // TODO(logan): When we remove points points, this can be removed too as
    // we assert that we're at a different frame offset from the last time
    // we paused.
    const lineChanged = startLocation.generatedLine !== generatedLocation.generatedLine;
    const columnChanged =
      startLocation.generatedColumn !== generatedLocation.generatedColumn;
    if (!lineChanged && !columnChanged) {
      return false;
    }

    // When pause points are specified for the source,
    // we should pause when we are at a stepOver pause point
    const pausePoints = generatedLocation.generatedSourceActor.pausePoints;
    const pausePoint = pausePoints &&
      findPausePointForLocation(pausePoints, generatedLocation);

    if (pausePoint) {
      return pausePoint.step;
    }

    return script.getOffsetMetadata(offset).isStepStart;
  },

  _makeOnStep: function({ thread, pauseAndRespond, startFrame,
                          startLocation, steppingType, completion, rewinding }) {
    // Breaking in place: we should always pause.
    if (steppingType === "break") {
      return () => pauseAndRespond(this);
    }

    // Otherwise take what a "step" means into consideration.
    return function() {
      // onStep is called with 'this' set to the current frame.

      const generatedLocation = thread.sources.getFrameLocation(this);

      // Always continue execution if either:
      //
      // 1. We are in a source mapped region, but inside a null mapping
      //    (doesn't correlate to any region of generated source)
      // 2. The source we are in is black boxed.
      if (generatedLocation.generatedUrl == null
          || thread.sources.isBlackBoxed(generatedLocation.generatedUrl)) {
        return undefined;
      }

      // A step has occurred if we are rewinding and have changed frames.
      if (rewinding && this !== startFrame) {
        return pauseAndRespond(this);
      }

      // A step has occurred if we reached a step target.
      if (thread._intraFrameLocationIsStepTarget(startLocation,
                                                 this.script, this.offset)) {
        return pauseAndRespond(
          this,
          packet => thread.createCompletionGrip(packet, completion)
        );
      }

      // Otherwise, let execution continue (we haven't executed enough code to
      // consider this a "step" yet).
      return undefined;
    };
  },

  createCompletionGrip: function(packet, completion) {
    if (!completion) {
      return packet;
    }

    const createGrip = value => createValueGrip(value, this._pausePool, this.objectGrip);
    packet.why.frameFinished = {};

    if (completion.hasOwnProperty("return")) {
      packet.why.frameFinished.return = createGrip(completion.return);
    } else if (completion.hasOwnProperty("yield")) {
      packet.why.frameFinished.return = createGrip(completion.yield);
    } else if (completion.hasOwnProperty("throw")) {
      packet.why.frameFinished.throw = createGrip(completion.throw);
    }

    return packet;
  },

  /**
   * When replaying, we need to specify the offsets where a frame's onStep hook
   * should fire. Given that we are stepping forward (rewind == false) or
   * backwards (rewinding == true), return an array of all the step targets
   * that could be reached next from startLocation.
   */
  _findReplayingStepOffsets: function(startLocation, frame, rewinding) {
    const worklist = [frame.offset], seen = [], result = [];
    while (worklist.length) {
      const offset = worklist.pop();
      if (seen.includes(offset)) {
        continue;
      }
      seen.push(offset);
      if (this._intraFrameLocationIsStepTarget(startLocation, frame.script, offset)) {
        if (!result.includes(offset)) {
          result.push(offset);
        }
      } else {
        const neighbors = rewinding
            ? frame.script.getPredecessorOffsets(offset)
            : frame.script.getSuccessorOffsets(offset);
        for (const n of neighbors) {
          worklist.push(n);
        }
      }
    }
    return result;
  },

  /**
   * Define the JS hook functions for stepping.
   */
  _makeSteppingHooks: function(startLocation, steppingType, rewinding, completion) {
    // Bind these methods and state because some of the hooks are called
    // with 'this' set to the current frame. Rather than repeating the
    // binding in each _makeOnX method, just do it once here and pass it
    // in to each function.
    const steppingHookState = {
      pauseAndRespond: (frame, onPacket = k=>k) => this._pauseAndRespond(
        frame,
        { type: "resumeLimit" },
        onPacket
      ),
      thread: this,
      startFrame: this.youngestFrame,
      startLocation: startLocation,
      steppingType: steppingType,
      rewinding: rewinding,
      completion,
    };

    return {
      onEnterFrame: this._makeOnEnterFrame(steppingHookState),
      onPop: this._makeOnPop(steppingHookState),
      onStep: this._makeOnStep(steppingHookState),
    };
  },

  /**
   * Handle attaching the various stepping hooks we need to attach when we
   * receive a resume request with a resumeLimit property.
   *
   * @param Object request
   *        The request packet received over the RDP.
   * @returns A promise that resolves to true once the hooks are attached, or is
   *          rejected with an error packet.
   */
  _handleResumeLimit: async function(request) {
    let steppingType = request.resumeLimit.type;
    const rewinding = request.rewind;
    if (!["break", "step", "next", "finish", "warp"].includes(steppingType)) {
      return Promise.reject({
        error: "badParameterType",
        message: "Unknown resumeLimit type",
      });
    }

    if (steppingType == "warp") {
      // Time warp resume limits are handled by the caller.
      return true;
    }

    // If we are stepping out of the onPop handler, we want to use "next" mode
    // so that the parent frame's handlers behave consistently.
    if (steppingType === "finish" && this.youngestFrame.reportedPop) {
      steppingType = "next";
    }

    const generatedLocation = this.sources.getFrameLocation(this.youngestFrame);
    const { onEnterFrame, onPop, onStep } = this._makeSteppingHooks(
      generatedLocation,
      steppingType,
      rewinding
    );

    // Make sure there is still a frame on the stack if we are to continue
    // stepping.
    const stepFrame = this._getNextStepFrame(this.youngestFrame, rewinding);
    if (stepFrame) {
      switch (steppingType) {
        case "step":
          if (rewinding) {
            this.dbg.replayingOnPopFrame = onEnterFrame;
          } else {
            this.dbg.onEnterFrame = onEnterFrame;
          }
          // Fall through.
        case "break":
        case "next":
          if (stepFrame.script) {
            if (this.dbg.replaying) {
              const offsets =
                this._findReplayingStepOffsets(generatedLocation, stepFrame, rewinding);
              stepFrame.setReplayingOnStep(onStep, offsets);
            } else {
              stepFrame.onStep = onStep;
            }
          }
          // Fall through.
        case "finish":
          if (rewinding) {
            let olderFrame = stepFrame.older;
            while (olderFrame && !olderFrame.script) {
              olderFrame = olderFrame.older;
            }
            if (olderFrame) {
              olderFrame.setReplayingOnStep(onStep, [olderFrame.offset]);
            }
          } else {
            stepFrame.onPop = onPop;
          }
          break;
      }
    }

    return true;
  },

  /**
   * Clear the onStep and onPop hooks for all frames on the stack.
   */
  _clearSteppingHooks: function() {
    if (this.dbg.replaying) {
      this.dbg.replayClearSteppingHooks();
    } else {
      let frame = this.youngestFrame;
      if (frame && frame.live) {
        while (frame) {
          frame.onStep = undefined;
          frame.onPop = undefined;
          frame = frame.older;
        }
      }
    }
  },

  /**
   * Handle a protocol request to resume execution of the debuggee.
   */
  onResume: function(request) {
    if (this._state !== "paused") {
      return {
        error: "wrongState",
        message: "Can't resume when debuggee isn't paused. Current state is '"
          + this._state + "'",
        state: this._state,
      };
    }

    // In case of multiple nested event loops (due to multiple debuggers open in
    // different tabs or multiple debugger clients connected to the same tab)
    // only allow resumption in a LIFO order.
    if (this._nestedEventLoops.size && this._nestedEventLoops.lastPausedUrl
        && (this._nestedEventLoops.lastPausedUrl !== this._parent.url
            || this._nestedEventLoops.lastConnection !== this.conn)) {
      return {
        error: "wrongOrder",
        message: "trying to resume in the wrong order.",
        lastPausedUrl: this._nestedEventLoops.lastPausedUrl,
      };
    }

    const rewinding = request && request.rewind;
    if (rewinding && !this.dbg.replaying) {
      return {
        error: "cantRewind",
        message: "Can't rewind a debuggee that is not replaying.",
      };
    }

    let resumeLimitHandled;
    if (request && request.resumeLimit) {
      resumeLimitHandled = this._handleResumeLimit(request);
    } else {
      this._clearSteppingHooks();
      resumeLimitHandled = Promise.resolve(true);
    }

    return resumeLimitHandled.then(() => {
      this.maybePauseOnExceptions();

      // When replaying execution in a separate process we need to explicitly
      // notify that process when to resume execution.
      if (this.dbg.replaying) {
        if (request && request.resumeLimit && request.resumeLimit.type == "warp") {
          this.dbg.replayTimeWarp(request.resumeLimit.target);
        } else if (rewinding) {
          this.dbg.replayResumeBackward();
        } else {
          this.dbg.replayResumeForward();
        }
      }

      const packet = this._resumed();
      this._popThreadPause();
      // Tell anyone who cares of the resume (as of now, that's the xpcshell harness and
      // devtools-startup.js when handling the --wait-for-jsdebugger flag)
      if (Services.obs) {
        Services.obs.notifyObservers(this, "devtools-thread-resumed");
      }
      return packet;
    }, error => {
      return error instanceof Error
        ? { error: "unknownError",
            message: DevToolsUtils.safeErrorString(error) }
        // It is a known error, and the promise was rejected with an error
        // packet.
        : error;
    });
  },

  /**
   * Spin up a nested event loop so we can synchronously resolve a promise.
   *
   * DON'T USE THIS UNLESS YOU ABSOLUTELY MUST! Nested event loops suck: the
   * world's state can change out from underneath your feet because JS is no
   * longer run-to-completion.
   *
   * @param p
   *        The promise we want to resolve.
   * @returns The promise's resolution.
   */
  unsafeSynchronize: function(p) {
    let needNest = true;
    let eventLoop;
    let returnVal;

    p.then((resolvedVal) => {
      needNest = false;
      returnVal = resolvedVal;
    })
    .catch((error) => {
      reportError(error, "Error inside unsafeSynchronize:");
    })
    .then(() => {
      if (eventLoop) {
        eventLoop.resolve();
      }
    });

    if (needNest) {
      eventLoop = this._nestedEventLoops.push();
      eventLoop.enter();
    }

    return returnVal;
  },

  /**
   * Set the debugging hook to pause on exceptions if configured to do so.
   */
  maybePauseOnExceptions: function() {
    if (this._options.pauseOnExceptions) {
      this.dbg.onExceptionUnwind = this.onExceptionUnwind.bind(this);
    }
  },

  /**
   * Helper method that returns the next frame when stepping.
   */
  _getNextStepFrame: function(frame, rewinding) {
    const endOfFrame =
      rewinding ? (frame.offset == frame.script.mainOffset) : frame.reportedPop;
    const stepFrame = endOfFrame ? frame.older : frame;
    if (!stepFrame || !stepFrame.script) {
      return null;
    }
    return stepFrame;
  },

  onFrames: function(request) {
    if (this.state !== "paused") {
      return { error: "wrongState",
               message: "Stack frames are only available while the debuggee is paused."};
    }

    const start = request.start ? request.start : 0;
    const count = request.count;

    // Find the starting frame...
    let frame = this.youngestFrame;
    let i = 0;
    while (frame && (i < start)) {
      frame = frame.older;
      i++;
    }

    // Return request.count frames, or all remaining
    // frames if count is not defined.
    const frames = [];
    for (; frame && (!count || i < (start + count)); i++, frame = frame.older) {
      const form = this._createFrameActor(frame).form();
      form.depth = i;

      let frameItem = null;

      const frameSourceActor = this.sources.createSourceActor(frame.script.source);
      if (frameSourceActor) {
        form.where = {
          actor: frameSourceActor.actorID,
          line: form.where.line,
          column: form.where.column,
        };
        frameItem = form;
      }
      frames.push(frameItem);
    }

    // Filter null values because createSourceActor can be falsey
    return { frames: frames.filter(x => !!x) };
  },

  onSources: function(request) {
    for (const source of this.dbg.findSources()) {
      this._addSource(source);
    }

    // No need to flush the new source packets here, as we are sending the
    // list of sources out immediately and we don't need to invoke the
    // overhead of an RDP packet for every source right now. Let the default
    // timeout flush the buffered packets.

    return {
      sources: this.sources.iter().map(s => s.form()),
    };
  },

  /**
   * Disassociate all breakpoint actors from their scripts and clear the
   * breakpoint handlers. This method can be used when the thread actor intends
   * to keep the breakpoint store, but needs to clear any actual breakpoints,
   * e.g. due to a page navigation. This way the breakpoint actors' script
   * caches won't hold on to the Debugger.Script objects leaking memory.
   */
  disableAllBreakpoints: function() {
    for (const bpActor of this.breakpointActorMap.findActors()) {
      bpActor.removeScripts();
    }
  },

  /**
   * Handle a protocol request to pause the debuggee.
   */
  onInterrupt: function(request) {
    if (this.state == "exited") {
      return { type: "exited" };
    } else if (this.state == "paused") {
      // TODO: return the actual reason for the existing pause.
      return { type: "paused", why: { type: "alreadyPaused" } };
    } else if (this.state != "running") {
      return { error: "wrongState",
               message: "Received interrupt request in " + this.state +
                        " state." };
    }

    try {
      // If execution should pause just before the next JavaScript bytecode is
      // executed, just set an onEnterFrame handler.
      if (request.when == "onNext" && !this.dbg.replaying) {
        const onEnterFrame = (frame) => {
          return this._pauseAndRespond(frame, { type: "interrupted", onNext: true });
        };
        this.dbg.onEnterFrame = onEnterFrame;

        return { type: "willInterrupt" };
      }

      if (this.dbg.replaying) {
        this.dbg.replayPause();
      }

      // If execution should pause immediately, just put ourselves in the paused
      // state.
      const packet = this._paused();
      if (!packet) {
        return { error: "notInterrupted" };
      }
      // onNext is set while replaying so that the client will treat us as paused
      // at a breakpoint. When replaying we may need to pause and interact with
      // the server even if there are no frames on the stack.
      packet.why = { type: "interrupted", onNext: this.dbg.replaying };

      // Send the response to the interrupt request now (rather than
      // returning it), because we're going to start a nested event loop
      // here.
      this.conn.send(packet);

      // Start a nested event loop.
      this._pushThreadPause();

      // We already sent a response to this request, don't send one
      // now.
      return null;
    } catch (e) {
      reportError(e);
      return { error: "notInterrupted", message: e.toString() };
    }
  },

  /**
   * Return the Debug.Frame for a frame mentioned by the protocol.
   */
  _requestFrame: function(frameID) {
    if (!frameID) {
      return this.youngestFrame;
    }

    if (this._framePool.has(frameID)) {
      return this._framePool.get(frameID).frame;
    }

    return undefined;
  },

  _paused: function(frame) {
    // We don't handle nested pauses correctly.  Don't try - if we're
    // paused, just continue running whatever code triggered the pause.
    // We don't want to actually have nested pauses (although we
    // have nested event loops).  If code runs in the debuggee during
    // a pause, it should cause the actor to resume (dropping
    // pause-lifetime actors etc) and then repause when complete.

    if (this.state === "paused") {
      return undefined;
    }

    this._state = "paused";

    // Clear stepping hooks.
    this.dbg.onEnterFrame = undefined;
    this.dbg.replayingOnPopFrame = undefined;
    this.dbg.onExceptionUnwind = undefined;
    this._clearSteppingHooks();

    // Create the actor pool that will hold the pause actor and its
    // children.
    assert(!this._pausePool, "No pause pool should exist yet");
    this._pausePool = new ActorPool(this.conn);
    this.conn.addActorPool(this._pausePool);

    // Give children of the pause pool a quick link back to the
    // thread...
    this._pausePool.threadActor = this;

    // Create the pause actor itself...
    assert(!this._pauseActor, "No pause actor should exist yet");
    this._pauseActor = new PauseActor(this._pausePool);
    this._pausePool.addActor(this._pauseActor);

    // Update the list of frames.
    const poppedFrames = this._updateFrames();

    // Send off the paused packet and spin an event loop.
    const packet = { from: this.actorID,
                     type: "paused",
                     actor: this._pauseActor.actorID };
    if (frame) {
      packet.frame = this._createFrameActor(frame).form();
    }

    if (this.dbg.replaying) {
      packet.executionPoint = this.dbg.replayCurrentExecutionPoint();
      packet.recordingEndpoint = this.dbg.replayRecordingEndpoint();
    }

    if (poppedFrames) {
      packet.poppedFrames = poppedFrames;
    }

    return packet;
  },

  _resumed: function() {
    this._state = "running";

    // Drop the actors in the pause actor pool.
    this.conn.removeActorPool(this._pausePool);

    this._pausePool = null;
    this._pauseActor = null;

    return { from: this.actorID, type: "resumed" };
  },

  /**
   * Expire frame actors for frames that have been popped.
   *
   * @returns A list of actor IDs whose frames have been popped.
   */
  _updateFrames: function() {
    const popped = [];

    // Create the actor pool that will hold the still-living frames.
    const framePool = new ActorPool(this.conn);
    const frameList = [];

    for (const frameActor of this._frameActors) {
      if (frameActor.frame.live) {
        framePool.addActor(frameActor);
        frameList.push(frameActor);
      } else {
        popped.push(frameActor.actorID);
      }
    }

    // Remove the old frame actor pool, this will expire
    // any actors that weren't added to the new pool.
    if (this._framePool) {
      this.conn.removeActorPool(this._framePool);
    }

    this._frameActors = frameList;
    this._framePool = framePool;
    this.conn.addActorPool(framePool);

    return popped;
  },

  _createFrameActor: function(frame) {
    if (frame.actor) {
      return frame.actor;
    }

    const actor = new FrameActor(frame, this);
    this._frameActors.push(actor);
    this._framePool.addActor(actor);
    frame.actor = actor;

    return actor;
  },

  /**
   * Create and return an environment actor that corresponds to the provided
   * Debugger.Environment.
   * @param Debugger.Environment environment
   *        The lexical environment we want to extract.
   * @param object pool
   *        The pool where the newly-created actor will be placed.
   * @return The EnvironmentActor for environment or undefined for host
   *         functions or functions scoped to a non-debuggee global.
   */
  createEnvironmentActor: function(environment, pool) {
    if (!environment) {
      return undefined;
    }

    if (environment.actor) {
      return environment.actor;
    }

    const actor = new EnvironmentActor(environment, this);
    pool.addActor(actor);
    environment.actor = actor;

    return actor;
  },

  /**
   * Return a protocol completion value representing the given
   * Debugger-provided completion value.
   */
  createProtocolCompletionValue: function(completion) {
    const protoValue = {};
    if (completion == null) {
      protoValue.terminated = true;
    } else if ("return" in completion) {
      protoValue.return = createValueGrip(
        completion.return,
        this._pausePool,
        this.objectGrip
      );
    } else if ("throw" in completion) {
      protoValue.throw = createValueGrip(
        completion.throw,
        this._pausePool,
        this.objectGrip
      );
    } else {
      protoValue.return = createValueGrip(
        completion.yield,
        this._pausePool,
        this.objectGrip
      );
    }
    return protoValue;
  },

  /**
   * Create a grip for the given debuggee object.
   *
   * @param value Debugger.Object
   *        The debuggee object value.
   * @param pool ActorPool
   *        The actor pool where the new object actor will be added.
   */
  objectGrip: function(value, pool) {
    if (!pool.objectActors) {
      pool.objectActors = new WeakMap();
    }

    if (pool.objectActors.has(value)) {
      return pool.objectActors.get(value).form();
    }

    if (this.threadLifetimePool.objectActors.has(value)) {
      return this.threadLifetimePool.objectActors.get(value).form();
    }

    const actor = new PauseScopedObjectActor(value, {
      getGripDepth: () => this._gripDepth,
      incrementGripDepth: () => this._gripDepth++,
      decrementGripDepth: () => this._gripDepth--,
      createValueGrip: v => {
        if (this._pausePool) {
          return createValueGrip(v, this._pausePool, this.pauseObjectGrip);
        }

        return createValueGrip(v, this.threadLifetimePool, this.objectGrip);
      },
      sources: () => this.sources,
      createEnvironmentActor: (e, p) => this.createEnvironmentActor(e, p),
      promote: () => this.threadObjectGrip(actor),
      isThreadLifetimePool: () => actor.registeredPool !== this.threadLifetimePool,
      getGlobalDebugObject: () => this.globalDebugObject,
    }, this.conn);
    pool.addActor(actor);
    pool.objectActors.set(value, actor);
    return actor.form();
  },

  /**
   * Create a grip for the given debuggee object with a pause lifetime.
   *
   * @param value Debugger.Object
   *        The debuggee object value.
   */
  pauseObjectGrip: function(value) {
    if (!this._pausePool) {
      throw new Error("Object grip requested while not paused.");
    }

    return this.objectGrip(value, this._pausePool);
  },

  /**
   * Extend the lifetime of the provided object actor to thread lifetime.
   *
   * @param actor object
   *        The object actor.
   */
  threadObjectGrip: function(actor) {
    // We want to reuse the existing actor ID, so we just remove it from the
    // current pool's weak map and then let pool.addActor do the rest.
    actor.registeredPool.objectActors.delete(actor.obj);
    this.threadLifetimePool.addActor(actor);
    this.threadLifetimePool.objectActors.set(actor.obj, actor);
  },

  /**
   * Handle a protocol request to promote multiple pause-lifetime grips to
   * thread-lifetime grips.
   *
   * @param aRequest object
   *        The protocol request object.
   */
  onThreadGrips: function(request) {
    if (this.state != "paused") {
      return { error: "wrongState" };
    }

    if (!request.actors) {
      return { error: "missingParameter",
               message: "no actors were specified" };
    }

    for (const actorID of request.actors) {
      const actor = this._pausePool.get(actorID);
      if (actor) {
        this.threadObjectGrip(actor);
      }
    }
    return {};
  },

  /**
   * Create a long string grip that is scoped to a pause.
   *
   * @param string String
   *        The string we are creating a grip for.
   */
  pauseLongStringGrip: function(string) {
    return longStringGrip(string, this._pausePool);
  },

  /**
   * Create a long string grip that is scoped to a thread.
   *
   * @param string String
   *        The string we are creating a grip for.
   */
  threadLongStringGrip: function(string) {
    return longStringGrip(string, this._threadLifetimePool);
  },

  // JS Debugger API hooks.

  /**
   * A function that the engine calls when a call to a debug event hook,
   * breakpoint handler, watchpoint handler, or similar function throws some
   * exception.
   *
   * @param exception exception
   *        The exception that was thrown in the debugger code.
   */
  uncaughtExceptionHook: function(exception) {
    dumpn("Got an exception: " + exception.message + "\n" + exception.stack);
  },

  /**
   * A function that the engine calls when a debugger statement has been
   * executed in the specified frame.
   *
   * @param frame Debugger.Frame
   *        The stack frame that contained the debugger statement.
   */
  onDebuggerStatement: function(frame) {
    // Don't pause if we are currently stepping (in or over) or the frame is
    // black-boxed.
    const { generatedSourceActor } = this.sources.getFrameLocation(frame);
    const url = generatedSourceActor ? generatedSourceActor.url : null;

    if (this.skipBreakpoints || this.sources.isBlackBoxed(url) || frame.onStep) {
      return undefined;
    }

    return this._pauseAndRespond(frame, { type: "debuggerStatement" });
  },

  onSkipBreakpoints: function({ skip }) {
    this.skipBreakpoints = skip;
    return { skip };
  },

  onPauseOnExceptions: function({pauseOnExceptions, ignoreCaughtExceptions}) {
    Object.assign(this._options, { pauseOnExceptions, ignoreCaughtExceptions });
    this.maybePauseOnExceptions();
    return {};
  },

  /*
   * A function that the engine calls when a recording/replaying process has
   * changed its position: a checkpoint was reached or a switch between a
   * recording and replaying child process occurred.
   */
  replayingOnPositionChange: function(sendProgress) {
    const recording = this.dbg.replayIsRecording();
    const executionPoint = this.dbg.replayCurrentExecutionPoint();
    sendProgress(recording, executionPoint);
  },

  /**
   * A function that the engine calls when replay has hit a point where it will
   * pause, even if no breakpoint has been set. Such points include hitting the
   * beginning or end of the replay, or reaching the target of a time warp.
   *
   * @param frame Debugger.Frame
   *        The youngest stack frame, or null.
   */
  replayingOnForcedPause: function(frame) {
    if (frame) {
      this._pauseAndRespond(frame, { type: "replayForcedPause" });
    } else {
      const packet = this._paused(frame);
      if (!packet) {
        return;
      }
      packet.why = "replayForcedPause";

      this.conn.send(packet);
      this._pushThreadPause();
    }
  },

  /**
   * A function that the engine calls when an exception has been thrown and has
   * propagated to the specified frame.
   *
   * @param youngestFrame Debugger.Frame
   *        The youngest remaining stack frame.
   * @param value object
   *        The exception that was thrown.
   */
  onExceptionUnwind: function(youngestFrame, value) {
    let willBeCaught = false;
    for (let frame = youngestFrame; frame != null; frame = frame.older) {
      if (frame.script.isInCatchScope(frame.offset)) {
        willBeCaught = true;
        break;
      }
    }

    if (willBeCaught && this._options.ignoreCaughtExceptions) {
      return undefined;
    }

    // NS_ERROR_NO_INTERFACE exceptions are a special case in browser code,
    // since they're almost always thrown by QueryInterface functions, and
    // handled cleanly by native code.
    if (value == Cr.NS_ERROR_NO_INTERFACE) {
      return undefined;
    }

    const { generatedSourceActor } = this.sources.getFrameLocation(youngestFrame);
    const url = generatedSourceActor ? generatedSourceActor.url : null;

    // Don't pause on exceptions thrown while inside an evaluation being done on
    // behalf of the client.
    if (this.insideClientEvaluation) {
      return undefined;
    }

    if (this.skipBreakpoints || this.sources.isBlackBoxed(url)) {
      return undefined;
    }

    try {
      const packet = this._paused(youngestFrame);
      if (!packet) {
        return undefined;
      }

      packet.why = { type: "exception",
                     exception: createValueGrip(value, this._pausePool,
                                                this.objectGrip),
      };
      this.conn.send(packet);

      this._pushThreadPause();
    } catch (e) {
      reportError(e, "Got an exception during TA_onExceptionUnwind: ");
    }

    return undefined;
  },

  /**
   * A function that the engine calls when a new script has been loaded into the
   * scope of the specified debuggee global.
   *
   * @param script Debugger.Script
   *        The source script that has been loaded into a debuggee compartment.
   * @param global Debugger.Object
   *        A Debugger.Object instance whose referent is the global object.
   */
  onNewScript: function(script, global) {
    this._addSource(script.source);
  },

  /**
   * A function called when there's a new source from a thread actor's sources.
   * Emits `newSource` on the thread actor.
   *
   * @param {SourceActor} source
   */
  onNewSourceEvent: function(source) {
    // Bug 1516197: New sources are likely detected due to either user
    // interaction on the page, or devtools requests sent to the server.
    // We use executeSoon because we don't want to block those operations
    // by sending packets in the middle of them.
    DevToolsUtils.executeSoon(() => {
      this.conn.send({
        from: this.actorID,
        type: "newSource",
        source: source.form(),
      });
    });
  },

  /**
   * A function called when there's an updated source from a thread actor' sources.
   * Emits `updatedSource` on the target actor.
   *
   * @param {SourceActor} source
   */
  onUpdatedSourceEvent: function(source) {
    this.conn.send({
      from: this._parent.actorID,
      type: "updatedSource",
      source: source.form(),
    });
  },

  /**
   * Add the provided source to the server cache.
   *
   * @param aSource Debugger.Source
   *        The source that will be stored.
   * @returns true, if the source was added; false otherwise.
   */
  _addSource: function(source) {
    if (!this.sources.allowSource(source)) {
      return false;
    }

    // Preloaded WebExtension content scripts may be cached internally by
    // ExtensionContent.jsm and ThreadActor would ignore them on a page reload
    // because it finds them in the _debuggerSourcesSeen WeakSet,
    // and so we also need to be sure that there is still a source actor for the source.
    let sourceActor;
    if (this._debuggerSourcesSeen.has(source) && this.sources.hasSourceActor(source)) {
      sourceActor = this.sources.getSourceActor(source);
    } else {
      sourceActor = this.sources.createSourceActor(source);
    }

    if (this._onLoadBreakpointURLs.has(source.url)) {
      this.setBreakpoint({ sourceUrl: source.url, line: 1 }, {});
    }

    const bpActors = this.breakpointActorMap.findActors()
    .filter((actor) => {
      return actor.location.sourceUrl && actor.location.sourceUrl == source.url;
    });

    for (const actor of bpActors) {
      sourceActor.applyBreakpoint(actor);
    }

    this._debuggerSourcesSeen.add(source);
    return true;
  },

  onDump: function() {
    return {
      pauseOnExceptions: this._options.pauseOnExceptions,
      ignoreCaughtExceptions: this._options.ignoreCaughtExceptions,
      skipBreakpoints: this.skipBreakpoints,
      breakpoints: this.breakpointActorMap.listKeys(),
    };
  },
});

Object.assign(ThreadActor.prototype.requestTypes, {
  "attach": ThreadActor.prototype.onAttach,
  "detach": ThreadActor.prototype.onDetach,
  "reconfigure": ThreadActor.prototype.onReconfigure,
  "resume": ThreadActor.prototype.onResume,
  "frames": ThreadActor.prototype.onFrames,
  "interrupt": ThreadActor.prototype.onInterrupt,
  "sources": ThreadActor.prototype.onSources,
  "threadGrips": ThreadActor.prototype.onThreadGrips,
  "skipBreakpoints": ThreadActor.prototype.onSkipBreakpoints,
  "pauseOnExceptions": ThreadActor.prototype.onPauseOnExceptions,
  "dumpThread": ThreadActor.prototype.onDump,
});

exports.ThreadActor = ThreadActor;

/**
 * Creates a PauseActor.
 *
 * PauseActors exist for the lifetime of a given debuggee pause.  Used to
 * scope pause-lifetime grips.
 *
 * @param ActorPool aPool
 *        The actor pool created for this pause.
 */
function PauseActor(pool) {
  this.pool = pool;
}

PauseActor.prototype = {
  actorPrefix: "pause",
};

/**
 * Creates an actor for handling chrome debugging. ChromeDebuggerActor is a
 * thin wrapper over ThreadActor, slightly changing some of its behavior.
 *
 * @param connection object
 *        The DebuggerServerConnection with which this ChromeDebuggerActor
 *        is associated. (Currently unused, but required to make this
 *        constructor usable with addGlobalActor.)
 *
 * @param parent object
 *        This actor's parent actor. See ThreadActor for a list of expected
 *        properties.
 */
function ChromeDebuggerActor(connection, parent) {
  ThreadActor.prototype.initialize.call(this, parent);
}

ChromeDebuggerActor.prototype = Object.create(ThreadActor.prototype);

Object.assign(ChromeDebuggerActor.prototype, {
  constructor: ChromeDebuggerActor,

  // A constant prefix that will be used to form the actor ID by the server.
  actorPrefix: "chromeDebugger",
});

exports.ChromeDebuggerActor = ChromeDebuggerActor;

// Utility functions.

/**
 * Report the given error in the error console and to stdout.
 *
 * @param Error error
 *        The error object you wish to report.
 * @param String prefix
 *        An optional prefix for the reported error message.
 */
var oldReportError = reportError;
this.reportError = function(error, prefix = "") {
  assert(error instanceof Error, "Must pass Error objects to reportError");
  const msg = prefix + error.message + ":\n" + error.stack;
  oldReportError(msg);
  dumpn(msg);
};

function findPausePointForLocation(pausePoints, location) {
  const { generatedLine: line, generatedColumn: column } = location;
  return pausePoints[line] && pausePoints[line][column];
}

/**
 * Unwrap a global that is wrapped in a |Debugger.Object|, or if the global has
 * become a dead object, return |undefined|.
 *
 * @param Debugger.Object wrappedGlobal
 *        The |Debugger.Object| which wraps a global.
 *
 * @returns {Object|undefined}
 *          Returns the unwrapped global object or |undefined| if unwrapping
 *          failed.
 */
exports.unwrapDebuggerObjectGlobal = wrappedGlobal => {
  try {
    // Because of bug 991399 we sometimes get nuked window references here. We
    // just bail out in that case.
    //
    // Note that addon sandboxes have a DOMWindow as their prototype. So make
    // sure that we can touch the prototype too (whatever it is), in case _it_
    // is it a nuked window reference. We force stringification to make sure
    // that any dead object proxies make themselves known.
    const global = wrappedGlobal.unsafeDereference();
    Object.getPrototypeOf(global) + "";
    return global;
  } catch (e) {
    return undefined;
  }
};
