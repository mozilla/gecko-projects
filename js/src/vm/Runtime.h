/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef vm_Runtime_h
#define vm_Runtime_h

#include "mozilla/Atomics.h"
#include "mozilla/Attributes.h"
#include "mozilla/LinkedList.h"
#include "mozilla/MemoryReporting.h"
#include "mozilla/PodOperations.h"
#include "mozilla/Scoped.h"
#include "mozilla/ThreadLocal.h"
#include "mozilla/Vector.h"

#include <setjmp.h>

#include "jsatom.h"
#include "jsclist.h"
#include "jsscript.h"

#ifdef XP_DARWIN
# include "wasm/WasmSignalHandlers.h"
#endif
#include "builtin/AtomicsObject.h"
#include "builtin/Intl.h"
#include "builtin/Promise.h"
#include "ds/FixedSizeHash.h"
#include "frontend/NameCollections.h"
#include "gc/GCRuntime.h"
#include "gc/Tracer.h"
#include "gc/ZoneGroup.h"
#include "irregexp/RegExpStack.h"
#include "js/Debug.h"
#include "js/GCVector.h"
#include "js/HashTable.h"
#ifdef DEBUG
# include "js/Proxy.h" // For AutoEnterPolicy
#endif
#include "js/UniquePtr.h"
#include "js/Vector.h"
#include "threading/Thread.h"
#include "vm/Caches.h"
#include "vm/CodeCoverage.h"
#include "vm/CommonPropertyNames.h"
#include "vm/DateTime.h"
#include "vm/GeckoProfiler.h"
#include "vm/MallocProvider.h"
#include "vm/Scope.h"
#include "vm/SharedImmutableStringsCache.h"
#include "vm/Stack.h"
#include "vm/Stopwatch.h"
#include "vm/Symbol.h"

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4100) /* Silence unreferenced formal parameter warnings */
#endif

namespace js {

class AutoAssertNoContentJS;
class AutoKeepAtoms;
class EnterDebuggeeNoExecute;
#ifdef JS_TRACE_LOGGING
class TraceLoggerThread;
#endif

typedef Vector<UniquePtr<PromiseTask>, 0, SystemAllocPolicy> PromiseTaskPtrVector;

} // namespace js

struct DtoaState;

#ifdef JS_SIMULATOR_ARM64
namespace vixl {
class Simulator;
}
#endif

namespace js {

extern MOZ_COLD void
ReportOutOfMemory(JSContext* cx);

/* Different signature because the return type has MOZ_MUST_USE_TYPE. */
extern MOZ_COLD mozilla::GenericErrorResult<OOM&>
ReportOutOfMemoryResult(JSContext* cx);

extern MOZ_COLD void
ReportAllocationOverflow(JSContext* maybecx);

extern MOZ_COLD void
ReportOverRecursed(JSContext* cx);

class Activation;
class ActivationIterator;
class WasmActivation;

namespace jit {
class JitRuntime;
class JitActivation;
struct PcScriptCache;
struct AutoFlushICache;
class CompileRuntime;

#ifdef JS_SIMULATOR_ARM64
typedef vixl::Simulator Simulator;
#elif defined(JS_SIMULATOR)
class Simulator;
#endif
} // namespace jit

/*
 * A FreeOp can do one thing: free memory. For convenience, it has delete_
 * convenience methods that also call destructors.
 *
 * FreeOp is passed to finalizers and other sweep-phase hooks so that we do not
 * need to pass a JSContext to those hooks.
 */
class FreeOp : public JSFreeOp
{
    Vector<void*, 0, SystemAllocPolicy> freeLaterList;
    jit::JitPoisonRangeVector jitPoisonRanges;

  public:
    static FreeOp* get(JSFreeOp* fop) {
        return static_cast<FreeOp*>(fop);
    }

    explicit FreeOp(JSRuntime* maybeRuntime);
    ~FreeOp();

    bool onMainThread() const {
        return runtime_ != nullptr;
    }

    bool maybeOffMainThread() const {
        // Sometimes background finalization happens on the main thread so
        // runtime_ being null doesn't always mean we are off the main thread.
        return !runtime_;
    }

    bool isDefaultFreeOp() const;

    inline void free_(void* p);
    inline void freeLater(void* p);

    inline bool appendJitPoisonRange(const jit::JitPoisonRange& range);

    template <class T>
    inline void delete_(T* p) {
        if (p) {
            p->~T();
            free_(p);
        }
    }
};

} /* namespace js */

namespace JS {
struct RuntimeSizes;
} // namespace JS

/* Various built-in or commonly-used names pinned on first context. */
struct JSAtomState
{
#define PROPERTYNAME_FIELD(idpart, id, text) js::ImmutablePropertyNamePtr id;
    FOR_EACH_COMMON_PROPERTYNAME(PROPERTYNAME_FIELD)
#undef PROPERTYNAME_FIELD
#define PROPERTYNAME_FIELD(name, code, init, clasp) js::ImmutablePropertyNamePtr name;
    JS_FOR_EACH_PROTOTYPE(PROPERTYNAME_FIELD)
#undef PROPERTYNAME_FIELD
#define PROPERTYNAME_FIELD(name) js::ImmutablePropertyNamePtr name;
    JS_FOR_EACH_WELL_KNOWN_SYMBOL(PROPERTYNAME_FIELD)
#undef PROPERTYNAME_FIELD
#define PROPERTYNAME_FIELD(name) js::ImmutablePropertyNamePtr Symbol_##name;
    JS_FOR_EACH_WELL_KNOWN_SYMBOL(PROPERTYNAME_FIELD)
#undef PROPERTYNAME_FIELD

