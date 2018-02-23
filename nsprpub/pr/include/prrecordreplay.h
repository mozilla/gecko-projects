/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef prrecordreplay_h___
#define prrecordreplay_h___

#include "prlog.h"
#include "prtypes.h"

PR_BEGIN_EXTERN_C

NSPR_DATA_API(PRBool) gPRIsRecordingOrReplaying;

// Whether the current process is recording or replaying an execution.
static inline PRBool PR_IsRecordingOrReplaying() { return gPRIsRecordingOrReplaying; }

// Initialize record/replay state for the process.
void PR_RecordReplayInitialize();

// Load an external interface used while recording or replaying.
extern void* PR_RecordReplayLoadInterface(const char* aName);

// Helper for lazily loading a record/replay interface and calling it.
#define PR_CALL_RECORD_REPLAY_INTERFACE(aName, aActuals)        \
  PR_BEGIN_MACRO                                                \
    PR_ASSERT(PR_IsRecordingOrReplaying());                     \
    static void *gCallback;                                     \
    if (!gCallback) {                                           \
      gCallback = PR_RecordReplayLoadInterface(aName);          \
      PR_ASSERT(gCallback);                                     \
    }                                                           \
    ((void (*)()) gCallback) aActuals;                          \
  PR_END_MACRO

PR_END_EXTERN_C

#endif /* prrecordreplay_h___ */
