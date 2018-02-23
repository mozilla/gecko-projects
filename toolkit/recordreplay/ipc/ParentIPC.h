/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_toolkit_recordreplay_ipc_ParentIPC_h
#define mozilla_toolkit_recordreplay_ipc_ParentIPC_h

#include "mozilla/dom/ContentChild.h"
#include "mozilla/ipc/MessageChannel.h"
#include "mozilla/ipc/ProcessChild.h"
#include "mozilla/ipc/ProtocolUtils.h"
#include "mozilla/ipc/ScopedXREEmbed.h"
#include "mozilla/ipc/Shmem.h"
#include "js/ReplayHooks.h"

namespace mozilla {
namespace recordreplay {
namespace parent {

// The middleman process is a content process that manages communication with
// the replaying process. It performs IPC with the chrome process in the normal
// fashion for a content process, using the normal IPDL protocols.
// Communication with the replaying process is done via the PReplay protocol,
// with all the restrictions that entails (see PReplay.ipdl). This file is
// responsible for managing communication with the replaying process.

///////////////////////////////////////////////////////////////////////////////
// Public API
///////////////////////////////////////////////////////////////////////////////

// Save the recording up to the current point in execution.
void SaveRecording(const nsCString& aFilename);

// Get the message channel used to communicate with the UI process.
ipc::MessageChannel* ChannelToUIProcess();

void
Initialize(int aArgc, char* aArgv[], base::ProcessId aParentPid, uint64_t aChildID,
           dom::ContentChild* aContentChild);

} // namespace parent
} // namespace recordreplay
} // namespace mozilla

#endif // mozilla_toolkit_recordreplay_ipc_ParentIPC_h