    js::ImmutablePropertyNamePtr* wellKnownSymbolNames() {
#define FIRST_PROPERTYNAME_FIELD(name) return &name;
    JS_FOR_EACH_WELL_KNOWN_SYMBOL(FIRST_PROPERTYNAME_FIELD)
#undef FIRST_PROPERTYNAME_FIELD
    }

    js::ImmutablePropertyNamePtr* wellKnownSymbolDescriptions() {
#define FIRST_PROPERTYNAME_FIELD(name) return &Symbol_ ##name;
    JS_FOR_EACH_WELL_KNOWN_SYMBOL(FIRST_PROPERTYNAME_FIELD)
#undef FIRST_PROPERTYNAME_FIELD
    }
};

namespace js {

/*
 * Storage for well-known symbols. It's a separate struct from the Runtime so
 * that it can be shared across multiple runtimes. As in JSAtomState, each
 * field is a smart pointer that's immutable once initialized.
 * `rt->wellKnownSymbols->iterator` is convertible to Handle<Symbol*>.
 *
 * Well-known symbols are never GC'd. The description() of each well-known
 * symbol is a permanent atom.
 */
struct WellKnownSymbols
{
#define DECLARE_SYMBOL(name) js::ImmutableSymbolPtr name;
    JS_FOR_EACH_WELL_KNOWN_SYMBOL(DECLARE_SYMBOL)
#undef DECLARE_SYMBOL

    const ImmutableSymbolPtr& get(size_t u) const {
        MOZ_ASSERT(u < JS::WellKnownSymbolLimit);
        const ImmutableSymbolPtr* symbols = reinterpret_cast<const ImmutableSymbolPtr*>(this);
        return symbols[u];
    }

    const ImmutableSymbolPtr& get(JS::SymbolCode code) const {
        return get(size_t(code));
    }

    WellKnownSymbols() {}
    WellKnownSymbols(const WellKnownSymbols&) = delete;
    WellKnownSymbols& operator=(const WellKnownSymbols&) = delete;
};

#define NAME_OFFSET(name)       offsetof(JSAtomState, name)

inline HandlePropertyName
AtomStateOffsetToName(const JSAtomState& atomState, size_t offset)
{
    return *reinterpret_cast<js::ImmutablePropertyNamePtr*>((char*)&atomState + offset);
}

// There are several coarse locks in the enum below. These may be either
// per-runtime or per-process. When acquiring more than one of these locks,
// the acquisition must be done in the order below to avoid deadlocks.
enum RuntimeLock {
    ExclusiveAccessLock,
    HelperThreadStateLock,
    GCLock
};

inline bool
CanUseExtraThreads()
{
    extern bool gCanUseExtraThreads;
    return gCanUseExtraThreads;
}

void DisableExtraThreads();

class AutoLockForExclusiveAccess;
} // namespace js

struct JSRuntime : public js::MallocProvider<JSRuntime>
{
  private:
    friend class js::Activation;
    friend class js::ActivationIterator;
    friend class js::jit::JitActivation;
    friend class js::WasmActivation;
    friend class js::jit::CompileRuntime;

  public:
    /*
     * If non-null, another runtime guaranteed to outlive this one and whose
     * permanent data may be used by this one where possible.
     */
    JSRuntime* const parentRuntime;

#ifdef DEBUG
    /* The number of child runtimes that have this runtime as their parent. */
    mozilla::Atomic<size_t> childRuntimeCount;

    class AutoUpdateChildRuntimeCount
    {
        JSRuntime* parent_;

      public:
        explicit AutoUpdateChildRuntimeCount(JSRuntime* parent)
          : parent_(parent)
        {
            if (parent_)
                parent_->childRuntimeCount++;
        }

        ~AutoUpdateChildRuntimeCount() {
            if (parent_)
                parent_->childRuntimeCount--;
        }
    };

    AutoUpdateChildRuntimeCount updateChildRuntimeCount;
#endif

    /*
     * The profiler sampler generation after the latest sample.
     *
     * The lapCount indicates the number of largest number of 'laps'
     * (wrapping from high to low) that occurred when writing entries
     * into the sample buffer.  All JitcodeGlobalMap entries referenced
     * from a given sample are assigned the generation of the sample buffer
     * at the START of the run.  If multiple laps occur, then some entries
     * (towards the end) will be written out with the "wrong" generation.
     * The lapCount indicates the required fudge factor to use to compare
     * entry generations with the sample buffer generation.
     */
    mozilla::Atomic<uint32_t, mozilla::ReleaseAcquire> profilerSampleBufferGen_;
    mozilla::Atomic<uint32_t, mozilla::ReleaseAcquire> profilerSampleBufferLapCount_;

    uint32_t profilerSampleBufferGen() {
        return profilerSampleBufferGen_;
    }
    void resetProfilerSampleBufferGen() {
        profilerSampleBufferGen_ = 0;
    }
    void setProfilerSampleBufferGen(uint32_t gen) {
        // May be called from sampler thread or signal handler; use
        // compareExchange to make sure we have monotonic increase.
        for (;;) {
            uint32_t curGen = profilerSampleBufferGen_;
            if (curGen >= gen)
                break;

            if (profilerSampleBufferGen_.compareExchange(curGen, gen))
                break;
        }
    }

