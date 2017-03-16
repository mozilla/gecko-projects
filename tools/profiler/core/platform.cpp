/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <algorithm>
#include <ostream>
#include <fstream>
#include <sstream>
#include <errno.h>

#include "platform.h"
#include "PlatformMacros.h"
#include "mozilla/ArrayUtils.h"
#include "mozilla/Atomics.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/Vector.h"
#include "GeckoProfiler.h"
#include "ProfilerIOInterposeObserver.h"
#include "mozilla/StackWalk.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/ThreadLocal.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/Sprintf.h"
#include "mozilla/StaticPtr.h"
#include "PseudoStack.h"
#include "ThreadInfo.h"
#include "nsIHttpProtocolHandler.h"
#include "nsIObserverService.h"
#include "nsIProfileSaveEvent.h"
#include "nsIXULAppInfo.h"
#include "nsIXULRuntime.h"
#include "nsDirectoryServiceUtils.h"
#include "nsDirectoryServiceDefs.h"
#include "nsXULAppAPI.h"
#include "nsProfilerStartParams.h"
#include "mozilla/Services.h"
#include "nsThreadUtils.h"
#include "ProfileGatherer.h"
#include "ProfilerMarkers.h"
#include "shared-libraries.h"

#ifdef MOZ_TASK_TRACER
#include "GeckoTaskTracer.h"
#endif

#if defined(PROFILE_JAVA)
# include "FennecJNINatives.h"
# include "FennecJNIWrappers.h"
#endif

#if defined(MOZ_PROFILING) && \
    (defined(GP_OS_windows) || defined(GP_OS_darwin))
# define USE_NS_STACKWALK
#endif

// This should also work on ARM Linux, but not tested there yet.
#if defined(GP_arm_android)
# define USE_EHABI_STACKWALK
# include "EHABIStackWalk.h"
#endif

#if defined(GP_PLAT_amd64_linux) || defined(GP_PLAT_x86_linux)
# define USE_LUL_STACKWALK
# include "lul/LulMain.h"
# include "lul/platform-linux-lul.h"
#endif

#ifdef MOZ_VALGRIND
# include <valgrind/memcheck.h>
#else
# define VALGRIND_MAKE_MEM_DEFINED(_addr,_len)   ((void)0)
#endif

#if defined(GP_OS_windows)
typedef CONTEXT tickcontext_t;
#elif defined(GP_OS_linux) || defined(GP_OS_android)
#include <ucontext.h>
typedef ucontext_t tickcontext_t;
#endif

using namespace mozilla;

#if defined(PROFILE_JAVA)
class GeckoJavaSampler : public mozilla::java::GeckoJavaSampler::Natives<GeckoJavaSampler>
{
private:
  GeckoJavaSampler();

public:
  static double GetProfilerTime() {
    if (!profiler_is_active()) {
      return 0.0;
    }
    return profiler_time();
  };
};
#endif

class SamplerThread;

// Per-thread state.
MOZ_THREAD_LOCAL(PseudoStack *) tlsPseudoStack;

// This class contains most of the profiler's global state. gPS is the single
// instance. Most profile operations can't do anything useful when gPS is not
// instantiated, so we release-assert its non-nullness in all such operations.
//
// Accesses to gPS are guarded by gPSMutex. Every getter and setter takes a
// PS::AutoLock reference as an argument as proof that the gPSMutex is
// currently locked. This makes it clear when gPSMutex is locked and helps
// avoid accidental unlocked accesses to global state. There are ways to
// circumvent this mechanism, but please don't do so without *very* good reason
// and a detailed explanation.
//
// Other from the lock protection, this class is essentially a thin wrapper and
// contains very little "smarts" itself.
//
class ProfilerState
{
public:
  // Shorter names for local use.
  typedef ProfilerStateMutex Mutex;
  typedef mozilla::BaseAutoLock<Mutex> AutoLock;

  // Only functions that take a LockRef arg can modify this class's fields.
  typedef const AutoLock& LockRef;

  typedef std::vector<ThreadInfo*> ThreadVector;

  ProfilerState()
    : mEnvVarEntries(0)
    , mEntries(0)
    , mEnvVarInterval(0)
    , mInterval(0)
    , mFeatureDisplayListDump(false)
    , mFeatureGPU(false)
    , mFeatureJava(false)
    , mFeatureJS(false)
    , mFeatureLayersDump(false)
    , mFeatureLeaf(false)
    , mFeatureMemory(false)
    , mFeaturePrivacy(false)
    , mFeatureRestyle(false)
    , mFeatureStackWalk(false)
    , mFeatureTaskTracer(false)
    , mFeatureThreads(false)
    , mBuffer(nullptr)
    , mGatherer(nullptr)
    , mIsPaused(false)
#if defined(GP_OS_linux) || defined(GP_OS_android)
    , mWasPaused(false)
#endif
    , mSamplerThread(nullptr)
#ifdef USE_LUL_STACKWALK
    , mLUL(nullptr)
#endif
    , mInterposeObserver(nullptr)
    , mFrameNumber(0)
    , mLatestRecordedFrameNumber(0)
  {}

  #define GET_AND_SET(type_, name_) \
    type_ name_(LockRef) const { return m##name_; } \
    void Set##name_(LockRef, type_ a##name_) { m##name_ = a##name_; }

  GET_AND_SET(TimeStamp, StartTime)

  GET_AND_SET(int, EnvVarEntries)
  GET_AND_SET(int, Entries)

  GET_AND_SET(int, EnvVarInterval)
  GET_AND_SET(double, Interval)

  Vector<std::string>& Features(LockRef) { return mFeatures; }

  Vector<std::string>& ThreadNameFilters(LockRef) { return mThreadNameFilters; }

  GET_AND_SET(bool, FeatureDisplayListDump)
  GET_AND_SET(bool, FeatureGPU)
  GET_AND_SET(bool, FeatureJava)
  GET_AND_SET(bool, FeatureJS)
  GET_AND_SET(bool, FeatureLayersDump)
  GET_AND_SET(bool, FeatureLeaf)
  GET_AND_SET(bool, FeatureMemory)
  GET_AND_SET(bool, FeaturePrivacy)
  GET_AND_SET(bool, FeatureRestyle)
  GET_AND_SET(bool, FeatureStackWalk)
  GET_AND_SET(bool, FeatureTaskTracer)
  GET_AND_SET(bool, FeatureThreads)

  GET_AND_SET(ProfileBuffer*, Buffer)

  GET_AND_SET(ProfileGatherer*, Gatherer)

  ThreadVector& Threads(LockRef) { return mThreads; }

  static bool IsActive(LockRef) { return sActivityGeneration > 0; }
  static uint32_t ActivityGeneration(LockRef) { return sActivityGeneration; }
  static void SetInactive(LockRef) { sActivityGeneration = 0; }
  static void SetActive(LockRef)
  {
    sActivityGeneration = sNextActivityGeneration;
    // On overflow, reset to 1 instead of 0, because 0 means inactive.
    sNextActivityGeneration = (sNextActivityGeneration == 0xffffffff)
                            ? 1
                            : sNextActivityGeneration + 1;
  }

  GET_AND_SET(bool, IsPaused)

#if defined(GP_OS_linux) || defined(GP_OS_android)
  GET_AND_SET(bool, WasPaused)
#endif

  GET_AND_SET(class SamplerThread*, SamplerThread)

#ifdef USE_LUL_STACKWALK
  GET_AND_SET(lul::LUL*, LUL)
#endif

  GET_AND_SET(mozilla::ProfilerIOInterposeObserver*, InterposeObserver)

  GET_AND_SET(int, FrameNumber)
  GET_AND_SET(int, LatestRecordedFrameNumber)

  #undef GET_AND_SET

private:
  // When profiler_init() or profiler_start() was most recently called.
  mozilla::TimeStamp mStartTime;

  // The number of entries in mBuffer. mEnvVarEntries comes from an environment
  // variable and can override the value passed in to profiler_start(). Zeroed
  // when the profiler is inactive.
  int mEnvVarEntries;
  int mEntries;

  // The interval between samples, measured in milliseconds. mEnvVarInterval
  // comes from an environment variable and can override the value passed in to
  // profiler_start(). Zeroed when the profiler is inactive.
  int mEnvVarInterval;
  double mInterval;

  // The profile features that are enabled. Cleared when the profiler is
  // inactive.
  Vector<std::string> mFeatures;

  // Substrings of names of threads we want to profile. Cleared when the
  // profiler is inactive
  Vector<std::string> mThreadNameFilters;

  // Configuration flags derived from mFeatures. Cleared when the profiler is
  // inactive.
  bool mFeatureDisplayListDump;
  bool mFeatureGPU;
  bool mFeatureJava;
  bool mFeatureJS;
  bool mFeatureLayersDump;
  bool mFeatureLeaf;
  bool mFeatureMemory;
  bool mFeaturePrivacy;
  bool mFeatureRestyle;
  bool mFeatureStackWalk;
  bool mFeatureTaskTracer;
  bool mFeatureThreads;

  // The buffer into which all samples are recorded. Always used in conjunction
  // with mThreads. Null when the profiler is inactive.
  ProfileBuffer* mBuffer;

  // A helper class that is used when saving profiles. Null when the profiler
  // is inactive.
  RefPtr<mozilla::ProfileGatherer> mGatherer;

  // All the registered threads.
  ThreadVector mThreads;

  // Is the profiler active? The obvious way to track this is with a bool,
  // sIsActive, but then we could have the following scenario.
  //
  // - profiler_stop() locks gPSMutex, zeroes sIsActive, unlocks gPSMutex,
  //   deletes the SamplerThread (which does a join).
  //
  // - profiler_start() runs on a different thread, locks gPSMutex, sets
  //   sIsActive, unlocks gPSMutex -- all before the join completes.
  //
  // - SamplerThread::Run() locks gPSMutex, sees that sIsActive is set, and
  //   continues as if the start/stop pair didn't occur. Also profiler_stop()
  //   is stuck, unable to finish.
  //
  // Instead, we use an integer, sActivityGeneration; zero means inactive,
  // non-zero means active. Furthermore, each time the profiler is activated
  // the value increases by 1 (as tracked by sNextActivityGeneration). This
  // allows SamplerThread::Run() to distinguish the current activation from any
  // subsequent activations.
  //
  // These variables are static because they can be referred to by
  // SamplerThread::Run() even after gPS has been destroyed by
  // profiler_shutdown().
  static uint32_t sActivityGeneration;
  static uint32_t sNextActivityGeneration;

  // Is the profiler paused? False when the profiler is inactive.
  bool mIsPaused;

#if defined(GP_OS_linux) || defined(GP_OS_android)
  // Used to record whether the profiler was paused just before forking. False
  // at all times except just before/after forking.
  bool mWasPaused;
#endif

  // The current sampler thread. Null when the profiler is inactive.
  class SamplerThread* mSamplerThread;

#ifdef USE_LUL_STACKWALK
  // LUL's state. Null prior to the first activation, non-null thereafter.
  lul::LUL* mLUL;
#endif

  // The interposer that records main thread I/O. Null when the profiler is
  // inactive.
  mozilla::ProfilerIOInterposeObserver* mInterposeObserver;

  // The current frame number and the most recent frame number recorded in a
  // sample.
  int mFrameNumber;
  int mLatestRecordedFrameNumber;
};

// A shorter name for use within this compilation unit.
typedef ProfilerState PS;

uint32_t PS::sActivityGeneration = 0;
uint32_t PS::sNextActivityGeneration = 1;

// The profiler state. Set by profiler_init(), cleared by profiler_shutdown().
PS* gPS = nullptr;

// The mutex that guards accesses to gPS.
static PS::Mutex gPSMutex;

// The name of the main thread.
static const char* const kMainThreadName = "GeckoMain";

static bool
CanNotifyObservers()
{
  MOZ_RELEASE_ASSERT(NS_IsMainThread());

#if defined(GP_OS_android)
  // Android ANR reporter uses the profiler off the main thread.
  return NS_IsMainThread();
#else
  return true;
#endif
}

////////////////////////////////////////////////////////////////////////
// BEGIN tick/unwinding code

// TickSample captures the information collected for each sample.
class TickSample {
public:
  TickSample()
    : pc(NULL)
    , sp(NULL)
    , fp(NULL)
    , lr(NULL)
    , context(NULL)
    , isSamplingCurrentThread(false)
    , threadInfo(nullptr)
    , rssMemory(0)
    , ussMemory(0)
  {}

  void PopulateContext(void* aContext);

