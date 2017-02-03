/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/ArrayUtils.h"
#include "mozilla/Atomics.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/MemoryReporting.h"
#include "mozilla/ThreadLocal.h"
#include "mozilla/Unused.h"

#if defined(XP_DARWIN)
#include <mach/mach.h>
#elif defined(XP_UNIX)
#include <sys/resource.h>
#elif defined(XP_WIN)
#include <windows.h>
#endif // defined(XP_DARWIN) || defined(XP_UNIX) || defined(XP_WIN)

#include <locale.h>
#include <string.h>

#ifdef JS_CAN_CHECK_THREADSAFE_ACCESSES
# include <sys/mman.h>
#endif

#include "jsatom.h"
#include "jsgc.h"
#include "jsmath.h"
#include "jsobj.h"
#include "jsscript.h"
#include "jswatchpoint.h"
#include "jswin.h"
#include "jswrapper.h"

#include "builtin/Promise.h"
#include "gc/GCInternals.h"
#include "jit/arm/Simulator-arm.h"
#include "jit/arm64/vixl/Simulator-vixl.h"
#include "jit/IonBuilder.h"
#include "jit/JitCompartment.h"
#include "jit/mips32/Simulator-mips32.h"
#include "jit/mips64/Simulator-mips64.h"
#include "js/Date.h"
#include "js/MemoryMetrics.h"
#include "js/SliceBudget.h"
#include "vm/Debugger.h"
#include "wasm/WasmSignalHandlers.h"

#include "jscntxtinlines.h"
#include "jsgcinlines.h"

using namespace js;
using namespace js::gc;

using mozilla::Atomic;
using mozilla::DebugOnly;
using mozilla::NegativeInfinity;
using mozilla::PodZero;
using mozilla::PodArrayZero;
using mozilla::PositiveInfinity;
using JS::GenericNaN;
using JS::DoubleNaNValue;

/* static */ MOZ_THREAD_LOCAL(JSContext*) js::TlsContext;
/* static */ Atomic<size_t> JSRuntime::liveRuntimesCount;

namespace js {
    bool gCanUseExtraThreads = true;
} // namespace js

void
js::DisableExtraThreads()
{
    gCanUseExtraThreads = false;
}

const JSSecurityCallbacks js::NullSecurityCallbacks = { };

static const JSWrapObjectCallbacks DefaultWrapObjectCallbacks = {
    TransparentObjectWrapper,
    nullptr
};

static size_t
ReturnZeroSize(const void* p)
{
    return 0;
}

JSRuntime::JSRuntime(JSRuntime* parentRuntime)
  : parentRuntime(parentRuntime),
#ifdef DEBUG
    updateChildRuntimeCount(parentRuntime),
#endif
    profilerSampleBufferGen_(0),
    profilerSampleBufferLapCount_(1),
    telemetryCallback(nullptr),
    getIncumbentGlobalCallback(nullptr),
    enqueuePromiseJobCallback(nullptr),
    enqueuePromiseJobCallbackData(nullptr),
    promiseRejectionTrackerCallback(nullptr),
    promiseRejectionTrackerCallbackData(nullptr),
    startAsyncTaskCallback(nullptr),
    finishAsyncTaskCallback(nullptr),
    promiseTasksToDestroy(mutexid::PromiseTaskPtrVector),
    hadOutOfMemory(false),
    allowRelazificationForTesting(false),
    destroyCompartmentCallback(nullptr),
    sizeOfIncludingThisCompartmentCallback(nullptr),
    destroyZoneCallback(nullptr),
    sweepZoneCallback(nullptr),
    compartmentNameCallback(nullptr),
    externalStringSizeofCallback(nullptr),
    securityCallbacks(&NullSecurityCallbacks),
    DOMcallbacks(nullptr),
    destroyPrincipals(nullptr),
    readPrincipals(nullptr),
    warningReporter(nullptr),
    geckoProfiler_(thisFromCtor()),
    buildIdOp(nullptr),
    trustedPrincipals_(nullptr),
    wrapObjectCallbacks(&DefaultWrapObjectCallbacks),
    preserveWrapperCallback(nullptr),
    scriptEnvironmentPreparer(nullptr),
    ctypesActivityCallback(nullptr),
    windowProxyClass_(nullptr),
    exclusiveAccessLock(mutexid::RuntimeExclusiveAccess),