    uint32_t profilerSampleBufferLapCount() {
        MOZ_ASSERT(profilerSampleBufferLapCount_ > 0);
        return profilerSampleBufferLapCount_;
    }
    void resetProfilerSampleBufferLapCount() {
        profilerSampleBufferLapCount_ = 1;
    }
    void updateProfilerSampleBufferLapCount(uint32_t lapCount) {
        MOZ_ASSERT(profilerSampleBufferLapCount_ > 0);

        // May be called from sampler thread or signal handler; use
        // compareExchange to make sure we have monotonic increase.
        for (;;) {
            uint32_t curLapCount = profilerSampleBufferLapCount_;
            if (curLapCount >= lapCount)
                break;

            if (profilerSampleBufferLapCount_.compareExchange(curLapCount, lapCount))
                break;
        }
    }

    /* Call this to accumulate telemetry data. */
    js::UnprotectedData<JSAccumulateTelemetryDataCallback> telemetryCallback;
  public:
    // Accumulates data for Firefox telemetry. |id| is the ID of a JS_TELEMETRY_*
    // histogram. |key| provides an additional key to identify the histogram.
    // |sample| is the data to add to the histogram.
    void addTelemetry(int id, uint32_t sample, const char* key = nullptr);

    void setTelemetryCallback(JSRuntime* rt, JSAccumulateTelemetryDataCallback callback);

  public:
    js::UnprotectedData<JSGetIncumbentGlobalCallback> getIncumbentGlobalCallback;
    js::UnprotectedData<JSEnqueuePromiseJobCallback> enqueuePromiseJobCallback;
    js::UnprotectedData<void*> enqueuePromiseJobCallbackData;

    js::UnprotectedData<JSPromiseRejectionTrackerCallback> promiseRejectionTrackerCallback;
    js::UnprotectedData<void*> promiseRejectionTrackerCallbackData;

    js::UnprotectedData<JS::StartAsyncTaskCallback> startAsyncTaskCallback;
    js::UnprotectedData<JS::FinishAsyncTaskCallback> finishAsyncTaskCallback;
    js::ExclusiveData<js::PromiseTaskPtrVector> promiseTasksToDestroy;

    JSObject* getIncumbentGlobal(JSContext* cx);
    bool enqueuePromiseJob(JSContext* cx, js::HandleFunction job, js::HandleObject promise,
                           js::HandleObject incumbentGlobal);
    void addUnhandledRejectedPromise(JSContext* cx, js::HandleObject promise);
    void removeUnhandledRejectedPromise(JSContext* cx, js::HandleObject promise);

    /* Had an out-of-memory error which did not populate an exception. */
    mozilla::Atomic<bool> hadOutOfMemory;

    /*
     * Allow relazifying functions in compartments that are active. This is
     * only used by the relazifyFunctions() testing function.
     */
    js::UnprotectedData<bool> allowRelazificationForTesting;

    /* Compartment destroy callback. */
    js::UnprotectedData<JSDestroyCompartmentCallback> destroyCompartmentCallback;

    /* Compartment memory reporting callback. */
    js::UnprotectedData<JSSizeOfIncludingThisCompartmentCallback> sizeOfIncludingThisCompartmentCallback;

    /* Zone destroy callback. */
    js::UnprotectedData<JSZoneCallback> destroyZoneCallback;

    /* Zone sweep callback. */
    js::UnprotectedData<JSZoneCallback> sweepZoneCallback;

    /* Call this to get the name of a compartment. */
    js::UnprotectedData<JSCompartmentNameCallback> compartmentNameCallback;

    /* Callback for doing memory reporting on external strings. */
    js::UnprotectedData<JSExternalStringSizeofCallback> externalStringSizeofCallback;

    js::UnprotectedData<mozilla::UniquePtr<js::SourceHook>> sourceHook;

    js::UnprotectedData<const JSSecurityCallbacks*> securityCallbacks;
    js::UnprotectedData<const js::DOMCallbacks*> DOMcallbacks;
    js::UnprotectedData<JSDestroyPrincipalsOp> destroyPrincipals;
    js::UnprotectedData<JSReadPrincipalsOp> readPrincipals;

    /* Optional warning reporter. */
    js::UnprotectedData<JS::WarningReporter> warningReporter;

  private:
    /* Gecko profiling metadata */
    js::UnprotectedData<js::GeckoProfiler> geckoProfiler_;
  public:
    js::GeckoProfiler& geckoProfiler() { return geckoProfiler_.ref(); }

    // Heap GC roots for PersistentRooted pointers.
    js::UnprotectedData<mozilla::EnumeratedArray<JS::RootKind, JS::RootKind::Limit,
                                                 mozilla::LinkedList<JS::PersistentRooted<void*>>>> heapRoots;

    void tracePersistentRoots(JSTracer* trc);
    void finishPersistentRoots();

    void finishRoots();

  public:
    js::UnprotectedData<JS::BuildIdOp> buildIdOp;

    /* AsmJSCache callbacks are runtime-wide. */
    js::UnprotectedData<JS::AsmJSCacheOps> asmJSCacheOps;

  private:
    js::UnprotectedData<const JSPrincipals*> trustedPrincipals_;
  public:
    void setTrustedPrincipals(const JSPrincipals* p) { trustedPrincipals_ = p; }
    const JSPrincipals* trustedPrincipals() const { return trustedPrincipals_; }

    js::UnprotectedData<const JSWrapObjectCallbacks*> wrapObjectCallbacks;
    js::UnprotectedData<js::PreserveWrapperCallback> preserveWrapperCallback;

    js::UnprotectedData<js::ScriptEnvironmentPreparer*> scriptEnvironmentPreparer;

