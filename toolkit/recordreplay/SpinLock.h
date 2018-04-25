/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_toolkit_recordreplay_SpinLock_h
#define mozilla_toolkit_recordreplay_SpinLock_h

#include "mozilla/Assertions.h"
#include "mozilla/Atomics.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/GuardObjects.h"

#ifdef XP_MACOSX
#include <sched.h>
#endif

namespace mozilla {
namespace recordreplay {

// This file provides a couple of primitive lock implementations that are
// implemented using atomic operations. Using these locks does not write to any
// heap locations other than the lock's members, nor will it call any system
// locking APIs. These locks are used in places where reentrance into APIs
// needs to be avoided, or where writes to heap memory are not allowed.

// A basic spin lock.
class SpinLock
{
public:
  inline void Lock();
  inline void Unlock();

private:
  Atomic<bool, SequentiallyConsistent, Behavior::DontPreserve> mLocked;
};

// A basic read/write spin lock. This lock permits either multiple readers and
// no writers, or one writer.
class ReadWriteSpinLock
{
public:
  inline void Lock(bool aRead);
  inline void Unlock(bool aRead);

private:
  SpinLock mLock; // Protects mReaders.
  int32_t mReaders; // -1 when in use for writing.
};

// RAII class to lock a spin lock.
struct MOZ_RAII AutoSpinLock
{
  explicit AutoSpinLock(SpinLock& aLock MOZ_GUARD_OBJECT_NOTIFIER_PARAM)
    : mLock(aLock)
  {
    MOZ_GUARD_OBJECT_NOTIFIER_INIT;
    mLock.Lock();
  }

  ~AutoSpinLock()
  {
    mLock.Unlock();
  }

private:
  MOZ_DECL_USE_GUARD_OBJECT_NOTIFIER
  SpinLock& mLock;
};

// RAII class to lock a read/write spin lock for reading.
struct AutoReadSpinLock
{
  explicit AutoReadSpinLock(ReadWriteSpinLock& aLock MOZ_GUARD_OBJECT_NOTIFIER_PARAM)
    : mLock(aLock)
  {
    MOZ_GUARD_OBJECT_NOTIFIER_INIT;
    mLock.Lock(/* aRead = */ true);
  }

  ~AutoReadSpinLock()
  {
    mLock.Unlock(/* aRead = */ true);
  }

private:
  MOZ_DECL_USE_GUARD_OBJECT_NOTIFIER
  ReadWriteSpinLock& mLock;
};

// RAII class to lock a read/write spin lock for writing.
struct AutoWriteSpinLock
{
  explicit AutoWriteSpinLock(ReadWriteSpinLock& aLock MOZ_GUARD_OBJECT_NOTIFIER_PARAM)
    : mLock(aLock)
  {
    MOZ_GUARD_OBJECT_NOTIFIER_INIT;
    mLock.Lock(/* aRead = */ false);
  }

  ~AutoWriteSpinLock()
  {
    mLock.Unlock(/* aRead = */ false);
  }

private:
  MOZ_DECL_USE_GUARD_OBJECT_NOTIFIER
  ReadWriteSpinLock& mLock;
};

///////////////////////////////////////////////////////////////////////////////
// Inline definitions
///////////////////////////////////////////////////////////////////////////////

// Try to yield execution to another thread.
static inline void
ThreadYield()
{
#ifdef XP_MACOSX
  sched_yield();
#elif defined(WIN32)
  SwitchToThread();
#else
  MOZ_CRASH();
#endif
}

inline void
SpinLock::Lock()
{
  while (mLocked.exchange(true)) {
    ThreadYield();
  }
}

inline void
SpinLock::Unlock()
{
  DebugOnly<bool> rv = mLocked.exchange(false);
  MOZ_ASSERT(rv);
}

inline void
ReadWriteSpinLock::Lock(bool aRead)
{
  bool done;
  do {
    AutoSpinLock ex(mLock);
    done = aRead ? (mReaders != -1) : (mReaders == 0);
    if (done) {
      mReaders = aRead ? (mReaders + 1) : -1;
    }
  } while (!done);
}

inline void
ReadWriteSpinLock::Unlock(bool aRead)
{
  AutoSpinLock ex(mLock);
  if (aRead) {
    MOZ_ASSERT(mReaders > 0);
    mReaders--;
  } else {
    MOZ_ASSERT(mReaders == -1);
    mReaders = 0;
  }
}

} // namespace recordreplay
} // namespace mozilla

#endif // mozilla_toolkit_recordreplay_SpinLock_h
