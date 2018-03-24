/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Hooks for communication between debuggers in a replayed process.

#ifndef js_ReplayHooks_h
#define js_ReplayHooks_h

#include "jsapi.h"

#include "mozilla/Vector.h"

namespace JS {
namespace replay {

typedef mozilla::Vector<char16_t> CharBuffer;

// Identification for a position during JS execution in the replaying process.
struct ExecutionPosition
{
    enum Kind {
        Invalid,
        Break,       // No frameIndex
        OnStep,
        OnPop,       // No offset, script/frameIndex is optional
        EnterFrame,  // No offset/script/frameIndex
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
        }
        MOZ_CRASH("Bad ExecutionPosition kind");
    }
};

// These hooks are used for transmitting messages between a ReplayDebugger in
// a middleman process and corresponding state in the replaying process.
struct Hooks
{
    // Send a JSON message to or from the replayed process.
    void (*debugRequestMiddleman)(const CharBuffer& buffer, CharBuffer* response);
    void (*debugRequestReplay)(CharBuffer* buffer);
    void (*debugResponseReplay)(const CharBuffer& buffer);

    // Set or clear a breakpoint in the replayed process.
    void (*setBreakpointMiddleman)(size_t id, const ExecutionPosition& pos);
    void (*setBreakpointReplay)(size_t id, const ExecutionPosition& pos);

    // Allow the replayed process to resume execution.
    void (*resumeMiddleman)(bool forward, bool hitOtherBreakpoint);
    void (*resumeReplay)(bool forward, bool hitOtherBreakpoints);
    void (*pauseMiddleman)();

    // Notify the middleman about a breakpoint that was hit.
    void (*hitBreakpointReplay)(size_t id, bool recoveringFromDivergence);
    bool (*hitBreakpointMiddleman)(JSContext* cx, size_t id);

    // Notify the middleman about a snapshot that was hit.
    void (*hitSnapshotReplay)(size_t id, bool final, bool interim);

    // Return whether the middleman is able to rewind the replayed process.
    bool (*canRewindMiddleman)();

    // Finish recovering from an unhandled divergence at a breakpoint, and send
    // a response to the middleman for the last request.
    void (*respondAfterRecoveringFromDivergence)();

    // Notify the debugger that it should always take temporary snapshots.
    void (*alwaysTakeTemporarySnapshots)();
};

extern Hooks hooks;

} // namespace replay
} // namespace JS

#endif /* js_ReplayHooks_h */