    js::UnprotectedData<js::CTypesActivityCallback> ctypesActivityCallback;

  private:
    js::UnprotectedData<const js::Class*> windowProxyClass_;

  public:
    const js::Class* maybeWindowProxyClass() const {
        return windowProxyClass_;
    }
    void setWindowProxyClass(const js::Class* clasp) {
        windowProxyClass_ = clasp;
    }

  private:
    /*
     * Head of circular list of all enabled Debuggers that have
     * onNewGlobalObject handler methods established.
     */
    js::UnprotectedData<JSCList> onNewGlobalObjectWatchers_;
  public:
    JSCList& onNewGlobalObjectWatchers() { return onNewGlobalObjectWatchers_.ref(); }

  private:
    /*
     * Lock taken when using per-runtime or per-zone data that could otherwise
     * be accessed simultaneously by multiple threads.
     *
     * Locking this only occurs if there is actually a thread other than the
     * main thread which could access such data.
     */
    js::Mutex exclusiveAccessLock;
#ifdef DEBUG
    bool mainThreadHasExclusiveAccess;
#endif

    /* Number of non-main threads with exclusive access to some zone. */
    js::UnprotectedData<size_t> numExclusiveThreads;

    friend class js::AutoLockForExclusiveAccess;

  public:
    void setUsedByExclusiveThread(JS::Zone* zone);
    void clearUsedByExclusiveThread(JS::Zone* zone);

    bool exclusiveThreadsPresent() const {
        return numExclusiveThreads > 0;
    }

#ifdef DEBUG
    bool currentThreadHasExclusiveAccess() const {
        return (!exclusiveThreadsPresent() && mainThreadHasExclusiveAccess) ||
            exclusiveAccessLock.ownedByCurrentThread();
    }
#endif

    // How many compartments there are across all zones. This number includes
    // off main thread context compartments, so it isn't necessarily equal to the
    // number of compartments visited by CompartmentsIter.
    js::UnprotectedData<size_t> numCompartments;

    /* Locale-specific callbacks for string conversion. */
    js::UnprotectedData<const JSLocaleCallbacks*> localeCallbacks;

    /* Default locale for Internationalization API */
    js::UnprotectedData<char*> defaultLocale;

    /* Default JSVersion. */
    js::UnprotectedData<JSVersion> defaultVersion_;

  private:
    /* Code coverage output. */
    js::UnprotectedData<js::coverage::LCovRuntime> lcovOutput_;
  public:
    js::coverage::LCovRuntime& lcovOutput() { return lcovOutput_.ref(); }

  private:
    js::UnprotectedData<js::jit::JitRuntime*> jitRuntime_;

    /*
     * Self-hosting state cloned on demand into other compartments. Shared with the parent
     * runtime if there is one.
     */
    js::WriteOnceData<js::NativeObject*> selfHostingGlobal_;

    static js::GlobalObject*
    createSelfHostingGlobal(JSContext* cx);

    bool getUnclonedSelfHostedValue(JSContext* cx, js::HandlePropertyName name,
                                    js::MutableHandleValue vp);
    JSFunction* getUnclonedSelfHostedFunction(JSContext* cx, js::HandlePropertyName name);

    js::jit::JitRuntime* createJitRuntime(JSContext* cx);

  public:
    js::jit::JitRuntime* getJitRuntime(JSContext* cx) {
        return jitRuntime_ ? jitRuntime_.ref() : createJitRuntime(cx);
    }
    js::jit::JitRuntime* jitRuntime() const {
        return jitRuntime_.ref();
    }
    bool hasJitRuntime() const {
        return !!jitRuntime_;
    }

    // These will be removed soon.
  private:
    JSContext* singletonContext;
    js::ZoneGroup* singletonZoneGroup;
  public:

    JSContext* unsafeContextFromAnyThread() const { return singletonContext; }
    JSContext* contextFromMainThread() const {
        MOZ_ASSERT(CurrentThreadCanAccessRuntime(this));
        return singletonContext;
    }

    js::ZoneGroup* zoneGroupFromAnyThread() const { return singletonZoneGroup; }
    js::ZoneGroup* zoneGroupFromMainThread() const {
        MOZ_ASSERT(CurrentThreadCanAccessRuntime(this));
        return singletonZoneGroup;
    }

  private:
    // Used to generate random keys for hash tables.
    mozilla::Maybe<mozilla::non_crypto::XorShift128PlusRNG> randomKeyGenerator_;
    mozilla::non_crypto::XorShift128PlusRNG& randomKeyGenerator();

  public:
    mozilla::HashCodeScrambler randomHashCodeScrambler();
    mozilla::non_crypto::XorShift128PlusRNG forkRandomKeyGenerator();

    //-------------------------------------------------------------------------
    // Self-hosting support
    //-------------------------------------------------------------------------

    bool hasInitializedSelfHosting() const {
        return selfHostingGlobal_;
    }

    bool initSelfHosting(JSContext* cx);
    void finishSelfHosting();
    void traceSelfHostingGlobal(JSTracer* trc);
    bool isSelfHostingGlobal(JSObject* global) {
        return global == selfHostingGlobal_;
    }
    bool isSelfHostingCompartment(JSCompartment* comp) const;
    bool isSelfHostingZone(const JS::Zone* zone) const;
    bool createLazySelfHostedFunctionClone(JSContext* cx, js::HandlePropertyName selfHostedName,
                                           js::HandleAtom name, unsigned nargs,
                                           js::HandleObject proto,
                                           js::NewObjectKind newKind,
                                           js::MutableHandleFunction fun);
    bool cloneSelfHostedFunctionScript(JSContext* cx, js::Handle<js::PropertyName*> name,
                                       js::Handle<JSFunction*> targetFun);
    bool cloneSelfHostedValue(JSContext* cx, js::Handle<js::PropertyName*> name,
                              js::MutableHandleValue vp);
    void assertSelfHostedFunctionHasCanonicalName(JSContext* cx, js::HandlePropertyName name);

