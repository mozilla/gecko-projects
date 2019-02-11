/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: set ts=8 sts=2 et sw=2 tw=80:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Definitions related to javascript type inference. */

#ifndef vm_TypeInference_h
#define vm_TypeInference_h

#include "mozilla/Attributes.h"
#include "mozilla/Maybe.h"
#include "mozilla/MemoryReporting.h"

#include "jsfriendapi.h"
#include "jstypes.h"

#include "ds/LifoAlloc.h"
#include "gc/Barrier.h"
#include "jit/IonTypes.h"
#include "js/AllocPolicy.h"
#include "js/HeapAPI.h"  // js::CurrentThreadCanAccessZone
#include "js/UbiNode.h"
#include "js/Utility.h"
#include "js/Vector.h"
#include "threading/ProtectedData.h"  // js::ZoneData
#include "vm/Shape.h"
#include "vm/TypeSet.h"

namespace js {

class TypeConstraint;
class TypeScript;
class TypeZone;
class CompilerConstraintList;
class HeapTypeSetKey;

namespace jit {

class ICScript;
struct IonScript;
class TempAllocator;

}  // namespace jit

// If there is an OOM while sweeping types, the type information is deoptimized
// so that it stays correct (i.e. overapproximates the possible types in the
// zone), but constraints might not have been triggered on the deoptimization
// or even copied over completely. In this case, destroy all JIT code and new
// script information in the zone, the only things whose correctness depends on
// the type constraints.
class AutoClearTypeInferenceStateOnOOM {
  Zone* zone;

  AutoClearTypeInferenceStateOnOOM(const AutoClearTypeInferenceStateOnOOM&) =
      delete;
  void operator=(const AutoClearTypeInferenceStateOnOOM&) = delete;

 public:
  explicit AutoClearTypeInferenceStateOnOOM(Zone* zone);
  ~AutoClearTypeInferenceStateOnOOM();
};

class MOZ_RAII AutoSweepBase {
  // Make sure we don't GC while this class is live since GC might trigger
  // (incremental) sweeping.
  JS::AutoCheckCannotGC nogc;
};

// Sweep an ObjectGroup. Functions that expect a swept group should take a
// reference to this class.
class MOZ_RAII AutoSweepObjectGroup : public AutoSweepBase {
#ifdef DEBUG
  ObjectGroup* group_;
#endif

 public:
  inline explicit AutoSweepObjectGroup(ObjectGroup* group);
#ifdef DEBUG
  inline ~AutoSweepObjectGroup();

  ObjectGroup* group() const { return group_; }
#endif
};

// Sweep a TypeScript. Functions that expect a swept script should take a
// reference to this class.
class MOZ_RAII AutoSweepTypeScript : public AutoSweepBase {
#ifdef DEBUG
  Zone* zone_;
  TypeScript* typeScript_;
#endif

 public:
  inline explicit AutoSweepTypeScript(JSScript* script);
#ifdef DEBUG
  inline ~AutoSweepTypeScript();

  TypeScript* typeScript() const { return typeScript_; }
  Zone* zone() const { return zone_; }
#endif
};

CompilerConstraintList* NewCompilerConstraintList(jit::TempAllocator& alloc);

bool AddClearDefiniteGetterSetterForPrototypeChain(JSContext* cx,
                                                   ObjectGroup* group,
                                                   HandleId id);

bool AddClearDefiniteFunctionUsesInScript(JSContext* cx, ObjectGroup* group,
                                          JSScript* script,
                                          JSScript* calleeScript);

// For groups where only a small number of objects have been allocated, this
// structure keeps track of all objects in the group. Once COUNT objects have
// been allocated, this structure is cleared and the objects are analyzed, to
// perform the new script properties analyses or determine if an unboxed
// representation can be used.
class PreliminaryObjectArray {
 public:
  static const uint32_t COUNT = 20;

