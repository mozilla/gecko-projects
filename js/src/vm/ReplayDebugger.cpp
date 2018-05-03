/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ReplayDebugger.h"

#include "frontend/BytecodeCompiler.h"
#include "js/ReplayHooks.h"

#include "vm/Debugger-inl.h"

using namespace js;
using mozilla::Maybe;

#define TRY(op) do { if (!(op)) MOZ_CRASH(#op); } while (false)

extern JS_FRIEND_API(void) JS_StackDump();

// Memory management overview.
//
// The ReplayDebugger lives in the middleman process, while the queries it
// performs execute in the replaying process, via IPC calls and hooks.
//
// Scripts and script source objects are identified by an index into global
// vectors in the replaying process. The replaying process prevents scripts and
// script source objects from ever being collected, so these indexes are stable
// across time. If the process is rewound to a point where a script/sso does
// not exist, the index will simply be out of bounds in the vector.
//
// Other things --- objects, envs, and frames --- which the replay debugger
// tracks can only be manipulated while the replaying process is paused at some
// point of execution. The ids for these things are indexes into vectors which
// are reset after unpausing. After the replaying process either resumes
// execution or is rewound the ReplayDebugger disallows further access on the
// debug object wrappers which represent the things.

JS::replay::Hooks JS::replay::hooks;

/* static */ mozilla::LinkedList<ReplayDebugger> ReplayDebugger::gReplayDebuggers;

ReplayDebugger::ReplayDebugger(JSContext* cx, Debugger* dbg)
  : debugger(dbg),
    debugScripts(cx),
    debugSources(cx),
    debugObjects(cx),
    debugEnvs(cx),
    debugFrames(cx),
    runtime(cx->runtime())
{
    for (ReplayDebugger* dbg : gReplayDebuggers)
        MOZ_RELEASE_ASSERT(dbg->runtime == cx->runtime());
    gReplayDebuggers.insertFront(this);
}

ReplayDebugger::~ReplayDebugger()
{
    // Accesses on gReplayDebuggers will race if ReplayDebugger is destroyed off thread.
    MOZ_RELEASE_ASSERT(runtime == TlsContext.get()->runtime());
}

bool
ReplayDebugger::init()
{
    return debugScripts.init() && debugSources.init() && debugObjects.init() && debugEnvs.init();
}

void
ReplayDebugger::trace(JSTracer* trc)
{
    for (DebugObjectMap::Enum e(debugScripts); !e.empty(); e.popFront())
        TraceManuallyBarrieredEdge(trc, &e.front().value(), "ReplayDebugger::debugScripts");

    for (DebugObjectMap::Enum e(debugSources); !e.empty(); e.popFront())
        TraceManuallyBarrieredEdge(trc, &e.front().value(), "ReplayDebugger::debugScriptSources");

    for (DebugObjectMap::Enum e(debugObjects); !e.empty(); e.popFront())
        TraceManuallyBarrieredEdge(trc, &e.front().value(), "ReplayDebugger::debugObjects");

    for (DebugObjectMap::Enum e(debugEnvs); !e.empty(); e.popFront())
        TraceManuallyBarrieredEdge(trc, &e.front().value(), "ReplayDebugger::debugEnvs");

    for (size_t i = 0; i < debugFrames.length(); i++) {
        if (debugFrames[i])
            TraceManuallyBarrieredEdge(trc, &debugFrames[i], "ReplayDebugger::debugFrames");
    }
}

void
ReplayDebugger::resumeBackward()
{
    for (ReplayDebugger* dbg : gReplayDebuggers)
        dbg->invalidateAfterUnpause();
    JS::replay::hooks.resumeMiddleman(/* forward = */ false);
}

void
ReplayDebugger::resumeForward()
{
    for (ReplayDebugger* dbg : gReplayDebuggers)
        dbg->invalidateAfterUnpause();
    JS::replay::hooks.resumeMiddleman(/* forward = */ true);
}

void
ReplayDebugger::pause()
{
    JS::replay::hooks.pauseMiddleman();
}

bool
ReplayDebugger::notYetImplemented(JSContext* cx, HandleObject obj, CallArgs& args)
{
    JS_ReportErrorASCII(cx, "Operation on replay debugger is not yet implemented");
    return false;
}

bool
ReplayDebugger::notAllowed(JSContext* cx, HandleObject obj, CallArgs& args)
{
    JS_ReportErrorASCII(cx, "Operation on replay debugger is not allowed");
    return false;
}

///////////////////////////////////////////////////////////////////////////////
// Activity structure
///////////////////////////////////////////////////////////////////////////////

static void
DataHolderFinalize(FreeOp* fop, JSObject* obj)
{
    js_free(obj->as<NativeObject>().getPrivate());
}

static const ClassOps gDataHolderClassOps = {
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    DataHolderFinalize
};

static const Class gDataHolderClass = {
    "DataHolder",
    JSCLASS_HAS_PRIVATE | JSCLASS_HAS_RESERVED_SLOTS(1) | JSCLASS_BACKGROUND_FINALIZE,
    &gDataHolderClassOps
};

// Get an object which holds a non-moveable data buffer alive.
static JSObject*
NewDataHolder(JSContext* cx, const uint8_t* pdata, size_t len)
{
    JSObject* obj = NewObjectWithGivenProto(cx, &gDataHolderClass, nullptr);
    if (!obj)
        return nullptr;

    uint8_t* newData = (uint8_t*) js_malloc(len);
    if (!newData)
        return nullptr;
    memcpy(newData, pdata, len);
    obj->as<NativeObject>().setPrivate(newData);
    obj->as<NativeObject>().setReservedSlot(0, Int32Value(len));
    return obj;
}

static void
GetDataHolderData(JSObject* obj, uint8_t** pdata, size_t* plen)
{
    MOZ_RELEASE_ASSERT(obj->getClass() == &gDataHolderClass);
    *pdata = (uint8_t*) obj->as<NativeObject>().getPrivate();
    *plen = obj->as<NativeObject>().getReservedSlot(0).toInt32();
}

// Class for generating an arbitrary number of handles that remain stable
// throughout the lifetime of the class.
template <typename T>
struct HandleFactory : public JS::CustomAutoRooter
{
    static const size_t ChunkCapacity = 8;
    explicit HandleFactory(JSContext* cx)
      : CustomAutoRooter(cx), cx(cx), count(0), chunks(cx)
    {}

    virtual void trace(JSTracer* trc) override {
        for (size_t i = 0; i < count; i++) {
            T* v = rawPointer(i);
            TraceRoot(trc, v, "HandleFactory");
        }
    }

    Handle<T> newHandle(const T& v) {
        if (count == ChunkCapacity * (chunks.length() + 1)) {
            T* buf = (T*) js_malloc(ChunkCapacity * sizeof(T));
            if (!buf || !chunks.append(buf)) {
                ReportOutOfMemory(cx);
                return Handle<T>::fromMarkedLocation(&base[0]);
            }
        }
        T* ptr = rawPointer(count++);
        *ptr = v;
        return Handle<T>::fromMarkedLocation(ptr);
    }

  private:
    T* rawPointer(size_t i) {
        T* chunk = (i < ChunkCapacity) ? base : chunks[(i / ChunkCapacity) - 1];
        return &chunk[i % ChunkCapacity];
    }

    JSContext* cx;
    size_t count;
    T base[ChunkCapacity];
    Vector<T*> chunks;
};

struct ReplayDebugger::Activity
{
    JSContext* cx;

    explicit Activity(JSContext* cx)
      : cx(cx), valueHandles(cx), objectHandles(cx), stringHandles(cx)
    {
        MOZ_RELEASE_ASSERT(!cx->isExceptionPending());
    }

    bool success() { return !cx->isExceptionPending(); }

#define MAKE_ACCESSORS(Name, Type)                                      \
    Type get ##Name## Property(HandleObject obj, const char* property) { \
        if (obj && success()) {                                         \
            RootedValue rv(cx);                                         \
            if (JS_GetProperty(cx, obj, property, &rv))                 \
                return valueTo ##Name (rv);                             \
        }                                                               \
        fail();                                                         \
        return valueTo ##Name (UndefinedValue());                       \
    }                                                                   \
    Type get ##Name## Element(HandleObject obj, size_t index) {         \
        if (obj && success()) {                                         \
            RootedValue rv(cx);                                         \
            if (JS_GetElement(cx, obj, index, &rv))                     \
                return valueTo ##Name (rv);                             \
        }                                                               \
        fail();                                                         \
        return valueTo ##Name (UndefinedValue());                       \
    }                                                                   \
    void defineProperty(HandleObject obj, const char* property, Type v) { \
        RootedValue nv(cx, valueFrom(v));                               \
        if (obj && success()) {                                         \
            if (nv.isString()) {                                        \
                RootedString newString(cx, nv.toString());              \
                if (!cx->compartment()->wrap(cx, &newString))           \
                    return;                                             \
                nv.setString(newString);                                \
            }                                                           \
            if (JS_DefineProperty(cx, obj, property, nv, JSPROP_ENUMERATE)) \
                return;                                                 \
        }                                                               \
        fail();                                                         \
    }

    MAKE_ACCESSORS(Value, HandleValue)
    MAKE_ACCESSORS(Object, HandleObject)
    MAKE_ACCESSORS(String, HandleString)
    MAKE_ACCESSORS(Scalar, size_t)

#undef MAKE_ACCESSORS

    HandleObject newObject() {
        return handlify(JS_NewObject(cx, nullptr));
    }

    HandleObject newRequestObject(const char* kind) {
        HandleObject obj = newObject();
        defineProperty(obj, "kind", kind);
        return obj;
    }

    HandleObject newArray() {
        return handlify(NewDenseEmptyArray(cx));
    }

    void pushArray(HandleObject array, HandleValue value) {
        if (array && success()) {
            if (NewbornArrayPush(cx, array, value))
                return;
        }
        fail();
    }

    void pushArray(HandleObject array, HandleObject value) {
        pushArray(array, handlify(ObjectOrNullValue(value)));
    }

    bool hasProperty(HandleObject obj, const char* property) {
        if (obj && success()) {
            bool found;
            if (JS_HasProperty(cx, obj, property, &found))
                return found;
        }
        fail();
        return false;
    }

    bool getBooleanProperty(HandleObject obj, const char* property) {
        return getScalarProperty(obj, property);
    }

    HandleString getNonNullStringProperty(HandleObject obj, const char* property) {
        HandleString rv = getStringProperty(obj, property);
        if (rv)
            return rv;
        fail();
        return HandlePropertyName(cx->names().empty);
    }

    HandleValue getStringOrUndefinedProperty(HandleObject obj, const char* property) {
        HandleString str = getStringProperty(obj, property);
        return handlify(str ? StringValue(str) : UndefinedValue());
    }

    HandleValue getStringOrNullProperty(HandleObject obj, const char* property) {
        HandleString str = getStringProperty(obj, property);
        return handlify(str ? StringValue(str) : NullValue());
    }

    size_t getMaybeScalarProperty(HandleObject obj, const char* property) {
        HandleValue v = getValueProperty(obj, property);
        if (v.isUndefined())
            return 0;
        return valueToScalar(v);
    }

    void defineProperty(HandleObject obj, const char* property, const char* v) {
        defineProperty(obj, property, handlify(v));
    }

    void defineProperty(HandleObject obj, const char* property, const char16_t* v) {
        defineProperty(obj, property, handlify(v));
    }

    HandleObject sendRequest(HandleObject request);

    bool stringEquals(HandleString str, const char* ascii) {
        bool match;
        return JS_StringEqualsAscii(cx, str, ascii, &match) && match;
    }

    void getBinaryProperty(HandleObject obj, const char* property, uint8_t** pdata, size_t* plen) {
        // Use a DataHolder object to make sure the data pointer cannot move
        // around even if the underlying GC things are moved.
        RootedValue value(cx, getValueProperty(obj, property));
        if (value.isUndefined()) {
            *pdata = nullptr;
            *plen = 0;
            return;
        }
        if (value.isString()) {
            if (!success())
                return;
            JSString* str = value.toString();
            AutoStableStringChars sc(cx);
            if (!sc.init(cx, str))
                return;
            if (!sc.isLatin1()) {
                JS_ReportErrorASCII(cx, "Expected latin1 chars");
                return;
            }
            mozilla::Range<const Latin1Char> chars = sc.latin1Range();
            JSObject* holder = NewDataHolder(cx, chars.begin().get(), chars.length());
            if (!holder)
                return;
            defineProperty(obj, property, handlify(holder));
            value.setObject(*holder);
        }
        if (!value.isObject() || value.toObject().getClass() != &gDataHolderClass) {
            fail();
            return;
        }
        GetDataHolderData(&value.toObject(), pdata, plen);
    }

    void defineBinaryProperty(HandleObject obj, const char* property, uint8_t* data, size_t len) {
        RootedString str(cx, JS_NewStringCopyN(cx, (char*) data, len));
        if (str)
            defineProperty(obj, property, str);
    }

    HandleObject getObjectData(HandleObject obj) {
        if (!Debugger::isReplayingChildJSObject(obj))
            return nullptr;
        return handlify(static_cast<JSObject*>(obj->as<NativeObject>().getPrivate()));
    }

    HandleValue handlify(const Value& v) { return valueHandles.newHandle(v); }
    HandleObject handlify(JSObject* v) { return objectHandles.newHandle(v); }
    HandleString handlify(JSString* v) { return stringHandles.newHandle(v); }

    HandleString handlify(const char* v) {
        RootedString str(cx, JS_AtomizeString(cx, v));
        return handlify(str ? str : cx->names().empty);
    }

    HandleString handlify(const char16_t* v) {
        RootedString str(cx, AtomizeChars(cx, v, js_strlen(v)));
        return handlify(str ? str : cx->names().empty);
    }

    HandleValue valueify(const char* v) { return handlify(StringValue(handlify(v))); }

  private:
    void fail(const char* text = nullptr) {
        if (!cx->isExceptionPending())
            JS_ReportErrorASCII(cx, "%s", text ? text : "Conversion error");
    }

    HandleValue valueToValue(const Value& v) { return handlify(v); }
    HandleObject valueToObject(const Value& v) {
        if (v.isObject())
            return handlify(&v.toObject());
        if (!v.isUndefined() && !v.isNull())
            fail();
        return nullptr;
    }
    HandleString valueToString(const Value& v) {
        if (v.isString())
            return handlify(v.toString());
        if (!v.isUndefined() && !v.isNull())
            fail();
        return nullptr;
    }
    size_t valueToScalar(const Value& v) {
        if (v.isNumber())
            return (size_t) v.toNumber();
        fail();
        return 0;
    }

    Value valueFrom(HandleValue v) { return v; }
    Value valueFrom(HandleObject v) { return ObjectOrNullValue(v); }
    Value valueFrom(HandleString v) {
        if (v)
            return StringValue(v);
        fail();
        return UndefinedValue();
    }
    Value valueFrom(size_t v) {
        // Scalar values can be any uint32, -1 or any GC cell pointer. Since
        // the latter can be stored in the mantissa of a double, we should be
        // able to convert in and then out of a double without losing
        // information.
        if (v == (size_t) -1)
            return Int32Value(-1);
        MOZ_RELEASE_ASSERT(v >= 0);
        MOZ_RELEASE_ASSERT(size_t(double(v)) == v);
        return NumberValue((double) v);
    }

    HandleFactory<Value> valueHandles;
    HandleFactory<JSObject*> objectHandles;
    HandleFactory<JSString*> stringHandles;
};