#ifdef DEBUG
    mainThreadHasExclusiveAccess(false),
#endif
    numExclusiveThreads(0),
    numCompartments(0),
    localeCallbacks(nullptr),
    defaultLocale(nullptr),
    defaultVersion_(JSVERSION_DEFAULT),
    lcovOutput_(),
    jitRuntime_(nullptr),
    selfHostingGlobal_(nullptr),
    singletonContext(nullptr),
    singletonZoneGroup(nullptr),
    gc(thisFromCtor()),
    gcInitialized(false),
    NaNValue(DoubleNaNValue()),
    negativeInfinityValue(DoubleValue(NegativeInfinity<double>())),
    positiveInfinityValue(DoubleValue(PositiveInfinity<double>())),
    emptyString(nullptr),
    defaultFreeOp_(nullptr),
#if !EXPOSE_INTL_API
    thousandsSeparator(nullptr),
    decimalSeparator(nullptr),
    numGrouping(nullptr),
#endif
    beingDestroyed_(false),
    allowContentJS_(true),
    atoms_(nullptr),
    atomsCompartment_(nullptr),
    staticStrings(nullptr),
    commonNames(nullptr),
    permanentAtoms(nullptr),
    wellKnownSymbols(nullptr),
    jitSupportsFloatingPoint(false),
    jitSupportsUnalignedAccesses(false),
    jitSupportsSimd(false),
    offthreadIonCompilationEnabled_(true),
    parallelParsingEnabled_(true),
    autoWritableJitCodeActive_(false),
    largeAllocationFailureCallback(nullptr),
    oomCallback(nullptr),
    debuggerMallocSizeOf(ReturnZeroSize),
    lastAnimationTime(0),
    performanceMonitoring_(thisFromCtor()),
    ionLazyLinkListSize_(0),
    stackFormat_(parentRuntime ? js::StackFormat::Default
                               : js::StackFormat::SpiderMonkey)
{
    liveRuntimesCount++;

    /* Initialize infallibly first, so we can goto bad and JS_DestroyRuntime. */
    JS_INIT_CLIST(&onNewGlobalObjectWatchers());

    PodZero(&asmJSCacheOps);
    lcovOutput().init();
}

bool
JSRuntime::init(JSContext* cx, uint32_t maxbytes, uint32_t maxNurseryBytes)
{
    if (CanUseExtraThreads() && !EnsureHelperThreadsInitialized())
        return false;

    singletonContext = cx;

    defaultFreeOp_ = js_new<js::FreeOp>(this);
    if (!defaultFreeOp_)
        return false;

    ScopedJSDeletePtr<ZoneGroup> zoneGroup(js_new<ZoneGroup>(this));
    if (!zoneGroup)
        return false;
    singletonZoneGroup = zoneGroup;

    if (!gc.init(maxbytes, maxNurseryBytes))
        return false;

    if (!zoneGroup->init(maxNurseryBytes))
        return false;
    zoneGroup.forget();

    ScopedJSDeletePtr<Zone> atomsZone(new_<Zone>(this, nullptr));
    if (!atomsZone || !atomsZone->init(true))
        return false;

    JS::CompartmentOptions options;
    ScopedJSDeletePtr<JSCompartment> atomsCompartment(new_<JSCompartment>(atomsZone.get(), options));
    if (!atomsCompartment || !atomsCompartment->init(nullptr))
        return false;

    gc.atomsZone = atomsZone.get();
    if (!atomsZone->compartments().append(atomsCompartment.get()))
        return false;

    atomsCompartment->setIsSystem(true);
    atomsCompartment->setIsAtomsCompartment();

    atomsZone.forget();
    this->atomsCompartment_ = atomsCompartment.forget();

    if (!symbolRegistry_.ref().init())
        return false;

    if (!scriptDataTable_.ref().init())
        return false;

    /* The garbage collector depends on everything before this point being initialized. */
    gcInitialized = true;

    if (!InitRuntimeNumberState(this))
        return false;

    JS::ResetTimeZone();

    jitSupportsFloatingPoint = js::jit::JitSupportsFloatingPoint();
    jitSupportsUnalignedAccesses = js::jit::JitSupportsUnalignedAccesses();
    jitSupportsSimd = js::jit::JitSupportsSimd();

    if (!wasm::EnsureSignalHandlers(this))
        return false;

    if (!geckoProfiler().init())
        return false;

    if (!parentRuntime) {
        sharedImmutableStrings_ = js::SharedImmutableStringsCache::Create();
        if (!sharedImmutableStrings_)
            return false;
    }

    return true;
}