 private:
  // All objects with the type which have been allocated. The pointers in
  // this array are weak.
  JSObject* objects[COUNT] = {};  // zeroes

 public:
  PreliminaryObjectArray() = default;

  void registerNewObject(PlainObject* res);
  void unregisterObject(PlainObject* obj);

  JSObject* get(size_t i) const {
    MOZ_ASSERT(i < COUNT);
    return objects[i];
  }

  bool full() const;
  bool empty() const;
  void sweep();
};

class PreliminaryObjectArrayWithTemplate : public PreliminaryObjectArray {
  HeapPtr<Shape*> shape_;

 public:
  explicit PreliminaryObjectArrayWithTemplate(Shape* shape) : shape_(shape) {}

  void clear() { shape_.init(nullptr); }

  Shape* shape() { return shape_; }

  void maybeAnalyze(JSContext* cx, ObjectGroup* group, bool force = false);

  void trace(JSTracer* trc);

  static void writeBarrierPre(
      PreliminaryObjectArrayWithTemplate* preliminaryObjects);
};

/**
 * A type representing the initializer of a property within a script being
 * 'new'd.
 */
class TypeNewScriptInitializer {
 public:
  enum Kind { SETPROP, SETPROP_FRAME, DONE } kind;
  uint32_t offset;

  TypeNewScriptInitializer(Kind kind, uint32_t offset)
      : kind(kind), offset(offset) {}
};

/* Is this a reasonable PC to be doing inlining on? */
inline bool isInlinableCall(jsbytecode* pc);

bool ClassCanHaveExtraProperties(const Class* clasp);

class RecompileInfo {
  JSScript* script_;
  IonCompilationId id_;

 public:
  RecompileInfo(JSScript* script, IonCompilationId id)
      : script_(script), id_(id) {}

  JSScript* script() const { return script_; }

  inline jit::IonScript* maybeIonScriptToInvalidate(const TypeZone& zone) const;

  inline bool shouldSweep(const TypeZone& zone);

  bool operator==(const RecompileInfo& other) const {
    return script_ == other.script_ && id_ == other.id_;
  }
};

// The RecompileInfoVector has a MinInlineCapacity of one so that invalidating a
// single IonScript doesn't require an allocation.
typedef Vector<RecompileInfo, 1, SystemAllocPolicy> RecompileInfoVector;

/* Persistent type information for a script, retained across GCs. */
class TypeScript {
  friend class ::JSScript;

  // The freeze constraints added to stack type sets will only directly
  // invalidate the script containing those stack type sets. This Vector
  // contains compilations that inlined this script, so we can invalidate
  // them as well.
  RecompileInfoVector inlinedCompilations_;

  // ICScript and TypeScript have the same lifetimes, so we store a pointer to
  // ICScript here to not increase sizeof(JSScript).
  using ICScriptPtr = js::UniquePtr<js::jit::ICScript>;
  ICScriptPtr icScript_;

  // Number of TypeSets in typeArray_.
  uint32_t numTypeSets_;

  // This field is used to avoid binary searches for the sought entry when
  // bytecode map queries are in linear order.
  uint32_t bytecodeTypeMapHint_ = 0;

  struct Flags {
    // Flag set when discarding JIT code to indicate this script is on the stack
    // and type information and JIT code should not be discarded.
    bool active : 1;

    // Generation for type sweeping. If out of sync with the TypeZone's
    // generation, this TypeScript needs to be swept.
    bool typesGeneration : 1;

    // Whether freeze constraints for stack type sets have been generated.
    bool hasFreezeConstraints : 1;
  };
  Flags flags_ = {};  // Zero-initialize flags.

  // Variable-size array. This is followed by the bytecode type map.
  StackTypeSet typeArray_[1];

  StackTypeSet* typeArrayDontCheckGeneration() {
    // Ensure typeArray_ is the last data member of TypeScript.
    static_assert(sizeof(TypeScript) ==
                      sizeof(typeArray_) + offsetof(TypeScript, typeArray_),
                  "typeArray_ must be the last member of TypeScript");
    return const_cast<StackTypeSet*>(typeArray_);
  }