    //-------------------------------------------------------------------------
    // Locale information
    //-------------------------------------------------------------------------

    /*
     * Set the default locale for the ECMAScript Internationalization API
     * (Intl.Collator, Intl.NumberFormat, Intl.DateTimeFormat).
     * Note that the Internationalization API encourages clients to
     * specify their own locales.
     * The locale string remains owned by the caller.
     */
    bool setDefaultLocale(const char* locale);

    /* Reset the default locale to OS defaults. */
    void resetDefaultLocale();

    /* Gets current default locale. String remains owned by context. */
    const char* getDefaultLocale();

    JSVersion defaultVersion() const { return defaultVersion_; }
    void setDefaultVersion(JSVersion v) { defaultVersion_ = v; }

    /* Garbage collector state, used by jsgc.c. */
    js::gc::GCRuntime   gc;

    /* Garbage collector state has been successfully initialized. */
    js::WriteOnceData<bool> gcInitialized;

    bool hasZealMode(js::gc::ZealMode mode) { return gc.hasZealMode(mode); }

    void lockGC() {
        gc.lockGC();
    }

    void unlockGC() {
        gc.unlockGC();
    }

    /* Well-known numbers. */
    const js::Value     NaNValue;
    const js::Value     negativeInfinityValue;
    const js::Value     positiveInfinityValue;

    js::WriteOnceData<js::PropertyName*> emptyString;

  private:
    js::WriteOnceData<js::FreeOp*> defaultFreeOp_;

  public:
    js::FreeOp* defaultFreeOp() {
        MOZ_ASSERT(defaultFreeOp_);
        return defaultFreeOp_;
    }

#if !EXPOSE_INTL_API
    /* Number localization, used by jsnum.cpp. */
    js::WriteOnceData<const char*> thousandsSeparator;
    js::WriteOnceData<const char*> decimalSeparator;
    js::WriteOnceData<const char*> numGrouping;
#endif

  private:
    mozilla::Maybe<js::SharedImmutableStringsCache> sharedImmutableStrings_;

  public:
    // If this particular JSRuntime has a SharedImmutableStringsCache, return a
    // pointer to it, otherwise return nullptr.
    js::SharedImmutableStringsCache* maybeThisRuntimeSharedImmutableStrings() {
        return sharedImmutableStrings_.isSome() ? &*sharedImmutableStrings_ : nullptr;
    }

    // Get a reference to this JSRuntime's or its parent's
    // SharedImmutableStringsCache.
    js::SharedImmutableStringsCache& sharedImmutableStrings() {
        MOZ_ASSERT_IF(parentRuntime, !sharedImmutableStrings_);
        MOZ_ASSERT_IF(!parentRuntime, sharedImmutableStrings_);
        return parentRuntime ? parentRuntime->sharedImmutableStrings() : *sharedImmutableStrings_;
    }

  private:
    js::WriteOnceData<bool> beingDestroyed_;
  public:
    bool isBeingDestroyed() const {
        return beingDestroyed_;
    }

  private:
    bool allowContentJS_;
  public:
    bool allowContentJS() const {
        return allowContentJS_;
    }

    friend class js::AutoAssertNoContentJS;

  private:
    // Set of all atoms other than those in permanentAtoms and staticStrings.
    // Reading or writing this set requires the calling thread to use
    // AutoLockForExclusiveAccess.
    js::ExclusiveAccessLockOrGCTaskData<js::AtomSet*> atoms_;

    // Compartment and associated zone containing all atoms in the runtime, as
    // well as runtime wide IonCode stubs. Modifying the contents of this
    // compartment requires the calling thread to use AutoLockForExclusiveAccess.
    js::WriteOnceData<JSCompartment*> atomsCompartment_;

    // Set of all live symbols produced by Symbol.for(). All such symbols are
    // allocated in the atomsCompartment. Reading or writing the symbol
    // registry requires the calling thread to use AutoLockForExclusiveAccess.
    js::ExclusiveAccessLockOrGCTaskData<js::SymbolRegistry> symbolRegistry_;

  public:
    bool initializeAtoms(JSContext* cx);
    void finishAtoms();
    bool atomsAreFinished() const { return !atoms_; }

    void sweepAtoms();

    js::AtomSet& atoms(js::AutoLockForExclusiveAccess& lock) {
        return *atoms_;
    }
    js::AtomSet& unsafeAtoms() {
        return *atoms_;
    }
    JSCompartment* atomsCompartment(js::AutoLockForExclusiveAccess& lock) {
        return atomsCompartment_;
    }
    JSCompartment* unsafeAtomsCompartment() {
        return atomsCompartment_;
    }

    bool isAtomsCompartment(JSCompartment* comp) {
        return comp == atomsCompartment_;
    }

    // The atoms compartment is the only one in its zone.
    bool isAtomsZone(const JS::Zone* zone) const {
        return zone == gc.atomsZone;
    }

    bool activeGCInAtomsZone();

