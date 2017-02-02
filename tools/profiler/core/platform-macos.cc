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

#include "ThreadResponsiveness.h"
#include "nsThreadUtils.h"

// Memory profile
#include "nsMemoryReporterManager.h"

#include "platform.h"
#include "mozilla/TimeStamp.h"

using mozilla::TimeStamp;
using mozilla::TimeDuration;

// this port is based off of v8 svn revision 9837

// XXX: this is a very stubbed out implementation
// that only supports a single Sampler
struct SamplerRegistry {
  static void AddActiveSampler(Sampler *sampler) {
    ASSERT(!SamplerRegistry::sampler);
    SamplerRegistry::sampler = sampler;
  }
  static void RemoveActiveSampler(Sampler *sampler) {
    SamplerRegistry::sampler = NULL;
  }
  static Sampler *sampler;
};

Sampler *SamplerRegistry::sampler = NULL;

#ifdef DEBUG
// 0 is never a valid thread id on MacOSX since a pthread_t is a pointer.
static const pthread_t kNoThread = (pthread_t) 0;
#endif

void OS::Startup() {
}

void OS::Sleep(int milliseconds) {
  usleep(1000 * milliseconds);
}

void OS::SleepMicro(int microseconds) {
  usleep(microseconds);
}

class PlatformData {
 public:
  PlatformData() : profiled_thread_(mach_thread_self())
  {
  }

  ~PlatformData() {
    // Deallocate Mach port for thread.
    mach_port_deallocate(mach_task_self(), profiled_thread_);
  }

  thread_act_t profiled_thread() { return profiled_thread_; }

 private:
  // Note: for profiled_thread_ Mach primitives are used instead of PThread's
  // because the latter doesn't provide thread manipulation primitives required.
  // For details, consult "Mac OS X Internals" book, Section 7.3.
  thread_act_t profiled_thread_;
};

/* static */ auto
Sampler::AllocPlatformData(int aThreadId) -> UniquePlatformData
{
  return UniquePlatformData(new PlatformData);
}

void
Sampler::PlatformDataDestructor::operator()(PlatformData* aData)
{
  delete aData;
}

// SamplerThread objects are used for creating and running threads. When the
// Start() method is called the new thread starts running the Run() method in
// the new thread. The SamplerThread object should not be deallocated before
// the thread has terminated.
class SamplerThread
{
public:
  explicit SamplerThread(double interval)
    : mIntervalMicro(floor(interval * 1000 + 0.5))
  {
    if (mIntervalMicro <= 0) {
      mIntervalMicro = 1;
    }
  }

  static void SetThreadName() {
    // pthread_setname_np is only available in 10.6 or later, so test
    // for it at runtime.
    int (*dynamic_pthread_setname_np)(const char*);
    *reinterpret_cast<void**>(&dynamic_pthread_setname_np) =
      dlsym(RTLD_DEFAULT, "pthread_setname_np");
    if (!dynamic_pthread_setname_np)
      return;

    dynamic_pthread_setname_np("SamplerThread");
  }

  static void* ThreadEntry(void* aArg) {
    SamplerThread* thread = reinterpret_cast<SamplerThread*>(aArg);

    thread->mThread = pthread_self();
    SetThreadName();
    ASSERT(thread->mThread != kNoThread);
    thread->Run();
    return NULL;
  }

  void Start() {
    pthread_attr_t* attr_ptr = NULL;
    pthread_create(&mThread, attr_ptr, ThreadEntry, this);
    ASSERT(mThread != kNoThread);
  }

  void Join() {
    pthread_join(mThread, NULL);
  }

  static void AddActiveSampler(Sampler* sampler) {
    SamplerRegistry::AddActiveSampler(sampler);
    if (mInstance == NULL) {
      mInstance = new SamplerThread(sampler->interval());
      mInstance->Start();
    }
  }

  static void RemoveActiveSampler(Sampler* sampler) {
    mInstance->Join();
    //XXX: unlike v8 we need to remove the active sampler after doing the Join
    // because we drop the sampler immediately
    SamplerRegistry::RemoveActiveSampler(sampler);
    delete mInstance;
    mInstance = NULL;
  }

  void Run() {
    TimeDuration lastSleepOverhead = 0;
    TimeStamp sampleStart = TimeStamp::Now();
    while (SamplerRegistry::sampler->IsActive()) {
      SamplerRegistry::sampler->DeleteExpiredMarkers();
      if (!SamplerRegistry::sampler->IsPaused()) {
        MutexAutoLock lock(*Sampler::sRegisteredThreadsMutex);
        const std::vector<ThreadInfo*>& threads =
          SamplerRegistry::sampler->GetRegisteredThreads();
        bool isFirstProfiledThread = true;
        for (uint32_t i = 0; i < threads.size(); i++) {
          ThreadInfo* info = threads[i];

          // This will be null if we're not interested in profiling this thread.
          if (!info->Profile() || info->IsPendingDelete())
            continue;

          PseudoStack::SleepState sleeping = info->Stack()->observeSleeping();
          if (sleeping == PseudoStack::SLEEPING_AGAIN) {
            info->Profile()->DuplicateLastSample();
            continue;
          }

          info->Profile()->GetThreadResponsiveness()->Update();

          ThreadProfile* thread_profile = info->Profile();

          SampleContext(SamplerRegistry::sampler, thread_profile,
                        isFirstProfiledThread);
          isFirstProfiledThread = false;
        }
      }

      TimeStamp targetSleepEndTime = sampleStart + TimeDuration::FromMicroseconds(mIntervalMicro);
      TimeStamp beforeSleep = TimeStamp::Now();
      TimeDuration targetSleepDuration = targetSleepEndTime - beforeSleep;
      double sleepTime = std::max(0.0, (targetSleepDuration - lastSleepOverhead).ToMicroseconds());
      OS::SleepMicro(sleepTime);
      sampleStart = TimeStamp::Now();
      lastSleepOverhead = sampleStart - (beforeSleep + TimeDuration::FromMicroseconds(sleepTime));
    }
  }

