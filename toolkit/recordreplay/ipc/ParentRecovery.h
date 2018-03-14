/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_toolkit_recordreplay_ipc_ParentRecovery_h
#define mozilla_toolkit_recordreplay_ipc_ParentRecovery_h

#include "ParentIPC.h"
#include "Channel.h"

namespace mozilla {
namespace recordreplay {
namespace recovery {

// This file has definitions for recovering a crashed or hung child process by
// directing a newly spawned child process to the same point of execution.

void BeginRecovery();

bool IsRecovering();

void NoteOutgoingMessage(const channel::Message& aMsg);

// Returns whether to handle the message in the normal fashion.
bool NoteIncomingMessage(const channel::Message& aMsg);

} // namespace recovery
} // namespace recordreplay
} // namespace mozilla

#endif // mozilla_toolkit_recordreplay_ipc_ParentRecovery_h