  Address pc;  // Instruction pointer.
  Address sp;  // Stack pointer.
  Address fp;  // Frame pointer.
  Address lr;  // ARM link register
  void* context;   // The context from the signal handler, if available. On
                   // Win32 this may contain the windows thread context.
  bool isSamplingCurrentThread;
  ThreadInfo* threadInfo;
  mozilla::TimeStamp timestamp;
  int64_t rssMemory;
  int64_t ussMemory;
};

static void
AddDynamicCodeLocationTag(ProfileBuffer* aBuffer, const char* aStr)
{
  aBuffer->addTag(ProfileBufferEntry::CodeLocation(""));

  size_t strLen = strlen(aStr) + 1;   // +1 for the null terminator
  for (size_t j = 0; j < strLen; ) {
    // Store as many characters in the void* as the platform allows.
    char text[sizeof(void*)];
    size_t len = sizeof(void*) / sizeof(char);
    if (j+len >= strLen) {
      len = strLen - j;
    }
    memcpy(text, &aStr[j], len);
    j += sizeof(void*) / sizeof(char);

    // Cast to *((void**) to pass the text data to a void*.
    aBuffer->addTag(ProfileBufferEntry::EmbeddedString(*((void**)(&text[0]))));
  }
}

static void
AddPseudoEntry(ProfileBuffer* aBuffer, volatile js::ProfileEntry& entry,
               PseudoStack* stack, void* lastpc)
{
  // Pseudo-frames with the BEGIN_PSEUDO_JS flag are just annotations and
  // should not be recorded in the profile.
  if (entry.hasFlag(js::ProfileEntry::BEGIN_PSEUDO_JS)) {
    return;
  }

  int lineno = -1;

  // First entry has kind CodeLocation. Check for magic pointer bit 1 to
  // indicate copy.
  const char* sampleLabel = entry.label();

  if (entry.isCopyLabel()) {
    // Store the string using 1 or more EmbeddedString tags.
    // That will happen to the preceding tag.
    AddDynamicCodeLocationTag(aBuffer, sampleLabel);
    if (entry.isJs()) {
      JSScript* script = entry.script();
      if (script) {
        if (!entry.pc()) {
          // The JIT only allows the top-most entry to have a nullptr pc.
          MOZ_ASSERT(&entry == &stack->mStack[stack->stackSize() - 1]);

          // If stack-walking was disabled, then that's just unfortunate.
          if (lastpc) {
            jsbytecode* jspc = js::ProfilingGetPC(stack->mContext, script,
                                                  lastpc);
            if (jspc) {
              lineno = JS_PCToLineNumber(script, jspc);
            }
          }
        } else {
          lineno = JS_PCToLineNumber(script, entry.pc());
        }
      }
    } else {
      lineno = entry.line();
    }
  } else {
    aBuffer->addTag(ProfileBufferEntry::CodeLocation(sampleLabel));

    // XXX: Bug 1010578. Don't assume a CPP entry and try to get the line for
    // js entries as well.
    if (entry.isCpp()) {
      lineno = entry.line();
    }
  }

  if (lineno != -1) {
    aBuffer->addTag(ProfileBufferEntry::LineNumber(lineno));
  }

  uint32_t category = entry.category();
  MOZ_ASSERT(!(category & js::ProfileEntry::IS_CPP_ENTRY));
  MOZ_ASSERT(!(category & js::ProfileEntry::FRAME_LABEL_COPY));

  if (category) {
    aBuffer->addTag(ProfileBufferEntry::Category((int)category));
  }
}

struct NativeStack
{
  void** pc_array;
  void** sp_array;
  size_t size;
  size_t count;
};

mozilla::Atomic<bool> WALKING_JS_STACK(false);

struct AutoWalkJSStack
{
  bool walkAllowed;

  AutoWalkJSStack() : walkAllowed(false) {
    walkAllowed = WALKING_JS_STACK.compareExchange(false, true);
  }