  void SampleContext(Sampler* sampler, ThreadProfile* thread_profile,
                     bool isFirstProfiledThread)
  {
    thread_act_t profiled_thread =
      thread_profile->GetPlatformData()->profiled_thread();

    TickSample sample_obj;
    TickSample* sample = &sample_obj;

    // Unique Set Size is not supported on Mac.
    sample->ussMemory = 0;
    sample->rssMemory = 0;

    // XXX: this is an off-main-thread use of gSampler
    if (isFirstProfiledThread && gSampler->ProfileMemory()) {
      sample->rssMemory = nsMemoryReporterManager::ResidentFast();
    }

    // We're using thread_suspend on OS X because pthread_kill (which is what
    // we're using on Linux) has less consistent performance and causes
    // strange crashes, see bug 1166778 and bug 1166808.

    if (KERN_SUCCESS != thread_suspend(profiled_thread)) return;

#if V8_HOST_ARCH_X64
    thread_state_flavor_t flavor = x86_THREAD_STATE64;
    x86_thread_state64_t state;
    mach_msg_type_number_t count = x86_THREAD_STATE64_COUNT;
#if __DARWIN_UNIX03
#define REGISTER_FIELD(name) __r ## name
#else
#define REGISTER_FIELD(name) r ## name
#endif  // __DARWIN_UNIX03
#elif V8_HOST_ARCH_IA32
    thread_state_flavor_t flavor = i386_THREAD_STATE;
    i386_thread_state_t state;
    mach_msg_type_number_t count = i386_THREAD_STATE_COUNT;
#if __DARWIN_UNIX03
#define REGISTER_FIELD(name) __e ## name
#else
#define REGISTER_FIELD(name) e ## name
#endif  // __DARWIN_UNIX03
#else
#error Unsupported Mac OS X host architecture.
#endif  // V8_HOST_ARCH

    if (thread_get_state(profiled_thread,
                         flavor,
                         reinterpret_cast<natural_t*>(&state),
                         &count) == KERN_SUCCESS) {
      sample->pc = reinterpret_cast<Address>(state.REGISTER_FIELD(ip));
      sample->sp = reinterpret_cast<Address>(state.REGISTER_FIELD(sp));
      sample->fp = reinterpret_cast<Address>(state.REGISTER_FIELD(bp));
      sample->timestamp = mozilla::TimeStamp::Now();
      sample->threadProfile = thread_profile;

      sampler->Tick(sample);
    }
    thread_resume(profiled_thread);
  }

private:
  pthread_t mThread;

  int mIntervalMicro;

  static SamplerThread* mInstance;

  DISALLOW_COPY_AND_ASSIGN(SamplerThread);
};

#undef REGISTER_FIELD

SamplerThread* SamplerThread::mInstance = NULL;

void Sampler::Start() {
  ASSERT(!IsActive());
  SetActive(true);
  SamplerThread::AddActiveSampler(this);
}

void Sampler::Stop() {
  ASSERT(IsActive());
  SetActive(false);
  SamplerThread::RemoveActiveSampler(this);
}

/* static */ Thread::tid_t
Thread::GetCurrentId()
{
  return gettid();
}

void TickSample::PopulateContext(void* aContext)
{
  // Note that this asm changes if PopulateContext's parameter list is altered
#if defined(SPS_PLAT_amd64_darwin)
  asm (
      // Compute caller's %rsp by adding to %rbp:
      // 8 bytes for previous %rbp, 8 bytes for return address
      "leaq 0x10(%%rbp), %0\n\t"
      // Dereference %rbp to get previous %rbp
      "movq (%%rbp), %1\n\t"
      :
      "=r"(sp),
      "=r"(fp)
  );
#elif defined(SPS_PLAT_x86_darwin)
  asm (
      // Compute caller's %esp by adding to %ebp:
      // 4 bytes for aContext + 4 bytes for return address +
      // 4 bytes for previous %ebp
      "leal 0xc(%%ebp), %0\n\t"
      // Dereference %ebp to get previous %ebp
      "movl (%%ebp), %1\n\t"
      :
      "=r"(sp),
      "=r"(fp)
  );
#else
# error "Unsupported architecture"
#endif
  pc = reinterpret_cast<Address>(__builtin_extract_return_addr(
                                    __builtin_return_address(0)));
}