void
JSRuntime::destroyRuntime()
{
    MOZ_ASSERT(!JS::CurrentThreadIsHeapBusy());
    MOZ_ASSERT(childRuntimeCount == 0);

    sharedIntlData.ref().destroyInstance();

    if (gcInitialized) {
        /*
         * Finish any in-progress GCs first. This ensures the parseWaitingOnGC
         * list is empty in CancelOffThreadParses.
         */
        JSContext* cx = TlsContext.get();
        if (JS::IsIncrementalGCInProgress(cx))
            FinishGC(cx);

        /* Free source hook early, as its destructor may want to delete roots. */
        sourceHook = nullptr;

        /*
         * Cancel any pending, in progress or completed Ion compilations and
         * parse tasks. Waiting for wasm and compression tasks is done
         * synchronously (on the main thread or during parse tasks), so no
         * explicit canceling is needed for these.
         */
        CancelOffThreadIonCompile(this);
        CancelOffThreadParses(this);

        /* Remove persistent GC roots. */
        gc.finishRoots();

        /*
         * Flag us as being destroyed. This allows the GC to free things like
         * interned atoms and Ion trampolines.
         */
        beingDestroyed_ = true;

        /* Allow the GC to release scripts that were being profiled. */
        zoneGroupFromMainThread()->profilingScripts = false;

        /* Set the profiler sampler buffer generation to invalid. */
        profilerSampleBufferGen_ = UINT32_MAX;

        JS::PrepareForFullGC(cx);
        gc.gc(GC_NORMAL, JS::gcreason::DESTROY_RUNTIME);
    }

    AutoNoteSingleThreadedRegion anstr;

    MOZ_ASSERT(ionLazyLinkListSize_ == 0);
    MOZ_ASSERT(ionLazyLinkList().isEmpty());

    MOZ_ASSERT(!numExclusiveThreads);
    AutoLockForExclusiveAccess lock(this);

    /*
     * Even though all objects in the compartment are dead, we may have keep
     * some filenames around because of gcKeepAtoms.
     */
    FreeScriptData(this, lock);

#if !EXPOSE_INTL_API
    FinishRuntimeNumberState(this);
#endif

    gc.finish();
    atomsCompartment_ = nullptr;

    js_delete(defaultFreeOp_.ref());

    js_free(defaultLocale);
    js_delete(jitRuntime_.ref());

    DebugOnly<size_t> oldCount = liveRuntimesCount--;
    MOZ_ASSERT(oldCount > 0);

#ifdef JS_TRACE_LOGGING
    DestroyTraceLoggerMainThread(this);
#endif

    js_delete(zoneGroupFromMainThread());
}

void
JSRuntime::addTelemetry(int id, uint32_t sample, const char* key)
{
    if (telemetryCallback)
        (*telemetryCallback)(id, sample, key);
}

void
JSRuntime::setTelemetryCallback(JSRuntime* rt, JSAccumulateTelemetryDataCallback callback)
{
    rt->telemetryCallback = callback;
}