  ~AutoWalkJSStack() {
    if (walkAllowed) {
      WALKING_JS_STACK = false;
    }
  }
};

static void
MergeStacksIntoProfile(ProfileBuffer* aBuffer, TickSample* aSample,
                       NativeStack& aNativeStack)
{
  NotNull<PseudoStack*> pseudoStack = aSample->threadInfo->Stack();
  volatile js::ProfileEntry* pseudoFrames = pseudoStack->mStack;
  uint32_t pseudoCount = pseudoStack->stackSize();

  // Make a copy of the JS stack into a JSFrame array. This is necessary since,
  // like the native stack, the JS stack is iterated youngest-to-oldest and we
  // need to iterate oldest-to-youngest when adding entries to aInfo.

  // Synchronous sampling reports an invalid buffer generation to
  // ProfilingFrameIterator to avoid incorrectly resetting the generation of
  // sampled JIT entries inside the JS engine. See note below concerning 'J'
  // entries.
  uint32_t startBufferGen;
  startBufferGen = aSample->isSamplingCurrentThread
                 ? UINT32_MAX
                 : aBuffer->mGeneration;
  uint32_t jsCount = 0;
  JS::ProfilingFrameIterator::Frame jsFrames[1000];

  // Only walk jit stack if profiling frame iterator is turned on.
  if (pseudoStack->mContext &&
      JS::IsProfilingEnabledForContext(pseudoStack->mContext)) {
    AutoWalkJSStack autoWalkJSStack;
    const uint32_t maxFrames = mozilla::ArrayLength(jsFrames);

    if (aSample && autoWalkJSStack.walkAllowed) {
      JS::ProfilingFrameIterator::RegisterState registerState;
      registerState.pc = aSample->pc;
      registerState.sp = aSample->sp;
      registerState.lr = aSample->lr;

      JS::ProfilingFrameIterator jsIter(pseudoStack->mContext,
                                        registerState,
                                        startBufferGen);
      for (; jsCount < maxFrames && !jsIter.done(); ++jsIter) {
        // See note below regarding 'J' entries.
        if (aSample->isSamplingCurrentThread || jsIter.isWasm()) {
          uint32_t extracted =
            jsIter.extractStack(jsFrames, jsCount, maxFrames);
          jsCount += extracted;
          if (jsCount == maxFrames) {
            break;
          }
        } else {
          mozilla::Maybe<JS::ProfilingFrameIterator::Frame> frame =
            jsIter.getPhysicalFrameWithoutLabel();
          if (frame.isSome()) {
            jsFrames[jsCount++] = frame.value();
          }
        }
      }
    }
  }

  // Start the sample with a root entry.
  aBuffer->addTag(ProfileBufferEntry::Sample("(root)"));

  // While the pseudo-stack array is ordered oldest-to-youngest, the JS and
  // native arrays are ordered youngest-to-oldest. We must add frames to aInfo
  // oldest-to-youngest. Thus, iterate over the pseudo-stack forwards and JS
  // and native arrays backwards. Note: this means the terminating condition
  // jsIndex and nativeIndex is being < 0.
  uint32_t pseudoIndex = 0;
  int32_t jsIndex = jsCount - 1;
  int32_t nativeIndex = aNativeStack.count - 1;

  uint8_t* lastPseudoCppStackAddr = nullptr;

  // Iterate as long as there is at least one frame remaining.
  while (pseudoIndex != pseudoCount || jsIndex >= 0 || nativeIndex >= 0) {
    // There are 1 to 3 frames available. Find and add the oldest.
    uint8_t* pseudoStackAddr = nullptr;
    uint8_t* jsStackAddr = nullptr;
    uint8_t* nativeStackAddr = nullptr;

    if (pseudoIndex != pseudoCount) {
      volatile js::ProfileEntry& pseudoFrame = pseudoFrames[pseudoIndex];

      if (pseudoFrame.isCpp()) {
        lastPseudoCppStackAddr = (uint8_t*) pseudoFrame.stackAddress();
      }

      // Skip any pseudo-stack JS frames which are marked isOSR. Pseudostack
      // frames are marked isOSR when the JS interpreter enters a jit frame on
      // a loop edge (via on-stack-replacement, or OSR). To avoid both the
      // pseudoframe and jit frame being recorded (and showing up twice), the
      // interpreter marks the interpreter pseudostack entry with the OSR flag
      // to ensure that it doesn't get counted.
      if (pseudoFrame.isJs() && pseudoFrame.isOSR()) {
          pseudoIndex++;
          continue;
      }

      MOZ_ASSERT(lastPseudoCppStackAddr);
      pseudoStackAddr = lastPseudoCppStackAddr;
    }

    if (jsIndex >= 0) {
      jsStackAddr = (uint8_t*) jsFrames[jsIndex].stackAddress;
    }

    if (nativeIndex >= 0) {
      nativeStackAddr = (uint8_t*) aNativeStack.sp_array[nativeIndex];
    }

    // If there's a native stack entry which has the same SP as a pseudo stack
    // entry, pretend we didn't see the native stack entry.  Ditto for a native
    // stack entry which has the same SP as a JS stack entry.  In effect this
    // means pseudo or JS entries trump conflicting native entries.
    if (nativeStackAddr && (pseudoStackAddr == nativeStackAddr ||
                            jsStackAddr == nativeStackAddr)) {
      nativeStackAddr = nullptr;
      nativeIndex--;
      MOZ_ASSERT(pseudoStackAddr || jsStackAddr);
    }

    // Sanity checks.
    MOZ_ASSERT_IF(pseudoStackAddr, pseudoStackAddr != jsStackAddr &&
                                   pseudoStackAddr != nativeStackAddr);
    MOZ_ASSERT_IF(jsStackAddr, jsStackAddr != pseudoStackAddr &&
                               jsStackAddr != nativeStackAddr);
    MOZ_ASSERT_IF(nativeStackAddr, nativeStackAddr != pseudoStackAddr &&
                                   nativeStackAddr != jsStackAddr);

    // Check to see if pseudoStack frame is top-most.
    if (pseudoStackAddr > jsStackAddr && pseudoStackAddr > nativeStackAddr) {
      MOZ_ASSERT(pseudoIndex < pseudoCount);
      volatile js::ProfileEntry& pseudoFrame = pseudoFrames[pseudoIndex];
      AddPseudoEntry(aBuffer, pseudoFrame, pseudoStack, nullptr);
      pseudoIndex++;
      continue;
    }

    // Check to see if JS jit stack frame is top-most
    if (jsStackAddr > nativeStackAddr) {
      MOZ_ASSERT(jsIndex >= 0);
      const JS::ProfilingFrameIterator::Frame& jsFrame = jsFrames[jsIndex];

      // Stringifying non-wasm JIT frames is delayed until streaming time. To
      // re-lookup the entry in the JitcodeGlobalTable, we need to store the
      // JIT code address (OptInfoAddr) in the circular buffer.
      //
      // Note that we cannot do this when we are sychronously sampling the
      // current thread; that is, when called from profiler_get_backtrace. The
      // captured backtrace is usually externally stored for an indeterminate
      // amount of time, such as in nsRefreshDriver. Problematically, the
      // stored backtrace may be alive across a GC during which the profiler
      // itself is disabled. In that case, the JS engine is free to discard its
      // JIT code. This means that if we inserted such OptInfoAddr entries into
      // the buffer, nsRefreshDriver would now be holding on to a backtrace
      // with stale JIT code return addresses.
      if (aSample->isSamplingCurrentThread ||
          jsFrame.kind == JS::ProfilingFrameIterator::Frame_Wasm) {
        AddDynamicCodeLocationTag(aBuffer, jsFrame.label);
      } else {
        MOZ_ASSERT(jsFrame.kind == JS::ProfilingFrameIterator::Frame_Ion ||
                   jsFrame.kind == JS::ProfilingFrameIterator::Frame_Baseline);
        aBuffer->addTag(
          ProfileBufferEntry::JitReturnAddr(jsFrames[jsIndex].returnAddress));
      }

      jsIndex--;
      continue;
    }

    // If we reach here, there must be a native stack entry and it must be the
    // greatest entry.
    if (nativeStackAddr) {
      MOZ_ASSERT(nativeIndex >= 0);
      void* addr = (void*)aNativeStack.pc_array[nativeIndex];
      aBuffer->addTag(ProfileBufferEntry::NativeLeafAddr(addr));
    }
    if (nativeIndex >= 0) {
      nativeIndex--;
    }
  }

  // Update the JS context with the current profile sample buffer generation.
  //
  // Do not do this for synchronous sampling, which create their own
  // ProfileBuffers.
  if (!aSample->isSamplingCurrentThread && pseudoStack->mContext) {
    MOZ_ASSERT(aBuffer->mGeneration >= startBufferGen);
    uint32_t lapCount = aBuffer->mGeneration - startBufferGen;
    JS::UpdateJSContextProfilerSampleBufferGen(pseudoStack->mContext,
                                               aBuffer->mGeneration,
                                               lapCount);
  }
}

#if defined(GP_OS_windows)
static uintptr_t GetThreadHandle(PlatformData* aData);
#endif

#ifdef USE_NS_STACKWALK
static void
StackWalkCallback(uint32_t aFrameNumber, void* aPC, void* aSP, void* aClosure)
{
  NativeStack* nativeStack = static_cast<NativeStack*>(aClosure);
  MOZ_ASSERT(nativeStack->count < nativeStack->size);
  nativeStack->sp_array[nativeStack->count] = aSP;
  nativeStack->pc_array[nativeStack->count] = aPC;
  nativeStack->count++;
}

static void
DoNativeBacktrace(PS::LockRef aLock, ProfileBuffer* aBuffer,
                  TickSample* aSample)
{
  void* pc_array[1000];
  void* sp_array[1000];
  NativeStack nativeStack = {
    pc_array,
    sp_array,
    mozilla::ArrayLength(pc_array),
    0
  };

  // Start with the current function. We use 0 as the frame number here because
  // the FramePointerStackWalk() and MozStackWalk() calls below will use 1..N.
  // This is a bit weird but it doesn't matter because StackWalkCallback()
  // doesn't use the frame number argument.
  StackWalkCallback(/* frameNum */ 0, aSample->pc, aSample->sp, &nativeStack);

  uint32_t maxFrames = uint32_t(nativeStack.size - nativeStack.count);

#if defined(GP_OS_darwin) || (defined(GP_PLAT_x86_windows))
  void* stackEnd = aSample->threadInfo->StackTop();
  if (aSample->fp >= aSample->sp && aSample->fp <= stackEnd) {
    FramePointerStackWalk(StackWalkCallback, /* skipFrames */ 0, maxFrames,
                          &nativeStack, reinterpret_cast<void**>(aSample->fp),
                          stackEnd);
  }
#else
  // Win64 always omits frame pointers so for it we use the slower
  // MozStackWalk().
  uintptr_t thread = GetThreadHandle(aSample->threadInfo->GetPlatformData());
  MOZ_ASSERT(thread);
  MozStackWalk(StackWalkCallback, /* skipFrames */ 0, maxFrames, &nativeStack,
               thread, /* platformData */ nullptr);
#endif

  MergeStacksIntoProfile(aBuffer, aSample, nativeStack);
}
#endif

#ifdef USE_EHABI_STACKWALK
static void
DoNativeBacktrace(PS::LockRef aLock, ProfileBuffer* aBuffer,
                  TickSample* aSample)
{
  void* pc_array[1000];
  void* sp_array[1000];
  NativeStack nativeStack = {
    pc_array,
    sp_array,
    mozilla::ArrayLength(pc_array),
    0
  };

  const mcontext_t* mcontext =
    &reinterpret_cast<ucontext_t*>(aSample->context)->uc_mcontext;
  mcontext_t savedContext;
  NotNull<PseudoStack*> pseudoStack = aInfo.Stack();

  nativeStack.count = 0;

  // The pseudostack contains an "EnterJIT" frame whenever we enter
  // JIT code with profiling enabled; the stack pointer value points
  // the saved registers.  We use this to unwind resume unwinding
  // after encounting JIT code.
  for (uint32_t i = pseudoStack->stackSize(); i > 0; --i) {
    // The pseudostack grows towards higher indices, so we iterate
    // backwards (from callee to caller).
    volatile js::ProfileEntry& entry = pseudoStack->mStack[i - 1];
    if (!entry.isJs() && strcmp(entry.label(), "EnterJIT") == 0) {
      // Found JIT entry frame.  Unwind up to that point (i.e., force
      // the stack walk to stop before the block of saved registers;
      // note that it yields nondecreasing stack pointers), then restore
      // the saved state.
      uint32_t* vSP = reinterpret_cast<uint32_t*>(entry.stackAddress());

      nativeStack.count += EHABIStackWalk(*mcontext,
                                          /* stackBase = */ vSP,
                                          sp_array + nativeStack.count,
                                          pc_array + nativeStack.count,
                                          nativeStack.size - nativeStack.count);

      memset(&savedContext, 0, sizeof(savedContext));

      // See also: struct EnterJITStack in js/src/jit/arm/Trampoline-arm.cpp
      savedContext.arm_r4  = *vSP++;
      savedContext.arm_r5  = *vSP++;
      savedContext.arm_r6  = *vSP++;
      savedContext.arm_r7  = *vSP++;
      savedContext.arm_r8  = *vSP++;
      savedContext.arm_r9  = *vSP++;
      savedContext.arm_r10 = *vSP++;
      savedContext.arm_fp  = *vSP++;
      savedContext.arm_lr  = *vSP++;
      savedContext.arm_sp  = reinterpret_cast<uint32_t>(vSP);
      savedContext.arm_pc  = savedContext.arm_lr;
      mcontext = &savedContext;
    }
  }

  // Now unwind whatever's left (starting from either the last EnterJIT frame
  // or, if no EnterJIT was found, the original registers).
  nativeStack.count += EHABIStackWalk(*mcontext,
                                      aInfo.StackTop(),
                                      sp_array + nativeStack.count,
                                      pc_array + nativeStack.count,
                                      nativeStack.size - nativeStack.count);

  MergeStacksIntoProfile(aInfo, aSample, nativeStack);
}
#endif

#ifdef USE_LUL_STACKWALK
static void
DoNativeBacktrace(PS::LockRef aLock, ProfileBuffer* aBuffer,
                  TickSample* aSample)
{
  const mcontext_t* mc =
    &reinterpret_cast<ucontext_t*>(aSample->context)->uc_mcontext;

  lul::UnwindRegs startRegs;
  memset(&startRegs, 0, sizeof(startRegs));

#if defined(GP_PLAT_amd64_linux)
  startRegs.xip = lul::TaggedUWord(mc->gregs[REG_RIP]);
  startRegs.xsp = lul::TaggedUWord(mc->gregs[REG_RSP]);
  startRegs.xbp = lul::TaggedUWord(mc->gregs[REG_RBP]);
#elif defined(GP_PLAT_arm_android)
  startRegs.r15 = lul::TaggedUWord(mc->arm_pc);
  startRegs.r14 = lul::TaggedUWord(mc->arm_lr);
  startRegs.r13 = lul::TaggedUWord(mc->arm_sp);
  startRegs.r12 = lul::TaggedUWord(mc->arm_ip);
  startRegs.r11 = lul::TaggedUWord(mc->arm_fp);
  startRegs.r7  = lul::TaggedUWord(mc->arm_r7);
#elif defined(GP_PLAT_x86_linux) || defined(GP_PLAT_x86_android)
  startRegs.xip = lul::TaggedUWord(mc->gregs[REG_EIP]);
  startRegs.xsp = lul::TaggedUWord(mc->gregs[REG_ESP]);
  startRegs.xbp = lul::TaggedUWord(mc->gregs[REG_EBP]);
#else
# error "Unknown plat"
#endif

  // Copy up to N_STACK_BYTES from rsp-REDZONE upwards, but not going past the
  // stack's registered top point.  Do some basic sanity checks too.  This
  // assumes that the TaggedUWord holding the stack pointer value is valid, but
  // it should be, since it was constructed that way in the code just above.

  lul::StackImage stackImg;

  {
#if defined(GP_PLAT_amd64_linux)
    uintptr_t rEDZONE_SIZE = 128;
    uintptr_t start = startRegs.xsp.Value() - rEDZONE_SIZE;
#elif defined(GP_PLAT_arm_android)
    uintptr_t rEDZONE_SIZE = 0;
    uintptr_t start = startRegs.r13.Value() - rEDZONE_SIZE;
#elif defined(GP_PLAT_x86_linux) || defined(GP_PLAT_x86_android)
    uintptr_t rEDZONE_SIZE = 0;
    uintptr_t start = startRegs.xsp.Value() - rEDZONE_SIZE;
#else
#   error "Unknown plat"
#endif
    uintptr_t end = reinterpret_cast<uintptr_t>(aSample->threadInfo->StackTop());
    uintptr_t ws  = sizeof(void*);
    start &= ~(ws-1);
    end   &= ~(ws-1);
    uintptr_t nToCopy = 0;
    if (start < end) {
      nToCopy = end - start;
      if (nToCopy > lul::N_STACK_BYTES)
        nToCopy = lul::N_STACK_BYTES;
    }
    MOZ_ASSERT(nToCopy <= lul::N_STACK_BYTES);
    stackImg.mLen       = nToCopy;
    stackImg.mStartAvma = start;
    if (nToCopy > 0) {
      memcpy(&stackImg.mContents[0], (void*)start, nToCopy);
      (void)VALGRIND_MAKE_MEM_DEFINED(&stackImg.mContents[0], nToCopy);
    }
  }

  // The maximum number of frames that LUL will produce.  Setting it
  // too high gives a risk of it wasting a lot of time looping on
  // corrupted stacks.
  const int MAX_NATIVE_FRAMES = 256;

  size_t scannedFramesAllowed = 0;

  uintptr_t framePCs[MAX_NATIVE_FRAMES];
  uintptr_t frameSPs[MAX_NATIVE_FRAMES];
  size_t framesAvail = mozilla::ArrayLength(framePCs);
  size_t framesUsed  = 0;
  size_t scannedFramesAcquired = 0;
  lul::LUL* lul = gPS->LUL(aLock);
  lul->Unwind(&framePCs[0], &frameSPs[0],
              &framesUsed, &scannedFramesAcquired,
              framesAvail, scannedFramesAllowed,
              &startRegs, &stackImg);

  NativeStack nativeStack = {
    reinterpret_cast<void**>(framePCs),
    reinterpret_cast<void**>(frameSPs),
    mozilla::ArrayLength(framePCs),
    0
  };

  nativeStack.count = framesUsed;

  MergeStacksIntoProfile(aBuffer, aSample, nativeStack);

  // Update stats in the LUL stats object.  Unfortunately this requires
  // three global memory operations.
  lul->mStats.mContext += 1;
  lul->mStats.mCFI     += framesUsed - 1 - scannedFramesAcquired;
  lul->mStats.mScanned += scannedFramesAcquired;
}
#endif

static void
DoSampleStackTrace(PS::LockRef aLock, ProfileBuffer* aBuffer,
                   TickSample* aSample)
{
  NativeStack nativeStack = { nullptr, nullptr, 0, 0 };
  MergeStacksIntoProfile(aBuffer, aSample, nativeStack);

  if (gPS->FeatureLeaf(aLock)) {
    aBuffer->addTag(ProfileBufferEntry::NativeLeafAddr((void*)aSample->pc));
  }
}

// This function is called for each sampling period with the current program
// counter. It is called within a signal and so must be re-entrant.
static void
Tick(PS::LockRef aLock, ProfileBuffer* aBuffer, TickSample* aSample)
{
  ThreadInfo& threadInfo = *aSample->threadInfo;

  aBuffer->addTag(ProfileBufferEntry::ThreadId(threadInfo.ThreadId()));

  mozilla::TimeDuration delta = aSample->timestamp - gPS->StartTime(aLock);
  aBuffer->addTag(ProfileBufferEntry::Time(delta.ToMilliseconds()));

  NotNull<PseudoStack*> stack = threadInfo.Stack();

#if defined(USE_NS_STACKWALK) || defined(USE_EHABI_STACKWALK) || \
    defined(USE_LUL_STACKWALK)
  if (gPS->FeatureStackWalk(aLock)) {
    DoNativeBacktrace(aLock, aBuffer, aSample);
  } else {
    DoSampleStackTrace(aLock, aBuffer, aSample);
  }
#else
  DoSampleStackTrace(aLock, aBuffer, aSample);
#endif

  // Don't process the PeudoStack's markers if we're synchronously sampling the
  // current thread.
  if (!aSample->isSamplingCurrentThread) {
    ProfilerMarkerLinkedList* pendingMarkersList = stack->getPendingMarkers();
    while (pendingMarkersList && pendingMarkersList->peek()) {
      ProfilerMarker* marker = pendingMarkersList->popHead();
      aBuffer->addStoredMarker(marker);
      aBuffer->addTag(ProfileBufferEntry::Marker(marker));
    }
  }

  if (threadInfo.GetThreadResponsiveness()->HasData()) {
    mozilla::TimeDuration delta =
      threadInfo.GetThreadResponsiveness()->GetUnresponsiveDuration(
        aSample->timestamp);
    aBuffer->addTag(ProfileBufferEntry::Responsiveness(delta.ToMilliseconds()));
  }

  // rssMemory is equal to 0 when we are not recording.
  if (aSample->rssMemory != 0) {
    double rssMemory = static_cast<double>(aSample->rssMemory);
    aBuffer->addTag(ProfileBufferEntry::ResidentMemory(rssMemory));
  }

  // ussMemory is equal to 0 when we are not recording.
  if (aSample->ussMemory != 0) {
    double ussMemory = static_cast<double>(aSample->ussMemory);
    aBuffer->addTag(ProfileBufferEntry::UnsharedMemory(ussMemory));
  }

  int frameNumber = gPS->FrameNumber(aLock);
  if (frameNumber != gPS->LatestRecordedFrameNumber(aLock)) {
    aBuffer->addTag(ProfileBufferEntry::FrameNumber(frameNumber));
    gPS->SetLatestRecordedFrameNumber(aLock, frameNumber);
  }
}

// END tick/unwinding code
////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////
// BEGIN saving/streaming code

class ProfileSaveEvent final : public nsIProfileSaveEvent
{
public:
  typedef void (*AddSubProfileFunc)(const char* aProfile, void* aClosure);
  NS_DECL_ISUPPORTS

