/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <dlfcn.h>
#include <unistd.h>
#include <sys/mman.h>
#include <mach/mach_init.h>
#include <mach-o/dyld.h>
#include <mach-o/getsect.h>

#include <AvailabilityMacros.h>

#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <libkern/OSAtomic.h>
#include <mach/mach.h>
#include <mach/semaphore.h>
#include <mach/task.h>
#include <mach/vm_statistics.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <math.h>

// this port is based off of v8 svn revision 9837

/* static */ Thread::tid_t
Thread::GetCurrentId()
{
  return gettid();
}

static void
SleepMicro(int aMicroseconds)
{
  aMicroseconds = std::max(0, aMicroseconds);

  usleep(aMicroseconds);
  // FIXME: the OSX 10.12 page for usleep says "The usleep() function is
  // obsolescent.  Use nanosleep(2) instead."  This implementation could be
  // merged with the linux-android version.  Also, this doesn't handle the
  // case where the usleep call is interrupted by a signal.
}

class PlatformData
{
public:
  explicit PlatformData(int aThreadId) : profiled_thread_(mach_thread_self())
  {
    MOZ_COUNT_CTOR(PlatformData);
  }

  ~PlatformData() {
    // Deallocate Mach port for thread.
    mach_port_deallocate(mach_task_self(), profiled_thread_);

    MOZ_COUNT_DTOR(PlatformData);
  }

  thread_act_t profiled_thread() { return profiled_thread_; }

private:
  // Note: for profiled_thread_ Mach primitives are used instead of PThread's
  // because the latter doesn't provide thread manipulation primitives required.
  // For details, consult "Mac OS X Internals" book, Section 7.3.
  thread_act_t profiled_thread_;
};

////////////////////////////////////////////////////////////////////////
// BEGIN SamplerThread target specifics

static void
SetThreadName()
{
  // pthread_setname_np is only available in 10.6 or later, so test
  // for it at runtime.
  int (*dynamic_pthread_setname_np)(const char*);
  *reinterpret_cast<void**>(&dynamic_pthread_setname_np) =
    dlsym(RTLD_DEFAULT, "pthread_setname_np");
  if (!dynamic_pthread_setname_np)
    return;

  dynamic_pthread_setname_np("SamplerThread");
}

static void*
ThreadEntry(void* aArg)
{
  auto thread = static_cast<SamplerThread*>(aArg);
  SetThreadName();
  thread->Run();
  return nullptr;
}

SamplerThread::SamplerThread(PS::LockRef aLock, uint32_t aActivityGeneration,
                             double aIntervalMilliseconds)
  : mActivityGeneration(aActivityGeneration)
  , mIntervalMicroseconds(
      std::max(1, int(floor(aIntervalMilliseconds * 1000 + 0.5))))
{
  MOZ_RELEASE_ASSERT(NS_IsMainThread());

  pthread_attr_t* attr_ptr = nullptr;
  if (pthread_create(&mThread, attr_ptr, ThreadEntry, this) != 0) {
    MOZ_CRASH("pthread_create failed");
  }
}

SamplerThread::~SamplerThread()
{
  pthread_join(mThread, nullptr);
}

void
SamplerThread::Stop(PS::LockRef aLock)
{
  MOZ_RELEASE_ASSERT(NS_IsMainThread());
}

void
SamplerThread::SuspendAndSampleAndResumeThread(PS::LockRef aLock,
                                               TickSample* aSample)
{
  thread_act_t samplee_thread =
    aSample->mThreadInfo->GetPlatformData()->profiled_thread();

  //----------------------------------------------------------------//
  // Suspend the samplee thread and get its context.

  // We're using thread_suspend on OS X because pthread_kill (which is what we
  // at one time used on Linux) has less consistent performance and causes
  // strange crashes, see bug 1166778 and bug 1166808.  thread_suspend
  // is also just a lot simpler to use.

  if (KERN_SUCCESS != thread_suspend(samplee_thread)) {
    return;
  }

  //----------------------------------------------------------------//
  // Sample the target thread.

  // WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING
  //
  // The profiler's "critical section" begins here.  We must be very careful
  // what we do here, or risk deadlock.  See the corresponding comment in
  // platform-linux-android.cpp for details.

#if defined(GP_ARCH_amd64)
  thread_state_flavor_t flavor = x86_THREAD_STATE64;
  x86_thread_state64_t state;
  mach_msg_type_number_t count = x86_THREAD_STATE64_COUNT;
# if __DARWIN_UNIX03
#  define REGISTER_FIELD(name) __r ## name
# else
#  define REGISTER_FIELD(name) r ## name
# endif  // __DARWIN_UNIX03

#elif defined(GP_ARCH_x86)
  thread_state_flavor_t flavor = i386_THREAD_STATE;
  i386_thread_state_t state;
  mach_msg_type_number_t count = i386_THREAD_STATE_COUNT;
# if __DARWIN_UNIX03
#  define REGISTER_FIELD(name) __e ## name
# else
#  define REGISTER_FIELD(name) e ## name
# endif  // __DARWIN_UNIX03

#else
# error Unsupported Mac OS X host architecture.
#endif  // GP_ARCH_*

  if (thread_get_state(samplee_thread,
                       flavor,
                       reinterpret_cast<natural_t*>(&state),
                       &count) == KERN_SUCCESS) {
    aSample->mPC = reinterpret_cast<Address>(state.REGISTER_FIELD(ip));
    aSample->mSP = reinterpret_cast<Address>(state.REGISTER_FIELD(sp));
    aSample->mFP = reinterpret_cast<Address>(state.REGISTER_FIELD(bp));

    Tick(aLock, gPS->Buffer(aLock), aSample);
  }

#undef REGISTER_FIELD

  //----------------------------------------------------------------//
  // Resume the target thread.

  thread_resume(samplee_thread);

  // The profiler's critical section ends here.
  //
  // WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING
}

// END SamplerThread target specifics
////////////////////////////////////////////////////////////////////////

static void
PlatformInit(PS::LockRef aLock)
{
}

void
TickSample::PopulateContext(void* aContext)
{
  MOZ_ASSERT(!aContext);
  // Note that this asm changes if PopulateContext's parameter list is altered
#if defined(GP_ARCH_amd64)
  asm (
      // Compute caller's %rsp by adding to %rbp:
      // 8 bytes for previous %rbp, 8 bytes for return address
      "leaq 0x10(%%rbp), %0\n\t"
      // Dereference %rbp to get previous %rbp
      "movq (%%rbp), %1\n\t"
      :
      "=r"(mSP),
      "=r"(mFP)
  );
#elif defined(GP_ARCH_x86)
  asm (
      // Compute caller's %esp by adding to %ebp:
      // 4 bytes for aContext + 4 bytes for return address +
      // 4 bytes for previous %ebp
      "leal 0xc(%%ebp), %0\n\t"
      // Dereference %ebp to get previous %ebp
      "movl (%%ebp), %1\n\t"
      :
      "=r"(mSP),
      "=r"(mFP)
  );
#else
# error "Unsupported architecture"
#endif
  mPC = reinterpret_cast<Address>(__builtin_extract_return_addr(
                                    __builtin_return_address(0)));
}