void
JSRuntime::addSizeOfIncludingThis(mozilla::MallocSizeOf mallocSizeOf, JS::RuntimeSizes* rtSizes)
{
    // Several tables in the runtime enumerated below can be used off thread.
    AutoLockForExclusiveAccess lock(this);

    // For now, measure the size of the derived class (JSContext).
    // TODO (bug 1281529): make memory reporting reflect the new
    // JSContext/JSRuntime world better.
    JSContext* cx = unsafeContextFromAnyThread();
    rtSizes->object += mallocSizeOf(cx);

    rtSizes->atomsTable += atoms(lock).sizeOfIncludingThis(mallocSizeOf);

    if (!parentRuntime) {
        rtSizes->atomsTable += mallocSizeOf(staticStrings);
        rtSizes->atomsTable += mallocSizeOf(commonNames);
        rtSizes->atomsTable += permanentAtoms->sizeOfIncludingThis(mallocSizeOf);
    }

    rtSizes->contexts += cx->sizeOfExcludingThis(mallocSizeOf);

    rtSizes->temporary += cx->tempLifoAlloc().sizeOfExcludingThis(mallocSizeOf);

    rtSizes->interpreterStack += cx->interpreterStack().sizeOfExcludingThis(mallocSizeOf);

    ZoneGroupCaches& caches = zoneGroupFromAnyThread()->caches();

    if (MathCache* cache = caches.maybeGetMathCache())
        rtSizes->mathCache += cache->sizeOfIncludingThis(mallocSizeOf);

    if (sharedImmutableStrings_) {
        rtSizes->sharedImmutableStringsCache +=
            sharedImmutableStrings_->sizeOfExcludingThis(mallocSizeOf);
    }

    rtSizes->sharedIntlData += sharedIntlData.ref().sizeOfExcludingThis(mallocSizeOf);

    rtSizes->uncompressedSourceCache +=
        caches.uncompressedSourceCache.sizeOfExcludingThis(mallocSizeOf);

    rtSizes->scriptData += scriptDataTable(lock).sizeOfExcludingThis(mallocSizeOf);
    for (ScriptDataTable::Range r = scriptDataTable(lock).all(); !r.empty(); r.popFront())
        rtSizes->scriptData += mallocSizeOf(r.front());

    if (jitRuntime_) {
        jitRuntime_->execAlloc().addSizeOfCode(&rtSizes->code);
        jitRuntime_->backedgeExecAlloc().addSizeOfCode(&rtSizes->code);
    }

    rtSizes->gc.marker += gc.marker.sizeOfExcludingThis(mallocSizeOf);
    rtSizes->gc.nurseryCommitted += zoneGroupFromAnyThread()->nursery().sizeOfHeapCommitted();
    rtSizes->gc.nurseryMallocedBuffers += zoneGroupFromAnyThread()->nursery().sizeOfMallocedBuffers(mallocSizeOf);
    zoneGroupFromAnyThread()->storeBuffer().addSizeOfExcludingThis(mallocSizeOf, &rtSizes->gc);
}

static bool
InvokeInterruptCallback(JSContext* cx)
{
    MOZ_ASSERT(cx->requestDepth >= 1);

    cx->runtime()->gc.gcIfRequested();

    // A worker thread may have requested an interrupt after finishing an Ion
    // compilation.
    jit::AttachFinishedCompilations(cx);

    // Important: Additional callbacks can occur inside the callback handler
    // if it re-enters the JS engine. The embedding must ensure that the
    // callback is disconnected before attempting such re-entry.
    if (cx->interruptCallbackDisabled)
        return true;

    bool stop = false;
    for (JSInterruptCallback cb : cx->interruptCallbacks()) {
        if (!cb(cx))
            stop = true;
    }

    if (!stop) {
        // Debugger treats invoking the interrupt callback as a "step", so
        // invoke the onStep handler.
        if (cx->compartment()->isDebuggee()) {
            ScriptFrameIter iter(cx);
            if (!iter.done() &&
                cx->compartment() == iter.compartment() &&
                iter.script()->stepModeEnabled())
            {
                RootedValue rval(cx);
                switch (Debugger::onSingleStep(cx, &rval)) {
                  case JSTRAP_ERROR:
                    return false;
                  case JSTRAP_CONTINUE:
                    return true;
                  case JSTRAP_RETURN:
                    // See note in Debugger::propagateForcedReturn.
                    Debugger::propagateForcedReturn(cx, iter.abstractFramePtr(), rval);
                    return false;
                  case JSTRAP_THROW:
                    cx->setPendingException(rval);
                    return false;
                  default:;
                }
            }
        }

        return true;
    }

    // No need to set aside any pending exception here: ComputeStackString
    // already does that.
    JSString* stack = ComputeStackString(cx);
    JSFlatString* flat = stack ? stack->ensureFlat(cx) : nullptr;

    const char16_t* chars;
    AutoStableStringChars stableChars(cx);
    if (flat && stableChars.initTwoByte(cx, flat))
        chars = stableChars.twoByteRange().begin().get();
    else
        chars = u"(stack not available)";
    JS_ReportErrorFlagsAndNumberUC(cx, JSREPORT_WARNING, GetErrorMessage, nullptr,
                                   JSMSG_TERMINATED, chars);

    return false;
}