  uint32_t typesGeneration() const { return uint32_t(flags_.typesGeneration); }
  void setTypesGeneration(uint32_t generation) {
    MOZ_ASSERT(generation <= 1);
    flags_.typesGeneration = generation;
  }

 public:
  TypeScript(JSScript* script, ICScriptPtr&& icScript, uint32_t numTypeSets);

  bool hasFreezeConstraints(const js::AutoSweepTypeScript& sweep) const {
    MOZ_ASSERT(sweep.typeScript() == this);
    return flags_.hasFreezeConstraints;
  }
  void setHasFreezeConstraints(const js::AutoSweepTypeScript& sweep) {
    MOZ_ASSERT(sweep.typeScript() == this);
    flags_.hasFreezeConstraints = true;
  }

  inline bool typesNeedsSweep(Zone* zone) const;
  void sweepTypes(const js::AutoSweepTypeScript& sweep, Zone* zone);

  RecompileInfoVector& inlinedCompilations(
      const js::AutoSweepTypeScript& sweep) {
    MOZ_ASSERT(sweep.typeScript() == this);
    return inlinedCompilations_;
  }
  MOZ_MUST_USE bool addInlinedCompilation(const js::AutoSweepTypeScript& sweep,
                                          RecompileInfo info) {
    MOZ_ASSERT(sweep.typeScript() == this);
    if (!inlinedCompilations_.empty() && inlinedCompilations_.back() == info) {
      return true;
    }
    return inlinedCompilations_.append(info);
  }

  uint32_t numTypeSets() const { return numTypeSets_; }

  uint32_t* bytecodeTypeMapHint() { return &bytecodeTypeMapHint_; }

  bool active() const { return flags_.active; }
  void setActive() { flags_.active = true; }
  void resetActive() { flags_.active = false; }

  jit::ICScript* icScript() const {
    MOZ_ASSERT(icScript_);
    return icScript_.get();
  }

  /* Array of type sets for variables and JOF_TYPESET ops. */
  StackTypeSet* typeArray(const js::AutoSweepTypeScript& sweep) {
    MOZ_ASSERT(sweep.typeScript() == this);
    return typeArrayDontCheckGeneration();
  }

  uint32_t* bytecodeTypeMap() {
    MOZ_ASSERT(numTypeSets_ > 0);
    return reinterpret_cast<uint32_t*>(typeArray_ + numTypeSets_);
  }

  static inline StackTypeSet* ThisTypes(JSScript* script);
  static inline StackTypeSet* ArgTypes(JSScript* script, unsigned i);

  /* Get the type set for values observed at an opcode. */
  static inline StackTypeSet* BytecodeTypes(JSScript* script, jsbytecode* pc);

  template <typename TYPESET>
  static inline TYPESET* BytecodeTypes(JSScript* script, jsbytecode* pc,
                                       uint32_t* bytecodeMap, uint32_t* hint,
                                       TYPESET* typeArray);

  /*
   * Monitor a bytecode pushing any value. This must be called for any opcode
   * which is JOF_TYPESET, and where either the script has not been analyzed
   * by type inference or where the pc has type barriers. For simplicity, we
   * always monitor JOF_TYPESET opcodes in the interpreter and stub calls,
   * and only look at barriers when generating JIT code for the script.
   */
  static inline void Monitor(JSContext* cx, JSScript* script, jsbytecode* pc,
                             const js::Value& val);
  static inline void Monitor(JSContext* cx, JSScript* script, jsbytecode* pc,
                             TypeSet::Type type);
  static inline void Monitor(JSContext* cx, const js::Value& rval);

  static inline void Monitor(JSContext* cx, JSScript* script, jsbytecode* pc,
                             StackTypeSet* types, const js::Value& val);