  ProfileSaveEvent(AddSubProfileFunc aFunc, void* aClosure)
    : mFunc(aFunc)
    , mClosure(aClosure)
  {}

  NS_IMETHOD AddSubProfile(const char* aProfile) override {
    mFunc(aProfile, mClosure);
    return NS_OK;
  }

private:
  ~ProfileSaveEvent() {}

  AddSubProfileFunc mFunc;
  void* mClosure;
};

NS_IMPL_ISUPPORTS(ProfileSaveEvent, nsIProfileSaveEvent)

static void
AddSharedLibraryInfoToStream(std::ostream& aStream, const SharedLibrary& aLib)
{
  aStream << "{";
  aStream << "\"start\":" << aLib.GetStart();
  aStream << ",\"end\":" << aLib.GetEnd();
  aStream << ",\"offset\":" << aLib.GetOffset();
  aStream << ",\"name\":\"" << aLib.GetNativeDebugName() << "\"";
  const std::string& breakpadId = aLib.GetBreakpadId();
  aStream << ",\"breakpadId\":\"" << breakpadId << "\"";
  aStream << "}";
}

static std::string
GetSharedLibraryInfoStringInternal()
{
  SharedLibraryInfo info = SharedLibraryInfo::GetInfoForSelf();
  if (info.GetSize() == 0) {
    return "[]";
  }

  std::ostringstream os;
  os << "[";
  AddSharedLibraryInfoToStream(os, info.GetEntry(0));

  for (size_t i = 1; i < info.GetSize(); i++) {
    os << ",";
    AddSharedLibraryInfoToStream(os, info.GetEntry(i));
  }

  os << "]";
  return os.str();
}

static void
StreamTaskTracer(PS::LockRef aLock, SpliceableJSONWriter& aWriter)
{
#ifdef MOZ_TASK_TRACER
  aWriter.StartArrayProperty("data");
  {
    UniquePtr<nsTArray<nsCString>> data =
      mozilla::tasktracer::GetLoggedData(gPS->StartTime(aLock));
    for (uint32_t i = 0; i < data->Length(); ++i) {
      aWriter.StringElement((data->ElementAt(i)).get());
    }
  }
  aWriter.EndArray();

  aWriter.StartArrayProperty("threads");
  {
    const PS::ThreadVector& threads = gPS->Threads(aLock);
    for (size_t i = 0; i < threads.size(); i++) {
      // Thread meta data
      ThreadInfo* info = threads.at(i);
      aWriter.StartObjectElement();
      {
        if (XRE_GetProcessType() == GeckoProcessType_Plugin) {
          // TODO Add the proper plugin name
          aWriter.StringProperty("name", "Plugin");
        } else {
          aWriter.StringProperty("name", info->Name());
        }
        aWriter.IntProperty("tid", static_cast<int>(info->ThreadId()));
      }
      aWriter.EndObject();
    }
  }
  aWriter.EndArray();

  aWriter.DoubleProperty(
    "start", static_cast<double>(mozilla::tasktracer::GetStartTime()));
#endif
}

static void
StreamMetaJSCustomObject(PS::LockRef aLock, SpliceableJSONWriter& aWriter)
{
  MOZ_RELEASE_ASSERT(NS_IsMainThread());

  aWriter.IntProperty("version", 3);
  aWriter.DoubleProperty("interval", gPS->Interval(aLock));
  aWriter.IntProperty("stackwalk", gPS->FeatureStackWalk(aLock));

#ifdef DEBUG
  aWriter.IntProperty("debug", 1);
#else
  aWriter.IntProperty("debug", 0);
#endif

  aWriter.IntProperty("gcpoison", JS::IsGCPoisoning() ? 1 : 0);

  bool asyncStacks = Preferences::GetBool("javascript.options.asyncstack");
  aWriter.IntProperty("asyncstack", asyncStacks);

  // The "startTime" field holds the number of milliseconds since midnight
  // January 1, 1970 GMT. This grotty code computes (Now - (Now - StartTime))
  // to convert gPS->StartTime() into that form.
  mozilla::TimeDuration delta =
    mozilla::TimeStamp::Now() - gPS->StartTime(aLock);
  aWriter.DoubleProperty(
    "startTime", static_cast<double>(PR_Now()/1000.0 - delta.ToMilliseconds()));

  aWriter.IntProperty("processType", XRE_GetProcessType());

  nsresult res;
  nsCOMPtr<nsIHttpProtocolHandler> http =
    do_GetService(NS_NETWORK_PROTOCOL_CONTRACTID_PREFIX "http", &res);

  if (!NS_FAILED(res)) {
    nsAutoCString string;

    res = http->GetPlatform(string);
    if (!NS_FAILED(res)) {
      aWriter.StringProperty("platform", string.Data());
    }

    res = http->GetOscpu(string);
    if (!NS_FAILED(res)) {
      aWriter.StringProperty("oscpu", string.Data());
    }

    res = http->GetMisc(string);
    if (!NS_FAILED(res)) {
      aWriter.StringProperty("misc", string.Data());
    }
  }

  nsCOMPtr<nsIXULRuntime> runtime = do_GetService("@mozilla.org/xre/runtime;1");
  if (runtime) {
    nsAutoCString string;

    res = runtime->GetXPCOMABI(string);
    if (!NS_FAILED(res))
      aWriter.StringProperty("abi", string.Data());

    res = runtime->GetWidgetToolkit(string);
    if (!NS_FAILED(res))
      aWriter.StringProperty("toolkit", string.Data());
  }

  nsCOMPtr<nsIXULAppInfo> appInfo =
    do_GetService("@mozilla.org/xre/app-info;1");

  if (appInfo) {
    nsAutoCString string;
    res = appInfo->GetName(string);
    if (!NS_FAILED(res))
      aWriter.StringProperty("product", string.Data());
  }
}

struct SubprocessClosure
{
  explicit SubprocessClosure(SpliceableJSONWriter* aWriter)
    : mWriter(aWriter)
  {}

