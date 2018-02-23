/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_toolkit_recordreplay_Monitor_h
#define mozilla_toolkit_recordreplay_Monitor_h

#include "prcvar.h"

#include <pthread.h>

namespace mozilla {
namespace recordreplay {

// Simple wrapper around a PRLock and PRCondVar. This is a lighter weight
// abstraction than mozilla::Monitor and has simpler interactions with the
// record/replay system.
class Monitor
{
public:
  Monitor() {
    AutoEnsurePassThroughThreadEvents pt;
    mLock = PR_NewLock();
    mCondVar = PR_NewCondVar(mLock);
  }

  ~Monitor() {
    PR_DestroyLock(mLock);
    PR_DestroyCondVar(mCondVar);
  }

  void Lock() { PR_Lock(mLock); }
  void Unlock() { PR_Unlock(mLock); }
  void Wait() { PR_WaitCondVar(mCondVar, PR_INTERVAL_NO_TIMEOUT); }
  void Notify() { PR_NotifyCondVar(mCondVar); }
  void NotifyAll() { PR_NotifyAllCondVar(mCondVar); }

private:
  PRLock* mLock;
  PRCondVar* mCondVar;
};

// RAII class to lock a monitor.
struct MOZ_RAII MonitorAutoLock
{
  MonitorAutoLock(Monitor& aMonitor MOZ_GUARD_OBJECT_NOTIFIER_PARAM)
    : mMonitor(aMonitor)
  {
    MOZ_GUARD_OBJECT_NOTIFIER_INIT;
    mMonitor.Lock();
  }

  ~MonitorAutoLock()
  {
    mMonitor.Unlock();
  }

private:
  MOZ_DECL_USE_GUARD_OBJECT_NOTIFIER
  Monitor& mMonitor;
};

// RAII class to unlock a monitor.
struct MOZ_RAII MonitorAutoUnlock
{
  MonitorAutoUnlock(Monitor& aMonitor MOZ_GUARD_OBJECT_NOTIFIER_PARAM)
    : mMonitor(aMonitor)
  {
    MOZ_GUARD_OBJECT_NOTIFIER_INIT;
    mMonitor.Unlock();
  }

  ~MonitorAutoUnlock()
  {
    mMonitor.Lock();
  }

private:
  MOZ_DECL_USE_GUARD_OBJECT_NOTIFIER
  Monitor& mMonitor;
};

} // namespace recordreplay
} // namespace mozilla

#endif // mozilla_toolkit_recordreplay_Monitor_h
