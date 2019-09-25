/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: set ts=8 sts=2 et sw=2 tw=80:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Writable stream default controller abstract operations. */

#ifndef builtin_streams_WritableStreamDefaultControllerOperations_h
#define builtin_streams_WritableStreamDefaultControllerOperations_h

#include "mozilla/Attributes.h"  // MOZ_MUST_USE

#include "js/RootingAPI.h"  // JS::Handle
#include "js/Value.h"       // JS::Value

struct JSContext;

namespace js {

class WritableStream;

extern MOZ_MUST_USE bool SetUpWritableStreamDefaultControllerFromUnderlyingSink(
    JSContext* cx, JS::Handle<WritableStream*> stream,
    JS::Handle<JS::Value> underlyingSink, double highWaterMark,
    JS::Handle<JS::Value> sizeAlgorithm);

}  // namespace js

#endif  // builtin_streams_WritableStreamDefaultControllerOperations_h