  /* Monitor an assignment at a SETELEM on a non-integer identifier. */
  static inline void MonitorAssign(JSContext* cx, HandleObject obj, jsid id);

  /* Add a type for a variable in a script. */
  static inline void SetThis(JSContext* cx, JSScript* script,
                             TypeSet::Type type);
  static inline void SetThis(JSContext* cx, JSScript* script,
                             const js::Value& value);
  static inline void SetArgument(JSContext* cx, JSScript* script, unsigned arg,
                                 TypeSet::Type type);
  static inline void SetArgument(JSContext* cx, JSScript* script, unsigned arg,
                                 const js::Value& value);

  /*
   * Freeze all the stack type sets in a script, for a compilation. Returns
   * copies of the type sets which will be checked against the actual ones
   * under FinishCompilation, to detect any type changes.
   */
  static bool FreezeTypeSets(CompilerConstraintList* constraints,
                             JSScript* script, TemporaryTypeSet** pThisTypes,
                             TemporaryTypeSet** pArgTypes,
                             TemporaryTypeSet** pBytecodeTypes);

  static void Purge(JSContext* cx, HandleScript script);

  void destroy(Zone* zone);

  size_t sizeOfIncludingThis(mozilla::MallocSizeOf mallocSizeOf) const {
    // Note: icScript_ size is reported in jit::AddSizeOfBaselineData.
    return mallocSizeOf(this);
  }

  static constexpr size_t offsetOfICScript() {
    // Note: icScript_ is a UniquePtr that stores the raw pointer. If that ever
    // changes and this assertion fails, we should stop using UniquePtr.
    static_assert(sizeof(icScript_) == sizeof(uintptr_t),
                  "JIT code assumes icScript_ is pointer-sized");
    return offsetof(TypeScript, icScript_);
  }

#ifdef DEBUG
  void printTypes(JSContext* cx, HandleScript script);
#endif
};

// Ensures no TypeScripts are purged in the current zone.
class MOZ_RAII AutoKeepTypeScripts {
  TypeZone& zone_;
  bool prev_;

  AutoKeepTypeScripts(const AutoKeepTypeScripts&) = delete;
  void operator=(const AutoKeepTypeScripts&) = delete;

 public:
  explicit inline AutoKeepTypeScripts(JSContext* cx);
  inline ~AutoKeepTypeScripts();
};

class RecompileInfo;

// Generate the type constraints for the compilation. Sets |isValidOut| based on
// whether the type constraints still hold.
bool FinishCompilation(JSContext* cx, HandleScript script,
                       CompilerConstraintList* constraints,
                       IonCompilationId compilationId, bool* isValidOut);

// Update the actual types in any scripts queried by constraints with any
// speculative types added during the definite properties analysis.
void FinishDefinitePropertiesAnalysis(JSContext* cx,
                                      CompilerConstraintList* constraints);

struct AutoEnterAnalysis;

class TypeZone {
  JS::Zone* const zone_;

  /* Pool for type information in this zone. */
  static const size_t TYPE_LIFO_ALLOC_PRIMARY_CHUNK_SIZE = 8 * 1024;
  ZoneData<LifoAlloc> typeLifoAlloc_;

  // Under CodeGenerator::link, the id of the current compilation.
  ZoneData<mozilla::Maybe<IonCompilationId>> currentCompilationId_;

  TypeZone(const TypeZone&) = delete;
  void operator=(const TypeZone&) = delete;

 public:
  // Current generation for sweeping.
  ZoneOrGCTaskOrIonCompileData<uint32_t> generation;

  // During incremental sweeping, allocator holding the old type information
  // for the zone.
  ZoneData<LifoAlloc> sweepTypeLifoAlloc;

  ZoneData<bool> sweepingTypes;
  ZoneData<bool> oomSweepingTypes;

  ZoneData<bool> keepTypeScripts;

  // The topmost AutoEnterAnalysis on the stack, if there is one.
  ZoneData<AutoEnterAnalysis*> activeAnalysis;