void
JSContext::requestInterrupt(InterruptMode mode)
{
    interrupt_ = true;
    jitStackLimit = UINTPTR_MAX;

    if (mode == JSContext::RequestInterruptUrgent) {
        // If this interrupt is urgent (slow script dialog and garbage
        // collection among others), take additional steps to
        // interrupt corner cases where the above fields are not
        // regularly polled.  Wake both ilooping JIT code and
        // Atomics.wait().
        fx.lock();
        if (fx.isWaiting())
            fx.wake(FutexThread::WakeForJSInterrupt);
        fx.unlock();
        InterruptRunningJitCode(runtime());
    }
}

bool
JSContext::handleInterrupt()
{
    MOZ_ASSERT(CurrentThreadCanAccessRuntime(runtime()));
    if (interrupt_ || jitStackLimit == UINTPTR_MAX) {
        interrupt_ = false;
        resetJitStackLimit();
        return InvokeInterruptCallback(this);
    }
    return true;
}

bool
JSRuntime::setDefaultLocale(const char* locale)
{
    if (!locale)
        return false;
    resetDefaultLocale();
    defaultLocale = JS_strdup(contextFromMainThread(), locale);
    return defaultLocale != nullptr;
}

void
JSRuntime::resetDefaultLocale()
{
    js_free(defaultLocale);
    defaultLocale = nullptr;
}

const char*
JSRuntime::getDefaultLocale()
{
    if (defaultLocale)
        return defaultLocale;

    const char* locale;
#ifdef HAVE_SETLOCALE
    locale = setlocale(LC_ALL, nullptr);
#else
    locale = getenv("LANG");
#endif
    // convert to a well-formed BCP 47 language tag
    if (!locale || !strcmp(locale, "C"))
        locale = "und";

    char* lang = JS_strdup(contextFromMainThread(), locale);
    if (!lang)
        return nullptr;

    char* p;
    if ((p = strchr(lang, '.')))
        *p = '\0';
    while ((p = strchr(lang, '_')))
        *p = '-';

    defaultLocale = lang;
    return defaultLocale;
}

void
JSRuntime::traceSharedIntlData(JSTracer* trc)
{
    sharedIntlData.ref().trace(trc);
}

void
JSContext::triggerActivityCallback(bool active)
{
    if (!activityCallback)
        return;

    /*
     * The activity callback must not trigger a GC: it would create a cirular
     * dependency between entering a request and Rooted's requirement of being
     * in a request. In practice this callback already cannot trigger GC. The
     * suppression serves to inform the exact rooting hazard analysis of this
     * property and ensures that it remains true in the future.
     */
    AutoSuppressGC suppress(this);

    activityCallback(activityCallbackArg, active);
}

FreeOp::FreeOp(JSRuntime* maybeRuntime)
  : JSFreeOp(maybeRuntime)
{
    MOZ_ASSERT_IF(maybeRuntime, CurrentThreadCanAccessRuntime(maybeRuntime));
}

FreeOp::~FreeOp()
{
    for (size_t i = 0; i < freeLaterList.length(); i++)
        free_(freeLaterList[i]);

    if (!jitPoisonRanges.empty())
        jit::ExecutableAllocator::poisonCode(runtime(), jitPoisonRanges);
}

bool
FreeOp::isDefaultFreeOp() const
{
    return runtime_ && runtime_->defaultFreeOp() == this;
}

JSObject*
JSRuntime::getIncumbentGlobal(JSContext* cx)
{
    MOZ_ASSERT(cx->runtime()->getIncumbentGlobalCallback,
               "Must set a callback using JS_SetGetIncumbentGlobalCallback before using Promises");

    return cx->runtime()->getIncumbentGlobalCallback(cx);
}

