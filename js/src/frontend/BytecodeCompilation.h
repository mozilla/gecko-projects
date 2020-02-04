/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: set ts=8 sts=2 et sw=2 tw=80:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef frontend_BytecodeCompilation_h
#define frontend_BytecodeCompilation_h

#include "mozilla/Assertions.h"  // MOZ_ASSERT
#include "mozilla/Attributes.h"  // MOZ_MUST_USE, MOZ_STACK_CLASS
#include "mozilla/Maybe.h"       // mozilla::Maybe, mozilla::Nothing
#include "mozilla/Utf8.h"        // mozilla::Utf8Unit

#include <stddef.h>  // size_t
#include <stdint.h>  // uint32_t

#include "jstypes.h"                // JS_PUBLIC_API
#include "frontend/ParseContext.h"  // js::frontend::UsedNameTracker
#include "frontend/ParseInfo.h"
#include "frontend/SharedContext.h"  // js::frontend::Directives, js::frontend::{,Eval,Global}SharedContext
#include "js/CompileOptions.h"  // JS::ReadOnlyCompileOptions
#include "js/RootingAPI.h"      // JS::{,Mutable}Handle, JS::Rooted
#include "js/SourceText.h"      // JS::SourceText
#include "vm/JSContext.h"       // js::AutoKeepAtoms
#include "vm/JSScript.h"  // js::{FunctionAsync,Generator}Kind, js::LazyScript, JSScript, js::ScriptSource, js::ScriptSourceObject
#include "vm/Scope.h"     // js::ScopeKind

class JS_PUBLIC_API JSFunction;
class JS_PUBLIC_API JSObject;

namespace js {

namespace frontend {

class EitherParser;

template <typename Unit>
class SourceAwareCompiler;
template <typename Unit>
class ScriptCompiler;
template <typename Unit>
class ModuleCompiler;
template <typename Unit>
class StandaloneFunctionCompiler;

// The BytecodeCompiler class contains resources common to compiling scripts and
// function bodies.
class MOZ_STACK_CLASS BytecodeCompiler {
 protected:
  AutoKeepAtoms keepAtoms;

  JSContext* cx;
  const JS::ReadOnlyCompileOptions& options;
  ParseInfo& parseInfo;

  Directives directives;

  JS::Rooted<JSScript*> script;

  ScriptSource* scriptSource() const {
    return parseInfo.sourceObject->source();
  };

 protected:
  BytecodeCompiler(JSContext* cx, ParseInfo& parseInfo,
                   const JS::ReadOnlyCompileOptions& options);

  template <typename Unit>
  friend class SourceAwareCompiler;
  template <typename Unit>
  friend class ScriptCompiler;
  template <typename Unit>
  friend class ModuleCompiler;
  template <typename Unit>
  friend class StandaloneFunctionCompiler;

 public:
  JSContext* context() const { return cx; }

  ScriptSourceObject* sourceObjectPtr() const { return parseInfo.sourceObject; }

  JS::Handle<JSScript*> getScript() { return script; }

 protected:
  void assertSourceCreated() const {
    MOZ_ASSERT(parseInfo.sourceObject != nullptr);
    MOZ_ASSERT(scriptSource() != nullptr);
  }

  // Create a script for source of the given length, using the explicitly-
  // provided toString offsets as the created script's offsets in the source.
  MOZ_MUST_USE bool internalCreateScript(HandleObject functionOrGlobal,
                                         uint32_t toStringStart,
                                         uint32_t toStringEnd,
                                         uint32_t sourceBufferLength);

  MOZ_MUST_USE bool emplaceEmitter(mozilla::Maybe<BytecodeEmitter>& emitter,
                                   const EitherParser& parser,
                                   SharedContext* sharedContext);

  // This function lives here, not in SourceAwareCompiler, because it mostly
  // uses fields in *this* class.
  template <typename Unit>
  MOZ_MUST_USE bool assignSource(JS::SourceText<Unit>& sourceBuffer) {
    return scriptSource()->assignSource(cx, options, sourceBuffer);
  }

  bool canLazilyParse() const;
};

class MOZ_STACK_CLASS GlobalScriptInfo final : public BytecodeCompiler {
  GlobalSharedContext globalsc_;

 public:
  GlobalScriptInfo(JSContext* cx, ParseInfo& parseInfo,
                   const JS::ReadOnlyCompileOptions& options,
                   ScopeKind scopeKind)
      : BytecodeCompiler(cx, parseInfo, options),
        globalsc_(cx, scopeKind, parseInfo, directives,
                  options.extraWarningsOption) {
    MOZ_ASSERT(scopeKind == ScopeKind::Global ||
               scopeKind == ScopeKind::NonSyntactic);
  }

  GlobalSharedContext* sharedContext() { return &globalsc_; }
};

extern JSScript* CompileGlobalScript(GlobalScriptInfo& info,
                                     JS::SourceText<char16_t>& srcBuf);

extern JSScript* CompileGlobalScript(GlobalScriptInfo& info,
                                     JS::SourceText<mozilla::Utf8Unit>& srcBuf);

class MOZ_STACK_CLASS EvalScriptInfo final : public BytecodeCompiler {
  JS::Handle<JSObject*> environment_;
  EvalSharedContext evalsc_;

 public:
  EvalScriptInfo(JSContext* cx, ParseInfo& parseInfo,
                 const JS::ReadOnlyCompileOptions& options,
                 JS::Handle<JSObject*> environment,
                 JS::Handle<Scope*> enclosingScope)
      : BytecodeCompiler(cx, parseInfo, options),
        environment_(environment),
        evalsc_(cx, environment_, parseInfo, enclosingScope, directives,
                options.extraWarningsOption) {}

  HandleObject environment() { return environment_; }

  EvalSharedContext* sharedContext() { return &evalsc_; }
};

extern JSScript* CompileEvalScript(EvalScriptInfo& info,
                                   JS::SourceText<char16_t>& srcBuf);

class MOZ_STACK_CLASS ModuleInfo final : public BytecodeCompiler {
 public:
  ModuleInfo(JSContext* cx, ParseInfo& parseInfo,
             const JS::ReadOnlyCompileOptions& options)
      : BytecodeCompiler(cx, parseInfo, options) {}
};

class MOZ_STACK_CLASS StandaloneFunctionInfo final : public BytecodeCompiler {
 public:
  StandaloneFunctionInfo(JSContext* cx, ParseInfo& parseInfo,
                         const JS::ReadOnlyCompileOptions& options)
      : BytecodeCompiler(cx, parseInfo, options) {}
};

extern MOZ_MUST_USE bool CompileLazyFunction(JSContext* cx,
                                             JS::Handle<LazyScript*> lazy,
                                             const char16_t* units,
                                             size_t length);

extern MOZ_MUST_USE bool CompileLazyFunction(JSContext* cx,
                                             JS::Handle<LazyScript*> lazy,
                                             const mozilla::Utf8Unit* units,
                                             size_t length);

}  // namespace frontend

}  // namespace js

#endif  // frontend_BytecodeCompilation_h
