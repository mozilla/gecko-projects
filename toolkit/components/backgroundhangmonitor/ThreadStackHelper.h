/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_ThreadStackHelper_h
#define mozilla_ThreadStackHelper_h

#include "js/ProfilingStack.h"
#include "HangDetails.h"
#include "nsThread.h"

#include <stddef.h>

#if defined(XP_LINUX)
#include <signal.h>
#include <semaphore.h>
#include <sys/types.h>
#elif defined(XP_WIN)
#include <windows.h>
#elif defined(XP_MACOSX)
#include <mach/mach.h>
#endif

// Support pseudostack and native stack on these platforms.
#if defined(XP_LINUX) || defined(XP_WIN) || defined(XP_MACOSX)
#  ifdef MOZ_GECKO_PROFILER
#    define MOZ_THREADSTACKHELPER_PSEUDO
#    define MOZ_THREADSTACKHELPER_NATIVE
#  endif
#endif


// Android x86 builds consistently crash in the Background Hang Reporter. bug
// 1368520.
#if defined(__ANDROID__)
#  undef MOZ_THREADSTACKHELPER_PSEUDO
#  undef MOZ_THREADSTACKHELPER_NATIVE
#endif

namespace mozilla {

/**
 * ThreadStackHelper is used to retrieve the profiler pseudo-stack of a
 * thread, as an alternative of using the profiler to take a profile.
 * The target thread first declares an ThreadStackHelper instance;
 * then another thread can call ThreadStackHelper::GetStack to retrieve
 * the pseudo-stack of the target thread at that instant.
 *
 * Only non-copying labels are included in the stack, which means labels
 * with custom text and markers are not included.
 */
class ThreadStackHelper : public ProfilerStackCollector
{
public:
  // When a native stack is gathered, this vector holds the raw program counter
  // values that FramePointerStackWalk will return to us after it walks the
  // stack. When gathering the Telemetry payload, Telemetry will take care of
  // mapping these program counters to proper addresses within modules.
  typedef NativeHangStack NativeStack;

private:
  HangStack* mStackToFill;
  Array<char, nsThread::kRunnableNameBufSize>* mRunnableNameBuffer;
  // const PseudoStack* const mPseudoStack;
  size_t mMaxStackSize;
  size_t mMaxBufferSize;
  size_t mDesiredStackSize;
  size_t mDesiredBufferSize;

  bool PrepareStackBuffer(HangStack& aStack);

public:
  /**
   * Create a ThreadStackHelper instance targeting the current thread.
   */
  ThreadStackHelper();

  /**
   * Retrieve the current interleaved stack of the thread associated with this ThreadStackHelper.
   *
   * @param aStack        HangStack instance to be filled.
   * @param aRunnableName The name of the current runnable on the target thread.
   * @param aStackWalk    If true, native stack frames will be collected
   *                      along with pseudostack frames.
   */
  void GetStack(HangStack& aStack, nsACString& aRunnableName, bool aStackWalk);

protected:
  /**
   * ProfilerStackCollector
   */
  virtual void SetIsMainThread() override;
  virtual void CollectNativeLeafAddr(void* aAddr) override;
  virtual void CollectJitReturnAddr(void* aAddr) override;
  virtual void CollectWasmFrame(const char* aLabel) override;
  virtual void CollectPseudoEntry(const js::ProfileEntry& aEntry) override;

private:
  void TryAppendFrame(mozilla::HangStack::Frame aFrame);

  // The profiler's unique thread identifier for the target thread.
  int mThreadId;
};

} // namespace mozilla

#endif // mozilla_ThreadStackHelper_h