static bool
FillCharBufferCallback(const char16_t* buf, uint32_t len, void* data)
{
    JS::replay::CharBuffer* buffer = (JS::replay::CharBuffer*) data;
    MOZ_RELEASE_ASSERT(buffer->length() == 0);
    return buffer->append(buf, len);
}

HandleObject
ReplayDebugger::Activity::sendRequest(HandleObject request)
{
    if (!success())
        return nullptr;

    JS::replay::CharBuffer requestBuffer;
    if (!ToJSONMaybeSafely(cx, request, FillCharBufferCallback, &requestBuffer))
        return nullptr;

    JS::replay::CharBuffer responseBuffer;
    JS::replay::hooks.debugRequestMiddleman(requestBuffer, &responseBuffer);

    RootedValue responseValue(cx);
    if (!JS_ParseJSON(cx, responseBuffer.begin(), responseBuffer.length(), &responseValue))
        return nullptr;

    if (!responseValue.isObject()) {
        JS_ReportErrorASCII(cx, "Expected object from ParseJSON");
        return nullptr;
    }
    HandleObject response = handlify(&responseValue.toObject());
    if (HandleString exception = getStringProperty(response, "exception")) {
        char* str = JS_EncodeString(cx, exception);
        JS_ReportErrorASCII(cx, "Exception thrown in replaying process: %s", str);
        js_free(str);
        return nullptr;
    }
    return response;
}

///////////////////////////////////////////////////////////////////////////////
// Global methods
///////////////////////////////////////////////////////////////////////////////

bool
ReplayDebugger::content(JSContext* cx, CallArgs& args)
{
    if (!args.requireAtLeast(cx, "Debugger.replayingContent", 1))
        return false;

    if (!args[0].isString()) {
        JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr, JSMSG_NOT_EXPECTED_TYPE,
                                  "Debugger.replayingContent", "string",
                                  InformalValueTypeName(args[0]));
    }

    Activity a(cx);
    HandleObject request = a.newRequestObject("getContent");
    a.defineProperty(request, "url", args[0]);
    HandleObject res = a.sendRequest(request);

    if (a.success())
        args.rval().setObject(*res);
    return a.success();
}

///////////////////////////////////////////////////////////////////////////////
// Script management
///////////////////////////////////////////////////////////////////////////////

JSObject*
ReplayDebugger::addScript(JSContext* cx, size_t id, HandleObject data)
{
    DebugObjectMap::AddPtr p = debugScripts.lookupForAdd(id);
    if (!p) {
        RootedObject proto(cx, &debugger->toJSObject()->getReservedSlot(Debugger::JSSLOT_DEBUG_SCRIPT_PROTO).toObject());
        NativeObject* obj = debugger->createChildObject(cx, &DebuggerScript_class, proto, true);
        if (!obj || !debugScripts.add(p, id, obj))
            return nullptr;
        MOZ_RELEASE_ASSERT(!IsInsideNursery(obj)); // No barriers in DebugObjectMap.
        obj->setPrivateGCThing(data);
    }
    return p->value();
}

HandleObject
ReplayDebugger::getScript(Activity& a, size_t id)
{
    DebugObjectMap::Ptr p = debugScripts.lookup(id);
    if (!p)
        return nullptr;
    return a.handlify(p->value());
}

bool
ReplayDebugger::getScriptStructure(JSContext* cx, HandleObject obj, ScriptStructure* script)
{
    Activity a(cx);
    HandleObject data = a.getObjectData(obj);
    RootedObject structure(cx, a.getObjectProperty(data, "structure"));

    if (!structure) {
        size_t id = a.getScalarProperty(data, "id");
        HandleObject request = a.newRequestObject("getStructure");
        a.defineProperty(request, "id", id);
        structure = a.sendRequest(request);
        a.defineProperty(data, "structure", structure);
    }

    a.getBinaryProperty(structure, "code", &script->code, &script->totalLength);
    script->codeLength = a.getScalarProperty(structure, "codeLength");
    a.getBinaryProperty(structure, "trynotes", &script->trynotes, &script->trynotesLength);
    script->lineno = a.getScalarProperty(structure, "lineno");
    script->mainOffset = a.getScalarProperty(structure, "mainOffset");
    return a.success();
}

///////////////////////////////////////////////////////////////////////////////
// Script functions
///////////////////////////////////////////////////////////////////////////////

bool
ReplayDebugger::findScripts(JSContext* cx, MutableHandle<GCVector<JSObject*>> scriptObjects)
{
    Activity a(cx);
    HandleObject request = a.newRequestObject("findScripts");
    HandleObject array = a.sendRequest(request);

    size_t length = a.getScalarProperty(array, "length");
    for (size_t i = 0; i < length; i++) {
        HandleObject script = a.getObjectElement(array, i);
        size_t id = a.getScalarProperty(script, "id");
        if (!a.success())
            return false;

        JSObject* obj = addScript(cx, id, script);
        if (!obj || !scriptObjects.append(obj))
            return false;
    }
    return a.success();
}

bool
ReplayDebugger::scriptDisplayName(JSContext* cx, HandleObject obj, CallArgs& args)
{
    Activity a(cx);
    HandleObject data = a.getObjectData(obj);
    args.rval().set(a.getStringOrUndefinedProperty(data, "displayName"));
    return a.success();
}

/* static */ bool
ReplayDebugger::scriptUrl(JSContext* cx, HandleObject obj, MutableHandleValue rv)
{
    Activity a(cx);
    HandleObject data = a.getObjectData(obj);
    rv.set(a.getStringOrNullProperty(data, "url"));
    return a.success();
}

bool
ReplayDebugger::scriptUrl(JSContext* cx, HandleObject obj, CallArgs& args)
{
    return scriptUrl(cx, obj, args.rval());
}

/* static */ bool
ReplayDebugger::scriptStartLine(JSContext* cx, HandleObject obj, MutableHandleValue rv)
{
    Activity a(cx);
    HandleObject data = a.getObjectData(obj);
    rv.setInt32(a.getScalarProperty(data, "startLine"));
    return a.success();
}

bool
ReplayDebugger::scriptStartLine(JSContext* cx, HandleObject obj, CallArgs& args)
{
    return scriptStartLine(cx, obj, args.rval());
}

/* static */ bool
ReplayDebugger::scriptLineCount(JSContext* cx, HandleObject obj, MutableHandleValue rv)
{
    Activity a(cx);
    HandleObject data = a.getObjectData(obj);
    rv.setInt32(a.getScalarProperty(data, "lineCount"));
    return a.success();
}

bool
ReplayDebugger::scriptLineCount(JSContext* cx, HandleObject obj, CallArgs& args)
{
    return scriptLineCount(cx, obj, args.rval());
}

bool
ReplayDebugger::scriptSource(JSContext* cx, HandleObject obj, MutableHandleValue rv)
{
    Activity a(cx);
    HandleObject data = a.getObjectData(obj);
    size_t id = a.getScalarProperty(data, "sourceId");

    DebugObjectMap::AddPtr p = debugSources.lookupForAdd(id);
    if (!p) {
        HandleObject request = a.newRequestObject("getSource");
        a.defineProperty(request, "id", id);
        HandleObject data = a.sendRequest(request);
        if (!a.success())
            return false;

        RootedObject proto(cx, &debugger->toJSObject()->getReservedSlot(Debugger::JSSLOT_DEBUG_SOURCE_PROTO).toObject());
        NativeObject* obj = debugger->createChildObject(cx, &DebuggerSource_class, proto, true);
        if (!obj || !debugSources.add(p, id, obj))
            return false;
        MOZ_RELEASE_ASSERT(!IsInsideNursery(obj)); // No barriers in DebugObjectMap.
        obj->setPrivateGCThing(data);
    }

    rv.setObject(*p->value());
    return a.success();
}

bool
ReplayDebugger::scriptSource(JSContext* cx, HandleObject obj, CallArgs& args)
{
    return scriptSource(cx, obj, args.rval());
}

bool
ReplayDebugger::scriptSourceStart(JSContext* cx, HandleObject obj, CallArgs& args)
{
    Activity a(cx);
    HandleObject data = a.getObjectData(obj);
    args.rval().setInt32(a.getScalarProperty(data, "sourceStart"));
    return a.success();
}

bool
ReplayDebugger::scriptSourceLength(JSContext* cx, HandleObject obj, CallArgs& args)
{
    Activity a(cx);
    HandleObject data = a.getObjectData(obj);
    args.rval().setInt32(a.getScalarProperty(data, "sourceLength"));
    return a.success();
}

///////////////////////////////////////////////////////////////////////////////
// Script Source functions
///////////////////////////////////////////////////////////////////////////////

bool
ReplayDebugger::sourceText(JSContext* cx, HandleObject obj, CallArgs& args)
{
    Activity a(cx);
    HandleObject data = a.getObjectData(obj);
    if (HandleString text = a.getStringProperty(data, "text")) {
        args.rval().setString(text);
    } else {
        JSString* str = NewStringCopyZ<CanGC>(cx, "[no source]");
        if (!str)
            return false;
        args.rval().setString(str);
    }
    return a.success();
}

bool
ReplayDebugger::sourceUrl(JSContext* cx, HandleObject obj, CallArgs& args)
{
    Activity a(cx);
    HandleObject data = a.getObjectData(obj);
    args.rval().set(a.getStringOrNullProperty(data, "url"));
    return a.success();
}

bool
ReplayDebugger::sourceDisplayUrl(JSContext* cx, HandleObject obj, CallArgs& args)
{
    Activity a(cx);
    HandleObject data = a.getObjectData(obj);
    args.rval().set(a.getStringOrNullProperty(data, "displayUrl"));
    return a.success();
}

bool
ReplayDebugger::sourceElement(JSContext* cx, HandleObject obj, CallArgs& args)
{
    // Source elements are not yet available while replaying.
    args.rval().setNull();
    return true;
}

bool
ReplayDebugger::sourceElementProperty(JSContext* cx, HandleObject obj, CallArgs& args)
{
    Activity a(cx);
    HandleObject data = a.getObjectData(obj);
    args.rval().set(a.getStringOrUndefinedProperty(data, "elementProperty"));
    return a.success();
}

bool
ReplayDebugger::sourceIntroductionScript(JSContext* cx, HandleObject obj, CallArgs& args)
{
    Activity a(cx);
    HandleObject data = a.getObjectData(obj);
    size_t id = a.getMaybeScalarProperty(data, "introductionScript");
    if (id)
        args.rval().setObjectOrNull(getScript(a, id));
    else
        args.rval().setUndefined();
    return a.success();
}

bool
ReplayDebugger::sourceIntroductionOffset(JSContext* cx, HandleObject obj, CallArgs& args)
{
    Activity a(cx);
    HandleObject data = a.getObjectData(obj);
    if (a.getMaybeScalarProperty(data, "introductionScript")) {
        size_t offset = a.getMaybeScalarProperty(data, "introductionOffset");
        args.rval().setInt32(offset);
    } else {
        args.rval().setUndefined();
    }
    return a.success();
}