    js::SymbolRegistry& symbolRegistry(js::AutoLockForExclusiveAccess& lock) {
        return symbolRegistry_.ref();
    }
    js::SymbolRegistry& unsafeSymbolRegistry() {
        return symbolRegistry_.ref();
    }

    // Permanent atoms are fixed during initialization of the runtime and are
    // not modified or collected until the runtime is destroyed. These may be
    // shared with another, longer living runtime through |parentRuntime| and
    // can be freely accessed with no locking necessary.

    // Permanent atoms pre-allocated for general use.
    js::WriteOnceData<js::StaticStrings*> staticStrings;

    // Cached pointers to various permanent property names.
    js::WriteOnceData<JSAtomState*> commonNames;

    // All permanent atoms in the runtime, other than those in staticStrings.
    // Unlike |atoms_|, access to this does not require
    // AutoLockForExclusiveAccess because it is frozen and thus read-only.
    js::WriteOnceData<js::FrozenAtomSet*> permanentAtoms;

    bool transformToPermanentAtoms(JSContext* cx);

    // Cached well-known symbols (ES6 rev 24 6.1.5.1). Like permanent atoms,
    // these are shared with the parentRuntime, if any.
    js::WriteOnceData<js::WellKnownSymbols*> wellKnownSymbols;

    /* Shared Intl data for this runtime. */
    js::UnprotectedData<js::SharedIntlData> sharedIntlData;

    void traceSharedIntlData(JSTracer* trc);

    // Table of bytecode and other data that may be shared across scripts
    // within the runtime. This may be modified by threads using
    // AutoLockForExclusiveAccess.
  private:
    js::ExclusiveAccessLockData<js::ScriptDataTable> scriptDataTable_;
  public:
    js::ScriptDataTable& scriptDataTable(js::AutoLockForExclusiveAccess& lock) {
        return scriptDataTable_.ref();
    }

    js::WriteOnceData<bool> jitSupportsFloatingPoint;
    js::WriteOnceData<bool> jitSupportsUnalignedAccesses;
    js::WriteOnceData<bool> jitSupportsSimd;

  private:
    static mozilla::Atomic<size_t> liveRuntimesCount;

  public:
    static bool hasLiveRuntimes() {
        return liveRuntimesCount > 0;
    }

    explicit JSRuntime(JSRuntime* parentRuntime);

    // destroyRuntime is used instead of a destructor, to ensure the downcast
    // to JSContext remains valid. The final GC triggered here depends on this.
    void destroyRuntime();

    bool init(JSContext* cx, uint32_t maxbytes, uint32_t maxNurseryBytes);

    JSRuntime* thisFromCtor() { return this; }

  public:
    /*
     * Call this after allocating memory held by GC things, to update memory
     * pressure counters or report the OOM error if necessary. If oomError and
     * cx is not null the function also reports OOM error.
     *
     * The function must be called outside the GC lock and in case of OOM error
     * the caller must ensure that no deadlock possible during OOM reporting.
     */
    void updateMallocCounter(size_t nbytes);
    void updateMallocCounter(JS::Zone* zone, size_t nbytes);

    void reportAllocationOverflow() { js::ReportAllocationOverflow(nullptr); }

    /*
     * This should be called after system malloc/calloc/realloc returns nullptr
     * to try to recove some memory or to report an error.  For realloc, the
     * original pointer must be passed as reallocPtr.
     *
     * The function must be called outside the GC lock.
     */
    JS_FRIEND_API(void*) onOutOfMemory(js::AllocFunction allocator, size_t nbytes,
                                       void* reallocPtr = nullptr, JSContext* maybecx = nullptr);

    /*  onOutOfMemory but can call the largeAllocationFailureCallback. */
    JS_FRIEND_API(void*) onOutOfMemoryCanGC(js::AllocFunction allocator, size_t nbytes,
                                            void* reallocPtr = nullptr);

    void addSizeOfIncludingThis(mozilla::MallocSizeOf mallocSizeOf, JS::RuntimeSizes* runtime);

  private:
    // Settings for how helper threads can be used.
    mozilla::Atomic<bool> offthreadIonCompilationEnabled_;
    mozilla::Atomic<bool> parallelParsingEnabled_;

    js::UnprotectedData<bool> autoWritableJitCodeActive_;

  public:

    // Note: these values may be toggled dynamically (in response to about:config
    // prefs changing).
    void setOffthreadIonCompilationEnabled(bool value) {
        offthreadIonCompilationEnabled_ = value;
    }
    bool canUseOffthreadIonCompilation() const {
        return offthreadIonCompilationEnabled_;
    }
    void setParallelParsingEnabled(bool value) {
        parallelParsingEnabled_ = value;
    }
    bool canUseParallelParsing() const {
        return parallelParsingEnabled_;
    }

    void toggleAutoWritableJitCodeActive(bool b) {
        MOZ_ASSERT(autoWritableJitCodeActive_ != b, "AutoWritableJitCode should not be nested.");
        autoWritableJitCodeActive_ = b;
    }

    /* See comment for JS::SetLargeAllocationFailureCallback in jsapi.h. */
    js::UnprotectedData<JS::LargeAllocationFailureCallback> largeAllocationFailureCallback;
    js::UnprotectedData<void*> largeAllocationFailureCallbackData;

    /* See comment for JS::SetOutOfMemoryCallback in jsapi.h. */
    js::UnprotectedData<JS::OutOfMemoryCallback> oomCallback;
    js::UnprotectedData<void*> oomCallbackData;