bool
JSRuntime::enqueuePromiseJob(JSContext* cx, HandleFunction job, HandleObject promise,
                             HandleObject incumbentGlobal)
{
    MOZ_ASSERT(cx->runtime()->enqueuePromiseJobCallback,
               "Must set a callback using JS_SetEnqeueuPromiseJobCallback before using Promises");
    MOZ_ASSERT_IF(incumbentGlobal, !IsWrapper(incumbentGlobal) && !IsWindowProxy(incumbentGlobal));

    void* data = cx->runtime()->enqueuePromiseJobCallbackData;
    RootedObject allocationSite(cx);
    if (promise) {
        RootedObject unwrappedPromise(cx, promise);
        // While the job object is guaranteed to be unwrapped, the promise
        // might be wrapped. See the comments in
        // intrinsic_EnqueuePromiseReactionJob for details.
        if (IsWrapper(promise))
            unwrappedPromise = UncheckedUnwrap(promise);
        if (unwrappedPromise->is<PromiseObject>())
            allocationSite = JS::GetPromiseAllocationSite(unwrappedPromise);
    }
    return cx->runtime()->enqueuePromiseJobCallback(cx, job, allocationSite, incumbentGlobal, data);
}

void
JSRuntime::addUnhandledRejectedPromise(JSContext* cx, js::HandleObject promise)
{
    MOZ_ASSERT(promise->is<PromiseObject>());
    if (!cx->runtime()->promiseRejectionTrackerCallback)
        return;

    void* data = cx->runtime()->promiseRejectionTrackerCallbackData;
    cx->runtime()->promiseRejectionTrackerCallback(cx, promise,
                                                   PromiseRejectionHandlingState::Unhandled, data);
}

void
JSRuntime::removeUnhandledRejectedPromise(JSContext* cx, js::HandleObject promise)
{
    MOZ_ASSERT(promise->is<PromiseObject>());
    if (!cx->runtime()->promiseRejectionTrackerCallback)
        return;

    void* data = cx->runtime()->promiseRejectionTrackerCallbackData;
    cx->runtime()->promiseRejectionTrackerCallback(cx, promise,
                                                   PromiseRejectionHandlingState::Handled, data);
}

mozilla::non_crypto::XorShift128PlusRNG&
JSRuntime::randomKeyGenerator()
{
    MOZ_ASSERT(CurrentThreadCanAccessRuntime(this));
    if (randomKeyGenerator_.isNothing()) {
        mozilla::Array<uint64_t, 2> seed;
        GenerateXorShift128PlusSeed(seed);
        randomKeyGenerator_.emplace(seed[0], seed[1]);
    }
    return randomKeyGenerator_.ref();
}

mozilla::HashCodeScrambler
JSRuntime::randomHashCodeScrambler()
{
    auto& rng = randomKeyGenerator();
    return mozilla::HashCodeScrambler(rng.next(), rng.next());
}

mozilla::non_crypto::XorShift128PlusRNG
JSRuntime::forkRandomKeyGenerator()
{
    auto& rng = randomKeyGenerator();
    return mozilla::non_crypto::XorShift128PlusRNG(rng.next(), rng.next());
}

void
JSRuntime::updateMallocCounter(size_t nbytes)
{
    updateMallocCounter(nullptr, nbytes);
}

void
JSRuntime::updateMallocCounter(JS::Zone* zone, size_t nbytes)
{
    gc.updateMallocCounter(zone, nbytes);
}