bool
ReplayDebugger::sourceIntroductionType(JSContext* cx, HandleObject obj, CallArgs& args)
{
    Activity a(cx);
    HandleObject data = a.getObjectData(obj);
    args.rval().set(a.getStringOrUndefinedProperty(data, "introductionType"));
    return a.success();
}

bool
ReplayDebugger::getSourceMapUrl(JSContext* cx, HandleObject obj, CallArgs& args)
{
    Activity a(cx);
    HandleObject data = a.getObjectData(obj);
    args.rval().set(a.getStringOrUndefinedProperty(data, "sourceMapUrl"));
    return a.success();
}

bool
ReplayDebugger::sourceCanonicalId(JSContext* cx, HandleObject obj, CallArgs& args)
{
    Activity a(cx);
    HandleObject data = a.getObjectData(obj);
    args.rval().setInt32(a.getScalarProperty(data, "id"));
    return a.success();
}

///////////////////////////////////////////////////////////////////////////////
// Frame management
///////////////////////////////////////////////////////////////////////////////

// Frame index used to indicate the newest frame on the stack.
static const size_t NEWEST_FRAME_INDEX = (size_t) -1;

HandleObject
ReplayDebugger::getFrame(Activity& a, size_t index)
{
    if (index == NEWEST_FRAME_INDEX) {
        if (debugFrames.length())
            return a.handlify(debugFrames.back());
    } else {
        MOZ_RELEASE_ASSERT(index < debugFrames.length());
        if (debugFrames[index])
            return a.handlify(debugFrames[index]);
    }

    HandleObject request = a.newRequestObject("getFrame");
    a.defineProperty(request, "index", index);
    HandleObject data = a.sendRequest(request);
    if (!a.success())
        return nullptr;

    if (index == NEWEST_FRAME_INDEX) {
        index = a.getMaybeScalarProperty(data, "index");

        // Fill in debugFrames for older frames.
        while (index >= debugFrames.length()) {
            if (!debugFrames.append(nullptr))
                return nullptr;
        }
    }

    RootedObject proto(a.cx, &debugger->toJSObject()->getReservedSlot(Debugger::JSSLOT_DEBUG_FRAME_PROTO).toObject());
    NativeObject* frameObj = debugger->createChildObject(a.cx, &DebuggerFrame::class_, proto, true);
    if (!frameObj)
        return nullptr;
    frameObj->setPrivateGCThing(data);

    debugFrames[index] = frameObj;
    return a.handlify(frameObj);
}

///////////////////////////////////////////////////////////////////////////////
// Frame functions
///////////////////////////////////////////////////////////////////////////////

bool
ReplayDebugger::getNewestFrame(JSContext* cx, MutableHandleValue rv)
{
    Activity a(cx);
    HandleObject obj = getFrame(a, (size_t) -1);
    rv.setNull();
    if (obj) {
        // If there is no frame then the object's data will have no type.
        HandleObject data = a.getObjectData(obj);
        if (a.getStringProperty(data, "type"))
            rv.setObject(*obj);
    } else {
        rv.setNull();
    }
    return a.success();
}

bool
ReplayDebugger::frameType(JSContext* cx, HandleObject obj, CallArgs& args)
{
    Activity a(cx);
    HandleObject data = a.getObjectData(obj);
    args.rval().setString(a.getNonNullStringProperty(data, "type"));
    return a.success();
}

bool
ReplayDebugger::frameCallee(JSContext* cx, HandleObject obj, CallArgs& args)
{
    Activity a(cx);
    HandleObject data = a.getObjectData(obj);
    size_t callee = a.getScalarProperty(data, "callee");
    args.rval().setObjectOrNull(getObject(a, callee));
    return a.success();
}

bool
ReplayDebugger::frameGenerator(JSContext* cx, HandleObject obj, CallArgs& args)
{
    Activity a(cx);
    HandleObject data = a.getObjectData(obj);
    args.rval().setBoolean(a.getBooleanProperty(data, "generator"));
    return a.success();
}

bool
ReplayDebugger::frameConstructing(JSContext* cx, HandleObject obj, CallArgs& args)
{
    Activity a(cx);
    HandleObject data = a.getObjectData(obj);
    args.rval().setBoolean(a.getBooleanProperty(data, "constructing"));
    return a.success();
}

bool
ReplayDebugger::frameThis(JSContext* cx, HandleObject obj, CallArgs& args)
{
    Activity a(cx);
    HandleObject data = a.getObjectData(obj);
    args.rval().set(convertValueFromJSON(a, a.getObjectProperty(data, "thisv")));
    return a.success();
}

bool
ReplayDebugger::frameOlder(JSContext* cx, HandleObject obj, CallArgs& args)
{
    Activity a(cx);
    HandleObject data = a.getObjectData(obj);
    size_t index = a.getScalarProperty(data, "index");
    if (index == 0) {
        // This is the oldest frame.
        args.rval().setNull();
    } else {
        args.rval().setObjectOrNull(getFrame(a, index - 1));
    }
    return a.success();
}

bool
ReplayDebugger::frameScript(JSContext* cx, HandleObject obj, CallArgs& args)
{
    Activity a(cx);
    HandleObject data = a.getObjectData(obj);
    size_t id = a.getScalarProperty(data, "script");
    args.rval().setObjectOrNull(getScript(a, id));
    return a.success();
}

bool
ReplayDebugger::frameOffset(JSContext* cx, HandleObject obj, CallArgs& args)
{
    Activity a(cx);
    HandleObject data = a.getObjectData(obj);
    args.rval().setInt32(a.getScalarProperty(data, "offset"));
    return a.success();
}

bool
ReplayDebugger::frameEnvironment(JSContext* cx, HandleObject obj, CallArgs& args)
{
    Activity a(cx);
    HandleObject data = a.getObjectData(obj);
    args.rval().setObjectOrNull(getEnv(a, a.getScalarProperty(data, "environment")));
    return a.success();
}

bool
ReplayDebugger::frameEvaluate(JSContext* cx, HandleObject obj, HandleString str,
                              ResumeMode* presumeMode, MutableHandleValue result)
{
    Activity a(cx);

    // If no frame was specified then evaluate in the topmost stack frame.
    size_t frameIndex = (size_t) -1;
    if (obj) {
        HandleObject data = a.getObjectData(obj);
        frameIndex = a.getScalarProperty(data, "index");
    }

    HandleObject request = a.newRequestObject("frameEvaluate");
    a.defineProperty(request, "frameIndex", frameIndex);
    a.defineProperty(request, "text", str);

    HandleObject response = a.sendRequest(request);

    *presumeMode = a.getBooleanProperty(response, "throwing") ? ResumeMode::Throw : ResumeMode::Return;
    result.set(convertValueFromJSON(a, a.getObjectProperty(response, "result")));
    return a.success();
}

/* static */ bool
ReplayDebugger::frameHasArguments(JSContext* cx, HandleObject obj, bool* rv)
{
    Activity a(cx);
    HandleObject data = a.getObjectData(obj);
    *rv = a.getBooleanProperty(data, "hasArguments");
    return a.success();
}

/* static */ bool
ReplayDebugger::frameNumActualArgs(JSContext* cx, HandleObject obj, size_t* rv)
{
    Activity a(cx);
    HandleObject data = a.getObjectData(obj);
    HandleObject actuals = a.getObjectProperty(data, "actuals");
    *rv = actuals ? a.getScalarProperty(actuals, "length") : 0;
    return a.success();
}

bool
ReplayDebugger::frameArgument(JSContext* cx, HandleObject obj, size_t index, MutableHandleValue rv)
{
    Activity a(cx);
    HandleObject data = a.getObjectData(obj);
    HandleObject actuals = a.getObjectProperty(data, "actuals");
    if (actuals && index < (size_t) a.getScalarProperty(actuals, "length"))
        rv.set(convertValueFromJSON(a, a.getObjectElement(actuals, index)));
    else
        rv.setUndefined();
    return a.success();
}

///////////////////////////////////////////////////////////////////////////////
// Object management
///////////////////////////////////////////////////////////////////////////////

HandleObject
ReplayDebugger::getObjectOrNull(Activity& a, size_t id)
{
    if (!id)
        return nullptr;
    DebugObjectMap::AddPtr p = debugObjects.lookupForAdd(id);
    if (!p) {
        HandleObject request = a.newRequestObject("getObject");
        a.defineProperty(request, "id", id);
        HandleObject data = a.sendRequest(request);
        if (!a.success())
            return nullptr;

        RootedObject proto(a.cx, &debugger->toJSObject()->getReservedSlot(Debugger::JSSLOT_DEBUG_OBJECT_PROTO).toObject());
        NativeObject* obj = debugger->createChildObject(a.cx, &DebuggerObject::class_, proto, true);
        if (!obj || !debugObjects.add(p, id, obj))
            return nullptr;
        MOZ_RELEASE_ASSERT(!IsInsideNursery(obj)); // No barriers in DebugObjectMap.

        obj->setPrivateGCThing(data);
    }
    return a.handlify(p->value());
}

HandleObject
ReplayDebugger::getObject(Activity& a, size_t id)
{
    if (!id) {
        JS_ReportErrorASCII(a.cx, "Null object");
        return nullptr;
    }
    return getObjectOrNull(a, id);
}

///////////////////////////////////////////////////////////////////////////////
// Object functions
///////////////////////////////////////////////////////////////////////////////

bool
ReplayDebugger::objectProto(JSContext* cx, HandleObject obj, CallArgs& args)
{
    Activity a(cx);
    HandleObject data = a.getObjectData(obj);
    size_t proto = a.getScalarProperty(data, "proto");
    args.rval().setObjectOrNull(getObjectOrNull(a, proto));
    return a.success();
}

bool
ReplayDebugger::objectClass(JSContext* cx, HandleObject obj, CallArgs& args)
{
    Activity a(cx);
    HandleObject data = a.getObjectData(obj);
    args.rval().setString(a.getNonNullStringProperty(data, "className"));
    return a.success();
}

bool
ReplayDebugger::objectCallable(JSContext* cx, HandleObject obj, CallArgs& args)
{
    Activity a(cx);
    HandleObject data = a.getObjectData(obj);
    args.rval().setBoolean(a.getBooleanProperty(data, "callable"));
    return a.success();
}

bool
ReplayDebugger::objectExplicitName(JSContext* cx, HandleObject obj, CallArgs& args)
{
    Activity a(cx);
    HandleObject data = a.getObjectData(obj);
    args.rval().set(a.getStringOrUndefinedProperty(data, "explicitName"));
    return a.success();
}

bool
ReplayDebugger::objectDisplayName(JSContext* cx, HandleObject obj, CallArgs& args)
{
    Activity a(cx);
    HandleObject data = a.getObjectData(obj);
    args.rval().set(a.getStringOrUndefinedProperty(data, "displayName"));
    return a.success();
}

static HandleObject
NewArrayWithPropertyDescriptorNames(ReplayDebugger::Activity& a, HandleObject jsonProperties)
{
    HandleObject res = a.newArray();
    size_t length = a.getScalarProperty(jsonProperties, "length");
    for (size_t i = 0; i < length; i++) {
        HandleObject desc = a.getObjectElement(jsonProperties, i);
        a.pushArray(res, a.getStringOrUndefinedProperty(desc, "name"));
    }
    return res;
}

bool
ReplayDebugger::objectParameterNames(JSContext* cx, HandleObject obj, CallArgs& args)
{
    Activity a(cx);
    HandleObject data = a.getObjectData(obj);

    // Don't fetch parameterNames from the replaying process if we know the
    // object is not a function.
    HandleString className = a.getNonNullStringProperty(data, "className");
    if (!a.stringEquals(className, "Function")) {
        args.rval().setUndefined();
        return a.success();
    }

    RootedObject parameterNames(cx, a.getObjectProperty(data, "parameterNames"));
    if (!parameterNames) {
        HandleObject request = a.newRequestObject("getObjectParameterNames");
        a.defineProperty(request, "id", a.getScalarProperty(data, "id"));
        parameterNames = a.sendRequest(request);
        a.defineProperty(data, "parameterNames", parameterNames);
    }

    args.rval().setObjectOrNull(NewArrayWithPropertyDescriptorNames(a, parameterNames));
    return a.success();
}

bool
ReplayDebugger::objectScript(JSContext* cx, HandleObject obj, CallArgs& args)
{
    Activity a(cx);
    HandleObject data = a.getObjectData(obj);
    size_t id = a.getScalarProperty(data, "script");
    if (id) {
        args.rval().setObjectOrNull(getScript(a, id));
    } else {
        // Note: some devtools scripts (DevToolsUtils.hasSafeGetter) check for
        // undefined explicitly. DebuggerObject_getScript sometimes returns
        // undefined on a miss, sometimes null. Is this discrepancy by design?
        args.rval().setUndefined();
    }
    return a.success();
}

bool
ReplayDebugger::objectEnvironment(JSContext* cx, HandleObject obj, CallArgs& args)
{
    Activity a(cx);
    HandleObject data = a.getObjectData(obj);
    size_t id = a.getScalarProperty(data, "environment");
    args.rval().setObjectOrNull(getEnvOrNull(a, id));
    return a.success();
}

