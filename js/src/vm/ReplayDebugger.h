/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef vm_ReplayDebugger_h
#define vm_ReplayDebugger_h

#include "js/ReplayHooks.h"
#include "vm/Debugger.h"

// When a replayed process is being debugged, there are two Debuggers at play:
// one in the replayed process itself, and one in the middleman process which
// is debugging it. The two communicate via IPDL messages in the PReplay
// protocol.
//
// The debugger in the replayed process has a tenuous existence. Whenever a
// memory snapshot is taken or a GC is performed it is destroyed, and it is
// reconstructed afterwards according to messages sent from the middleman
// debugger. Note that the middleman may have multiple debuggers, but all their
// messages will be sent to the same replay debugger.
//
// ReplayDebugger manages the relationship between these debuggers.

namespace js {

class ReplayDebugger : public mozilla::LinkedListElement<ReplayDebugger>
{
  public:
    ReplayDebugger(JSContext* cx, Debugger* dbg);
    ~ReplayDebugger();
    bool init();

    static void onNewScript(JSContext* cx, HandleScript script);

    // Debugger methods.
    bool findScripts(JSContext* cx, MutableHandle<GCVector<JSObject*>> scriptObjects);
    void resumeBackward();
    void resumeForward();
    void pause();
    bool content(JSContext* cx, CallArgs& args);

    // Generic methods.
    bool notYetImplemented(JSContext* cx, HandleObject obj, CallArgs& args);
    bool notAllowed(JSContext* cx, HandleObject obj, CallArgs& args);

    // Script methods.
    bool scriptDisplayName(JSContext* cx, HandleObject obj, CallArgs& args);
    bool scriptUrl(JSContext* cx, HandleObject obj, CallArgs& args);
    bool scriptStartLine(JSContext* cx, HandleObject obj, CallArgs& args);
    bool scriptLineCount(JSContext* cx, HandleObject obj, CallArgs& args);
    bool scriptSource(JSContext* cx, HandleObject obj, CallArgs& args);
    bool scriptSourceStart(JSContext* cx, HandleObject obj, CallArgs& args);
    bool scriptSourceLength(JSContext* cx, HandleObject obj, CallArgs& args);
    bool setScriptBreakpoint(JSContext* cx, HandleObject obj, CallArgs& args);
    bool clearScriptBreakpoint(JSContext* cx, HandleObject obj, CallArgs& args);

    static bool scriptUrl(JSContext* cx, HandleObject obj, MutableHandleValue rv);
    static bool scriptStartLine(JSContext* cx, HandleObject obj, MutableHandleValue rv);
    static bool scriptLineCount(JSContext* cx, HandleObject obj, MutableHandleValue rv);
    bool scriptSource(JSContext* cx, HandleObject obj, MutableHandleValue rv);
    bool getScriptStructure(JSContext* cx, HandleObject obj, ScriptStructure* script);

    // ScriptSource methods.
    bool sourceText(JSContext* cx, HandleObject obj, CallArgs& args);
    bool sourceUrl(JSContext* cx, HandleObject obj, CallArgs& args);
    bool sourceDisplayUrl(JSContext* cx, HandleObject obj, CallArgs& args);
    bool sourceElement(JSContext* cx, HandleObject obj, CallArgs& args);
    bool sourceElementProperty(JSContext* cx, HandleObject obj, CallArgs& args);
    bool sourceIntroductionScript(JSContext* cx, HandleObject obj, CallArgs& args);
    bool sourceIntroductionOffset(JSContext* cx, HandleObject obj, CallArgs& args);
    bool sourceIntroductionType(JSContext* cx, HandleObject obj, CallArgs& args);
    bool getSourceMapUrl(JSContext* cx, HandleObject obj, CallArgs& args);
    bool sourceCanonicalId(JSContext* cx, HandleObject obj, CallArgs& args);

    // Frame methods.
    bool getNewestFrame(JSContext* cx, MutableHandleValue rv);
    bool frameType(JSContext* cx, HandleObject obj, CallArgs& args);
    bool frameCallee(JSContext* cx, HandleObject obj, CallArgs& args);
    bool frameGenerator(JSContext* cx, HandleObject obj, CallArgs& args);
    bool frameConstructing(JSContext* cx, HandleObject obj, CallArgs& args);
    bool frameThis(JSContext* cx, HandleObject obj, CallArgs& args);
    bool frameOlder(JSContext* cx, HandleObject obj, CallArgs& args);
    bool frameScript(JSContext* cx, HandleObject obj, CallArgs& args);
    bool frameOffset(JSContext* cx, HandleObject obj, CallArgs& args);
    bool frameEnvironment(JSContext* cx, HandleObject obj, CallArgs& args);
    bool frameEvaluate(JSContext* cx, HandleObject obj, HandleString str,
                       JSTrapStatus* pstatus, MutableHandleValue result);
    static bool frameHasArguments(JSContext* cx, HandleObject obj, bool* rv);
    static bool frameNumActualArgs(JSContext* cx, HandleObject obj, size_t* rv);
    bool frameArgument(JSContext* cx, HandleObject obj, size_t index, MutableHandleValue rv);

