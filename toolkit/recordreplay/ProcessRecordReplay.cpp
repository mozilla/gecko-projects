/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ProcessRecordReplay.h"

#include "ipc/ChildIPC.h"
#include "mozilla/Compression.h"
#include "mozilla/Maybe.h"
#include "mozilla/StaticMutex.h"
#include "Backtrace.h"
#include "Lock.h"
#include "MemorySnapshot.h"
#include "ProcessRedirect.h"
#include "ProcessRewind.h"
#include "Trigger.h"
#include "ValueIndex.h"
#include "WeakPointer.h"
#include "pratom.h"

#include <fcntl.h>
#include <unistd.h>

namespace mozilla {
namespace recordreplay {

static MOZ_NEVER_INLINE void
BusyWait()
{
  static volatile int value = 1;
  while (value) {}
}

///////////////////////////////////////////////////////////////////////////////
// Basic interface
///////////////////////////////////////////////////////////////////////////////

// Any reason the recording has been invalidated.
static const char* gRecordingInvalidReason;

File* gRecordingFile;
const char* gSnapshotMemoryPrefix;
const char* gSnapshotStackPrefix;

char* gInitializationFailureMessage;

static void
WriteRecordingMetadata(File* aFile)
{
#ifdef WIN32
  Stream& metadata = *aFile->OpenStream(StreamName::Main, 0);
  WriteLoadedLibraries(metadata);
#endif
}

static void
ReadRecordingMetadata()
{
#ifdef WIN32
  Stream& metadata = *gRecordingFile->OpenStream(StreamName::Main, 0);
  ReadLoadedLibraries(metadata);
#endif
}

static void DumpRecordingAssertions();

bool gInitialized;

// Whether to spew record/replay messages to stderr.
static bool gSpewEnabled;

extern "C" {

// This is called during NSPR initialization.
MOZ_EXPORT void
RecordReplayInterface_Initialize()
{
  const char* recordingFile = nullptr;
  if (TestEnv("RECORD")) {
    gIsRecording = gIsRecordingOrReplaying = gPRIsRecordingOrReplaying = true;
    recordingFile = getenv("RECORD");
    fprintf(stderr, "RECORDING %d %s\n", getpid(), recordingFile);
  } else if (TestEnv("REPLAY")) {
    gIsReplaying = gIsRecordingOrReplaying = gPRIsRecordingOrReplaying = true;
    recordingFile = getenv("REPLAY");
    fprintf(stderr, "REPLAYING %d %s\n", getpid(), recordingFile);
  } else if (TestEnv("MIDDLEMAN_RECORD") || TestEnv("MIDDLEMAN_REPLAY")) {
    gIsMiddleman = true;
    fprintf(stderr, "MIDDLEMAN %d\n", getpid());
  } else {
    MOZ_CRASH();
  }

  if (IsRecordingOrReplaying() && TestEnv("WAIT_AT_START")) {
    BusyWait();
  }

  if (IsMiddleman() && TestEnv("MIDDLEMAN_WAIT_AT_START")) {
    BusyWait();
  }

  if (TestEnv("RECORD_REPLAY_SPEW")) {
    gSpewEnabled = true;
  }

  EarlyInitializeRedirections();
  InitializeCallbacks();

  if (!IsRecordingOrReplaying()) {
    return;
  }

  char* tempFile = mktemp(strdup("/tmp/RecordingXXXXXX"));
  if (!strcmp(recordingFile, "*")) {
    recordingFile = tempFile;
  }

  gSnapshotMemoryPrefix = mktemp(strdup("/tmp/SnapshotMemoryXXXXXX"));
  gSnapshotStackPrefix = mktemp(strdup("/tmp/SnapshotStackXXXXXX"));

  InitializeCurrentTime();
  InitializeBacktraces();

  gRecordingFile = new File();
  if (!gRecordingFile->Open(recordingFile, (size_t) -1, IsRecording() ? File::WRITE : File::READ)) {
    gInitializationFailureMessage = strdup("Recording file is invalid/corrupt");
    return;
  }

  if (!InitializeRedirections()) {
    MOZ_RELEASE_ASSERT(gInitializationFailureMessage);
    return;
  }

  InitializeFiles(tempFile);

  Thread::InitializeThreads();

  Thread* thread = Thread::GetById(MainThreadId);
  MOZ_ASSERT(thread->Id() == MainThreadId);

  thread->BindToCurrent();
  thread->SetPassThrough(true);

  if (IsReplaying() && TestEnv("DUMP_RECORDING")) {
    DumpRecordingAssertions();
  }

  InitializeTriggers();
  InitializeWeakPointers();
  InitializeMemorySnapshots();
  Thread::SpawnAllThreads();
  Thread::InitializeOffThreadCallEvents();
  InitializeCountdownThread();

  if (IsReplaying()) {
    ReadRecordingMetadata();
    ReadWeakPointers();
  }

  thread->SetPassThrough(false);

  Lock::InitializeLocks();
  InitializeRewindState();

  gInitialized = true;
}

MOZ_EXPORT size_t
RecordReplayInterface_InternalRecordReplayValue(size_t aValue)
{
  MOZ_ASSERT(IsRecordingOrReplaying());

  if (AreThreadEventsPassedThrough()) {
    return aValue;
  }
  EnsureNotDivergedFromRecording();

  MOZ_RELEASE_ASSERT(!AreThreadEventsDisallowed());
  Thread* thread = Thread::Current();

  RecordReplayAssert("Value");
  thread->Events().RecordOrReplayThreadEvent(ThreadEvent::Value);
  thread->Events().RecordOrReplayValue(&aValue);
  return aValue;
}

MOZ_EXPORT void
RecordReplayInterface_InternalRecordReplayBytes(void* aData, size_t aSize)
{
  MOZ_ASSERT(IsRecordingOrReplaying());

  if (AreThreadEventsPassedThrough()) {
    return;
  }
  EnsureNotDivergedFromRecording();

  MOZ_RELEASE_ASSERT(!AreThreadEventsDisallowed());
  Thread* thread = Thread::Current();

  RecordReplayAssert("Bytes %d", (int) aSize);
  thread->Events().RecordOrReplayThreadEvent(ThreadEvent::Bytes);
  thread->Events().CheckInput(aSize);
  thread->Events().RecordOrReplayBytes(aData, aSize);
}

MOZ_EXPORT void
RecordReplayInterface_InternalInvalidateRecording(const char* aWhy)
{
  if (IsRecording()) {
    if (!gRecordingInvalidReason) {
      gRecordingInvalidReason = aWhy;
    }
    return;
  }

  child::ReportFatalError("Recording invalidated while replaying: %s", aWhy);
  Unreachable();
}

} // extern "C"

static void
CheckForInvalidRecording()
{
  if (gRecordingInvalidReason) {
    child::ReportFatalError("Recording is unusable: %s", gRecordingInvalidReason);
    Unreachable();
  }
}

void
SaveRecording(const char* aFilename)
{
  MOZ_RELEASE_ASSERT(Thread::CurrentIsMainThread());

  Thread::WaitForIdleThreads();

  CheckForInvalidRecording();

  {
    File file;
    file.Open(aFilename, (size_t) -1, File::WRITE);
    file.CloneFrom(*gRecordingFile);
    WriteWeakPointers(&file);
    WriteRecordingMetadata(&file);
  }

  child::NotifySavedRecording(aFilename);

  Thread::ResumeIdleThreads();
}

void
PrepareForFirstRecordingRewind()
{
  MOZ_RELEASE_ASSERT(IsRecording());

  // Note: this must be called while other threads are idle.
  MOZ_RELEASE_ASSERT(Thread::CurrentIsMainThread());

  CheckForInvalidRecording();

  char* filename = strdup(gRecordingFile->Filename());
  FileHandle fd = DirectOpenFile(filename, /* aWriting = */ false);

  // Finish up the recording file.
  WriteWeakPointers(gRecordingFile);
  WriteRecordingMetadata(gRecordingFile);
  gRecordingFile->Close();

  child::NotifySavedRecording(filename);
  free(filename);

  PrepareMemoryForFirstRecordingRewind(fd);

  // We are about to rewind, so there is nothing else to do.
}

void
FixupAfterRewind()
{
  if (!IsRecording()) {
    FixupFreeRegionsAfterRewind();
    return;
  }

  FileHandle fd = GetReplayFileAfterRecordingRewind();
  if (!fd) {
    return;
  }

  gIsRecording = false;
  gIsReplaying = true;

  FixupFreeRegionsAfterRewind();
  gRecordingFile->FixupAfterRecordingRewind(fd);
  Lock::FixupAfterRecordingRewind();
  FixupWeakPointersAfterRecordingRewind();
}

void Print(const char* aFormat, ...)
{
  va_list ap;
  va_start(ap, aFormat);
  char buf[2048];
  VsprintfLiteral(buf, aFormat, ap);
  va_end(ap);
  DirectPrint(buf);
}

void
PrintSpew(const char* aFormat, ...)
{
  if (!gSpewEnabled) {
    return;
  }
  va_list ap;
  va_start(ap, aFormat);
  char buf[2048];
  VsprintfLiteral(buf, aFormat, ap);
  va_end(ap);
  DirectPrint(buf);
}

///////////////////////////////////////////////////////////////////////////////
// Record/Replay Assertions
///////////////////////////////////////////////////////////////////////////////

static bool
BufferAppend(char** aBuf, size_t* aSize, const char* aText)
{
  size_t len = strlen(aText);
  if (len > *aSize) {
    return false;
  }
  memcpy(*aBuf, aText, len);
  (*aBuf) += len;
  (*aSize) -= len;
  return true;
}

static void
SetCurrentStackString(const char* aAssertion, char* aBuf, size_t aSize)
{
  void* addresses[50];
  size_t count = GetBacktrace(aAssertion, addresses, sizeof(addresses) / sizeof(addresses[0]));

  aSize--; // Reserve space for null terminator.
  for (size_t i = 0; i < count; i++) {
    if (!BufferAppend(&aBuf, &aSize, " ### ")) {
      break;
    }

    char buf[50];
    const char* name = SymbolName(addresses[i], buf, sizeof(buf));
    if (!BufferAppend(&aBuf, &aSize, name)) {
      break;
    }
  }

  *aBuf = 0;
}

// For debugging.
char*
PrintCurrentStackString()
{
  AutoEnsurePassThroughThreadEvents pt;
  char* buf = new char[1000];
  SetCurrentStackString("", buf, 1000);
  return buf;
}

static inline bool
AlwaysCaptureEventStack(const char* aText)
{
  return false;
}

// Bit included in assertion stream when the assertion is a text assert, rather
// than a byte sequence.
static const size_t AssertionBit = 1;

extern "C" {

MOZ_EXPORT void
RecordReplayInterface_InternalRecordReplayAssert(const char* aFormat, va_list aArgs)
{
#ifdef INCLUDE_RECORD_REPLAY_ASSERTIONS
  if (AreThreadEventsPassedThrough() || HasDivergedFromRecording()) {
    return;
  }

  MOZ_ASSERT(!AreThreadEventsDisallowed());
  Thread* thread = Thread::Current();

  // Record an assertion string consisting of the name of the assertion and
  // stack information about the current point of execution.
  char text[1024];
  VsprintfLiteral(text, aFormat, aArgs);
  if (IsRecording() && (thread->ShouldCaptureEventStacks() || AlwaysCaptureEventStack(text))) {
    AutoPassThroughThreadEvents pt;
    SetCurrentStackString(text, text + strlen(text), sizeof(text) - strlen(text));
  }

  size_t textLen = strlen(text);

  if (IsRecording()) {
    thread->Asserts().WriteScalar(thread->Events().StreamPosition());
    thread->Asserts().WriteScalar((textLen << 1) | AssertionBit);
    thread->Asserts().WriteBytes(text, textLen);
  } else {
    // While replaying, both the assertion's name and the current position in
    // the thread's events need to match up with what was recorded. The stack
    // portion of the assertion text does not need to match, it is used to help
    // track down the reason for the mismatch.
    bool match = true;
    size_t streamPos = thread->Asserts().ReadScalar();
    if (streamPos != thread->Events().StreamPosition()) {
      match = false;
    }
    size_t assertLen = thread->Asserts().ReadScalar() >> 1;

    char* buffer = thread->TakeBuffer(assertLen + 1);

    thread->Asserts().ReadBytes(buffer, assertLen);
    buffer[assertLen] = 0;

    if (assertLen < textLen || memcmp(buffer, text, textLen) != 0) {
      match = false;
    }

    if (!match) {
      for (size_t i = 0; i < Thread::NumRecentAsserts; i++) {
        if (thread->RecentAssert(i)) {
          Print("Thread %d Recent %d: %s\n",
                (int) thread->Id(), (int) i, thread->RecentAssert(i));
        }
      }

      {
        AutoPassThroughThreadEvents pt;
        SetCurrentStackString(text, text + strlen(text), sizeof(text) - strlen(text));
      }

      child::ReportFatalError("Assertion Mismatch: Thread %d\n"
                              "Recorded: %s [%d]\n"
                              "Replayed: %s [%d]\n",
                              (int) thread->Id(), buffer, (int) streamPos, text,
                              (int) thread->Events().StreamPosition());
      Unreachable();
    }

    thread->RestoreBuffer(buffer);

    // Push this assert onto the recent assertions in the thread.
    free(thread->RecentAssert(Thread::NumRecentAsserts - 1));
    for (size_t i = Thread::NumRecentAsserts - 1; i >= 1; i--) {
      thread->RecentAssert(i) = thread->RecentAssert(i - 1);
    }
    thread->RecentAssert(0) = strdup(text);
  }
#endif // INCLUDE_RECORD_REPLAY_ASSERTIONS
}

MOZ_EXPORT void
RecordReplayInterface_InternalRecordReplayAssertBytes(const void* aData, size_t aSize)
{
#ifdef INCLUDE_RECORD_REPLAY_ASSERTIONS
  RecordReplayAssert("AssertBytes");

  if (AreThreadEventsPassedThrough() || HasDivergedFromRecording()) {
    return;
  }

  MOZ_ASSERT(!AreThreadEventsDisallowed());
  Thread* thread = Thread::Current();

  if (IsRecording()) {
    thread->Asserts().WriteScalar(thread->Events().StreamPosition());
    thread->Asserts().WriteScalar(aSize << 1);
    thread->Asserts().WriteBytes(aData, aSize);
  } else {
    bool match = true;
    size_t streamPos = thread->Asserts().ReadScalar();
    if (streamPos != thread->Events().StreamPosition()) {
      match = false;
    }
    size_t oldSize = thread->Asserts().ReadScalar() >> 1;
    if (oldSize != aSize) {
      match = false;
    }

    char* buffer = thread->TakeBuffer(oldSize);

    thread->Asserts().ReadBytes(buffer, oldSize);
    if (match && memcmp(buffer, aData, oldSize) != 0) {
      match = false;
    }

    if (!match) {
      // On a byte mismatch, print out some of the mismatched bytes, up to a
      // cutoff in case there are many mismatched bytes.
      if (oldSize == aSize) {
        static const size_t MAX_MISMATCHES = 100;
        size_t mismatches = 0;
        for (size_t i = 0; i < aSize; i++) {
          if (((char*)aData)[i] != buffer[i]) {
            Print("Position %d: %d %d\n", (int) i, (int) buffer[i], (int) ((char*)aData)[i]);
            if (++mismatches == MAX_MISMATCHES) {
              break;
            }
          }
        }
        if (mismatches == MAX_MISMATCHES) {
          Print("Position ...\n");
        }
      }

      child::ReportFatalError("Byte Comparison Check Failed: Position %d %d Length %d %d\n",
                              (int) streamPos, (int) thread->Events().StreamPosition(),
                              (int) oldSize, (int) aSize);
      Unreachable();
    }

    thread->RestoreBuffer(buffer);
  }
#endif // INCLUDE_RECORD_REPLAY_ASSERTIONS
}

MOZ_EXPORT void
RecordReplayRust_Assert(const uint8_t* aBuffer)
{
  RecordReplayAssert("%s", (const char*) aBuffer);
}

MOZ_EXPORT void
RecordReplayRust_BeginPassThroughThreadEvents()
{
  BeginPassThroughThreadEvents();
}

MOZ_EXPORT void
RecordReplayRust_EndPassThroughThreadEvents()
{
  EndPassThroughThreadEvents();
}

} // extern "C"

static void
DumpRecordingAssertions()
{
  Thread* thread = Thread::Current();

  for (size_t id = MainThreadId; id <= MaxRecordedThreadId; id++) {
    Stream* asserts = gRecordingFile->OpenStream(StreamName::Assert, id);
    if (asserts->AtEnd()) {
      continue;
    }

    fprintf(stderr, "Thread Assertions %d:\n", (int) id);
    while (!asserts->AtEnd()) {
      (void) asserts->ReadScalar();
      size_t shiftedLen = asserts->ReadScalar();
      size_t assertLen = shiftedLen >> 1;

      char* buffer = thread->TakeBuffer(assertLen + 1);
      asserts->ReadBytes(buffer, assertLen);
      buffer[assertLen] = 0;

      if (shiftedLen & AssertionBit) {
        fprintf(stderr, "%s\n", buffer);
      }

      thread->RestoreBuffer(buffer);
    }
  }

  fprintf(stderr, "Done with assertions, exiting...\n");
  _exit(0);
}

static ValueIndex* gGenericThings;
static StaticMutexNotRecorded gGenericThingsMutex;

extern "C" {

MOZ_EXPORT void
RecordReplayInterface_InternalRegisterThing(void* aThing)
{
  if (AreThreadEventsPassedThrough()) {
    return;
  }

  AutoOrderedAtomicAccess at;
  StaticMutexAutoLock lock(gGenericThingsMutex);
  if (!gGenericThings) {
    gGenericThings = new ValueIndex();
  }
  if (gGenericThings->Contains(aThing)) {
    gGenericThings->Remove(aThing);
  }
  gGenericThings->Insert(aThing);
}

MOZ_EXPORT void
RecordReplayInterface_InternalUnregisterThing(void* aThing)
{
  StaticMutexAutoLock lock(gGenericThingsMutex);
  if (gGenericThings) {
    gGenericThings->Remove(aThing);
  }
}

MOZ_EXPORT size_t
RecordReplayInterface_InternalThingIndex(void* aThing)
{
  if (!aThing) {
    return 0;
  }
  StaticMutexAutoLock lock(gGenericThingsMutex);
  size_t index = 0;
  if (gGenericThings) {
    gGenericThings->MaybeGetIndex(aThing, &index);
  }
  return index;
}

MOZ_EXPORT const char*
RecordReplayInterface_InternalVirtualThingName(void* aThing)
{
  void* vtable = *(void**)aThing;
  const char* name = SymbolNameRaw(vtable);
  return name ? name : "(unknown)";
}

} // extern "C"

} // namespace recordreplay
} // namespace mozilla