bool
ReplayDebugger::objectIsArrowFunction(JSContext* cx, HandleObject obj, CallArgs& args)
{
    Activity a(cx);
    HandleObject data = a.getObjectData(obj);
    args.rval().setBoolean(a.getBooleanProperty(data, "isArrowFunction"));
    return a.success();
}

bool
ReplayDebugger::objectIsBoundFunction(JSContext* cx, HandleObject obj, CallArgs& args)
{
    Activity a(cx);
    HandleObject data = a.getObjectData(obj);
    args.rval().setBoolean(a.getBooleanProperty(data, "isBoundFunction"));
    return a.success();
}

bool
ReplayDebugger::objectBoundTargetFunction(JSContext* cx, HandleObject obj, CallArgs& args)
{
    Activity a(cx);
    HandleObject data = a.getObjectData(obj);
    if (!a.getBooleanProperty(data, "isBoundFunction")) {
        args.rval().setUndefined();
        return a.success();
    }
    JS_ReportErrorASCII(cx, "boundTargetFunction NYI on replay objects");
    return false;
}

bool
ReplayDebugger::objectBoundThis(JSContext* cx, HandleObject obj, CallArgs& args)
{
    Activity a(cx);
    HandleObject data = a.getObjectData(obj);
    if (!a.getBooleanProperty(data, "isBoundFunction")) {
        args.rval().setUndefined();
        return a.success();
    }
    JS_ReportErrorASCII(cx, "boundThis NYI on replay objects");
    return false;
}

bool
ReplayDebugger::objectBoundArguments(JSContext* cx, HandleObject obj, CallArgs& args)
{
    Activity a(cx);
    HandleObject data = a.getObjectData(obj);
    if (!a.getBooleanProperty(data, "isBoundFunction")) {
        args.rval().setUndefined();
        return a.success();
    }
    JS_ReportErrorASCII(cx, "boundArguments NYI on replay objects");
    return false;
}

bool
ReplayDebugger::objectGlobal(JSContext* cx, HandleObject obj, CallArgs& args)
{
    Activity a(cx);
    HandleObject data = a.getObjectData(obj);
    size_t global = a.getScalarProperty(data, "global");
    args.rval().setObjectOrNull(getObject(a, global));
    return a.success();
}

bool
ReplayDebugger::objectIsProxy(JSContext* cx, HandleObject obj, CallArgs& args)
{
    Activity a(cx);
    HandleObject data = a.getObjectData(obj);
    args.rval().setBoolean(a.getBooleanProperty(data, "isScriptedProxy"));
    return a.success();
}

bool
ReplayDebugger::objectIsExtensible(JSContext* cx, HandleObject obj, CallArgs& args)
{
    Activity a(cx);
    HandleObject data = a.getObjectData(obj);
    args.rval().setBoolean(a.getBooleanProperty(data, "isExtensible"));
    return a.success();
}

bool
ReplayDebugger::objectIsSealed(JSContext* cx, HandleObject obj, CallArgs& args)
{
    Activity a(cx);
    HandleObject data = a.getObjectData(obj);
    args.rval().setBoolean(a.getBooleanProperty(data, "isSealed"));
    return a.success();
}

bool
ReplayDebugger::objectIsFrozen(JSContext* cx, HandleObject obj, CallArgs& args)
{
    Activity a(cx);
    HandleObject data = a.getObjectData(obj);
    args.rval().setBoolean(a.getBooleanProperty(data, "isFrozen"));
    return a.success();
}

static HandleObject
GetObjectProperties(ReplayDebugger::Activity& a, HandleObject data)
{
    HandleObject existing = a.getObjectProperty(data, "properties");
    if (existing)
        return existing;
    HandleObject request = a.newRequestObject("getObjectProperties");
    a.defineProperty(request, "id", a.getScalarProperty(data, "id"));
    HandleObject properties = a.sendRequest(request);
    a.defineProperty(data, "properties", properties);
    return properties;
}

static bool
JSONDescriptorMatches(ReplayDebugger::Activity& a, HandleObject desc, HandleId id)
{
    HandleString name = a.getNonNullStringProperty(desc, "name");
    RootedId descId(a.cx);
    if (!JS_StringToId(a.cx, name, &descId))
        return false;
    return id == descId;
}

bool
ReplayDebugger::objectOwnPropertyDescriptor(JSContext* cx, HandleObject obj, CallArgs& args)
{
    RootedId id(cx);
    if (!ValueToId<CanGC>(cx, args.get(0), &id))
        return false;

    Activity a(cx);
    HandleObject data = a.getObjectData(obj);
    HandleObject properties = GetObjectProperties(a, data);

    size_t length = a.getScalarProperty(properties, "length");
    for (size_t i = 0; i < length; i++) {
        HandleObject desc = a.getObjectElement(properties, i);
        if (JSONDescriptorMatches(a, desc, id)) {
            Rooted<PropertyDescriptor> ndesc(cx);
            ndesc.object().set(obj);
            ndesc.attributesRef() = a.getScalarProperty(desc, "attrs");
            if (size_t getter = a.getMaybeScalarProperty(desc, "getterObject"))
                ndesc.setGetterObject(getObject(a, getter));
            if (size_t setter = a.getMaybeScalarProperty(desc, "setterObject"))
                ndesc.setSetterObject(getObject(a, setter));
            ndesc.value().set(convertValueFromJSON(a, a.getObjectProperty(desc, "value")));
            return a.success() && FromPropertyDescriptor(cx, ndesc, args.rval());
        }
    }

    args.rval().setUndefined();
    return a.success();
}

bool
ReplayDebugger::objectOwnPropertyNames(JSContext* cx, HandleObject obj, CallArgs& args)
{
    return objectOwnPropertyKeys(cx, obj, JSITER_OWNONLY | JSITER_HIDDEN, args.rval());
}

bool
ReplayDebugger::objectOwnPropertySymbols(JSContext* cx, HandleObject obj, CallArgs& args)
{
    unsigned flags = JSITER_OWNONLY | JSITER_HIDDEN | JSITER_SYMBOLS | JSITER_SYMBOLSONLY;
    return objectOwnPropertyKeys(cx, obj, flags, args.rval());
}

bool
ReplayDebugger::objectOwnPropertyKeys(JSContext* cx, HandleObject obj, unsigned flags,
                                      MutableHandleValue rv)
{
    Activity a(cx);
    HandleObject data = a.getObjectData(obj);
    HandleObject properties = GetObjectProperties(a, data);

    rv.setObjectOrNull(NewArrayWithPropertyDescriptorNames(a, properties));
    return a.success();
}

bool
ReplayDebugger::objectCall(JSContext* cx, HandleObject obj, HandleValue thisv,
                           Handle<ValueVector> args,
                           ResumeMode* presumeMode, MutableHandleValue result)
{
    Activity a(cx);
    HandleObject data = a.getObjectData(obj);

    HandleObject request = a.newRequestObject("objectCall");
    a.defineProperty(request, "functionId", a.getScalarProperty(data, "id"));
    a.defineProperty(request, "thisv", convertValueToJSON(a, thisv));
    if (args.length()) {
        HandleObject array = a.newArray();
        a.defineProperty(request, "arguments", array);
        for (size_t i = 0; i < args.length(); i++)
            a.pushArray(array, convertValueToJSON(a, args[i]));
    }

    HandleObject response = a.sendRequest(request);
    *presumeMode = a.getBooleanProperty(response, "throwing") ? ResumeMode::Throw : ResumeMode::Return;
    result.set(convertValueFromJSON(a, a.getObjectProperty(response, "result")));
    return a.success();
}

bool
ReplayDebugger::objectUnsafeDereference(JSContext* cx, HandleObject obj, CallArgs& args)
{
    // Direct access to the referent of a Debugger.Object is not currently available.
    args.rval().setNull();
    return true;
}

bool
ReplayDebugger::objectUnwrap(JSContext* cx, HandleObject obj, CallArgs& args)
{
    Activity a(cx);
    HandleObject data = a.getObjectData(obj);

    if (!a.getBooleanProperty(data, "isProxy")) {
        args.rval().setObject(*obj);
        return a.success();
    }

    JS_ReportErrorASCII(cx, "unwrap NYI on replay object proxies");
    return false;
}

///////////////////////////////////////////////////////////////////////////////
// Env management
///////////////////////////////////////////////////////////////////////////////

HandleObject
ReplayDebugger::getEnvOrNull(Activity& a, size_t id)
{
    if (!id)
        return nullptr;
    DebugObjectMap::AddPtr p = debugEnvs.lookupForAdd(id);
    if (!p) {
        HandleObject request = a.newRequestObject("getEnvironment");
        a.defineProperty(request, "id", id);
        HandleObject data = a.sendRequest(request);
        if (!a.success())
            return nullptr;

        RootedObject proto(a.cx, &debugger->toJSObject()->getReservedSlot(Debugger::JSSLOT_DEBUG_ENV_PROTO).toObject());
        NativeObject* obj = debugger->createChildObject(a.cx, &DebuggerEnvironment::class_, proto, true);
        if (!obj || !debugEnvs.add(p, id, obj))
            return nullptr;
        MOZ_RELEASE_ASSERT(!IsInsideNursery(obj)); // No barriers in DebugObjectMap.
        obj->setPrivateGCThing(data);
    }
    return a.handlify(p->value());
}

HandleObject
ReplayDebugger::getEnv(Activity& a, size_t id)
{
    if (!id) {
        JS_ReportErrorASCII(a.cx, "Null environment");
        return nullptr;
    }
    return getEnvOrNull(a, id);
}

///////////////////////////////////////////////////////////////////////////////
// Env functions
///////////////////////////////////////////////////////////////////////////////

bool
ReplayDebugger::envType(JSContext* cx, HandleObject obj, CallArgs& args)
{
    Activity a(cx);
    HandleObject data = a.getObjectData(obj);
    args.rval().setString(a.getNonNullStringProperty(data, "type"));
    return a.success();
}

bool
ReplayDebugger::envParent(JSContext* cx, HandleObject obj, CallArgs& args)
{
    Activity a(cx);
    HandleObject data = a.getObjectData(obj);
    args.rval().setObjectOrNull(getEnvOrNull(a, a.getScalarProperty(data, "parent")));
    return a.success();
}

bool
ReplayDebugger::envObject(JSContext* cx, HandleObject obj, CallArgs& args)
{
    Activity a(cx);
    HandleObject data = a.getObjectData(obj);
    args.rval().setObjectOrNull(getObject(a, a.getMaybeScalarProperty(data, "object")));
    return a.success();
}

bool
ReplayDebugger::envCallee(JSContext* cx, HandleObject obj, CallArgs& args)
{
    Activity a(cx);
    HandleObject data = a.getObjectData(obj);
    args.rval().setObjectOrNull(getObjectOrNull(a, a.getScalarProperty(data, "callee")));
    return a.success();
}

bool
ReplayDebugger::envIsInspectable(JSContext* cx, HandleObject obj, CallArgs& args)
{
    // All ReplayDebugger environments are inspectable, as all compartments in
    // the replayed process are considered to be debuggees.
    args.rval().setBoolean(true);
    return true;
}

bool
ReplayDebugger::envIsOptimizedOut(JSContext* cx, HandleObject obj, CallArgs& args)
{
    Activity a(cx);
    HandleObject data = a.getObjectData(obj);
    args.rval().setBoolean(a.getBooleanProperty(data, "optimizedOut"));
    return a.success();
}

static HandleObject
GetEnvironmentNames(ReplayDebugger::Activity& a, HandleObject data)
{
    HandleObject existing = a.getObjectProperty(data, "names");
    if (existing)
        return existing;
    HandleObject request = a.newRequestObject("getEnvironmentNames");
    a.defineProperty(request, "id", a.getScalarProperty(data, "id"));
    HandleObject names = a.sendRequest(request);
    a.defineProperty(data, "names", names);
    return names;
}

bool
ReplayDebugger::envNames(JSContext* cx, HandleObject obj, CallArgs& args)
{
    Activity a(cx);
    HandleObject data = a.getObjectData(obj);
    HandleObject names = GetEnvironmentNames(a, data);

    args.rval().setObjectOrNull(NewArrayWithPropertyDescriptorNames(a, names));
    return a.success();
}

bool
ReplayDebugger::envVariable(JSContext* cx, HandleObject obj, CallArgs& args)
{
    RootedId id(cx);
    if (!ValueToId<CanGC>(cx, args.get(0), &id))
        return false;

    Activity a(cx);
    HandleObject data = a.getObjectData(obj);
    HandleObject names = GetEnvironmentNames(a, data);

    size_t length = a.getScalarProperty(names, "length");
    for (size_t i = 0; i < length; i++) {
        HandleObject desc = a.getObjectElement(names, i);
        if (JSONDescriptorMatches(a, desc, id)) {
            args.rval().set(convertValueFromJSON(a, a.getObjectProperty(desc, "value")));
            return a.success();
        }
    }
    args.rval().setUndefined();
    return a.success();
}

///////////////////////////////////////////////////////////////////////////////
// Breakpoints
///////////////////////////////////////////////////////////////////////////////

