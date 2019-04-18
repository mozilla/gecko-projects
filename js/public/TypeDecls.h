/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: set ts=8 sts=2 et sw=2 tw=80:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// This file contains public type declarations that are used *frequently*.  If
// it doesn't occur at least 10 times in Gecko, it probably shouldn't be in
// here.
//
// It includes only:
// - forward declarations of structs and classes;
// - typedefs;
// - enums (maybe).
// It does *not* contain any struct or class definitions.

#ifndef js_TypeDecls_h
#define js_TypeDecls_h

#include <stddef.h>
#include <stdint.h>

#include "js-config.h"

typedef uint8_t jsbytecode;

class JSAtom;
struct JSContext;
class JSFunction;
class JSObject;
struct JSRuntime;
class JSScript;
class JSString;
struct JSFreeOp;

namespace js {
class TempAllocPolicy;
};  // namespace js

namespace JS {

struct PropertyKey;

typedef unsigned char Latin1Char;

class Symbol;
class BigInt;
union Value;

class Compartment;
class Realm;
struct Runtime;
class Zone;

template <typename T>
class Handle;
template <typename T>
class MutableHandle;
template <typename T>
class Rooted;
template <typename T>
class PersistentRooted;
template <typename T>
class RootedVector;
template <typename T, typename AllocPolicy = js::TempAllocPolicy>
class StackGCVector;

typedef Handle<JSFunction*> HandleFunction;
typedef Handle<PropertyKey> HandleId;
typedef Handle<JSObject*> HandleObject;
typedef Handle<JSScript*> HandleScript;
typedef Handle<JSString*> HandleString;
typedef Handle<JS::Symbol*> HandleSymbol;
typedef Handle<JS::BigInt*> HandleBigInt;
typedef Handle<Value> HandleValue;
typedef Handle<StackGCVector<Value>> HandleValueVector;
typedef Handle<StackGCVector<JSObject*>> HandleObjectVector;
typedef Handle<StackGCVector<JS::PropertyKey>> HandleIdVector;

typedef MutableHandle<JSFunction*> MutableHandleFunction;
typedef MutableHandle<PropertyKey> MutableHandleId;
typedef MutableHandle<JSObject*> MutableHandleObject;
typedef MutableHandle<JSScript*> MutableHandleScript;
typedef MutableHandle<JSString*> MutableHandleString;
typedef MutableHandle<JS::Symbol*> MutableHandleSymbol;
typedef MutableHandle<JS::BigInt*> MutableHandleBigInt;
typedef MutableHandle<Value> MutableHandleValue;
typedef MutableHandle<StackGCVector<Value>> MutableHandleValueVector;
typedef MutableHandle<StackGCVector<JSObject*>> MutableHandleObjectVector;
typedef MutableHandle<StackGCVector<JS::PropertyKey>> MutableHandleIdVector;

typedef Rooted<JSObject*> RootedObject;
typedef Rooted<JSFunction*> RootedFunction;
typedef Rooted<JSScript*> RootedScript;
typedef Rooted<JSString*> RootedString;
typedef Rooted<JS::Symbol*> RootedSymbol;
typedef Rooted<JS::BigInt*> RootedBigInt;
typedef Rooted<PropertyKey> RootedId;
typedef Rooted<JS::Value> RootedValue;

typedef RootedVector<JS::Value> RootedValueVector;
typedef RootedVector<JSObject*> RootedObjectVector;
typedef RootedVector<JS::PropertyKey> RootedIdVector;

typedef PersistentRooted<JSFunction*> PersistentRootedFunction;
typedef PersistentRooted<PropertyKey> PersistentRootedId;
typedef PersistentRooted<JSObject*> PersistentRootedObject;
typedef PersistentRooted<JSScript*> PersistentRootedScript;
typedef PersistentRooted<JSString*> PersistentRootedString;
typedef PersistentRooted<JS::Symbol*> PersistentRootedSymbol;
typedef PersistentRooted<JS::BigInt*> PersistentRootedBigInt;
typedef PersistentRooted<Value> PersistentRootedValue;

template <typename T>
using HandleVector = Handle<StackGCVector<T>>;
template <typename T>
using MutableHandleVector = MutableHandle<StackGCVector<T>>;
}  // namespace JS

using jsid = JS::PropertyKey;

#endif /* js_TypeDecls_h */
