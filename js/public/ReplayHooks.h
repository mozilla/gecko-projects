/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Hooks for communication between debuggers in a replayed process.

#ifndef js_ReplayHooks_h
#define js_ReplayHooks_h

#include "mozilla/Vector.h"

#include "jsapi.h"

namespace JS {
namespace replay {

typedef mozilla::Vector<char16_t> CharBuffer;

// Identification for an execution position --- anyplace a breakpoint can be
// created --- during JS execution in a child process.
struct ExecutionPosition
{
    enum Kind {
        Invalid,
        Break,       // No frameIndex
        OnStep,
        OnPop,       // No offset, script/frameIndex is optional
        EnterFrame,  // No offset/script/frameIndex
        NewScript,   // No offset/script/frameIndex
    } kind;
    size_t script;
    size_t offset;
    size_t frameIndex;

    static const size_t EMPTY_SCRIPT = (size_t) -1;
    static const size_t EMPTY_OFFSET = (size_t) -1;
    static const size_t EMPTY_FRAME_INDEX = (size_t) -1;

    ExecutionPosition()
      : kind(Invalid), script(0), offset(0), frameIndex(0)
    {}

    explicit ExecutionPosition(Kind kind,
                               size_t script = EMPTY_SCRIPT,
                               size_t offset = EMPTY_OFFSET,
                               size_t frameIndex = EMPTY_FRAME_INDEX)
      : kind(kind), script(script), offset(offset), frameIndex(frameIndex)
    {}

    bool isValid() const { return kind != Invalid; }

    inline bool operator ==(const ExecutionPosition& o) const {
        return kind == o.kind
            && script == o.script
            && offset == o.offset
            && frameIndex == o.frameIndex;
    }

    inline bool operator !=(const ExecutionPosition& o) const { return !(*this == o); }

    // Return whether an execution point matching |o| also matches this.
    inline bool subsumes(const ExecutionPosition& o) const {
        return (*this == o)
            || (kind == OnPop && o.kind == OnPop && script == EMPTY_SCRIPT)
            || (kind == Break && o.kind == OnStep && script == o.script && offset == o.offset);
    }

    const char* kindString() const {
        switch (kind) {
          case Invalid: return "Invalid";
          case Break: return "Break";
          case OnStep: return "OnStep";
          case OnPop: return "OnPop";
          case EnterFrame: return "EnterFrame";
          case NewScript: return "NewScript";
        }
        MOZ_CRASH("Bad ExecutionPosition kind");
    }
};

// Progress counters increment as a runtime executes code, and provide a basis
// for identifying points in the JS execution of a runtime. A given
// ExecutionPosition may not be reached twice without an intervening increment
// of the runtime's progress counter.
typedef uint64_t ProgressCounter;

// Identification for an execution point where a process may pause.
struct ExecutionPoint
{
    // ID of the last normal checkpoint prior to this position.
    size_t checkpoint;

    // How much progress JS has made prior to reaching the position, or zero
    // if the execution point refers to the checkpoint itself.
    uint64_t progress;

    // The position reached after making the specified amount of progress,
    // invalid if the execution point refers to the checkpoint itself.
    ExecutionPosition position;

    ExecutionPoint()
      : checkpoint(mozilla::recordreplay::InvalidCheckpointId)
      , progress(0)
    {}

    explicit ExecutionPoint(size_t checkpoint)
      : checkpoint(checkpoint)
      , progress(0)
    {}

    ExecutionPoint(size_t checkpoint, uint64_t progress, const ExecutionPosition& pos)
      : checkpoint(checkpoint), progress(progress), position(pos)
    {
        // ExecutionPoint positions must be as precise as possible, and cannot
        // subsume other positions.
        MOZ_RELEASE_ASSERT(pos.kind != ExecutionPosition::OnPop ||
                           pos.script != ExecutionPosition::EMPTY_SCRIPT);
        MOZ_RELEASE_ASSERT(pos.kind != ExecutionPosition::Break);
    }

    bool hasPosition() const { return position.isValid(); }

    inline bool operator ==(const ExecutionPoint& o) const {
        return checkpoint == o.checkpoint
            && progress == o.progress
            && position == o.position;
    }

    inline bool operator !=(const ExecutionPoint& o) const { return !(*this == o); }
};

// These hooks are used for transmitting messages between a ReplayDebugger in
// a middleman process and corresponding state in a child process.
struct Hooks
{
    // Send a JSON message to or from the child process.
    void (*debugRequestMiddleman)(const CharBuffer& buffer, CharBuffer* response);
    void (*debugRequestReplay)(CharBuffer* buffer);
    void (*debugResponseReplay)(const CharBuffer& buffer);

    // Set or clear a breakpoint in the child process.
    void (*setBreakpointMiddleman)(size_t id, const ExecutionPosition& pos);
    void (*setBreakpointReplay)(size_t id, const ExecutionPosition& pos);

    // Allow the child process to resume execution.
    void (*resumeMiddleman)(bool forward);
    void (*resumeReplay)(bool forward);
    void (*pauseMiddleman)();

    // Notify the middleman about breakpoints that were hit.
    void (*hitBreakpointReplay)(bool endpoint, const uint32_t* breakpoints, size_t numBreakpoints);
    bool (*hitBreakpointMiddleman)(JSContext* cx, size_t id);

    // Notify the middleman about a checkpoint that was hit.
    void (*hitCheckpointReplay)(size_t id, bool endpoint);

    // Direct the child process to rewind to a specific checkpoint.
    void (*restoreCheckpointReplay)(size_t id);

    // Return whether the middleman is able to restore earlier checkpoints
    // (possibly by changing the active child process).
    bool (*canRewindMiddleman)();

    // Return whether this process is able to restore earlier checkpoints.
    bool (*canRewindReplay)();

    // After recovering from an unhandled recording divergence, enter the
    // correct pause state for being at a breakpoint and then send a response
    // to the middleman for the last request.
    void (*pauseAndRespondAfterRecoveringFromDivergence)();
    void (*respondAfterRecoveringFromDivergence)();

    // Keep track of the recording endpoint while recording, and notify the
    // middleman when it has been hit while replaying.
    ExecutionPoint (*getRecordingEndpoint)();
    void (*setRecordingEndpoint)(size_t index, const ExecutionPoint& endpoint);
    bool (*hitCurrentRecordingEndpointReplay)();

    // Notify the debugger that it should always save temporary checkpoints,
    // for testing.
    void (*alwaysSaveTemporaryCheckpoints)();
};

extern Hooks hooks;

} // namespace replay
} // namespace JS

#endif /* js_ReplayHooks_h */