typedef JS::replay::ExecutionPosition ExecutionPosition;

struct ReplayDebugger::Breakpoint
{
    PersistentRootedObject debugger;
    PersistentRootedObject handler;
    ExecutionPosition position;
    Breakpoint(JSContext* cx, JSObject* debugger, JSObject* handler,
               const ExecutionPosition& position)
      : debugger(cx, debugger), handler(cx, handler), position(position)
    {}
};
static Vector<ReplayDebugger::Breakpoint*, 0, SystemAllocPolicy> gReplayBreakpoints;

static bool
SetReplayBreakpoint(JSContext* cx, JSObject* debugger, JSObject* handler,
                    const ExecutionPosition& position)
{
    // Make sure we are always on the process main thread when using gReplayBreakpoints.
    MOZ_RELEASE_ASSERT(!cx->runtime()->parentRuntime);

    size_t breakpointId;
    for (breakpointId = 0; breakpointId < gReplayBreakpoints.length(); breakpointId++) {
        if (!gReplayBreakpoints[breakpointId])
            break;
    }
    if (breakpointId == gReplayBreakpoints.length() && !gReplayBreakpoints.append(nullptr)) {
        ReportOutOfMemory(cx);
        return false;
    }

    gReplayBreakpoints[breakpointId] =
        js_new<ReplayDebugger::Breakpoint>(cx, debugger, handler, position);
    if (!gReplayBreakpoints[breakpointId]) {
        ReportOutOfMemory(cx);
        return false;
    }

    JS::replay::hooks.setBreakpointMiddleman(breakpointId, position);
    return true;
}

static void
ClearReplayBreakpoints(JSContext* cx, Debugger* debugger,
                       const std::function<bool(const ReplayDebugger::Breakpoint&)>& match)
{
    // Make sure we are always on the process main thread when using gReplayBreakpoints.
    MOZ_RELEASE_ASSERT(!cx->runtime()->parentRuntime);

    for (size_t id = 0; id < gReplayBreakpoints.length(); id++) {
        if (gReplayBreakpoints[id] &&
            gReplayBreakpoints[id]->debugger == debugger->toJSObject() &&
            match(*gReplayBreakpoints[id]))
        {
            js_delete(gReplayBreakpoints[id]);
            gReplayBreakpoints[id] = nullptr;
            JS::replay::hooks.setBreakpointMiddleman(id, ExecutionPosition());
        }
    }
}

bool
ReplayDebugger::setHook(JSContext* cx, Debugger::Hook hook, HandleValue handler)
{
    ExecutionPosition::Kind kind;
    switch (hook) {
      case Debugger::OnEnterFrame:
        kind = ExecutionPosition::EnterFrame;
        break;
      case Debugger::OnNewScript:
        kind = ExecutionPosition::NewScript;
        break;
      default:
        return true;
    }

    if (handler.isUndefined()) {
        ClearReplayBreakpoints(cx, debugger, [=](const Breakpoint& breakpoint) {
                return breakpoint.position.kind == kind;
            });
        return true;
    }
    if (!handler.isObject()) {
        JS_ReportErrorASCII(cx, "onEnterFrame handler must be an object");
        return false;
    }

    ExecutionPosition position(kind);
    return SetReplayBreakpoint(cx, debugger->toJSObject(), &handler.toObject(), position);
}


/* static */ bool
ReplayDebugger::hitBreakpointMiddleman(JSContext* cx, size_t id)
{
    ReplayDebugger::Breakpoint* breakpoint = gReplayBreakpoints[id];
    MOZ_RELEASE_ASSERT(breakpoint);

    JSAutoRequest ar(cx);
    RootedObject debuggerObj(cx, breakpoint->debugger);
    Debugger* debugger = Debugger::fromJSObject(debuggerObj);

    JSAutoCompartment ac(cx, debuggerObj);
    bool res = debugger->replayDebugger()->hitBreakpoint(cx, breakpoint);

    // The replaying process will resume after this hook returns, if it hasn't
    // already been explicitly resumed.
    for (ReplayDebugger* dbg : gReplayDebuggers)
        dbg->invalidateAfterUnpause();

    return res;
}

bool
ReplayDebugger::setScriptBreakpoint(JSContext* cx, HandleObject obj, CallArgs& args)
{
    size_t offset;
    RootedObject handler(cx);
    if (!debugger->getBreakpointHandlerAndOffset(cx, args, obj, &offset, &handler))
        return false;

    Activity a(cx);
    HandleObject data = a.getObjectData(obj);
    size_t scriptId = a.getScalarProperty(data, "id");
    if (!a.success())
        return false;

    ExecutionPosition position(ExecutionPosition::Break, scriptId, offset);
    return SetReplayBreakpoint(cx, debugger->toJSObject(), handler, position);
}

bool
ReplayDebugger::clearScriptBreakpoint(JSContext* cx, HandleObject obj, CallArgs& args)
{
    RootedObject handler(cx, NonNullObject(cx, args.get(0)));
    if (!handler)
        return false;

    Activity a(cx);
    HandleObject data = a.getObjectData(obj);
    size_t scriptId = a.getScalarProperty(data, "id");
    if (!a.success())
        return false;

    ClearReplayBreakpoints(cx, debugger, [&](const Breakpoint& breakpoint) {
            return breakpoint.handler == handler
                && breakpoint.position.kind == ExecutionPosition::Break
                && breakpoint.position.script == scriptId;
        });
    return true;
}

bool
ReplayDebugger::setFrameOnStep(JSContext* cx, HandleObject obj, CallArgs& args)
{
    args.rval().setUndefined();

    RootedValue handler(cx, args.get(0));
    if (!IsValidHook(handler)) {
        JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr, JSMSG_NOT_CALLABLE_OR_UNDEFINED);
        return false;
    }

    Activity a(cx);
    HandleObject data = a.getObjectData(obj);
    size_t scriptId = a.getScalarProperty(data, "script");
    size_t frameIndex = a.getScalarProperty(data, "index");
    if (!a.success())
        return false;

    // Clear existing OnStep breakpoints for this frame.
    ClearReplayBreakpoints(cx, debugger, [=](const Breakpoint& breakpoint) {
            return breakpoint.position.script == scriptId
                && breakpoint.position.frameIndex == frameIndex
                && breakpoint.position.kind == ExecutionPosition::OnStep;
        });

    if (handler.isUndefined())
        return true;
    MOZ_RELEASE_ASSERT(handler.isObject());

    RootedValue offsetsValue(cx, args.get(1));
    if (!offsetsValue.isObject()) {
        JS_ReportErrorASCII(cx, "setFrameOnStep requires offset list");
        return false;
    }
    RootedObject offsets(cx, &offsetsValue.toObject());

    HandleObject scriptObj = getScript(a, scriptId);
    ScriptStructure structure;
    if (!getScriptStructure(cx, scriptObj, &structure))
        return false;

    uint32_t length;
    if (!GetLengthProperty(cx, offsets, &length))
        return false;

    for (size_t i = 0; i < length; i++) {
        RootedValue elem(cx);
        size_t offset;
        if (!JS_GetElement(cx, offsets, i, &elem) ||
            !ScriptOffset(cx, elem, &offset) ||
            !EnsureScriptOffsetIsValid(cx, structure.code, structure.codeLength, offset))
        {
            return false;
        }
        ExecutionPosition position(ExecutionPosition::OnStep, scriptId, offset, frameIndex);
        if (!SetReplayBreakpoint(cx, debugger->toJSObject(), &handler.toObject(), position))
            return false;
    }

    return true;
}

bool
ReplayDebugger::getFrameOnStep(JSContext* cx, HandleObject obj, CallArgs& args)
{
    Activity a(cx);
    HandleObject data = a.getObjectData(obj);
    size_t scriptId = a.getScalarProperty(data, "script");
    size_t frameIndex = a.getScalarProperty(data, "index");

    for (size_t i = 0; i < gReplayBreakpoints.length(); i++) {
        Breakpoint* breakpoint = gReplayBreakpoints[i];
        if (breakpoint &&
            breakpoint->debugger == debugger->toJSObject() &&
            breakpoint->position.script == scriptId &&
            breakpoint->position.frameIndex == frameIndex &&
            breakpoint->position.kind == ExecutionPosition::OnStep)
        {
            args.rval().setObject(*breakpoint->handler);
            return a.success();
        }
    }
    args.rval().setUndefined();
    return a.success();
}

bool
ReplayDebugger::setFrameOnPop(JSContext* cx, HandleObject obj, CallArgs& args)
{
    args.rval().setUndefined();

    RootedValue handler(cx, args.get(0));
    if (!IsValidHook(handler)) {
        JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr, JSMSG_NOT_CALLABLE_OR_UNDEFINED);
        return false;
    }

    Activity a(cx);
    HandleObject data = a.getObjectData(obj);
    size_t scriptId = a.getScalarProperty(data, "script");
    size_t frameIndex = a.getScalarProperty(data, "index");
    if (!a.success())
        return false;

    if (handler.isUndefined()) {
        ClearReplayBreakpoints(cx, debugger, [=](const Breakpoint& breakpoint) {
                return breakpoint.position.script == scriptId
                    && breakpoint.position.frameIndex == frameIndex
                    && breakpoint.position.kind == ExecutionPosition::OnPop;
            });
        return true;
    }
    MOZ_RELEASE_ASSERT(handler.isObject());

    ExecutionPosition position(ExecutionPosition::OnPop, scriptId,
                               ExecutionPosition::EMPTY_OFFSET, frameIndex);
    return SetReplayBreakpoint(cx, debugger->toJSObject(), &handler.toObject(), position);
}

bool
ReplayDebugger::getFrameOnPop(JSContext* cx, HandleObject obj, CallArgs& args)
{
    Activity a(cx);
    HandleObject data = a.getObjectData(obj);
    size_t scriptId = a.getScalarProperty(data, "script");
    size_t frameIndex = a.getScalarProperty(data, "index");

    for (size_t i = 0; i < gReplayBreakpoints.length(); i++) {
        Breakpoint* breakpoint = gReplayBreakpoints[i];
        if (breakpoint &&
            breakpoint->debugger == debugger->toJSObject() &&
            breakpoint->position.script == scriptId &&
            breakpoint->position.frameIndex == frameIndex &&
            breakpoint->position.kind == ExecutionPosition::OnPop)
        {
            args.rval().setObject(*breakpoint->handler);
            return a.success();
        }
    }
    args.rval().setUndefined();
    return a.success();
}

bool
ReplayDebugger::getOnPopFrame(JSContext* cx, MutableHandleValue rv)
{
    JS_ReportErrorASCII(cx, "get onPopFrame is NYI on replay debuggers");
    return false;
}

bool
ReplayDebugger::setOnPopFrame(JSContext* cx, HandleValue handler)
{
    if (handler.isUndefined()) {
        ClearReplayBreakpoints(cx, debugger, [=](const Breakpoint& breakpoint) {
                return breakpoint.position.kind == ExecutionPosition::OnPop
                    && breakpoint.position.script == ExecutionPosition::EMPTY_SCRIPT;
            });
        return true;
    }
    if (!handler.isObject()) {
        JS_ReportErrorASCII(cx, "onPopFrame handler must be an object");
        return false;
    }

    ExecutionPosition position(ExecutionPosition::OnPop);
    return SetReplayBreakpoint(cx, debugger->toJSObject(), &handler.toObject(), position);
}