    /*
     * These variations of malloc/calloc/realloc will call the
     * large-allocation-failure callback on OOM and retry the allocation.
     */
    static const unsigned LARGE_ALLOCATION = 25 * 1024 * 1024;

    template <typename T>
    T* pod_callocCanGC(size_t numElems) {
        T* p = pod_calloc<T>(numElems);
        if (MOZ_LIKELY(!!p))
            return p;
        size_t bytes;
        if (MOZ_UNLIKELY(!js::CalculateAllocSize<T>(numElems, &bytes))) {
            reportAllocationOverflow();
            return nullptr;
        }
        return static_cast<T*>(onOutOfMemoryCanGC(js::AllocFunction::Calloc, bytes));
    }

    template <typename T>
    T* pod_reallocCanGC(T* p, size_t oldSize, size_t newSize) {
        T* p2 = pod_realloc<T>(p, oldSize, newSize);
        if (MOZ_LIKELY(!!p2))
            return p2;
        size_t bytes;
        if (MOZ_UNLIKELY(!js::CalculateAllocSize<T>(newSize, &bytes))) {
            reportAllocationOverflow();
            return nullptr;
        }
        return static_cast<T*>(onOutOfMemoryCanGC(js::AllocFunction::Realloc, bytes, p));
    }

    /*
     * Debugger.Memory functions like takeCensus use this embedding-provided
     * function to assess the size of malloc'd blocks of memory.
     */
    js::UnprotectedData<mozilla::MallocSizeOf> debuggerMallocSizeOf;

    /* Last time at which an animation was played for this runtime. */
    mozilla::Atomic<int64_t> lastAnimationTime;

  private:
    js::UnprotectedData<js::PerformanceMonitoring> performanceMonitoring_;
  public:
    js::PerformanceMonitoring& performanceMonitoring() { return performanceMonitoring_.ref(); }

  private:
    /* List of Ion compilation waiting to get linked. */
    typedef mozilla::LinkedList<js::jit::IonBuilder> IonBuilderList;

    js::HelperThreadLockData<IonBuilderList> ionLazyLinkList_;
    js::HelperThreadLockData<size_t> ionLazyLinkListSize_;

  public:
    IonBuilderList& ionLazyLinkList();

    size_t ionLazyLinkListSize() {
        return ionLazyLinkListSize_;
    }

    void ionLazyLinkListRemove(js::jit::IonBuilder* builder);
    void ionLazyLinkListAdd(js::jit::IonBuilder* builder);

  private:
    /* The stack format for the current runtime.  Only valid on non-child
     * runtimes. */
    mozilla::Atomic<js::StackFormat, mozilla::ReleaseAcquire> stackFormat_;

  public:
    js::StackFormat stackFormat() const {
        const JSRuntime* rt = this;
        while (rt->parentRuntime) {
            MOZ_ASSERT(rt->stackFormat_ == js::StackFormat::Default);
            rt = rt->parentRuntime;
        }
        MOZ_ASSERT(rt->stackFormat_ != js::StackFormat::Default);
        return rt->stackFormat_;
    }
    void setStackFormat(js::StackFormat format) {
        MOZ_ASSERT(!parentRuntime);
        MOZ_ASSERT(format != js::StackFormat::Default);
        stackFormat_ = format;
    }

    // For inherited heap state accessors.
    friend class js::gc::AutoTraceSession;
    friend class JS::AutoEnterCycleCollection;
};

namespace js {

/*
 * Flags accompany script version data so that a) dynamically created scripts
 * can inherit their caller's compile-time properties and b) scripts can be
 * appropriately compared in the eval cache across global option changes. An
 * example of the latter is enabling the top-level-anonymous-function-is-error
 * option: subsequent evals of the same, previously-valid script text may have
 * become invalid.
 */
namespace VersionFlags {
static const unsigned MASK      = 0x0FFF; /* see JSVersion in jspubtd.h */
} /* namespace VersionFlags */

static inline JSVersion
VersionNumber(JSVersion version)
{
    return JSVersion(uint32_t(version) & VersionFlags::MASK);
}

static inline JSVersion
VersionExtractFlags(JSVersion version)
{
    return JSVersion(uint32_t(version) & ~VersionFlags::MASK);
}

static inline void
VersionCopyFlags(JSVersion* version, JSVersion from)
{
    *version = JSVersion(VersionNumber(*version) | VersionExtractFlags(from));
}

static inline bool
VersionHasFlags(JSVersion version)
{
    return !!VersionExtractFlags(version);
}

static inline bool
VersionIsKnown(JSVersion version)
{
    return VersionNumber(version) != JSVERSION_UNKNOWN;
}

inline void
FreeOp::free_(void* p)
{
    js_free(p);
}

inline void
FreeOp::freeLater(void* p)
{
    // FreeOps other than the defaultFreeOp() are constructed on the stack,
    // and won't hold onto the pointers to free indefinitely.
    MOZ_ASSERT(!isDefaultFreeOp());

    AutoEnterOOMUnsafeRegion oomUnsafe;
    if (!freeLaterList.append(p))
        oomUnsafe.crash("FreeOp::freeLater");
}

inline bool
FreeOp::appendJitPoisonRange(const jit::JitPoisonRange& range)
{
    // FreeOps other than the defaultFreeOp() are constructed on the stack,
    // and won't hold onto the pointers to free indefinitely.
    MOZ_ASSERT(!isDefaultFreeOp());

    return jitPoisonRanges.append(range);
}

/*
 * RAII class that takes the GC lock while it is live.
 *
 * Note that the lock may be temporarily released by use of AutoUnlockGC when
 * passed a non-const reference to this class.
 */
class MOZ_RAII AutoLockGC
{
  public:
    explicit AutoLockGC(JSRuntime* rt
                        MOZ_GUARD_OBJECT_NOTIFIER_PARAM)
      : runtime_(rt)
    {
        MOZ_GUARD_OBJECT_NOTIFIER_INIT;
        lock();
    }

