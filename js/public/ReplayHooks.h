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

// These hooks are used for transmitting messages between a ReplayDebugger in
// a middleman process and corresponding state in the replaying process.
struct Hooks
{
    // Send a JSON message to or from the replayed process.
    void (*debugRequestMiddleman)(const CharBuffer& buffer, CharBuffer* response);
    void (*debugRequestReplay)(CharBuffer* buffer);
    void (*debugResponseReplay)(const CharBuffer& buffer);

    // Allow the replayed process to resume execution.
    void (*resumeMiddleman)(bool forward, bool hitOtherBreakpoint);
    void (*resumeReplay)(bool forward, bool hitOtherBreakpoints);
    void (*pauseMiddleman)();

    // Notify the middleman about a breakpoint that was hit.
    void (*hitBreakpointReplay)(size_t id);
    bool (*hitBreakpointMiddleman)(JSContext* cx, size_t id);

    // Notify the middleman about a snapshot that was hit.
    void (*hitSnapshotReplay)(size_t id, bool final, bool interim, bool recorded);
};

extern Hooks hooks;

} // namespace replay
} // namespace JS

#endif /* js_ReplayHooks_h */