    // Handler methods.
    bool setFrameOnStep(JSContext* cx, HandleObject obj, CallArgs& args);
    bool getFrameOnStep(JSContext* cx, HandleObject obj, CallArgs& args);
    bool setFrameOnPop(JSContext* cx, HandleObject obj, CallArgs& args);
    bool getFrameOnPop(JSContext* cx, HandleObject obj, CallArgs& args);
    bool setOnEnterFrame(JSContext* cx, HandleValue handler);
    bool setOnPopFrame(JSContext* cx, HandleValue handler);
    bool getOnPopFrame(JSContext* cx, MutableHandleValue rv);

    // Object methods.
    bool objectProto(JSContext* cx, HandleObject obj, CallArgs& args);
    bool objectClass(JSContext* cx, HandleObject obj, CallArgs& args);
    bool objectCallable(JSContext* cx, HandleObject obj, CallArgs& args);
    bool objectExplicitName(JSContext* cx, HandleObject obj, CallArgs& args);
    bool objectDisplayName(JSContext* cx, HandleObject obj, CallArgs& args);
    bool objectParameterNames(JSContext* cx, HandleObject obj, CallArgs& args);
    bool objectScript(JSContext* cx, HandleObject obj, CallArgs& args);
    bool objectEnvironment(JSContext* cx, HandleObject obj, CallArgs& args);
    bool objectIsArrowFunction(JSContext* cx, HandleObject obj, CallArgs& args);
    bool objectIsBoundFunction(JSContext* cx, HandleObject obj, CallArgs& args);
    bool objectBoundTargetFunction(JSContext* cx, HandleObject obj, CallArgs& args);
    bool objectBoundThis(JSContext* cx, HandleObject obj, CallArgs& args);
    bool objectBoundArguments(JSContext* cx, HandleObject obj, CallArgs& args);
    bool objectGlobal(JSContext* cx, HandleObject obj, CallArgs& args);
    bool objectIsProxy(JSContext* cx, HandleObject obj, CallArgs& args);
    bool objectIsExtensible(JSContext* cx, HandleObject obj, CallArgs& args);
    bool objectIsSealed(JSContext* cx, HandleObject obj, CallArgs& args);
    bool objectIsFrozen(JSContext* cx, HandleObject obj, CallArgs& args);
    bool objectOwnPropertyDescriptor(JSContext* cx, HandleObject obj, CallArgs& args);
    bool objectOwnPropertyNames(JSContext* cx, HandleObject obj, CallArgs& args);
    bool objectOwnPropertySymbols(JSContext* cx, HandleObject obj, CallArgs& args);
    bool objectCall(JSContext* cx, HandleObject obj, HandleValue thisv, Handle<ValueVector> args,
                    JSTrapStatus* pstatus, MutableHandleValue result);
    bool objectUnsafeDereference(JSContext* cx, HandleObject obj, CallArgs& args);
    bool objectUnwrap(JSContext* cx, HandleObject obj, CallArgs& args);

  private:
    bool objectOwnPropertyKeys(JSContext* cx, HandleObject obj, unsigned flags,
                               MutableHandleValue rv);
  public:

    // Env methods.
    bool envType(JSContext* cx, HandleObject obj, CallArgs& args);
    bool envParent(JSContext* cx, HandleObject obj, CallArgs& args);
    bool envObject(JSContext* cx, HandleObject obj, CallArgs& args);
    bool envCallee(JSContext* cx, HandleObject obj, CallArgs& args);
    bool envIsInspectable(JSContext* cx, HandleObject obj, CallArgs& args);
    bool envIsOptimizedOut(JSContext* cx, HandleObject obj, CallArgs& args);
    bool envNames(JSContext* cx, HandleObject obj, CallArgs& args);
    bool envVariable(JSContext* cx, HandleObject obj, CallArgs& args);

    struct Breakpoint;
    static bool hitBreakpointMiddleman(JSContext* cx, size_t id);

