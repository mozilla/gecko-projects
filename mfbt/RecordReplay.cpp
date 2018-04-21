/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "RecordReplay.h"

#include "mozilla/Casting.h"

#include <stdlib.h>

#ifdef XP_MACOSX
#include <dlfcn.h>
#endif

#ifdef WIN32
#include <windows.h>
#endif

namespace mozilla {
namespace recordreplay {

#define FOR_EACH_INTERFACE(Macro)                               \
  Macro(InternalAreThreadEventsPassedThrough, bool, (), ())     \
  Macro(InternalAreThreadEventsDisallowed, bool, (), ())        \
  Macro(InternalRecordReplayValue, size_t, (size_t aValue), (aValue)) \
  Macro(InternalHasDivergedFromRecording, bool, (), ())         \
  Macro(InternalGeneratePLDHashTableCallbacks, const PLDHashTableOps*, \
        (const PLDHashTableOps* aOps), (aOps))                  \
  Macro(InternalUnwrapPLDHashTableCallbacks, const PLDHashTableOps*, \
        (const PLDHashTableOps* aOps), (aOps))                  \
  Macro(AllocateMemory, void*, (size_t aSize, AllocatedMemoryKind aKind), (aSize, aKind)) \
  Macro(InternalThingIndex, size_t, (void* aThing), (aThing))   \
  Macro(InternalVirtualThingName, const char*, (void* aThing), (aThing)) \
  Macro(NewCheckpoint, bool, (bool aTemporary), (aTemporary))

#define FOR_EACH_INTERFACE_VOID(Macro)                          \
  Macro(InternalBeginOrderedAtomicAccess, (), ())               \
  Macro(InternalEndOrderedAtomicAccess, (), ())                 \
  Macro(InternalBeginPassThroughThreadEvents, (), ())           \
  Macro(InternalEndPassThroughThreadEvents, (), ())             \
  Macro(InternalBeginDisallowThreadEvents, (), ())              \
  Macro(InternalEndDisallowThreadEvents, (), ())                \
  Macro(InternalBeginCaptureEventStacks, (), ())                \
  Macro(InternalEndCaptureEventStacks, (), ())                  \
  Macro(InternalRecordReplayBytes,                              \
        (void* aData, size_t aSize), (aData, aSize))            \
  Macro(DisallowUnhandledDivergeFromRecording, (), ())          \
  Macro(NotifyUnrecordedWait,                                   \
        (const std::function<void()>& aCallback), (aCallback))  \
  Macro(MaybeWaitForCheckpointSave, (), ())                     \
  Macro(InternalInvalidateRecording, (const char* aWhy), (aWhy)) \
  Macro(InternalDestroyPLDHashTableCallbacks,                   \
        (const PLDHashTableOps* aOps), (aOps))                  \
  Macro(InternalMovePLDHashTableContents,                       \
        (const PLDHashTableOps* aFirstOps, const PLDHashTableOps* aSecondOps), \
        (aFirstOps, aSecondOps))                                \
  Macro(SetCheckpointHooks,                                     \
        (BeforeCheckpointHook aBefore, AfterCheckpointHook aAfter), \
        (aBefore, aAfter))                                      \
  Macro(ResumeExecution, (), ())                                \
  Macro(RestoreCheckpointAndResume, (const CheckpointId& aId), (aId)) \
  Macro(DivergeFromRecording, (), ())                           \
  Macro(DeallocateMemory,                                       \
        (void* aAddress, size_t aSize, AllocatedMemoryKind aKind), (aAddress, aSize, aKind)) \
  Macro(SetWeakPointerJSRoot,                                   \
        (const void* aPtr, void* aJSObj), (aPtr, aJSObj))       \
  Macro(RegisterTrigger,                                        \
        (void* aObj, const std::function<void()>& aCallback),   \
        (aObj, aCallback))                                      \
  Macro(UnregisterTrigger,                                      \
        (void* aObj), (aObj))                                   \
  Macro(ActivateTrigger, (void* aObj), (aObj))                  \
  Macro(ExecuteTriggers, (), ())                                \
  Macro(InternalRecordReplayAssert, (const char* aFormat, va_list aArgs), (aFormat, aArgs)) \
  Macro(InternalRecordReplayAssertBytes,                        \
        (const void* aData, size_t aSize), (aData, aSize))      \
  Macro(InternalRegisterThing, (void* aThing), (aThing))        \
  Macro(InternalUnregisterThing, (void* aThing), (aThing))      \
  Macro(InternalRecordReplayDirective, (long aDirective), (aDirective))

#define DECLARE_SYMBOL(aName, aReturnType, aFormals, _) \
  static aReturnType (*gPtr ##aName) aFormals;
#define DECLARE_SYMBOL_VOID(aName, aFormals, _)  DECLARE_SYMBOL(aName, void, aFormals, _)

FOR_EACH_INTERFACE(DECLARE_SYMBOL)
FOR_EACH_INTERFACE_VOID(DECLARE_SYMBOL_VOID)

#undef DECLARE_SYMBOL
#undef DECLARE_SYMBOL_VOID

static void*
LoadSymbol(const char* aName)
{
  void* rv;
#if defined(XP_MACOSX)
  rv = dlsym(RTLD_DEFAULT, aName);
#elif defined(WIN32)
  rv = GetProcAddress(LoadLibraryA("xul.dll"), aName);
#else
#error "Unknown platform"
#endif
  MOZ_RELEASE_ASSERT(rv);
  return rv;
}

void
InitializeCallbacks()
{
#define INIT_SYMBOL(aName, _1, _2, _3)                  \
  BitwiseCast(LoadSymbol("RecordReplayInterface_" #aName), &gPtr ##aName);
#define INIT_SYMBOL_VOID(aName, _2, _3)  INIT_SYMBOL(aName, void, _2, _3)

FOR_EACH_INTERFACE(INIT_SYMBOL)
FOR_EACH_INTERFACE_VOID(INIT_SYMBOL_VOID)

#undef INIT_SYMBOL
#undef INIT_SYMBOL_VOID
}

#define DEFINE_WRAPPER(aName, aReturnType, aFormals, aActuals)  \
  aReturnType aName aFormals                                    \
  {                                                             \
    MOZ_ASSERT(IsRecordingOrReplaying() || IsMiddleman());      \
    return gPtr ##aName aActuals;                               \
  }

#define DEFINE_WRAPPER_VOID(aName, aFormals, aActuals)          \
  void aName aFormals                                           \
  {                                                             \
    MOZ_ASSERT(IsRecordingOrReplaying() || IsMiddleman());      \
    gPtr ##aName aActuals;                                      \
  }

FOR_EACH_INTERFACE(DEFINE_WRAPPER)
FOR_EACH_INTERFACE_VOID(DEFINE_WRAPPER_VOID)

#undef DEFINE_WRAPPER
#undef DEFINE_WRAPPER_VOID

bool gIsRecordingOrReplaying;
bool gIsRecording;
bool gIsReplaying;
bool gIsMiddleman;

} // recordreplay
} // mozilla
