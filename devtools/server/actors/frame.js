/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { ActorPool } = require("devtools/server/actors/common");
const { createValueGrip } = require("devtools/server/actors/object/utils");
const { ActorClassWithSpec } = require("devtools/shared/protocol");
const { frameSpec } = require("devtools/shared/specs/frame");

function formatDisplayName(frame) {
  if (frame.type === "call") {
    const callee = frame.callee;
    return callee.name || callee.userDisplayName || callee.displayName;
  }

  return `(${frame.type})`;
}

/**
 * An actor for a specified stack frame.
 */
const FrameActor = ActorClassWithSpec(frameSpec, {
  /**
   * Creates the Frame actor.
   *
   * @param frame Debugger.Frame
   *        The debuggee frame.
   * @param threadActor ThreadActor
   *        The parent thread actor for this frame.
   */
  initialize: function(frame, threadActor, depth) {
    this.frame = frame;
    this.threadActor = threadActor;
    this.depth = depth;
  },

  /**
   * A pool that contains frame-lifetime objects, like the environment.
   */
  _frameLifetimePool: null,
  get frameLifetimePool() {
    if (!this._frameLifetimePool) {
      this._frameLifetimePool = new ActorPool(this.conn, "frame");
      this.conn.addActorPool(this._frameLifetimePool);
    }
    return this._frameLifetimePool;
  },

  /**
   * Finalization handler that is called when the actor is being evicted from
   * the pool.
   */
  destroy: function() {
    this.conn.removeActorPool(this._frameLifetimePool);
    this._frameLifetimePool = null;
  },

  getEnvironment: function() {
    try {
      if (!this.frame.environment) {
        return {};
      }
    } catch (e) {
      // |this.frame| might not be live. FIXME Bug 1477030 we shouldn't be
      // using frames we derived from a point where we are not currently
      // paused at.
      return {};
    }

    const envActor = this.threadActor.createEnvironmentActor(
      this.frame.environment,
      this.frameLifetimePool
    );

    return envActor.form();
  },

  /**
   * Returns a frame form for use in a protocol message.
   */
  form: function() {
    const threadActor = this.threadActor;
    const form = {
      actor: this.actorID,
      type: this.frame.type,
      asyncCause: this.frame.onStack ? null : "await",

      // This should expand with "dead" when we support SavedFrames.
      state: this.frame.onStack ? "on-stack" : "suspended",
    };

    if (this.depth) {
      form.depth = this.depth;
    }

    if (this.frame.type != "wasmcall") {
      form.this = createValueGrip(
        this.frame.this,
        threadActor._pausePool,
        threadActor.objectGrip
      );
    }

    form.displayName = formatDisplayName(this.frame);
    form.arguments = this._args();

    if (this.frame.script) {
      const location = this.threadActor.sources.getFrameLocation(this.frame);
      form.where = {
        actor: location.sourceActor.actorID,
        line: location.line,
        column: location.column,
      };
    }

    if (!this.frame.older) {
      form.oldest = true;
    }

    return form;
  },

  _args: function() {
    if (!this.frame.onStack || !this.frame.arguments) {
      return [];
    }

    return this.frame.arguments.map(arg =>
      createValueGrip(
        arg,
        this.threadActor._pausePool,
        this.threadActor.objectGrip
      )
    );
  },
});

exports.FrameActor = FrameActor;
exports.formatDisplayName = formatDisplayName;