    static bool onLeaveFrame(JSContext* cx, AbstractFramePtr frame, jsbytecode* pc, bool ok);

    static void Initialize();

    void trace(JSTracer* trc);

    // For use in the replaying process.
    static void markRoots(JSTracer* trc);

    static void NoteNewGlobalObject(JSContext* cx, GlobalObject* global);

    struct Activity;

    // Identification for a position during JS execution in the replaying process.
    struct ExecutionPosition
    {
        enum Kind {
            Invalid,
            Break,       // No frameIndex
            OnStep,
            OnPop,       // No offset, script/frameIndex is optional
            EnterFrame,  // No offset/script/frameIndex
        } kind;
        size_t script;
        size_t offset;
        size_t frameIndex;

        static const size_t EMPTY_SCRIPT = (size_t) -1;
        static const size_t EMPTY_OFFSET = (size_t) -1;
        static const size_t EMPTY_FRAME_INDEX = (size_t) -1;

        ExecutionPosition()
          : kind(Invalid), script(0), offset(0), frameIndex(0)
        {}

        explicit ExecutionPosition(Kind kind,
                                   size_t script = EMPTY_SCRIPT,
                                   size_t offset = EMPTY_OFFSET,
                                   size_t frameIndex = EMPTY_FRAME_INDEX)
          : kind(kind), script(script), offset(offset), frameIndex(frameIndex)
        {}

        bool isValid() const { return kind != Invalid; }

        inline bool operator ==(const ExecutionPosition& o) const {
            return kind == o.kind
                && script == o.script
                && offset == o.offset
                && frameIndex == o.frameIndex;
        }
    };

  private:
    JSObject* addScript(JSContext* cx, size_t id, HandleObject data);

    HandleObject getScript(Activity& a, size_t id);
    HandleObject getFrame(Activity& a, size_t index);
    HandleObject getObject(Activity& a, size_t id);
    HandleObject getObjectOrNull(Activity& a, size_t id);
    HandleObject getEnv(Activity& a, size_t id);
    HandleObject getEnvOrNull(Activity& a, size_t id);
    HandleValue convertValueFromJSON(Activity& a, HandleObject jsonValue);
    HandleObject convertValueToJSON(Activity& a, HandleValue value);

    bool hitBreakpoint(JSContext* cx, Breakpoint* breakpoint);

    void invalidateAfterUnpause();

    typedef HashMap<size_t, NativeObject*> DebugObjectMap;

    Debugger* debugger;

    DebugObjectMap debugScripts;
    DebugObjectMap debugSources;
    DebugObjectMap debugObjects;
    DebugObjectMap debugEnvs;
    Vector<NativeObject*> debugFrames;

    static mozilla::LinkedList<ReplayDebugger> gReplayDebuggers;

    JSRuntime* runtime;

    static void InitializeContentSet();

    // Internal state/methods for coordinating between breakpoint/navigation
    // logic and the rest of the replaying process debugger.
  public:
    // Runtime which all data considered in the replaying process is associated
    // with. Worker runtimes are ignored entirely.
    static JSRuntime* gMainRuntime;

    // Global in which the debugger is installed in the replaying process.
    static PersistentRootedObject* gHookGlobal;

    // Handle a debugger request from the middleman.
    static void ProcessRequest(const char16_t* requestBuffer, size_t requestLength,
                               Maybe<JS::replay::CharBuffer>* responseBuffer);

    // Attempt to diverge from the recording during a debugger request,
    // returning whether the diverge was allowed.
    static bool MaybeDivergeFromRecording();

    // While paused, mark a change to an installed breakpoint to make before
    // the process resumes (or rewinds) execution.
    static void AddBreakpointOperation(size_t id, const ExecutionPosition& position);

    // While paused after popping a frame, indicate whether the frame threw and
    // the returned/thrown value. Returns false if not at the exit to a frame.
    static bool GetPoppedFrameResult(bool* throwing, MutableHandleValue result);

    // Convert between scripts and script IDs.
    static JSScript* IdScript(size_t id);
    static size_t ScriptId(JSScript* script);

    // Install any necessary breakpoints on a newly created script.
    static void MaybeSetupBreakpointsForScript(size_t id);

    // Clear the mapping from IDs to objects used when paused at a breakpoint.
    static void ClearDebuggerPausedObjects();

    // Return how many frames for scripts considered by the debugger are on the stack.
    static size_t CountScriptFrames(JSContext* cx);
};

} // namespace js

#endif /* vm_ReplayDebugger_h */