JS_FRIEND_API(void*)
JSRuntime::onOutOfMemory(AllocFunction allocFunc, size_t nbytes, void* reallocPtr, JSContext* maybecx)
{
    MOZ_ASSERT_IF(allocFunc != AllocFunction::Realloc, !reallocPtr);

    if (JS::CurrentThreadIsHeapBusy())
        return nullptr;

    if (!oom::IsSimulatedOOMAllocation()) {
        /*
         * Retry when we are done with the background sweeping and have stopped
         * all the allocations and released the empty GC chunks.
         */
        gc.onOutOfMallocMemory();
        void* p;
        switch (allocFunc) {
          case AllocFunction::Malloc:
            p = js_malloc(nbytes);
            break;
          case AllocFunction::Calloc:
            p = js_calloc(nbytes);
            break;
          case AllocFunction::Realloc:
            p = js_realloc(reallocPtr, nbytes);
            break;
          default:
            MOZ_CRASH();
        }
        if (p)
            return p;
    }

    if (maybecx)
        ReportOutOfMemory(maybecx);
    return nullptr;
}

void*
JSRuntime::onOutOfMemoryCanGC(AllocFunction allocFunc, size_t bytes, void* reallocPtr)
{
    if (largeAllocationFailureCallback && bytes >= LARGE_ALLOCATION)
        largeAllocationFailureCallback(largeAllocationFailureCallbackData);
    return onOutOfMemory(allocFunc, bytes, reallocPtr);
}

bool
JSRuntime::activeGCInAtomsZone()
{
    Zone* zone = atomsCompartment_->zone();
    return (zone->needsIncrementalBarrier() && !gc.isVerifyPreBarriersEnabled()) ||
           zone->wasGCStarted();
}

void
JSRuntime::setUsedByExclusiveThread(Zone* zone)
{
    MOZ_ASSERT(!zone->usedByExclusiveThread);
    MOZ_ASSERT(!zone->wasGCStarted());
    zone->usedByExclusiveThread = true;
    numExclusiveThreads++;
}

void
JSRuntime::clearUsedByExclusiveThread(Zone* zone)
{
    MOZ_ASSERT(zone->usedByExclusiveThread);
    zone->usedByExclusiveThread = false;
    numExclusiveThreads--;
    if (gc.fullGCForAtomsRequested() && !TlsContext.get())
        gc.triggerFullGCForAtoms();
}

bool
js::CurrentThreadCanAccessRuntime(const JSRuntime* rt)
{
    return rt->unsafeContextFromAnyThread() == TlsContext.get();
}

bool
js::CurrentThreadCanAccessZone(Zone* zone)
{
    if (CurrentThreadCanAccessRuntime(zone->runtime_))
        return true;

    // Only zones in use by an exclusive thread can be used off the main thread.
    // We don't keep track of which thread owns such zones though, so this check
    // is imperfect.
    return zone->usedByExclusiveThread;
}

#ifdef DEBUG
bool
js::CurrentThreadIsPerformingGC()
{
    return TlsContext.get()->performingGC;
}
#endif

JS_FRIEND_API(void)
JS::UpdateJSContextProfilerSampleBufferGen(JSContext* cx, uint32_t generation,
                                           uint32_t lapCount)
{
    cx->runtime()->setProfilerSampleBufferGen(generation);
    cx->runtime()->updateProfilerSampleBufferLapCount(lapCount);
}

JS_FRIEND_API(bool)
JS::IsProfilingEnabledForContext(JSContext* cx)
{
    MOZ_ASSERT(cx);
    return cx->runtime()->geckoProfiler().enabled();
}

JSRuntime::IonBuilderList&
JSRuntime::ionLazyLinkList()
{
    MOZ_ASSERT(CurrentThreadCanAccessRuntime(this),
               "Should only be mutated by the main thread.");
    return ionLazyLinkList_.ref();
}

void
JSRuntime::ionLazyLinkListRemove(jit::IonBuilder* builder)
{
    MOZ_ASSERT(CurrentThreadCanAccessRuntime(this),
               "Should only be mutated by the main thread.");
    MOZ_ASSERT(ionLazyLinkListSize_ > 0);

    builder->removeFrom(ionLazyLinkList());
    ionLazyLinkListSize_--;

    MOZ_ASSERT(ionLazyLinkList().isEmpty() == (ionLazyLinkListSize_ == 0));
}

void
JSRuntime::ionLazyLinkListAdd(jit::IonBuilder* builder)
{
    MOZ_ASSERT(CurrentThreadCanAccessRuntime(this),
               "Should only be mutated by the main thread.");
    ionLazyLinkList().insertFront(builder);
    ionLazyLinkListSize_++;
}