  SpliceableJSONWriter* mWriter;
};

static void
SubProcessCallback(const char* aProfile, void* aClosure)
{
  // Called by the observer to get their profile data included as a sub profile.
  SubprocessClosure* closure = (SubprocessClosure*)aClosure;

  // Add the string profile into the profile.
  closure->mWriter->StringElement(aProfile);
}

#if defined(PROFILE_JAVA)
static void
BuildJavaThreadJSObject(SpliceableJSONWriter& aWriter)
{
  aWriter.StringProperty("name", "Java Main Thread");

  aWriter.StartArrayProperty("samples");
  {
    for (int sampleId = 0; true; sampleId++) {
      bool firstRun = true;
      for (int frameId = 0; true; frameId++) {
        jni::String::LocalRef frameName =
            java::GeckoJavaSampler::GetFrameName(0, sampleId, frameId);

        // When we run out of frames, we stop looping.
        if (!frameName) {
          // If we found at least one frame, we have objects to close.
          if (!firstRun) {
            aWriter.EndArray();
            aWriter.EndObject();
          }
          break;
        }
        // The first time around, open the sample object and frames array.
        if (firstRun) {
          firstRun = false;

          double sampleTime =
              java::GeckoJavaSampler::GetSampleTime(0, sampleId);

          aWriter.StartObjectElement();
            aWriter.DoubleProperty("time", sampleTime);

            aWriter.StartArrayProperty("frames");
        }

        // Add a frame to the sample.
        aWriter.StartObjectElement();
        {
          aWriter.StringProperty("location",
                                 frameName->ToCString().BeginReading());
        }
        aWriter.EndObject();
      }

      // If we found no frames for this sample, we are done.
      if (firstRun) {
        break;
      }
    }
  }
  aWriter.EndArray();
}
#endif

static void
StreamJSON(PS::LockRef aLock, SpliceableJSONWriter& aWriter, double aSinceTime)
{
  MOZ_RELEASE_ASSERT(NS_IsMainThread());
  MOZ_RELEASE_ASSERT(gPS && gPS->IsActive(aLock));

  aWriter.Start(SpliceableJSONWriter::SingleLineStyle);
  {
    // Put shared library info
    aWriter.StringProperty("libs",
                           GetSharedLibraryInfoStringInternal().c_str());

    // Put meta data
    aWriter.StartObjectProperty("meta");
    {
      StreamMetaJSCustomObject(aLock, aWriter);
    }
    aWriter.EndObject();

    // Data of TaskTracer doesn't belong in the circular buffer.
    if (gPS->FeatureTaskTracer(aLock)) {
      aWriter.StartObjectProperty("tasktracer");
      StreamTaskTracer(aLock, aWriter);
      aWriter.EndObject();
    }

    // Lists the samples for each thread profile
    aWriter.StartArrayProperty("threads");
    {
      gPS->SetIsPaused(aLock, true);

      {
        const PS::ThreadVector& threads = gPS->Threads(aLock);
        for (size_t i = 0; i < threads.size(); i++) {
          // Thread not being profiled, skip it
          ThreadInfo* info = threads.at(i);
          if (!info->HasProfile()) {
            continue;
          }

          // Note that we intentionally include thread profiles which
          // have been marked for pending delete.

          info->StreamJSON(gPS->Buffer(aLock), aWriter, gPS->StartTime(aLock),
                           aSinceTime);
        }
      }

      // When notifying observers in other places in this file we are careful
      // to do it when gPSMutex is unlocked, to avoid deadlocks. But that's not
      // necessary here, because "profiler-subprocess" observers just call back
      // into SubprocessCallback, which is simple and doesn't lock gPSMutex.
      if (CanNotifyObservers()) {
        // Send a event asking any subprocesses (plugins) to
        // give us their information
        SubprocessClosure closure(&aWriter);
        nsCOMPtr<nsIObserverService> os =
          mozilla::services::GetObserverService();
        if (os) {
          RefPtr<ProfileSaveEvent> pse =
            new ProfileSaveEvent(SubProcessCallback, &closure);
          os->NotifyObservers(pse, "profiler-subprocess", nullptr);
        }
      }

#if defined(PROFILE_JAVA)
      if (gPS->FeatureJava(aLock)) {
        java::GeckoJavaSampler::Pause();

        aWriter.Start();
        {
          BuildJavaThreadJSObject(aWriter);
        }
        aWriter.End();

        java::GeckoJavaSampler::Unpause();
      }
#endif

      gPS->SetIsPaused(aLock, false);
    }
    aWriter.EndArray();
  }
  aWriter.End();
}

UniquePtr<char[]>
ToJSON(PS::LockRef aLock, double aSinceTime)
{
  MOZ_RELEASE_ASSERT(NS_IsMainThread());
  MOZ_RELEASE_ASSERT(gPS && gPS->IsActive(aLock));

  SpliceableChunkedJSONWriter b;
  StreamJSON(aLock, b, aSinceTime);
  return b.WriteFunc()->CopyData();
}

// END saving/streaming code
////////////////////////////////////////////////////////////////////////

ProfilerMarker::ProfilerMarker(const char* aMarkerName,
                               ProfilerMarkerPayload* aPayload,
                               double aTime)
  : mMarkerName(strdup(aMarkerName))
  , mPayload(aPayload)
  , mTime(aTime)
{
}

ProfilerMarker::~ProfilerMarker() {
  free(mMarkerName);
  delete mPayload;
}

void
ProfilerMarker::SetGeneration(uint32_t aGenID) {
  mGenID = aGenID;
}

double
ProfilerMarker::GetTime() const {
  return mTime;
}

void ProfilerMarker::StreamJSON(SpliceableJSONWriter& aWriter,
                                const TimeStamp& aStartTime,
                                UniqueStacks& aUniqueStacks) const
{
  // Schema:
  //   [name, time, data]

  aWriter.StartArrayElement();
  {
    aUniqueStacks.mUniqueStrings.WriteElement(aWriter, GetMarkerName());
    aWriter.DoubleElement(mTime);
    // TODO: Store the callsite for this marker if available:
    // if have location data
    //   b.NameValue(marker, "location", ...);
    if (mPayload) {
      aWriter.StartObjectElement();
      {
          mPayload->StreamPayload(aWriter, aStartTime, aUniqueStacks);
      }
      aWriter.EndObject();
    }
  }
  aWriter.EndArray();
}

// Verbosity control for the profiler.  The aim is to check env var
// MOZ_PROFILER_VERBOSE only once.

enum class Verbosity : int8_t { UNCHECKED, NOTVERBOSE, VERBOSE };

// The verbosity global and the mutex used to protect it. Unlike other globals
// in this file, gVerbosity is not within ProfilerState because it can be used
// before gPS is created.
static Verbosity gVerbosity = Verbosity::UNCHECKED;
static StaticMutex gVerbosityMutex;

bool
profiler_verbose()
{
  StaticMutexAutoLock lock(gVerbosityMutex);

  if (gVerbosity == Verbosity::UNCHECKED) {
    gVerbosity = getenv("MOZ_PROFILER_VERBOSE")
               ? Verbosity::VERBOSE
               : Verbosity::NOTVERBOSE;
  }

  return gVerbosity == Verbosity::VERBOSE;
}

static bool
set_profiler_interval(PS::LockRef aLock, const char* aInterval)
{
  if (aInterval) {
    errno = 0;
    long int n = strtol(aInterval, nullptr, 10);
    if (errno == 0 && 1 <= n && n <= 1000) {
      gPS->SetEnvVarInterval(aLock, n);
      return true;
    }
    return false;
  }

  return true;
}

static bool
set_profiler_entries(PS::LockRef aLock, const char* aEntries)
{
  if (aEntries) {
    errno = 0;
    long int n = strtol(aEntries, nullptr, 10);
    if (errno == 0 && n > 0) {
      gPS->SetEnvVarEntries(aLock, n);
      return true;
    }
    return false;
  }

  return true;
}

static bool
is_native_unwinding_avail()
{
# if defined(HAVE_NATIVE_UNWIND)
  return true;
#else
  return false;
#endif
}

static void
profiler_usage(int aExitCode)
{
  MOZ_RELEASE_ASSERT(NS_IsMainThread());
  MOZ_RELEASE_ASSERT(gPS);

  // Force-enable verbosity so that LOG prints something. The LOG calls below
  // lock gVerbosityMutex themselves, so this scope only needs to cover this
  // assignment.
  {
    StaticMutexAutoLock lock(gVerbosityMutex);
    gVerbosity = Verbosity::VERBOSE;
  }

  LOG ("");
  LOG ("Environment variable usage:");
  LOG ("");
  LOG ("  MOZ_PROFILER_HELP");
  LOG ("  If set to any value, prints this message.");
  LOG ("");
  LOG ("  MOZ_PROFILER_ENTRIES=<1..>      (count)");
  LOG ("  If unset, platform default is used.");
  LOG ("");
  LOG ("  MOZ_PROFILER_INTERVAL=<1..1000> (milliseconds)");
  LOG ("  If unset, platform default is used.");
  LOG ("");
  LOG ("  MOZ_PROFILER_VERBOSE");
  LOG ("  If set to any value, increases verbosity (recommended).");
  LOG ("");
  LOG ("  MOZ_PROFILER_LUL_TEST");
  LOG ("  If set to any value, runs LUL unit tests at startup of");
  LOG ("  the unwinder thread, and prints a short summary of ");
  LOG ("  results.");
  LOG ("");
  LOGF("  This platform %s native unwinding.",
       is_native_unwinding_avail() ? "supports" : "does not support");
  LOG ("");

  exit(aExitCode);
}

// Read env vars at startup, so as to set:
//   gPS->mEnvVarEntries, gPS->mEnvVarInterval
static void
ReadProfilerEnvVars(PS::LockRef aLock)
{
  const char* help     = getenv("MOZ_PROFILER_HELP");
  const char* entries  = getenv("MOZ_PROFILER_ENTRIES");
  const char* interval = getenv("MOZ_PROFILER_INTERVAL");

  if (help) {
    profiler_usage(0); // terminates execution
  }

  if (!set_profiler_entries(aLock, entries) ||
      !set_profiler_interval(aLock, interval)) {
    profiler_usage(1); // terminates execution
  }

  LOGF("entries  = %d (zero means \"platform default\")",
       gPS->EnvVarEntries(aLock));
  LOGF("interval = %d ms (zero means \"platform default\")",
       gPS->EnvVarInterval(aLock));
}

#ifdef HAVE_VA_COPY
#define VARARGS_ASSIGN(foo, bar)     VA_COPY(foo,bar)
#elif defined(HAVE_VA_LIST_AS_ARRAY)
#define VARARGS_ASSIGN(foo, bar)     foo[0] = bar[0]
#else
#define VARARGS_ASSIGN(foo, bar)     (foo) = (bar)
#endif

void
profiler_log(const char* aStr)
{
  // This function runs both on and off the main thread.

  profiler_tracing("log", aStr, TRACING_EVENT);
}

static void
locked_profiler_add_marker(PS::LockRef aLock, const char* aMarker,
                           ProfilerMarkerPayload* aPayload);

void
profiler_log(const char* aFmt, va_list aArgs)
{
  // This function runs both on and off the main thread.

  MOZ_RELEASE_ASSERT(gPS);

  PS::AutoLock lock(gPSMutex);

  if (!gPS->IsActive(lock) || gPS->FeaturePrivacy(lock)) {
    return;
  }

  // nsAutoCString AppendPrintf would be nicer but this is mozilla external
  // code.
  char buf[2048];
  va_list argsCpy;
  VARARGS_ASSIGN(argsCpy, aArgs);
  int required = VsprintfLiteral(buf, aFmt, argsCpy);
  va_end(argsCpy);

  if (required < 0) {
    // silently drop for now

  } else if (required < 2048) {
    auto marker = new ProfilerMarkerTracing("log", TRACING_EVENT);
    locked_profiler_add_marker(lock, buf, marker);

  } else {
    char* heapBuf = new char[required + 1];
    va_list argsCpy;
    VARARGS_ASSIGN(argsCpy, aArgs);
    vsnprintf(heapBuf, required + 1, aFmt, argsCpy);
    va_end(argsCpy);
    // EVENT_BACKTRACE could be used to get a source for all log events. This
    // could be a runtime flag later.
    auto marker = new ProfilerMarkerTracing("log", TRACING_EVENT);
    locked_profiler_add_marker(lock, heapBuf, marker);
    delete[] heapBuf;
  }
}

////////////////////////////////////////////////////////////////////////
// BEGIN externally visible functions

MOZ_DEFINE_MALLOC_SIZE_OF(GeckoProfilerMallocSizeOf)

NS_IMETHODIMP
GeckoProfilerReporter::CollectReports(nsIHandleReportCallback* aHandleReport,
                                      nsISupports* aData, bool aAnonymize)
{
  MOZ_RELEASE_ASSERT(NS_IsMainThread());

  size_t profSize = 0;
#if defined(USE_LUL_STACKWALK)
  size_t lulSize = 0;
#endif

  {
    PS::AutoLock lock(gPSMutex);

    if (gPS) {
      profSize = GeckoProfilerMallocSizeOf(gPS);

      const PS::ThreadVector& threads = gPS->Threads(lock);
      for (uint32_t i = 0; i < threads.size(); i++) {
        ThreadInfo* info = threads.at(i);
        profSize += info->SizeOfIncludingThis(GeckoProfilerMallocSizeOf);
      }

      if (gPS->IsActive(lock)) {
        profSize +=
          gPS->Buffer(lock)->SizeOfIncludingThis(GeckoProfilerMallocSizeOf);
      }

      // Measurement of the following things may be added later if DMD finds it
      // is worthwhile:
      // - gPS->mFeatures
      // - gPS->mThreadNameFilters
      // - gPS->mThreads itself (its elements' children are measured above)
      // - gPS->mGatherer
      // - gPS->mInterposeObserver

#if defined(USE_LUL_STACKWALK)
      lul::LUL* lul = gPS->LUL(lock);
      lulSize = lul ? lul->SizeOfIncludingThis(GeckoProfilerMallocSizeOf) : 0;
#endif
    }
  }

  MOZ_COLLECT_REPORT(
    "explicit/profiler/profiler-state", KIND_HEAP, UNITS_BYTES, profSize,
    "Memory used by the Gecko Profiler's ProfilerState object (excluding "
    "memory used by LUL).");

#if defined(USE_LUL_STACKWALK)
  MOZ_COLLECT_REPORT(
    "explicit/profiler/lul", KIND_HEAP, UNITS_BYTES, lulSize,
    "Memory used by LUL, a stack unwinder used by the Gecko Profiler.");
#endif

  return NS_OK;
}

NS_IMPL_ISUPPORTS(GeckoProfilerReporter, nsIMemoryReporter)

static bool
ThreadSelected(PS::LockRef aLock, const char* aThreadName)
{
  // This function runs both on and off the main thread.

  MOZ_RELEASE_ASSERT(gPS);

  const Vector<std::string>& threadNameFilters = gPS->ThreadNameFilters(aLock);

  if (threadNameFilters.empty()) {
    return true;
  }

  std::string name = aThreadName;
  std::transform(name.begin(), name.end(), name.begin(), ::tolower);

  for (uint32_t i = 0; i < threadNameFilters.length(); ++i) {
    std::string filter = threadNameFilters[i];
    std::transform(filter.begin(), filter.end(), filter.begin(), ::tolower);

    // Crude, non UTF-8 compatible, case insensitive substring search
    if (name.find(filter) != std::string::npos) {
      return true;
    }
  }

  return false;
}

static void
MaybeSetProfile(PS::LockRef aLock, ThreadInfo* aInfo)
{
  // This function runs both on and off the main thread.

  MOZ_RELEASE_ASSERT(gPS);

  if ((aInfo->IsMainThread() || gPS->FeatureThreads(aLock)) &&
      ThreadSelected(aLock, aInfo->Name())) {
    aInfo->SetHasProfile();
  }
}

static void
locked_register_thread(PS::LockRef aLock, const char* aName, void* stackTop)
{
  // This function runs both on and off the main thread.

  MOZ_RELEASE_ASSERT(gPS);

  PS::ThreadVector& threads = gPS->Threads(aLock);
  Thread::tid_t id = Thread::GetCurrentId();
  for (uint32_t i = 0; i < threads.size(); i++) {
    ThreadInfo* info = threads.at(i);
    if (info->ThreadId() == id && !info->IsPendingDelete()) {
      // Thread already registered. This means the first unregister will be
      // too early.
      MOZ_ASSERT(false);
      return;
    }
  }

  if (!tlsPseudoStack.init()) {
    return;
  }
  NotNull<PseudoStack*> stack = WrapNotNull(new PseudoStack());
  tlsPseudoStack.set(stack);

  ThreadInfo* info =
    new ThreadInfo(aName, id, NS_IsMainThread(), stack, stackTop);

  MaybeSetProfile(aLock, info);

  // This must come after the MaybeSetProfile() call.
  if (gPS->IsActive(aLock) && info->HasProfile() && gPS->FeatureJS(aLock)) {
    // This startJSSampling() call is on-thread, so we can poll manually to
    // start JS sampling immediately.
    stack->startJSSampling();
    stack->pollJSSampling();
  }

  threads.push_back(info);
}

// We #include these files directly because it means those files can use
// declarations from this file trivially.
#if defined(GP_OS_windows)
# include "platform-win32.cpp"
#elif defined(GP_OS_darwin)
# include "platform-macos.cpp"
#elif defined(GP_OS_linux) || defined(GP_OS_android)
# include "platform-linux-android.cpp"
#else
# error "bad platform"
#endif

static void
NotifyProfilerStarted(const int aEntries, double aInterval,
                      const char** aFeatures, uint32_t aFeatureCount,
                      const char** aThreadNameFilters, uint32_t aFilterCount)
{
  if (!CanNotifyObservers()) {
    return;
  }

  nsCOMPtr<nsIObserverService> os = mozilla::services::GetObserverService();
  if (!os) {
    return;
  }

  nsTArray<nsCString> featuresArray;
  for (size_t i = 0; i < aFeatureCount; ++i) {
    featuresArray.AppendElement(aFeatures[i]);
  }

  nsTArray<nsCString> threadNameFiltersArray;
  for (size_t i = 0; i < aFilterCount; ++i) {
    threadNameFiltersArray.AppendElement(aThreadNameFilters[i]);
  }

  nsCOMPtr<nsIProfilerStartParams> params =
    new nsProfilerStartParams(aEntries, aInterval, featuresArray,
                              threadNameFiltersArray);

  os->NotifyObservers(params, "profiler-started", nullptr);
}

static void
NotifyObservers(const char* aTopic)
{
  if (!CanNotifyObservers()) {
    return;
  }

  nsCOMPtr<nsIObserverService> os = mozilla::services::GetObserverService();
  if (!os) {
    return;
  }

  os->NotifyObservers(nullptr, aTopic, nullptr);
}

static void
locked_profiler_start(PS::LockRef aLock, const int aEntries, double aInterval,
                      const char** aFeatures, uint32_t aFeatureCount,
                      const char** aThreadNameFilters, uint32_t aFilterCount);

void
profiler_init(void* aStackTop)
{
  LOG("BEGIN profiler_init");

  MOZ_RELEASE_ASSERT(NS_IsMainThread());
  MOZ_RELEASE_ASSERT(!gPS);

  const char* features[] = { "js"
#if defined(PROFILE_JAVA)
                           , "java"
#endif
                           , "leaf"
#if defined(HAVE_NATIVE_UNWIND)
                           , "stackwalk"
#endif
                           , "threads"
                           };

  const char* threadFilters[] = { "GeckoMain", "Compositor" };

  {
    PS::AutoLock lock(gPSMutex);

    // We've passed the possible failure point. Instantiate gPS, which
    // indicates that the profiler has initialized successfully.
    gPS = new PS();

    set_stderr_callback(profiler_log);

    bool ignore;
    gPS->SetStartTime(lock, mozilla::TimeStamp::ProcessCreation(ignore));

    // Read settings from environment variables.
    ReadProfilerEnvVars(lock);

    locked_register_thread(lock, kMainThreadName, aStackTop);

    // Platform-specific initialization.
    PlatformInit(lock);

#ifdef MOZ_TASK_TRACER
    mozilla::tasktracer::InitTaskTracer();
#endif

#if defined(PROFILE_JAVA)
    if (mozilla::jni::IsFennec()) {
      GeckoJavaSampler::Init();
    }
#endif

    // (Linux-only) We could create gPS->mLUL and read unwind info into it at
    // this point. That would match the lifetime implied by destruction of it
    // in profiler_shutdown() just below. However, that gives a big delay on
    // startup, even if no profiling is actually to be done. So, instead, it is
    // created on demand at the first call to PlatformStart().

    // We can't open pref so we use an environment variable to know if we
    // should trigger the profiler on startup.
    // NOTE: Default
    const char *val = getenv("MOZ_PROFILER_STARTUP");
    if (!val || !*val) {
      LOG("END   profiler_init: MOZ_PROFILER_STARTUP not set");
      return;
    }

    locked_profiler_start(lock, PROFILE_DEFAULT_ENTRIES,
                          PROFILE_DEFAULT_INTERVAL,
                          features, MOZ_ARRAY_LENGTH(features),
                          threadFilters, MOZ_ARRAY_LENGTH(threadFilters));
  }

  // We do this with gPSMutex unlocked. The comment in profiler_stop() explains
  // why.
  NotifyProfilerStarted(PROFILE_DEFAULT_ENTRIES, PROFILE_DEFAULT_INTERVAL,
                        features, MOZ_ARRAY_LENGTH(features),
                        threadFilters, MOZ_ARRAY_LENGTH(threadFilters));

  LOG("END   profiler_init");
}

static void
locked_profiler_save_profile_to_file(PS::LockRef aLock, const char* aFilename);

static SamplerThread*
locked_profiler_stop(PS::LockRef aLock);

void
profiler_shutdown()
{
  LOG("BEGIN profiler_shutdown");

  MOZ_RELEASE_ASSERT(NS_IsMainThread());
  MOZ_RELEASE_ASSERT(gPS);

  // If the profiler is active we must get a handle to the SamplerThread before
  // gPS is destroyed, in order to delete it.
  SamplerThread* samplerThread = nullptr;
  {
    PS::AutoLock lock(gPSMutex);

    // Save the profile on shutdown if requested.
    if (gPS->IsActive(lock)) {
      const char* filename = getenv("MOZ_PROFILER_SHUTDOWN");
      if (filename) {
        locked_profiler_save_profile_to_file(lock, filename);
      }

      samplerThread = locked_profiler_stop(lock);
    }

    set_stderr_callback(nullptr);

    PS::ThreadVector& threads = gPS->Threads(lock);
    while (threads.size() > 0) {
      delete threads.back();
      threads.pop_back();
    }

#if defined(USE_LUL_STACKWALK)
    // Delete the LUL object if it actually got created.
    lul::LUL* lul = gPS->LUL(lock);
    if (lul) {
      delete lul;
      gPS->SetLUL(lock, nullptr);
    }
#endif

    delete gPS;
    gPS = nullptr;

    // We just destroyed gPS and the ThreadInfos it contains, so it is safe to
    // delete the PseudoStack. tlsPseudoStack is certain to still be the owner
    // of its PseudoStack because the main thread is never put in a "pending
    // delete" state.
    delete tlsPseudoStack.get();
    tlsPseudoStack.set(nullptr);

#ifdef MOZ_TASK_TRACER
    mozilla::tasktracer::ShutdownTaskTracer();
#endif
  }

  // We do these operations with gPSMutex unlocked. The comments in
  // profiler_stop() explain why.
  if (samplerThread) {
    NotifyObservers("profiler-stopped");
    delete samplerThread;
  }

  LOG("END   profiler_shutdown");
}

mozilla::UniquePtr<char[]>
profiler_get_profile(double aSinceTime)
{
  MOZ_RELEASE_ASSERT(NS_IsMainThread());
  MOZ_RELEASE_ASSERT(gPS);

  PS::AutoLock lock(gPSMutex);

  if (!gPS->IsActive(lock)) {
    return nullptr;
  }

  return ToJSON(lock, aSinceTime);
}

JSObject*
profiler_get_profile_jsobject(JSContext *aCx, double aSinceTime)
{
  MOZ_RELEASE_ASSERT(NS_IsMainThread());
  MOZ_RELEASE_ASSERT(gPS);

  // |val| must outlive |lock| to avoid a GC hazard.
  JS::RootedValue val(aCx);
  UniquePtr<char[]> buf = nullptr;

  {
    PS::AutoLock lock(gPSMutex);

    if (!gPS->IsActive(lock)) {
      return nullptr;
    }

    buf = ToJSON(lock, aSinceTime);
  }

  NS_ConvertUTF8toUTF16 js_string(nsDependentCString(buf.get()));
  auto buf16 = static_cast<const char16_t*>(js_string.get());
  MOZ_ALWAYS_TRUE(JS_ParseJSON(aCx, buf16, js_string.Length(), &val));

  return &val.toObject();
}

void
profiler_get_profile_jsobject_async(double aSinceTime,
                                    mozilla::dom::Promise* aPromise)
{
  MOZ_RELEASE_ASSERT(NS_IsMainThread());
  MOZ_RELEASE_ASSERT(gPS);

  PS::AutoLock lock(gPSMutex);

  if (!gPS->IsActive(lock)) {
    return;
  }

  gPS->Gatherer(lock)->Start(lock, aSinceTime, aPromise);
}

void
profiler_save_profile_to_file_async(double aSinceTime, const char* aFileName)
{
  MOZ_RELEASE_ASSERT(NS_IsMainThread());
  MOZ_RELEASE_ASSERT(gPS);

  nsCString filename(aFileName);
  NS_DispatchToMainThread(NS_NewRunnableFunction([=] () {

    PS::AutoLock lock(gPSMutex);

    // It's conceivable that profiler_stop() or profiler_shutdown() was called
    // between the dispatch and running of this runnable, so check for those.
    if (!gPS || !gPS->IsActive(lock)) {
      return;
    }

    gPS->Gatherer(lock)->Start(lock, aSinceTime, filename);
  }));
}

void
profiler_get_start_params(int* aEntries, double* aInterval,
                          mozilla::Vector<const char*>* aFilters,
                          mozilla::Vector<const char*>* aFeatures)
{
  MOZ_RELEASE_ASSERT(NS_IsMainThread());
  MOZ_RELEASE_ASSERT(gPS);

  if (NS_WARN_IF(!aEntries) || NS_WARN_IF(!aInterval) ||
      NS_WARN_IF(!aFilters) || NS_WARN_IF(!aFeatures)) {
    return;
  }

  PS::AutoLock lock(gPSMutex);

  *aEntries = gPS->Entries(lock);
  *aInterval = gPS->Interval(lock);

  const Vector<std::string>& threadNameFilters = gPS->ThreadNameFilters(lock);
  MOZ_ALWAYS_TRUE(aFilters->resize(threadNameFilters.length()));
  for (uint32_t i = 0; i < threadNameFilters.length(); ++i) {
    (*aFilters)[i] = threadNameFilters[i].c_str();
  }

  const Vector<std::string>& features = gPS->Features(lock);
  MOZ_ALWAYS_TRUE(aFeatures->resize(features.length()));
  for (size_t i = 0; i < features.length(); ++i) {
    (*aFeatures)[i] = features[i].c_str();
  }
}

void
profiler_will_gather_OOP_profile()
{
  MOZ_RELEASE_ASSERT(NS_IsMainThread());
  MOZ_RELEASE_ASSERT(gPS);

  // This function is called once per subprocess in response to the observation
  // of a "profile-subprocess-gather" notification. That notification
  // originates from ProfileGatherer::Start2(). The observers receive it and
  // immediately call this function, all while Start2() holds gPSMutex locked.
  // This is non-trivial, so we assert that gPSMutex is locked as expected...
  gPSMutex.AssertCurrentThreadOwns();

  // ...therefore we don't need to lock gPSMutex. But we need a PS::AutoLock to
  // access gPS, so we make a fake one. This is gross but it's hard to get the
  // "profile-subprocess-gather" observers to call back here any other way
  // without exposing ProfileGatherer, which causes other difficulties.
  static PS::Mutex sFakeMutex;
  PS::AutoLock fakeLock(sFakeMutex);

  MOZ_RELEASE_ASSERT(gPS->IsActive(fakeLock));

  gPS->Gatherer(fakeLock)->WillGatherOOPProfile();
}

void
profiler_gathered_OOP_profile()
{
  MOZ_RELEASE_ASSERT(NS_IsMainThread());
  MOZ_RELEASE_ASSERT(gPS);

  PS::AutoLock lock(gPSMutex);

  if (!gPS->IsActive(lock)) {
    return;
  }

  gPS->Gatherer(lock)->GatheredOOPProfile(lock);
}

void
profiler_OOP_exit_profile(const nsCString& aProfile)
{
  MOZ_RELEASE_ASSERT(NS_IsMainThread());
  MOZ_RELEASE_ASSERT(gPS);

  PS::AutoLock lock(gPSMutex);

  if (!gPS->IsActive(lock)) {
    return;
  }

  gPS->Gatherer(lock)->OOPExitProfile(aProfile);
}

static void
locked_profiler_save_profile_to_file(PS::LockRef aLock, const char* aFilename)
{
  MOZ_RELEASE_ASSERT(NS_IsMainThread());
  MOZ_RELEASE_ASSERT(gPS && gPS->IsActive(aLock));

  std::ofstream stream;
  stream.open(aFilename);
  if (stream.is_open()) {
    SpliceableJSONWriter w(mozilla::MakeUnique<OStreamJSONWriteFunc>(stream));
    StreamJSON(aLock, w, /* sinceTime */ 0);
    stream.close();
    LOGF("locked_profiler_save_profile_to_file: Saved to %s", aFilename);
  } else {
    LOG ("locked_profiler_save_profile_to_file: Failed to open file");
  }
}

void
profiler_save_profile_to_file(const char* aFilename)
{
  MOZ_RELEASE_ASSERT(NS_IsMainThread());
  MOZ_RELEASE_ASSERT(gPS);

  PS::AutoLock lock(gPSMutex);

  if (!gPS->IsActive(lock)) {
    return;
  }

  locked_profiler_save_profile_to_file(lock, aFilename);
}

const char**
profiler_get_features()
{
  MOZ_RELEASE_ASSERT(NS_IsMainThread());
  MOZ_RELEASE_ASSERT(gPS);

  static const char* features[] = {
#if defined(MOZ_PROFILING) && defined(HAVE_NATIVE_UNWIND)
    // Walk the C++ stack.
    "stackwalk",
#endif
    // Include the C++ leaf node if not stackwalking. DevTools
    // profiler doesn't want the native addresses.
    "leaf",
    // Profile Java code (Android only).
    "java",
    // Tell the JS engine to emit pseudostack entries in the prologue/epilogue.
    "js",
    // GPU Profiling (may not be supported by the GL)
    "gpu",
    // Profile the registered secondary threads.
    "threads",
    // Do not include user-identifiable information
    "privacy",
    // Dump the layer tree with the textures.
    "layersdump",
    // Dump the display list with the textures.
    "displaylistdump",
    // Add main thread I/O to the profile
    "mainthreadio",
    // Add RSS collection
    "memory",
    // Restyle profiling.
    "restyle",
#ifdef MOZ_TASK_TRACER
    // Start profiling with feature TaskTracer.
    "tasktracer",
#endif
    nullptr
  };

  return features;
}

void
profiler_get_buffer_info_helper(uint32_t* aCurrentPosition,
                                uint32_t* aEntries,
                                uint32_t* aGeneration)
{
  // This function is called by profiler_get_buffer_info(), which has already
  // zeroed the outparams.

  MOZ_RELEASE_ASSERT(NS_IsMainThread());
  MOZ_RELEASE_ASSERT(gPS);

  PS::AutoLock lock(gPSMutex);

  if (!gPS->IsActive(lock)) {
    return;
  }

  *aCurrentPosition = gPS->Buffer(lock)->mWritePos;
  *aEntries = gPS->Entries(lock);
  *aGeneration = gPS->Buffer(lock)->mGeneration;
}

static bool
hasFeature(const char** aFeatures, uint32_t aFeatureCount, const char* aFeature)
{
  for (size_t i = 0; i < aFeatureCount; i++) {
    if (strcmp(aFeatures[i], aFeature) == 0) {
      return true;
    }
  }
  return false;
}

static void
locked_profiler_start(PS::LockRef aLock, int aEntries, double aInterval,
                      const char** aFeatures, uint32_t aFeatureCount,
                      const char** aThreadNameFilters, uint32_t aFilterCount)
{
  LOG("BEGIN locked_profiler_start");

  MOZ_RELEASE_ASSERT(NS_IsMainThread());
  MOZ_RELEASE_ASSERT(gPS && !gPS->IsActive(aLock));

  bool ignore;
  gPS->SetStartTime(aLock, mozilla::TimeStamp::ProcessCreation(ignore));

  // Start with the default value. Then override with the passed-in value, if
  // reasonable. Then override with the env var value, if reasonable.
  int entries = PROFILE_DEFAULT_ENTRIES;
  if (aEntries > 0) {
    entries = aEntries;
  }
  if (gPS->EnvVarEntries(aLock) > 0) {
    entries = gPS->EnvVarEntries(aLock);
  }
  gPS->SetEntries(aLock, entries);

  // Ditto.
  double interval = PROFILE_DEFAULT_INTERVAL;
  if (aInterval > 0) {
    interval = aInterval;
  }
  if (gPS->EnvVarInterval(aLock) > 0) {
    interval = gPS->EnvVarInterval(aLock);
  }
  gPS->SetInterval(aLock, interval);

  // Deep copy aFeatures. Must precede the MaybeSetProfile() call below.
  Vector<std::string>& features = gPS->Features(aLock);
  MOZ_ALWAYS_TRUE(features.resize(aFeatureCount));
  for (uint32_t i = 0; i < aFeatureCount; ++i) {
    features[i] = aFeatures[i];
  }

  // Deep copy aThreadNameFilters. Must precede the MaybeSetProfile() call
  // below.
  Vector<std::string>& threadNameFilters = gPS->ThreadNameFilters(aLock);
  MOZ_ALWAYS_TRUE(threadNameFilters.resize(aFilterCount));
  for (uint32_t i = 0; i < aFilterCount; ++i) {
    threadNameFilters[i] = aThreadNameFilters[i];
  }

#define HAS_FEATURE(feature) hasFeature(aFeatures, aFeatureCount, feature)

  gPS->SetFeatureDisplayListDump(aLock, HAS_FEATURE("displaylistdump"));
  gPS->SetFeatureGPU(aLock, HAS_FEATURE("gpu"));
#if defined(PROFILE_JAVA)
  gPS->SetFeatureJava(aLock, mozilla::jni::IsFennec() && HAS_FEATURE("java"));
#endif
  bool featureJS = HAS_FEATURE("js");
  gPS->SetFeatureJS(aLock, featureJS);
  gPS->SetFeatureLayersDump(aLock, HAS_FEATURE("layersdump"));
  gPS->SetFeatureLeaf(aLock, HAS_FEATURE("leaf"));
  bool featureMainThreadIO = HAS_FEATURE("mainthreadio");
  gPS->SetFeatureMemory(aLock, HAS_FEATURE("memory"));
  gPS->SetFeaturePrivacy(aLock, HAS_FEATURE("privacy"));
  gPS->SetFeatureRestyle(aLock, HAS_FEATURE("restyle"));
  gPS->SetFeatureStackWalk(aLock, HAS_FEATURE("stackwalk"));
#ifdef MOZ_TASK_TRACER
  bool featureTaskTracer = HAS_FEATURE("tasktracer");
  gPS->SetFeatureTaskTracer(aLock, featureTaskTracer);
#endif
  // Profile non-main threads if we have a filter, because users sometimes ask
  // to filter by a list of threads but forget to explicitly request.
  // Must precede the MaybeSetProfile() call below.
  gPS->SetFeatureThreads(aLock, HAS_FEATURE("threads") || aFilterCount > 0);

#undef HAS_FEATURE

  gPS->SetBuffer(aLock, new ProfileBuffer(entries));

  gPS->SetGatherer(aLock, new mozilla::ProfileGatherer());

  // Set up profiling for each registered thread, if appropriate.
  const PS::ThreadVector& threads = gPS->Threads(aLock);
  for (uint32_t i = 0; i < threads.size(); i++) {
    ThreadInfo* info = threads.at(i);

    MaybeSetProfile(aLock, info);

    if (info->HasProfile() && !info->IsPendingDelete()) {
      info->Stack()->reinitializeOnResume();

      if (featureJS) {
        info->Stack()->startJSSampling();
      }
    }
  }

  if (featureJS) {
    // We just called startJSSampling() on all relevant threads. We can also
    // manually poll the current thread so it starts sampling immediately.
    if (PseudoStack* stack = tlsPseudoStack.get()) {
      stack->pollJSSampling();
    }
  }

#ifdef MOZ_TASK_TRACER
  if (featureTaskTracer) {
    mozilla::tasktracer::StartLogging();
  }
#endif

#if defined(PROFILE_JAVA)
  if (gPS->FeatureJava(aLock)) {
    int javaInterval = interval;
    // Java sampling doesn't accuratly keep up with 1ms sampling
    if (javaInterval < 10) {
      javaInterval = 10;
    }
    mozilla::java::GeckoJavaSampler::Start(javaInterval, 1000);
  }
#endif

  // Must precede the PS::ActivityGeneration() call below.
  PS::SetActive(aLock);

  gPS->SetIsPaused(aLock, false);

  // This creates the sampler thread. It doesn't start sampling immediately
  // because the main loop within Run() is blocked until this function's caller
  // unlocks gPSMutex.
  gPS->SetSamplerThread(aLock, new SamplerThread(aLock,
                                                 PS::ActivityGeneration(aLock),
                                                 interval));

  if (featureMainThreadIO) {
    auto interposeObserver = new mozilla::ProfilerIOInterposeObserver();
    gPS->SetInterposeObserver(aLock, interposeObserver);
    mozilla::IOInterposer::Register(mozilla::IOInterposeObserver::OpAll,
                                    interposeObserver);
  }

  LOG("END   locked_profiler_start");
}

void
profiler_start(int aEntries, double aInterval,
               const char** aFeatures, uint32_t aFeatureCount,
               const char** aThreadNameFilters, uint32_t aFilterCount)
{
  LOG("BEGIN profiler_start");

  MOZ_RELEASE_ASSERT(NS_IsMainThread());

  SamplerThread* samplerThread = nullptr;
  {
    PS::AutoLock lock(gPSMutex);

    // Initialize if necessary.
    if (!gPS) {
      profiler_init(nullptr);
    }

    // Reset the current state if the profiler is running.
    if (gPS->IsActive(lock)) {
      samplerThread = locked_profiler_stop(lock);
    }

    locked_profiler_start(lock, aEntries, aInterval, aFeatures, aFeatureCount,
                          aThreadNameFilters, aFilterCount);
  }

  // We do these operations with gPSMutex unlocked. The comments in
  // profiler_stop() explain why.
  if (samplerThread) {
    NotifyObservers("profiler-stopped");
    delete samplerThread;
  }
  NotifyProfilerStarted(aEntries, aInterval, aFeatures, aFeatureCount,
                        aThreadNameFilters, aFilterCount);

  LOG("END   profiler_start");
}

static MOZ_MUST_USE SamplerThread*
locked_profiler_stop(PS::LockRef aLock)
{
  LOG("BEGIN locked_profiler_stop");

  MOZ_RELEASE_ASSERT(NS_IsMainThread());
  MOZ_RELEASE_ASSERT(gPS && gPS->IsActive(aLock));

  // We clear things in roughly reverse order to their setting in
  // locked_profiler_start().

  mozilla::IOInterposer::Unregister(mozilla::IOInterposeObserver::OpAll,
                                    gPS->InterposeObserver(aLock));
  gPS->SetInterposeObserver(aLock, nullptr);

  // The Stop() call doesn't actually stop Run(); that happens in this
  // function's caller when the sampler thread is destroyed. Stop() just gives
  // the SamplerThread a chance to do some cleanup with gPSMutex locked.
  SamplerThread* samplerThread = gPS->SamplerThread(aLock);
  samplerThread->Stop(aLock);
  gPS->SetSamplerThread(aLock, nullptr);

  gPS->SetIsPaused(aLock, false);

  gPS->SetInactive(aLock);

#ifdef MOZ_TASK_TRACER
  if (gPS->FeatureTaskTracer(aLock)) {
    mozilla::tasktracer::StopLogging();
  }
#endif

  PS::ThreadVector& threads = gPS->Threads(aLock);
  for (uint32_t i = 0; i < threads.size(); i++) {
    ThreadInfo* info = threads.at(i);
    if (info->IsPendingDelete()) {
      // We've stopped profiling. Destroy ThreadInfo for dead threads.
      delete info;
      threads.erase(threads.begin() + i);
      i--;
    } else if (info->HasProfile() && gPS->FeatureJS(aLock)) {
      // Stop JS sampling live threads.
      info->Stack()->stopJSSampling();
    }
  }

  if (gPS->FeatureJS(aLock)) {
    // We just called stopJSSampling() on all relevant threads. We can also
    // manually poll the current thread so it stops profiling immediately.
    if (PseudoStack* stack = tlsPseudoStack.get()) {
      stack->pollJSSampling();
    }
  }

  // Cancel any in-flight async profile gathering requests.
  gPS->Gatherer(aLock)->Cancel();
  gPS->SetGatherer(aLock, nullptr);

  delete gPS->Buffer(aLock);
  gPS->SetBuffer(aLock, nullptr);

  gPS->SetFeatureDisplayListDump(aLock, false);
  gPS->SetFeatureGPU(aLock, false);
  gPS->SetFeatureJava(aLock, false);
  gPS->SetFeatureJS(aLock, false);
  gPS->SetFeatureLayersDump(aLock, false);
  gPS->SetFeatureLeaf(aLock, false);
  gPS->SetFeatureMemory(aLock, false);
  gPS->SetFeaturePrivacy(aLock, false);
  gPS->SetFeatureRestyle(aLock, false);
  gPS->SetFeatureStackWalk(aLock, false);
  gPS->SetFeatureTaskTracer(aLock, false);
  gPS->SetFeatureThreads(aLock, false);

  gPS->ThreadNameFilters(aLock).clear();

  gPS->Features(aLock).clear();

  gPS->SetInterval(aLock, 0.0);

  gPS->SetEntries(aLock, 0);

  LOG("END   locked_profiler_stop");

  return samplerThread;
}

void
profiler_stop()
{
  LOG("BEGIN profiler_stop");

  MOZ_RELEASE_ASSERT(NS_IsMainThread());
  MOZ_RELEASE_ASSERT(gPS);

  SamplerThread* samplerThread;
  {
    PS::AutoLock lock(gPSMutex);

    if (!gPS->IsActive(lock)) {
      LOG("END   profiler_stop: inactive");
      return;
    }

    samplerThread = locked_profiler_stop(lock);
  }

  // We notify observers with gPSMutex unlocked. Otherwise we might get a
  // deadlock, if code run by the observer calls a profiler function that locks
  // gPSMutex. (This has been seen in practise in bug 1346356.)
  NotifyObservers("profiler-stopped");

  // We delete with gPSMutex unlocked. Otherwise we would get a deadlock: we
  // would be waiting here with gPSMutex locked for SamplerThread::Run() to
  // return so the join operation within the destructor can complete, but Run()
  // needs to lock gPSMutex to return.
  //
  // Because this call occurs with gPSMutex unlocked, it -- including the final
  // iteration of Run()'s loop -- must be able detect deactivation and return
  // in a way that's safe with respect to other gPSMutex-locking operations
  // that may have occurred in the meantime.
  delete samplerThread;

  LOG("END   profiler_stop");
}

bool
profiler_is_paused()
{
  MOZ_RELEASE_ASSERT(NS_IsMainThread());
  MOZ_RELEASE_ASSERT(gPS);

  PS::AutoLock lock(gPSMutex);

  if (!gPS->IsActive(lock)) {
    return false;
  }

  return gPS->IsPaused(lock);
}

void
profiler_pause()
{
  MOZ_RELEASE_ASSERT(NS_IsMainThread());
  MOZ_RELEASE_ASSERT(gPS);

  {
    PS::AutoLock lock(gPSMutex);

    if (!gPS->IsActive(lock)) {
      return;
    }

    gPS->SetIsPaused(lock, true);
  }

  // gPSMutex must be unlocked when we notify, to avoid potential deadlocks.
  NotifyObservers("profiler-paused");
}

void
profiler_resume()
{
  MOZ_RELEASE_ASSERT(NS_IsMainThread());
  MOZ_RELEASE_ASSERT(gPS);

  PS::AutoLock lock(gPSMutex);

  {
    if (!gPS->IsActive(lock)) {
      return;
    }

    gPS->SetIsPaused(lock, false);
  }

  // gPSMutex must be unlocked when we notify, to avoid potential deadlocks.
  NotifyObservers("profiler-resumed");
}

bool
profiler_feature_active(const char* aName)
{
  // This function runs both on and off the main thread.

  MOZ_RELEASE_ASSERT(gPS);

  PS::AutoLock lock(gPSMutex);

  if (!gPS->IsActive(lock)) {
    return false;
  }

  if (strcmp(aName, "displaylistdump") == 0) {
    return gPS->FeatureDisplayListDump(lock);
  }

  if (strcmp(aName, "gpu") == 0) {
    return gPS->FeatureGPU(lock);
  }

  if (strcmp(aName, "layersdump") == 0) {
    return gPS->FeatureLayersDump(lock);
  }

  if (strcmp(aName, "restyle") == 0) {
    return gPS->FeatureRestyle(lock);
  }

  return false;
}

bool
profiler_is_active()
{
  // This function runs both on and off the main thread.

  MOZ_RELEASE_ASSERT(gPS);

  PS::AutoLock lock(gPSMutex);

  return gPS->IsActive(lock);
}

void
profiler_set_frame_number(int aFrameNumber)
{
  MOZ_RELEASE_ASSERT(NS_IsMainThread());
  MOZ_RELEASE_ASSERT(gPS);

  PS::AutoLock lock(gPSMutex);

  gPS->SetFrameNumber(lock, aFrameNumber);
}

void
profiler_register_thread(const char* aName, void* aGuessStackTop)
{
  MOZ_RELEASE_ASSERT(!NS_IsMainThread());
  MOZ_RELEASE_ASSERT(gPS);

  PS::AutoLock lock(gPSMutex);

  void* stackTop = GetStackTop(aGuessStackTop);
  locked_register_thread(lock, aName, stackTop);
}

void
profiler_unregister_thread()
{
  MOZ_RELEASE_ASSERT(!NS_IsMainThread());
  MOZ_RELEASE_ASSERT(gPS);

  PS::AutoLock lock(gPSMutex);

  Thread::tid_t id = Thread::GetCurrentId();

  bool wasPseudoStackTransferred = false;

  PS::ThreadVector& threads = gPS->Threads(lock);
  for (uint32_t i = 0; i < threads.size(); i++) {
    ThreadInfo* info = threads.at(i);
    if (info->ThreadId() == id && !info->IsPendingDelete()) {
      if (gPS->IsActive(lock)) {
        // We still want to show the results of this thread if you save the
        // profile shortly after a thread is terminated, which requires
        // transferring ownership of the PseudoStack to |info|. For now we will
        // defer the delete to profile stop.
        info->SetPendingDelete();
        wasPseudoStackTransferred = true;
      } else {
        delete info;
        threads.erase(threads.begin() + i);
      }
      break;
    }
  }

  // We don't call PseudoStack::stopJSSampling() here; there's no point doing
  // that for a JS thread that is in the process of disappearing.

  if (!wasPseudoStackTransferred) {
    delete tlsPseudoStack.get();
  }
  tlsPseudoStack.set(nullptr);
}

void
profiler_thread_sleep()
{
  // This function runs both on and off the main thread.

  MOZ_RELEASE_ASSERT(gPS);

  PseudoStack *stack = tlsPseudoStack.get();
  if (!stack) {
    return;
  }
  stack->setSleeping();
}

void
profiler_thread_wake()
{
  // This function runs both on and off the main thread.

  MOZ_RELEASE_ASSERT(gPS);

  PseudoStack *stack = tlsPseudoStack.get();
  if (!stack) {
    return;
  }
  stack->setAwake();
}

bool
profiler_thread_is_sleeping()
{
  MOZ_RELEASE_ASSERT(NS_IsMainThread());
  MOZ_RELEASE_ASSERT(gPS);

  PseudoStack *stack = tlsPseudoStack.get();
  if (!stack) {
    return false;
  }
  return stack->isSleeping();
}

void
profiler_js_interrupt_callback()
{
  // This function runs both on and off the main thread, on JS threads being
  // sampled.

  MOZ_RELEASE_ASSERT(gPS);

  PseudoStack *stack = tlsPseudoStack.get();
  if (!stack) {
    return;
  }

  stack->pollJSSampling();
}

double
profiler_time()
{
  // This function runs both on and off the main thread.

  MOZ_RELEASE_ASSERT(gPS);

  PS::AutoLock lock(gPSMutex);

  mozilla::TimeDuration delta =
    mozilla::TimeStamp::Now() - gPS->StartTime(lock);
  return delta.ToMilliseconds();
}

bool
profiler_is_active_and_not_in_privacy_mode()
{
  // This function runs both on and off the main thread.

  MOZ_RELEASE_ASSERT(gPS);

  PS::AutoLock lock(gPSMutex);

  return gPS->IsActive(lock) && !gPS->FeaturePrivacy(lock);
}

UniqueProfilerBacktrace
profiler_get_backtrace()
{
  MOZ_RELEASE_ASSERT(NS_IsMainThread());
  MOZ_RELEASE_ASSERT(gPS);

  PS::AutoLock lock(gPSMutex);

  if (!gPS->IsActive(lock) || gPS->FeaturePrivacy(lock)) {
    return nullptr;
  }

  PseudoStack* stack = tlsPseudoStack.get();
  if (!stack) {
    MOZ_ASSERT(stack);
    return nullptr;
  }
  Thread::tid_t tid = Thread::GetCurrentId();

  ProfileBuffer* buffer = new ProfileBuffer(GET_BACKTRACE_DEFAULT_ENTRIES);
  ThreadInfo* threadInfo =
    new ThreadInfo("SyncProfile", tid, NS_IsMainThread(), WrapNotNull(stack),
                   /* stackTop */ nullptr);
  threadInfo->SetHasProfile();

  TickSample sample;
  sample.threadInfo = threadInfo;

#if defined(HAVE_NATIVE_UNWIND)
#if defined(GP_OS_windows) || defined(GP_OS_linux) || defined(GP_OS_android)
  tickcontext_t context;
  sample.PopulateContext(&context);
#elif defined(GP_OS_darwin)
  sample.PopulateContext(nullptr);
#else
# error "unknown platform"
#endif
#endif

  sample.isSamplingCurrentThread = true;
  sample.timestamp = mozilla::TimeStamp::Now();

  Tick(lock, buffer, &sample);

  return UniqueProfilerBacktrace(new ProfilerBacktrace(buffer, threadInfo));
}

void
ProfilerBacktraceDestructor::operator()(ProfilerBacktrace* aBacktrace)
{
  delete aBacktrace;
}

// Fill the output buffer with the following pattern:
// "Label 1" "\0" "Label 2" "\0" ... "Label N" "\0" "\0"
// TODO: use the unwinder instead of pseudo stack.
void
profiler_get_backtrace_noalloc(char *output, size_t outputSize)
{
  MOZ_RELEASE_ASSERT(NS_IsMainThread());
  MOZ_RELEASE_ASSERT(gPS);

  MOZ_ASSERT(outputSize >= 2);
  char *bound = output + outputSize - 2;
  output[0] = output[1] = '\0';
  PseudoStack *pseudoStack = tlsPseudoStack.get();
  if (!pseudoStack) {
    return;
  }

  volatile js::ProfileEntry *pseudoFrames = pseudoStack->mStack;
  uint32_t pseudoCount = pseudoStack->stackSize();

  for (uint32_t i = 0; i < pseudoCount; i++) {
    size_t len = strlen(pseudoFrames[i].label());
    if (output + len >= bound)
      break;
    strcpy(output, pseudoFrames[i].label());
    output += len;
    *output++ = '\0';
    *output = '\0';
  }
}

static void
locked_profiler_add_marker(PS::LockRef aLock, const char* aMarker,
                           ProfilerMarkerPayload* aPayload)
{
  // This function runs both on and off the main thread.

  MOZ_RELEASE_ASSERT(gPS);
  MOZ_RELEASE_ASSERT(gPS->IsActive(aLock) && !gPS->FeaturePrivacy(aLock));

  // aPayload must be freed if we return early.
  mozilla::UniquePtr<ProfilerMarkerPayload> payload(aPayload);

  PseudoStack *stack = tlsPseudoStack.get();
  if (!stack) {
    return;
  }

  mozilla::TimeStamp origin = (payload && !payload->GetStartTime().IsNull())
                            ? payload->GetStartTime()
                            : mozilla::TimeStamp::Now();
  mozilla::TimeDuration delta = origin - gPS->StartTime(aLock);
  stack->addMarker(aMarker, payload.release(), delta.ToMilliseconds());
}

void
profiler_add_marker(const char* aMarker, ProfilerMarkerPayload* aPayload)
{
  // This function runs both on and off the main thread.

  MOZ_RELEASE_ASSERT(gPS);

  PS::AutoLock lock(gPSMutex);

  // aPayload must be freed if we return early.
  mozilla::UniquePtr<ProfilerMarkerPayload> payload(aPayload);

  if (!gPS->IsActive(lock) || gPS->FeaturePrivacy(lock)) {
    return;
  }

  locked_profiler_add_marker(lock, aMarker, payload.release());
}

void
profiler_tracing(const char* aCategory, const char* aInfo,
                 TracingMetadata aMetaData)
{
  // This function runs both on and off the main thread.

  MOZ_RELEASE_ASSERT(gPS);

  PS::AutoLock lock(gPSMutex);

  if (!gPS->IsActive(lock) || gPS->FeaturePrivacy(lock)) {
    return;
  }

  auto marker = new ProfilerMarkerTracing(aCategory, aMetaData);
  locked_profiler_add_marker(lock, aInfo, marker);
}

void
profiler_tracing(const char* aCategory, const char* aInfo,
                 UniqueProfilerBacktrace aCause, TracingMetadata aMetaData)
{
  // This function runs both on and off the main thread.

  MOZ_RELEASE_ASSERT(gPS);

  PS::AutoLock lock(gPSMutex);

  if (!gPS->IsActive(lock) || gPS->FeaturePrivacy(lock)) {
    return;
  }

  auto marker =
    new ProfilerMarkerTracing(aCategory, aMetaData, mozilla::Move(aCause));
  locked_profiler_add_marker(lock, aInfo, marker);
}

void
profiler_set_js_context(JSContext* aCx)
{
  // This function runs both on and off the main thread.

  MOZ_ASSERT(aCx);

  PseudoStack* stack = tlsPseudoStack.get();
  if (!stack) {
    return;
  }

  stack->setJSContext(aCx);
}

void
profiler_clear_js_context()
{
  // This function runs both on and off the main thread.

  MOZ_RELEASE_ASSERT(gPS);

  PseudoStack* stack = tlsPseudoStack.get();
  if (!stack) {
    return;
  }

  if (!stack->mContext) {
    return;
  }

  // On JS shut down, flush the current buffer as stringifying JIT samples
  // requires a live JSContext.

  PS::AutoLock lock(gPSMutex);

  if (gPS->IsActive(lock)) {
    gPS->SetIsPaused(lock, true);

    // Find the ThreadInfo corresponding to this thread, if there is one, and
    // flush it.
    const PS::ThreadVector& threads = gPS->Threads(lock);
    for (size_t i = 0; i < threads.size(); i++) {
      ThreadInfo* info = threads.at(i);
      if (info->HasProfile() && !info->IsPendingDelete() &&
          info->Stack() == stack) {
        info->FlushSamplesAndMarkers(gPS->Buffer(lock), gPS->StartTime(lock));
      }
    }

    gPS->SetIsPaused(lock, false);
  }

  // We don't call stack->stopJSSampling() here; there's no point doing
  // that for a JS thread that is in the process of disappearing.

  stack->mContext = nullptr;
}

// END externally visible functions
////////////////////////////////////////////////////////////////////////