    ~AutoLockGC() {
        unlock();
    }

    void lock() {
        MOZ_ASSERT(lockGuard_.isNothing());
        lockGuard_.emplace(runtime_->gc.lock);
    }

    void unlock() {
        MOZ_ASSERT(lockGuard_.isSome());
        lockGuard_.reset();
    }

    js::LockGuard<js::Mutex>& guard() {
        return lockGuard_.ref();
    }

  private:
    JSRuntime* runtime_;
    mozilla::Maybe<js::LockGuard<js::Mutex>> lockGuard_;
    MOZ_DECL_USE_GUARD_OBJECT_NOTIFIER

    AutoLockGC(const AutoLockGC&) = delete;
    AutoLockGC& operator=(const AutoLockGC&) = delete;
};

class MOZ_RAII AutoUnlockGC
{
  public:
    explicit AutoUnlockGC(AutoLockGC& lock
                          MOZ_GUARD_OBJECT_NOTIFIER_PARAM)
      : lock(lock)
    {
        MOZ_GUARD_OBJECT_NOTIFIER_INIT;
        lock.unlock();
    }

    ~AutoUnlockGC() {
        lock.lock();
    }

  private:
    AutoLockGC& lock;
    MOZ_DECL_USE_GUARD_OBJECT_NOTIFIER

    AutoUnlockGC(const AutoUnlockGC&) = delete;
    AutoUnlockGC& operator=(const AutoUnlockGC&) = delete;
};

/************************************************************************/

static MOZ_ALWAYS_INLINE void
MakeRangeGCSafe(Value* vec, size_t len)
{
    mozilla::PodZero(vec, len);
}

static MOZ_ALWAYS_INLINE void
MakeRangeGCSafe(Value* beg, Value* end)
{
    mozilla::PodZero(beg, end - beg);
}

static MOZ_ALWAYS_INLINE void
MakeRangeGCSafe(jsid* beg, jsid* end)
{
    for (jsid* id = beg; id != end; ++id)
        *id = INT_TO_JSID(0);
}

static MOZ_ALWAYS_INLINE void
MakeRangeGCSafe(jsid* vec, size_t len)
{
    MakeRangeGCSafe(vec, vec + len);
}

static MOZ_ALWAYS_INLINE void
MakeRangeGCSafe(Shape** beg, Shape** end)
{
    mozilla::PodZero(beg, end - beg);
}

static MOZ_ALWAYS_INLINE void
MakeRangeGCSafe(Shape** vec, size_t len)
{
    mozilla::PodZero(vec, len);
}

static MOZ_ALWAYS_INLINE void
SetValueRangeToUndefined(Value* beg, Value* end)
{
    for (Value* v = beg; v != end; ++v)
        v->setUndefined();
}

static MOZ_ALWAYS_INLINE void
SetValueRangeToUndefined(Value* vec, size_t len)
{
    SetValueRangeToUndefined(vec, vec + len);
}

static MOZ_ALWAYS_INLINE void
SetValueRangeToNull(Value* beg, Value* end)
{
    for (Value* v = beg; v != end; ++v)
        v->setNull();
}

static MOZ_ALWAYS_INLINE void
SetValueRangeToNull(Value* vec, size_t len)
{
    SetValueRangeToNull(vec, vec + len);
}

/*
 * Allocation policy that uses JSRuntime::pod_malloc and friends, so that
 * memory pressure is properly accounted for. This is suitable for
 * long-lived objects owned by the JSRuntime.
 *
 * Since it doesn't hold a JSContext (those may not live long enough), it
 * can't report out-of-memory conditions itself; the caller must check for
 * OOM and take the appropriate action.
 *
 * FIXME bug 647103 - replace these *AllocPolicy names.
 */
class RuntimeAllocPolicy
{
    JSRuntime* const runtime;

  public:
    MOZ_IMPLICIT RuntimeAllocPolicy(JSRuntime* rt) : runtime(rt) {}

    template <typename T>
    T* maybe_pod_malloc(size_t numElems) {
        return runtime->maybe_pod_malloc<T>(numElems);
    }

    template <typename T>
    T* maybe_pod_calloc(size_t numElems) {
        return runtime->maybe_pod_calloc<T>(numElems);
    }

    template <typename T>
    T* maybe_pod_realloc(T* p, size_t oldSize, size_t newSize) {
        return runtime->maybe_pod_realloc<T>(p, oldSize, newSize);
    }

    template <typename T>
    T* pod_malloc(size_t numElems) {
        return runtime->pod_malloc<T>(numElems);
    }

    template <typename T>
    T* pod_calloc(size_t numElems) {
        return runtime->pod_calloc<T>(numElems);
    }

    template <typename T>
    T* pod_realloc(T* p, size_t oldSize, size_t newSize) {
        return runtime->pod_realloc<T>(p, oldSize, newSize);
    }

    void free_(void* p) { js_free(p); }
    void reportAllocOverflow() const {}

    bool checkSimulatedOOM() const {
        return !js::oom::ShouldFailWithOOM();
    }
};

extern const JSSecurityCallbacks NullSecurityCallbacks;

} /* namespace js */

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#endif /* vm_Runtime_h */
