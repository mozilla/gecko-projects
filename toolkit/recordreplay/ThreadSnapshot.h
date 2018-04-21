/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_toolkit_recordreplay_ThreadSnapshot_h
#define mozilla_toolkit_recordreplay_ThreadSnapshot_h

#include "File.h"

namespace mozilla {
namespace recordreplay {

// Thread Snapshots Overview.
//
// The functions below are used when a thread saves or restores its stack and
// register state at a checkpoint. The steps taken in saving and restoring a
// thread snapshot are as follows:
//
// 1. Before idling (non-main threads) or before reaching a checkpoint (main
//    thread), the thread calls SaveThreadState. This saves the register state
//    for the thread as well as a portion of the top of the stack, and after
//    saving the state it returns true.
//
// 2. Once all other threads are idle, the main thread calls SaveThreadStack on
//    every thread, saving the remainder of the stack contents. (The portion
//    saved earlier gives threads leeway to perform operations after saving
//    their stack, mainly for entering an idle state.)
//
// 3. The thread stacks are now stored on disk. Later on, the main thread may
//    ensure that all threads are idle and then call, for every thread,
//    RestoreStackForLoadingByThread. This loads the stacks and prepares them
//    for restoring by the associated threads.
//
// 4. While still in their idle state, threads call ShouldRestoreThreadStack to
//    see if there is stack information for them to restore.
//
// 5. If ShouldRestoreThreadStack returns true, RestoreThreadStack is then
//    called to restore the stack and register state to the point where
//    SaveThreadState was originally called.
//
// 6. RestoreThreadStack does not return. Instead, control transfers to the
//    call to SaveThreadState, which returns false after being restored to.

// aStackSeparator is a pointer into the stack. Values shallower than this in
// the stack will be preserved as they are at the time of the SaveThreadState
// call, whereas deeper values will be preserved as they are at the point where
// the main thread saves the remainder of the stack.
bool SaveThreadState(size_t aId, int* aStackSeparator);

void SaveThreadStack(UntrackedStream& aStream, size_t aId);
void RestoreStackForLoadingByThread(UntrackedStream& aStream, size_t aId);
bool ShouldRestoreThreadStack(size_t aId);
void RestoreThreadStack(size_t aId);

// Initialize state for taking thread snapshots.
void InitializeThreadSnapshots(size_t aNumThreads);

} // namespace recordreplay
} // namespace mozilla

#endif // mozilla_toolkit_recordreplay_ThreadSnapshot_h
