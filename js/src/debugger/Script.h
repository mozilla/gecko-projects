/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: set ts=8 sts=2 et sw=2 tw=80:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef debugger_Script_h
#define debugger_Script_h

#include "mozilla/Variant.h"

#include "jsapi.h"

#include "debugger/Debugger.h"
#include "gc/Cell.h"
#include "gc/Rooting.h"
#include "js/CallArgs.h"
#include "js/Class.h"
#include "js/PropertySpec.h"
#include "js/RootingAPI.h"
#include "js/TracingAPI.h"
#include "js/TypeDecls.h"
#include "vm/GlobalObject.h"
#include "vm/JSScript.h"
#include "vm/NativeObject.h"
#include "wasm/WasmJS.h"

namespace js {

class DebuggerScript : public NativeObject {
 public:
  static const Class class_;

  enum {
    OWNER_SLOT,
    RESERVED_SLOTS,
  };

  static NativeObject* initClass(JSContext* cx, Handle<GlobalObject*> global,
                                 HandleObject debugCtor);
  static DebuggerScript* create(JSContext* cx, HandleObject proto,
                                Handle<DebuggerScriptReferent> referent,
                                HandleNativeObject debugger);

  static void trace(JSTracer* trc, JSObject* obj);

  inline gc::Cell* getReferentCell() const;
  inline DebuggerScriptReferent getReferent() const;

  static DebuggerScript* check(JSContext* cx, HandleValue v,
                               const char* fnname);
  static DebuggerScript* checkThis(JSContext* cx, const CallArgs& args,
                                   const char* fnname);

  // JS methods
  static bool getIsGeneratorFunction(JSContext* cx, unsigned argc, Value* vp);
  static bool getIsAsyncFunction(JSContext* cx, unsigned argc, Value* vp);
  static bool getIsModule(JSContext* cx, unsigned argc, Value* vp);
  static bool getDisplayName(JSContext* cx, unsigned argc, Value* vp);
  static bool getUrl(JSContext* cx, unsigned argc, Value* vp);
  static bool getStartLine(JSContext* cx, unsigned argc, Value* vp);
  static bool getLineCount(JSContext* cx, unsigned argc, Value* vp);
  static bool getSource(JSContext* cx, unsigned argc, Value* vp);
  static bool getSourceStart(JSContext* cx, unsigned argc, Value* vp);
  static bool getSourceLength(JSContext* cx, unsigned argc, Value* vp);
  static bool getMainOffset(JSContext* cx, unsigned argc, Value* vp);
  static bool getGlobal(JSContext* cx, unsigned argc, Value* vp);
  static bool getFormat(JSContext* cx, unsigned argc, Value* vp);
  static bool getChildScripts(JSContext* cx, unsigned argc, Value* vp);
  static bool getPossibleBreakpoints(JSContext* cx, unsigned argc, Value* vp);
  static bool getPossibleBreakpointOffsets(JSContext* cx, unsigned argc,
                                           Value* vp);
  static bool getOffsetMetadata(JSContext* cx, unsigned argc, Value* vp);
  static bool getOffsetLocation(JSContext* cx, unsigned argc, Value* vp);
  static bool getSuccessorOffsets(JSContext* cx, unsigned argc, Value* vp);
  static bool getPredecessorOffsets(JSContext* cx, unsigned argc, Value* vp);
  static bool getAllOffsets(JSContext* cx, unsigned argc, Value* vp);
  static bool getAllColumnOffsets(JSContext* cx, unsigned argc, Value* vp);
  static bool getLineOffsets(JSContext* cx, unsigned argc, Value* vp);
  static bool setBreakpoint(JSContext* cx, unsigned argc, Value* vp);
  static bool getBreakpoints(JSContext* cx, unsigned argc, Value* vp);
  static bool clearBreakpoint(JSContext* cx, unsigned argc, Value* vp);
  static bool clearAllBreakpoints(JSContext* cx, unsigned argc, Value* vp);
  static bool isInCatchScope(JSContext* cx, unsigned argc, Value* vp);
  static bool getOffsetsCoverage(JSContext* cx, unsigned argc, Value* vp);
  static bool construct(JSContext* cx, unsigned argc, Value* vp);

  template <typename T>
  static bool getUrlImpl(JSContext* cx, CallArgs& args, Handle<T*> script);

  static bool getSuccessorOrPredecessorOffsets(JSContext* cx, unsigned argc,
                                               Value* vp, const char* name,
                                               bool successor);

 private:
  static const ClassOps classOps_;

  static const JSPropertySpec properties_[];
  static const JSFunctionSpec methods_[];

  class SetPrivateMatcher;
  struct GetStartLineMatcher;
  struct GetLineCountMatcher;
  class GetSourceMatcher;
  class GetFormatMatcher;
  template <bool OnlyOffsets>
  class GetPossibleBreakpointsMatcher;
  class GetOffsetMetadataMatcher;
  class GetOffsetLocationMatcher;
  class GetSuccessorOrPredecessorOffsetsMatcher;
  class GetAllColumnOffsetsMatcher;
  class GetLineOffsetsMatcher;
  struct SetBreakpointMatcher;
  class ClearBreakpointMatcher;
  class IsInCatchScopeMatcher;
};

} /* namespace js */

#endif /* debugger_Script_h */