  explicit TypeZone(JS::Zone* zone);
  ~TypeZone();

  JS::Zone* zone() const { return zone_; }

  LifoAlloc& typeLifoAlloc() {
#ifdef JS_CRASH_DIAGNOSTICS
    MOZ_RELEASE_ASSERT(CurrentThreadCanAccessZone(zone_));
#endif
    return typeLifoAlloc_.ref();
  }

  void beginSweep();
  void endSweep(JSRuntime* rt);
  void clearAllNewScriptsOnOOM();

  /* Mark a script as needing recompilation once inference has finished. */
  void addPendingRecompile(JSContext* cx, const RecompileInfo& info);
  void addPendingRecompile(JSContext* cx, JSScript* script);

  void processPendingRecompiles(FreeOp* fop, RecompileInfoVector& recompiles);

  bool isSweepingTypes() const { return sweepingTypes; }
  void setSweepingTypes(bool sweeping) {
    MOZ_RELEASE_ASSERT(sweepingTypes != sweeping);
    MOZ_ASSERT_IF(sweeping, !oomSweepingTypes);
    sweepingTypes = sweeping;
    oomSweepingTypes = false;
  }
  void setOOMSweepingTypes() {
    MOZ_ASSERT(sweepingTypes);
    oomSweepingTypes = true;
  }
  bool hadOOMSweepingTypes() {
    MOZ_ASSERT(sweepingTypes);
    return oomSweepingTypes;
  }

  mozilla::Maybe<IonCompilationId> currentCompilationId() const {
    return currentCompilationId_.ref();
  }
  mozilla::Maybe<IonCompilationId>& currentCompilationIdRef() {
    return currentCompilationId_.ref();
  }
};

enum TypeSpewChannel {
  ISpewOps,    /* ops: New constraints and types. */
  ISpewResult, /* result: Final type sets. */
  SPEW_COUNT
};

#ifdef DEBUG

bool InferSpewActive(TypeSpewChannel channel);
const char* InferSpewColorReset();
const char* InferSpewColor(TypeConstraint* constraint);
const char* InferSpewColor(TypeSet* types);

#  define InferSpew(channel, ...)   \
    if (InferSpewActive(channel)) { \
      InferSpewImpl(__VA_ARGS__);   \
    } else {                        \
    }
void InferSpewImpl(const char* fmt, ...) MOZ_FORMAT_PRINTF(1, 2);

/* Check that the type property for id in group contains value. */
bool ObjectGroupHasProperty(JSContext* cx, ObjectGroup* group, jsid id,
                            const Value& value);

#else

inline const char* InferSpewColorReset() { return nullptr; }
inline const char* InferSpewColor(TypeConstraint* constraint) {
  return nullptr;
}
inline const char* InferSpewColor(TypeSet* types) { return nullptr; }

#  define InferSpew(channel, ...) \
    do {                          \
    } while (0)

#endif

// Prints type information for a context if spew is enabled or force is set.
void PrintTypes(JSContext* cx, JS::Compartment* comp, bool force);

} /* namespace js */

// JS::ubi::Nodes can point to object groups; they're js::gc::Cell instances
// with no associated compartment.
namespace JS {
namespace ubi {

template <>
class Concrete<js::ObjectGroup> : TracerConcrete<js::ObjectGroup> {
 protected:
  explicit Concrete(js::ObjectGroup* ptr)
      : TracerConcrete<js::ObjectGroup>(ptr) {}

 public:
  static void construct(void* storage, js::ObjectGroup* ptr) {
    new (storage) Concrete(ptr);
  }

  Size size(mozilla::MallocSizeOf mallocSizeOf) const override;

  const char16_t* typeName() const override { return concreteTypeName; }
  static const char16_t concreteTypeName[];
};

}  // namespace ubi
}  // namespace JS

#endif /* vm_TypeInference_h */