bool
ReplayDebugger::hitBreakpoint(JSContext* cx, Breakpoint* breakpoint)
{
    RootedObject handler(cx, breakpoint->handler);
    RootedValue handlerValue(cx, ObjectValue(*handler));
    RootedValue debuggerValue(cx, ObjectValue(*breakpoint->debugger));
    RootedValue frameValue(cx);
    if (!getNewestFrame(cx, &frameValue))
        return false;
    RootedValue rv(cx);
    switch (breakpoint->position.kind) {
      case ExecutionPosition::Break:
        if (!CallMethodIfPresent(cx, handler, "hit", 1, frameValue.address(), &rv))
            return false;
        break;
      case ExecutionPosition::OnStep:
        if (!Call(cx, handlerValue, frameValue, &rv))
            return false;
        break;
      case ExecutionPosition::OnPop: {
        if (breakpoint->position.script != ExecutionPosition::EMPTY_SCRIPT) {
            Activity a(cx);
            HandleObject request = a.newRequestObject("popFrameResult");
            HandleObject response = a.sendRequest(request);
            bool throwing = a.getBooleanProperty(response, "throwing");
            HandleValue result = convertValueFromJSON(a, a.getObjectProperty(response, "result"));
            if (!a.success())
                return false;

            RootedValue completion(cx);
            RootedValue value(cx);
            ResumeMode resumeMode;
            Debugger::resultToCompletion(cx, !throwing, result, &resumeMode, &value);
            if (!debugger->newCompletionValue(cx, resumeMode, value, &completion))
                return false;
            if (!Call(cx, handlerValue, frameValue, completion, &rv))
                return false;
            break;
        }
        // OnPop handlers without a script behave like an EnterFrame handler.
        MOZ_FALLTHROUGH;
      }
      case ExecutionPosition::EnterFrame:
        if (!Call(cx, handlerValue, debuggerValue, frameValue, &rv))
            return false;
        break;
      case ExecutionPosition::NewScript: {
        Activity a(cx);
        HandleObject request = a.newRequestObject("getNewScript");
        HandleObject script = a.sendRequest(request);
        size_t id = a.getScalarProperty(script, "id");
        if (!a.success())
            return false;

        JSObject* obj = addScript(cx, id, script);
        if (!obj)
            return false;
        RootedValue scriptValue(cx, ObjectValue(*obj));

        if (!Call(cx, handlerValue, debuggerValue, scriptValue, &rv))
            return false;
        break;
      }
      default:
        MOZ_CRASH("Bad breakpoint kind");
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////////
// Miscellaneous functions
///////////////////////////////////////////////////////////////////////////////

void
ReplayDebugger::invalidateAfterUnpause()
{
    // Remove all things that are unstable when the replaying process is
    // unpaused or rewound, and invalidate the debug objects so they can no
    // longer be used.

    for (DebugObjectMap::Enum e(debugObjects); !e.empty(); e.popFront()) {
        NativeObject* obj = e.front().value();
        obj->setPrivate(nullptr);
    }
    debugObjects.clear();

    for (DebugObjectMap::Enum e(debugEnvs); !e.empty(); e.popFront()) {
        NativeObject* obj = e.front().value();
        obj->setPrivate(nullptr);
    }
    debugEnvs.clear();

    for (size_t i = 0; i < debugFrames.length(); i++) {
        NativeObject* obj = debugFrames[i];
        if (obj)
            obj->setPrivate(nullptr);
    }
    debugFrames.clear();
}

static HandleObject
ConvertPrimitiveValueToJSON(ReplayDebugger::Activity& a, HandleValue value)
{
    HandleObject res = a.newObject();
    MOZ_RELEASE_ASSERT(!value.isObject());
    if (value.isUndefined()) {
        a.defineProperty(res, "special", "undefined");
    } else if (value.isDouble()) {
        if (value.toDouble() != value.toDouble())
            a.defineProperty(res, "special", "NaN");
        else if (value.toDouble() == mozilla::PositiveInfinity<double>())
            a.defineProperty(res, "special", "Infinity");
        else if (value.toDouble() == mozilla::NegativeInfinity<double>())
            a.defineProperty(res, "special", "-Infinity");
        else
            a.defineProperty(res, "primitive", a.handlify(value));
    } else if (value.isMagic()) {
        a.defineProperty(res, "magic", value.whyMagic());
    } else if (value.isSymbol()) {
        JS_ReportErrorASCII(a.cx, "Sending symbols between processes is NYI");
        return nullptr;
    } else {
        MOZ_RELEASE_ASSERT(value.isString() || value.isInt32() || value.isBoolean() || value.isNull());
        a.defineProperty(res, "primitive", a.handlify(value));
    }
    return res;
}

HandleObject
ReplayDebugger::convertValueToJSON(Activity& a, HandleValue value)
{
    if (!value.isObject())
        return ConvertPrimitiveValueToJSON(a, value);
    HandleObject res = a.newObject();
    if (value.toObject().getClass() != &DebuggerObject::class_) {
        JS_ReportErrorASCII(a.cx, "Can't send object to replaying process");
        return nullptr;
    }
    HandleObject obj = a.handlify(&value.toObject());
    HandleObject data = a.getObjectData(obj);
    size_t id = a.getScalarProperty(data, "id");
    a.defineProperty(res, "object", id);
    return res;
}

static HandleValue
ConvertPrimitiveValueFromJSON(ReplayDebugger::Activity& a, HandleObject jsonValue)
{
    MOZ_RELEASE_ASSERT(!a.getMaybeScalarProperty(jsonValue, "object"));
    if (HandleString str = a.getStringProperty(jsonValue, "special")) {
        if (a.stringEquals(str, "undefined"))
            return a.handlify(UndefinedValue());
        if (a.stringEquals(str, "NaN"))
            return a.handlify(JS_GetNaNValue(a.cx));
        if (a.stringEquals(str, "Infinity"))
            return a.handlify(JS_GetPositiveInfinityValue(a.cx));
        if (a.stringEquals(str, "-Infinity"))
            return a.handlify(JS_GetNegativeInfinityValue(a.cx));
        JS_ReportErrorASCII(a.cx, "Cannot decode value from replaying process");
        return a.handlify(UndefinedValue());
    }
    if (!a.getValueProperty(jsonValue, "magic").isUndefined()) {
        JSWhyMagic why = (JSWhyMagic) a.getScalarProperty(jsonValue, "magic");
        return a.handlify(MagicValue(why));
    }
    return a.getValueProperty(jsonValue, "primitive");
}

HandleValue
ReplayDebugger::convertValueFromJSON(Activity& a, HandleObject jsonValue)
{
    if (size_t id = a.getMaybeScalarProperty(jsonValue, "object")) {
        HandleObject obj = getObject(a, id);
        return a.handlify(ObjectOrNullValue(obj));
    }
    HandleValue v = ConvertPrimitiveValueFromJSON(a, jsonValue);
    if (v.isMagic()) {
        JSObject* obj = Debugger::createMagicValueObject(a.cx, v.whyMagic());
        return a.handlify(ObjectOrNullValue(obj));
    }
    return v;
}

///////////////////////////////////////////////////////////////////////////////
// Replaying process data
///////////////////////////////////////////////////////////////////////////////

// All scripts and script sources which are considered by the replay debugger.
static Vector<JSScript*, 0, SystemAllocPolicy> gDebuggerScripts;
static Vector<ScriptSourceObject*, 0, SystemAllocPolicy> gDebuggerScriptSources;

/* static */ size_t
ReplayDebugger::ScriptId(JSScript* script)
{
    for (size_t i = 0; i < gDebuggerScripts.length(); i++) {
        if (script == gDebuggerScripts[i])
            return i;
    }
    return 0;
}

/* static */ JSScript*
ReplayDebugger::IdScript(size_t id)
{
    return (id < gDebuggerScripts.length()) ? gDebuggerScripts[id] : nullptr;
}

static size_t
ScriptSourceId(ScriptSourceObject* sso)
{
    for (size_t i = 0; i < gDebuggerScriptSources.length(); i++) {
        if (sso == gDebuggerScriptSources[i])
            return i;
    }
    MOZ_CRASH("Unknown script source");
}

// All objects for which IDs have been generated during the current pause.
// This is reset when unpausing.
static Vector<JSObject*, 0, SystemAllocPolicy> gDebuggerPausedObjects;

static size_t
ObjectId(JSContext* cx, JSObject* obj)
{
    if (!obj)
        return 0;
    MOZ_RELEASE_ASSERT(!obj->is<ScriptSourceObject>());
    for (size_t i = 1; i < gDebuggerPausedObjects.length(); i++) {
        if (obj == gDebuggerPausedObjects[i])
            return i;
    }
    AutoEnterOOMUnsafeRegion oomUnsafe;
    if (gDebuggerPausedObjects.empty() && !gDebuggerPausedObjects.append(nullptr))
        oomUnsafe.crash("ObjectId");
    if (!gDebuggerPausedObjects.append(obj))
        oomUnsafe.crash("ObjectId");
    return gDebuggerPausedObjects.length() - 1;
}

static JSObject*
IdObject(size_t id)
{
    return gDebuggerPausedObjects[id];
}

/* static */ void
ReplayDebugger::ClearDebuggerPausedObjects()
{
    gDebuggerPausedObjects.clear();
}

static bool
ConsiderScript(JSScript* script)
{
    // Workaround script->filename() sometimes crashing.
    if (IsSystemZone(script->zone()))
        return false;

    const char* filename = script->filename();
    if (!filename)
        return false;
    if (!strcmp(filename, "self-hosted"))
        return false;
    if (!strncmp(filename, "resource:", 9))
        return false;
    if (!strncmp(filename, "chrome:", 7))
        return false;
    if (!script->scriptSource()->hasSourceData())
        return false;
    return true;
}

/* static */ void
ReplayDebugger::onNewScript(JSContext* cx, HandleScript script, bool toplevel)
{
    MOZ_RELEASE_ASSERT(mozilla::recordreplay::IsRecordingOrReplaying());

    if (mozilla::recordreplay::AreThreadEventsDisallowed()) {
        // This script is part of an eval on behalf of the debugger.
        return;
    }

    if (cx->runtime() != gMainRuntime || !ConsiderScript(script))
        return;

    AutoEnterOOMUnsafeRegion oomUnsafe;

    if (script->hasObjects()) {
        for (size_t i = 0; i < script->objects()->length; i++) {
            JSObject* obj = script->objects()->vector[i];
            if (obj->is<JSFunction>()) {
                RootedFunction fun(cx, &obj->as<JSFunction>());
                if (fun->isInterpreted()) {
                    RootedScript script(cx, JSFunction::getOrCreateScript(cx, fun));
                    if (!script)
                        oomUnsafe.crash("ReplayDebugger::onNewScript");
                    onNewScript(cx, script, false);
                }
            }
        }
    }

    for (size_t i = 1; i < gDebuggerScripts.length(); i++)
        MOZ_RELEASE_ASSERT(script != gDebuggerScripts[i]);

    if (gDebuggerScripts.empty() && !gDebuggerScripts.append(nullptr))
        oomUnsafe.crash("ReplayDebugger::onNewScript");
    if (!gDebuggerScripts.append(script))
        oomUnsafe.crash("ReplayDebugger::onNewScript");

    ScriptSourceObject* sso = &script->scriptSourceUnwrap();
    bool found = false;
    for (size_t i = 1; i < gDebuggerScriptSources.length(); i++) {
        if (sso == gDebuggerScriptSources[i]) {
            found = true;
            break;
        }
    }
    if (gDebuggerScriptSources.empty() && !gDebuggerScriptSources.append(nullptr))
        oomUnsafe.crash("ReplayDebugger::onNewScript");
    if (!found && !gDebuggerScriptSources.append(sso))
        oomUnsafe.crash("ReplayDebugger::onNewScript");

    HandleBreakpointsForNewScript(script, gDebuggerScripts.length() - 1, toplevel);
}

/* static */ void
ReplayDebugger::markRoots(JSTracer* trc)
{
    // Never collect scripts which the debugger might be interested in.
    if (!mozilla::recordreplay::IsRecordingOrReplaying() || trc->runtime() != gMainRuntime)
        return;

    for (size_t i = 1; i < gDebuggerScripts.length(); i++)
        TraceRoot(trc, &gDebuggerScripts[i], "ReplayDebugger::markRoots script");
    for (size_t i = 1; i < gDebuggerScriptSources.length(); i++)
        TraceRoot(trc, &gDebuggerScriptSources[i], "ReplayDebugger::markRoots script source");
    for (size_t i = 1; i < gDebuggerPausedObjects.length(); i++)
        TraceRoot(trc, &gDebuggerPausedObjects[i], "ReplayDebugger::markRoots object");
}

///////////////////////////////////////////////////////////////////////////////
// Replaying process content
///////////////////////////////////////////////////////////////////////////////

struct ContentInfo
{
    const void* token;
    char* filename;
    char* contentType;
    JS::SmallestEncoding encoding;
    Vector<uint8_t, 0, SystemAllocPolicy> content;

    ContentInfo(const void* token, const char* filename, const char* contentType,
                JS::SmallestEncoding encoding)
        : token(token),
          filename(strdup(filename)),
          contentType(strdup(contentType)),
          encoding(encoding)
    {}
};

struct ContentSet
{
    Mutex mutex;
    Vector<ContentInfo, 0, SystemAllocPolicy> contentList;

    ContentSet()
      : mutex(mutexid::ReplayContentSet)
    {}
};

static ContentSet* gContentSet;

/* static */ void
ReplayDebugger::InitializeContentSet()
{
    gContentSet = new ContentSet();
}

JS_PUBLIC_API(void)
JS::BeginContentParseForRecordReplay(const void* token,
                                     const char* filename, const char* contentType,
                                     SmallestEncoding encoding)
{
    MOZ_RELEASE_ASSERT(token);

    mozilla::recordreplay::RecordReplayAssert("BeginContentParseForRecordReplay %s", filename);

    MOZ_RELEASE_ASSERT(mozilla::recordreplay::IsRecordingOrReplaying());
    AutoEnterOOMUnsafeRegion oomUnsafe;
    LockGuard<Mutex> guard(gContentSet->mutex);
    for (ContentInfo& info : gContentSet->contentList)
        MOZ_RELEASE_ASSERT(info.token != token);
    if (!gContentSet->contentList.emplaceBack(token, filename, contentType, encoding))
        oomUnsafe.crash("BeginContentParseForRecordReplay");
}

JS_PUBLIC_API(void)
JS::AddContentParseDataForRecordReplay(const void* token, const void* buffer, size_t length)
{
    MOZ_RELEASE_ASSERT(token);

    mozilla::recordreplay::RecordReplayAssert("AddContentParseDataForRecordReplay %d", (int) length);

    MOZ_RELEASE_ASSERT(mozilla::recordreplay::IsRecordingOrReplaying());
    AutoEnterOOMUnsafeRegion oomUnsafe;
    LockGuard<Mutex> guard(gContentSet->mutex);
    for (ContentInfo& info : gContentSet->contentList) {
        if (info.token == token) {
            if (!info.content.append((const uint8_t*) buffer, length))
                oomUnsafe.crash("AddContentParseDataForRecordReplay");
            return;
        }
    }
    MOZ_CRASH("Unknown content parse token");
}

JS_PUBLIC_API(void)
JS::EndContentParseForRecordReplay(const void* token)
{
    MOZ_RELEASE_ASSERT(token);

    MOZ_RELEASE_ASSERT(mozilla::recordreplay::IsRecordingOrReplaying());
    LockGuard<Mutex> guard(gContentSet->mutex);
    for (ContentInfo& info : gContentSet->contentList) {
        if (info.token == token) {
            info.token = nullptr;
            return;
        }
    }
    MOZ_CRASH("Unknown content parse token");
}

static void
FetchContent(JSContext* cx, HandleString filename,
             MutableHandleString contentType, MutableHandleString content)
{
    AutoEnterOOMUnsafeRegion oomUnsafe;
    LockGuard<Mutex> guard(gContentSet->mutex);
    for (ContentInfo& info : gContentSet->contentList) {
        if (JS_FlatStringEqualsAscii(JS_ASSERT_STRING_IS_FLAT(filename), info.filename)) {
            contentType.set(JS_NewStringCopyZ(cx, info.contentType));
            switch (info.encoding) {
              case JS::SmallestEncoding::ASCII:
              case JS::SmallestEncoding::Latin1:
                content.set(JS_NewStringCopyN(cx, (const char*) info.content.begin(),
                                              info.content.length()));
                break;
              case JS::SmallestEncoding::UTF16:
                content.set(JS_NewUCStringCopyN(cx, (const char16_t*) info.content.begin(),
                                                info.content.length() / sizeof(char16_t)));
                break;
            }
            if (!contentType || !content)
                oomUnsafe.crash("FetchContent");
            return;
        }
    }
    contentType.set(JS_NewStringCopyZ(cx, "text/plain"));
    content.set(JS_NewStringCopyZ(cx, "Could not find record/replay content"));
    if (!contentType || !content)
        oomUnsafe.crash("FetchContent");
}

///////////////////////////////////////////////////////////////////////////////
// Replaying process request handling
///////////////////////////////////////////////////////////////////////////////

static HandleObject
ConvertValueToJSON(ReplayDebugger::Activity& a, HandleValue value)
{
    MOZ_RELEASE_ASSERT(mozilla::recordreplay::IsRecordingOrReplaying());
    if (!value.isObject())
        return ConvertPrimitiveValueToJSON(a, value);
    HandleObject res = a.newObject();
    a.defineProperty(res, "object", ObjectId(a.cx, &value.toObject()));
    return res;
}

static HandleValue
ConvertValueFromJSON(ReplayDebugger::Activity& a, HandleObject jsonValue)
{
    MOZ_RELEASE_ASSERT(mozilla::recordreplay::IsRecordingOrReplaying());
    if (size_t id = a.getScalarProperty(jsonValue, "object"))
        return a.handlify(ObjectValue(*IdObject(id)));
    return ConvertPrimitiveValueFromJSON(a, jsonValue);
}

static HandleObject
ScriptDescriptorObject(ReplayDebugger::Activity& a, size_t id)
{
    RootedScript script(a.cx, gDebuggerScripts[id]);

    HandleObject entry = a.newObject();
    a.defineProperty(entry, "id", id);
    a.defineProperty(entry, "sourceId", ScriptSourceId(&script->scriptSourceUnwrap()));
    a.defineProperty(entry, "startLine", script->lineno());
    a.defineProperty(entry, "lineCount", GetScriptLineExtent(script));
    a.defineProperty(entry, "sourceStart", script->sourceStart());
    a.defineProperty(entry, "sourceLength", script->sourceEnd() - script->sourceStart());

    JSFunction* func = script->functionNonDelazifying();
    if (func && func->displayAtom())
        a.defineProperty(entry, "displayName", a.handlify(func->displayAtom()));

    if (script->filename())
        a.defineProperty(entry, "url", script->filename());

    return entry;
}

static HandleObject
Respond_findScripts(ReplayDebugger::Activity& a, HandleObject request)
{
    HandleObject response = a.newArray();
    for (size_t i = 1; i < gDebuggerScripts.length(); i++)
        a.pushArray(response, ScriptDescriptorObject(a, i));
    return response;
}

static HandleObject
Respond_getNewScript(ReplayDebugger::Activity& a, HandleObject request)
{
    MOZ_RELEASE_ASSERT(gDebuggerScripts.length() > 1);
    return ScriptDescriptorObject(a, gDebuggerScripts.length() - 1);
}

static HandleObject
Respond_getContent(ReplayDebugger::Activity& a, HandleObject request)
{
    RootedString contentType(a.cx);
    RootedString content(a.cx);

    HandleString url = a.getStringProperty(request, "url");
    FetchContent(a.cx, url, &contentType, &content);

    HandleObject response = a.newObject();
    a.defineProperty(response, "contentType", contentType);
    a.defineProperty(response, "content", content);
    return response;
}

static HandleObject
Respond_getSource(ReplayDebugger::Activity& a, HandleObject request)
{
    size_t id = a.getScalarProperty(request, "id");

    if (id >= gDebuggerScriptSources.length()) {
        JS_ReportErrorASCII(a.cx, "Script source ID out of range");
        return nullptr;
    }
    Rooted<ScriptSourceObject*> sso(a.cx, gDebuggerScriptSources[id]);
    MOZ_RELEASE_ASSERT(sso);

    ScriptSource* ss = sso->source();

    HandleObject response = a.newObject();
    a.defineProperty(response, "id", id);

    if (ss->hasSourceData()) {
        RootedString str(a.cx, ss->substring(a.cx, 0, ss->length()));
        if (!str)
            return nullptr;
        a.defineProperty(response, "text", str);
    }

    if (ss->filename())
        a.defineProperty(response, "url", ss->filename());

    if (ss->hasDisplayURL())
        a.defineProperty(response, "displayUrl", ss->displayURL());

    if (sso->elementAttributeName().isString())
        a.defineProperty(response, "elementProperty", a.handlify(sso->elementAttributeName()));

    if (JSScript* script = sso->introductionScript()) {
        if (ConsiderScript(script)) {
            a.defineProperty(response, "introductionScript", ReplayDebugger::ScriptId(script));
            if (ss->hasIntroductionOffset())
                a.defineProperty(response, "introductionOffset", ss->introductionOffset());
        }
    }

    if (ss->hasIntroductionType())
        a.defineProperty(response, "introductionType", ss->introductionType());

    if (ss->hasSourceMapURL())
        a.defineProperty(response, "sourceMapUrl", ss->sourceMapURL());

    return response;
}

static HandleObject
Respond_getStructure(ReplayDebugger::Activity& a, HandleObject request)
{
    size_t id = a.getScalarProperty(request, "id");

    if (id >= gDebuggerScripts.length()) {
        JS_ReportErrorASCII(a.cx, "Script ID out of range");
        return nullptr;
    }
    JSScript* script = gDebuggerScripts[id];
    MOZ_RELEASE_ASSERT(script);
    MOZ_RELEASE_ASSERT(script->notes() == script->code() + script->length());

    HandleObject response = a.newObject();
    a.defineBinaryProperty(response, "code",
                           script->code(), script->length() + script->numNotes());
    a.defineProperty(response, "codeLength", script->length());
    if (script->hasTrynotes()) {
        a.defineBinaryProperty(response, "trynotes",
                               (uint8_t*) script->trynotes()->vector, script->trynotes()->length);
    }
    a.defineProperty(response, "lineno", script->lineno());
    a.defineProperty(response, "mainOffset", script->pcToOffset(script->main()));
    return response;
}

static HandleObject
Respond_getObject(ReplayDebugger::Activity& a, HandleObject request)
{
    size_t id = a.getScalarProperty(request, "id");

    RootedObject obj(a.cx, IdObject(id));
    RootedFunction fun(a.cx, obj->is<JSFunction>() ? &obj->as<JSFunction>() : nullptr);

    const char* className;
    RootedObject proto(a.cx);
    {
        AutoCompartment ac(a.cx, obj);
        className = GetObjectClassName(a.cx, obj);
        if (!GetPrototype(a.cx, obj, &proto))
            return nullptr;
    }

    RootedScript script(a.cx);
    if (fun && fun->isInterpreted()) {
        script = GetOrCreateFunctionScript(a.cx, fun);
        if (!script)
            return nullptr;
    }

    Rooted<Env*> env(a.cx);
    if (!GetObjectEnv(a.cx, obj, &env))
        return nullptr;

    bool isSealed, isFrozen, isExtensible;
    if (!ObjectIsSealedHelper(a.cx, obj, OpSeal, &isSealed) ||
        !ObjectIsSealedHelper(a.cx, obj, OpFreeze, &isFrozen) ||
        !ObjectIsSealedHelper(a.cx, obj, OpPreventExtensions /* see ObjectIsSealedHelper */, &isExtensible))
    {
        return nullptr;
    }

    HandleObject response = a.newObject();
    a.defineProperty(response, "id", id);
    a.defineProperty(response, "className", className);
    if (fun && fun->explicitName())
        a.defineProperty(response, "explicitName", a.handlify(fun->explicitName()));
    if (fun && fun->displayAtom())
        a.defineProperty(response, "displayName", a.handlify(fun->displayAtom()));
    a.defineProperty(response, "callable", obj->isCallable());
    a.defineProperty(response, "isArrowFunction", fun && fun->isArrow());
    a.defineProperty(response, "isBoundFunction", obj->isBoundFunction());
    a.defineProperty(response, "isProxy", obj->is<ProxyObject>());
    a.defineProperty(response, "isScriptedProxy", IsScriptedProxy(obj));
    a.defineProperty(response, "isExtensible", isExtensible);
    a.defineProperty(response, "isSealed", isSealed);
    a.defineProperty(response, "isFrozen", isFrozen);
    a.defineProperty(response, "script", ReplayDebugger::ScriptId(script));
    a.defineProperty(response, "environment", ObjectId(a.cx, env));
    a.defineProperty(response, "proto", ObjectId(a.cx, proto));
    a.defineProperty(response, "global", ObjectId(a.cx, &obj->global()));
    return response;
}

static HandleObject
Respond_getObjectProperties(ReplayDebugger::Activity& a, HandleObject request)
{
    if (!ReplayDebugger::MaybeDivergeFromRecording()) {
        HandleObject response = a.newArray();
        HandleObject entry = a.newObject();
        a.pushArray(response, entry);
        a.defineProperty(entry, "name", "Unknown properties");
        a.defineProperty(entry, "attrs", JSPROP_ENUMERATE);
        HandleValue v = a.valueify("Recording divergence in getObjectProperties");
        a.defineProperty(entry, "value", ConvertValueToJSON(a, v));
        return response;
    }

    size_t id = a.getScalarProperty(request, "id");
    RootedObject obj(a.cx, IdObject(id));

    AutoIdVector keys(a.cx);
    {
        Maybe<AutoCompartment> ac;
        ac.emplace(a.cx, obj);
        ErrorCopier ec(ac);
        if (!GetPropertyKeys(a.cx, obj, JSITER_OWNONLY | JSITER_HIDDEN, &keys))
            return nullptr;
    }

    HandleObject response = a.newArray();

    for (size_t i = 0; i < keys.length(); i++) {
        RootedId id(a.cx, keys[i]);
        a.cx->markId(id);

        Rooted<PropertyDescriptor> desc(a.cx);
        {
            Maybe<AutoCompartment> ac;
            ac.emplace(a.cx, obj);

            ErrorCopier ec(ac);
            if (!GetOwnPropertyDescriptor(a.cx, obj, id, &desc))
                return nullptr;
        }

        if (!desc.object())
            continue;

        HandleObject entry = a.newObject();
        a.pushArray(response, entry);

        if (JSID_IS_INT(id)) {
            RootedString str(a.cx, Int32ToString<CanGC>(a.cx, JSID_TO_INT(id)));
            if (!str)
                return nullptr;
            a.defineProperty(entry, "name", str);
        } else if (JSID_IS_ATOM(id)) {
            RootedString str(a.cx, JSID_TO_STRING(id));
            a.defineProperty(entry, "name", str);
        } else {
            JS_ReportErrorASCII(a.cx, "Unknown property ID kind in object");
            return nullptr;
        }

        a.defineProperty(entry, "attrs", desc.attributes());
        if (desc.hasGetterObject())
            a.defineProperty(entry, "getterObject", ObjectId(a.cx, desc.getterObject()));
        if (desc.hasSetterObject())
            a.defineProperty(entry, "setterObject", ObjectId(a.cx, desc.setterObject()));
        a.defineProperty(entry, "value", ConvertValueToJSON(a, desc.value()));
    }

    return response;
}

static HandleObject
Respond_getObjectParameterNames(ReplayDebugger::Activity& a, HandleObject request)
{
    size_t id = a.getScalarProperty(request, "id");
    RootedObject obj(a.cx, IdObject(id));

    HandleObject response = a.newArray();

    if (!obj->is<JSFunction>())
        return response;

    Rooted<StringVector> names(a.cx, StringVector(a.cx));
    if (!GetFunctionParameterNames(a.cx, obj.as<JSFunction>(), &names))
        return nullptr;

    for (size_t i = 0; i < names.length(); i++) {
        HandleObject entry = a.newObject();
        a.pushArray(response, entry);
        if (names[i])
            a.defineProperty(entry, "name", names[i]);
    }
    return response;
}

static HandleObject
Respond_objectCall(ReplayDebugger::Activity& a, HandleObject request)
{
    if (!ReplayDebugger::MaybeDivergeFromRecording()) {
        HandleObject response = a.newObject();
        a.defineProperty(response, "throwing", true);
        HandleValue rval = a.valueify("Recording divergence in objectCall");
        a.defineProperty(response, "result", ConvertValueToJSON(a, rval));
        return response;
    }

    size_t id = a.getScalarProperty(request, "functionId");
    RootedObject function(a.cx, IdObject(id));

    RootedValue calleev(a.cx, ObjectValue(*function));
    RootedValue thisv(a.cx, ConvertValueFromJSON(a, a.getObjectProperty(request, "thisv")));

    InvokeArgs args(a.cx);
    if (HandleObject array = a.getObjectProperty(request, "arguments")) {
        size_t length = a.getScalarProperty(array, "length");
        if (!args.init(a.cx, length))
            return nullptr;
        for (size_t i = 0; i < length; i++)
            args[i].set(ConvertValueFromJSON(a, a.getObjectElement(array, i)));
    } else {
        if (!args.init(a.cx, 0))
            return nullptr;
    }

    Maybe<AutoCompartment> ac;
    ac.emplace(a.cx, function);

    if (!a.cx->compartment()->wrap(a.cx, &thisv))
        return nullptr;
    for (unsigned i = 0; i < args.length(); i++) {
        if (!a.cx->compartment()->wrap(a.cx, args[i]))
            return nullptr;
    }

    RootedValue rval(a.cx);
    bool throwing = !Call(a.cx, calleev, thisv, args, &rval);
    if (throwing) {
        if (!a.cx->getPendingException(&rval))
            return nullptr;
        a.cx->clearPendingException();

        // Convert object errors to strings for simpler error reporting in the
        // middleman process.
        if (rval.isObject()) {
            if (!ToPrimitive(a.cx, &rval))
                a.cx->clearPendingException();
        }
    }

    ac.reset();

    HandleObject response = a.newObject();
    a.defineProperty(response, "throwing", throwing);
    a.defineProperty(response, "result", ConvertValueToJSON(a, rval));
    return response;
}

static HandleObject
Respond_getEnvironment(ReplayDebugger::Activity& a, HandleObject request)
{
    size_t id = a.getScalarProperty(request, "id");
    RootedObject env(a.cx, IdObject(id));

    HandleObject response = a.newObject();
    a.defineProperty(response, "id", id);
    a.defineProperty(response, "type", a.handlify(GetEnvTypeAtom(a.cx, env)));
    a.defineProperty(response, "parent", ObjectId(a.cx, env->enclosingEnvironment()));
    if (GetEnvType(env) != DebuggerEnvironmentType::Declarative)
        a.defineProperty(response, "object", ObjectId(a.cx, GetEnvObject(env)));
    a.defineProperty(response, "callee", ObjectId(a.cx, GetEnvCallee(env)));
    a.defineProperty(response, "optimizedOut", EnvIsOptimizedOut(env));
    return response;
}

static HandleObject
Respond_getEnvironmentNames(ReplayDebugger::Activity& a, HandleObject request)
{
    if (!ReplayDebugger::MaybeDivergeFromRecording()) {
        HandleObject response = a.newArray();
        HandleObject entry = a.newObject();
        a.pushArray(response, entry);
        a.defineProperty(entry, "name", "Unknown names");
        HandleValue v = a.valueify("Recording divergence in getEnvironmentNames");
        a.defineProperty(entry, "value", ConvertValueToJSON(a, v));
        return response;
    }

    size_t id = a.getScalarProperty(request, "id");
    RootedObject env(a.cx, IdObject(id));

    AutoIdVector keys(a.cx);
    {
        Maybe<AutoCompartment> ac;
        ac.emplace(a.cx, env);
        ErrorCopier ec(ac);
        if (!GetPropertyKeys(a.cx, env, JSITER_HIDDEN, &keys))
            return nullptr;
    }

    HandleObject response = a.newArray();

    for (size_t i = 0; i < keys.length(); i++) {
        RootedId id(a.cx, keys[i]);
        if (JSID_IS_ATOM(id) && frontend::IsIdentifier(JSID_TO_ATOM(id))) {
            HandleObject entry = a.newObject();
            a.pushArray(response, entry);

            RootedString str(a.cx, JSID_TO_STRING(id));
            a.defineProperty(entry, "name", str);

            RootedValue value(a.cx);
            {
                AutoCompartment ac(a.cx, env);
                if (!GetEnvVariable(a.cx, env, id, &value))
                    return nullptr;
            }
            a.defineProperty(entry, "value", ConvertValueToJSON(a, value));
        }
    }

    return response;
}

/* static */ size_t
ReplayDebugger::CountScriptFrames(JSContext* cx)
{
    size_t numFrames = 0;
    for (ScriptFrameIter iter(cx); !iter.done(); ++iter) {
        if (ConsiderScript(iter.script()))
            ++numFrames;
    }
    return numFrames;
}

static bool
ScriptFrameIterForIndex(JSContext* cx, size_t index, ScriptFrameIter& iter)
{
    size_t numFrames = ReplayDebugger::CountScriptFrames(cx);
    if (index >= numFrames) {
        JS_ReportErrorASCII(cx, "Not enough frames on stack");
        return false;
    }
    size_t indexFromTop = numFrames - 1 - index;
    size_t frame = 0;
    for (;; ++iter) {
        MOZ_RELEASE_ASSERT(!iter.done());
        if (iter.isIon() && !iter.ensureHasRematerializedFrame(cx))
            return false;
        if (ConsiderScript(iter.script())) {
            if (frame++ == indexFromTop)
                break;
        }
    }
    UpdateFrameIterPc(iter);
    return true;
}

static HandleObject
Respond_getFrame(ReplayDebugger::Activity& a, HandleObject request)
{
    size_t index = a.getScalarProperty(request, "index");
    HandleObject response = a.newObject();

    if (index == NEWEST_FRAME_INDEX) {
        size_t numFrames = ReplayDebugger::CountScriptFrames(a.cx);
        if (!numFrames) {
            // Return an empty object when there are no frames.
            return response;
        }
        index = numFrames - 1;
    }

    ScriptFrameIter iter(a.cx);
    if (!ScriptFrameIterForIndex(a.cx, index, iter))
        return nullptr;
    AbstractFramePtr framePtr = iter.abstractFramePtr();

    RootedValue thisv(a.cx);
    Rooted<Env*> env(a.cx);
    {
        AutoCompartment ac(a.cx, framePtr.environmentChain());
        if (!GetThisValueForDebuggerMaybeOptimizedOut(a.cx, framePtr, iter.pc(), &thisv))
            return nullptr;

        env = GetDebugEnvironmentForFrame(a.cx, framePtr, iter.pc());
        if (!env)
            return nullptr;
    }

    a.defineProperty(response, "index", index);
    a.defineProperty(response, "type", a.handlify(DebuggerFrame::getTypeAtom(a.cx, framePtr)));
    if (framePtr.isFunctionFrame())
        a.defineProperty(response, "callee", ObjectId(a.cx, framePtr.calleev().toObjectOrNull()));
    a.defineProperty(response, "environment", ObjectId(a.cx, env));
    a.defineProperty(response, "generator", framePtr.script()->isGenerator());
    a.defineProperty(response, "constructing",
                     iter.isFunctionFrame() && iter.isConstructing());
    a.defineProperty(response, "hasArguments", framePtr.hasArgs());
    a.defineProperty(response, "thisv", ConvertValueToJSON(a, thisv));
    a.defineProperty(response, "script", ReplayDebugger::ScriptId(framePtr.script()));
    a.defineProperty(response, "offset", framePtr.script()->pcToOffset(iter.pc()));

    if (framePtr.hasArgs() && framePtr.numActualArgs() > 0) {
        HandleObject actuals = a.newArray();
        a.defineProperty(response, "actuals", actuals);
        for (size_t i = 0; i < framePtr.numActualArgs(); i++) {
            RootedValue v(a.cx);
            if (!GetFrameActualArg(a.cx, framePtr, i, &v))
                return nullptr;
            a.pushArray(actuals, ConvertValueToJSON(a, v));
        }
    }

    ++iter;
    return response;
}

static HandleObject
Respond_frameEvaluate(ReplayDebugger::Activity& a, HandleObject request)
{
    if (!ReplayDebugger::MaybeDivergeFromRecording()) {
        HandleObject response = a.newObject();
        a.defineProperty(response, "throwing", true);
        HandleValue rval = a.valueify("Recording divergence in frameEvaluate");
        a.defineProperty(response, "result", ConvertValueToJSON(a, rval));
        return response;
    }

    size_t frameIndex = a.getScalarProperty(request, "frameIndex");
    HandleString text = a.getNonNullStringProperty(request, "text");

    ScriptFrameIter iter(a.cx);
    if (!ScriptFrameIterForIndex(a.cx, frameIndex, iter))
        return nullptr;
    AbstractFramePtr framePtr = iter.abstractFramePtr();

    RootedValue rval(a.cx);
    bool throwing;
    {
        AutoCompartment ac(a.cx, framePtr.environmentChain());

        Rooted<Env*> env(a.cx, GetDebugEnvironmentForFrame(a.cx, framePtr, iter.pc()));
        if (!env)
            return nullptr;

        AutoStableStringChars stableChars(a.cx);
        if (!stableChars.initTwoByte(a.cx, text))
            return nullptr;
        mozilla::Range<const char16_t> chars = stableChars.twoByteRange();

        LeaveDebuggeeNoExecute nnx(a.cx);
        throwing = !EvaluateInEnv(a.cx, env, framePtr, chars, "debugger eval code", 1, &rval);
    }

    if (throwing) {
        if (!a.cx->getPendingException(&rval))
            return nullptr;
        a.cx->clearPendingException();
    }

    HandleObject response = a.newObject();
    a.defineProperty(response, "throwing", throwing);
    a.defineProperty(response, "result", ConvertValueToJSON(a, rval));
    return response;
}

static HandleObject
Respond_popFrameResult(ReplayDebugger::Activity& a, HandleObject request)
{
    HandleObject response = a.newObject();

    bool throwing;
    RootedValue result(a.cx);
    ReplayDebugger::GetPoppedFrameResult(&throwing, &result);
    a.defineProperty(response, "throwing", throwing);
    a.defineProperty(response, "result", ConvertValueToJSON(a, result));
    return response;
}

#define FOR_EACH_REQUEST(Macro)                 \
    Macro(findScripts)                          \
    Macro(getNewScript)                         \
    Macro(getContent)                           \
    Macro(getSource)                            \
    Macro(getStructure)                         \
    Macro(getObject)                            \
    Macro(getObjectParameterNames)              \
    Macro(getEnvironment)                       \
    Macro(getFrame)                             \
    Macro(popFrameResult)                       \
    Macro(getObjectProperties)                  \
    Macro(objectCall)                           \
    Macro(getEnvironmentNames)                  \
    Macro(frameEvaluate)

static bool
RequestMatch(ReplayDebugger::Activity& a, HandleString kind, const char* name)
{
    return a.stringEquals(kind, name);
}

/* static */ void
ReplayDebugger::ProcessRequest(const char16_t* requestBuffer, size_t requestLength,
                               JS::replay::CharBuffer* responseBuffer)
{
    JSContext* cx = TlsContext.get();
    AutoCompartment ac(cx, *gHookGlobal);

    RootedValue requestValue(cx);
    TRY(JS_ParseJSON(cx, requestBuffer, requestLength, &requestValue));

    MOZ_RELEASE_ASSERT(requestValue.isObject());
    RootedObject request(cx, &requestValue.toObject());

    ReplayDebugger::Activity a(cx);
    HandleString kind = a.getNonNullStringProperty(request, "kind");
    MOZ_RELEASE_ASSERT(!cx->isExceptionPending());

    RootedObject response(cx);

#define HANDLE_REQUEST(Name)                                    \
    if (RequestMatch(a, kind, #Name))                           \
        response = Respond_ ##Name (a, request);
FOR_EACH_REQUEST(HANDLE_REQUEST)
#undef HANDLE_RESPONSE

    mozilla::recordreplay::DisallowUnhandledDivergeFromRecording();

    MOZ_RELEASE_ASSERT(cx->isExceptionPending() || response);

    if (cx->isExceptionPending()) {
        RootedValue exception(cx);
        TRY(cx->getPendingException(&exception));
        cx->clearPendingException();
        response = a.newObject();
        a.defineProperty(response, "exception", ConvertValueToJSON(a, exception));
        MOZ_RELEASE_ASSERT(a.success());
    }

    TRY(ToJSONMaybeSafely(cx, response, FillCharBufferCallback, responseBuffer));
}

#undef TRY
