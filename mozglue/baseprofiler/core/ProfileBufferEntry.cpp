/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "BaseProfiler.h"

#ifdef MOZ_BASE_PROFILER

#  include "ProfileBufferEntry.h"

#  include "platform.h"
#  include "ProfileBuffer.h"

#  include "mozilla/Logging.h"
#  include "mozilla/Sprintf.h"
#  include "mozilla/StackWalk.h"

#  include <ostream>

namespace mozilla {
namespace baseprofiler {

////////////////////////////////////////////////////////////////////////
// BEGIN ProfileBufferEntry

ProfileBufferEntry::ProfileBufferEntry()
    : mKind(Kind::INVALID), mStorage{0, 0, 0, 0, 0, 0, 0, 0} {}

// aString must be a static string.
ProfileBufferEntry::ProfileBufferEntry(Kind aKind, const char* aString)
    : mKind(aKind) {
  memcpy(mStorage, &aString, sizeof(aString));
}

ProfileBufferEntry::ProfileBufferEntry(Kind aKind, char aChars[kNumChars])
    : mKind(aKind) {
  memcpy(mStorage, aChars, kNumChars);
}

ProfileBufferEntry::ProfileBufferEntry(Kind aKind, void* aPtr) : mKind(aKind) {
  memcpy(mStorage, &aPtr, sizeof(aPtr));
}

ProfileBufferEntry::ProfileBufferEntry(Kind aKind, ProfilerMarker* aMarker)
    : mKind(aKind) {
  memcpy(mStorage, &aMarker, sizeof(aMarker));
}

ProfileBufferEntry::ProfileBufferEntry(Kind aKind, double aDouble)
    : mKind(aKind) {
  memcpy(mStorage, &aDouble, sizeof(aDouble));
}

ProfileBufferEntry::ProfileBufferEntry(Kind aKind, int aInt) : mKind(aKind) {
  memcpy(mStorage, &aInt, sizeof(aInt));
}

ProfileBufferEntry::ProfileBufferEntry(Kind aKind, int64_t aInt64)
    : mKind(aKind) {
  memcpy(mStorage, &aInt64, sizeof(aInt64));
}

ProfileBufferEntry::ProfileBufferEntry(Kind aKind, uint64_t aUint64)
    : mKind(aKind) {
  memcpy(mStorage, &aUint64, sizeof(aUint64));
}

const char* ProfileBufferEntry::GetString() const {
  const char* result;
  memcpy(&result, mStorage, sizeof(result));
  return result;
}

void* ProfileBufferEntry::GetPtr() const {
  void* result;
  memcpy(&result, mStorage, sizeof(result));
  return result;
}

ProfilerMarker* ProfileBufferEntry::GetMarker() const {
  ProfilerMarker* result;
  memcpy(&result, mStorage, sizeof(result));
  return result;
}

double ProfileBufferEntry::GetDouble() const {
  double result;
  memcpy(&result, mStorage, sizeof(result));
  return result;
}

int ProfileBufferEntry::GetInt() const {
  int result;
  memcpy(&result, mStorage, sizeof(result));
  return result;
}

int64_t ProfileBufferEntry::GetInt64() const {
  int64_t result;
  memcpy(&result, mStorage, sizeof(result));
  return result;
}

uint64_t ProfileBufferEntry::GetUint64() const {
  uint64_t result;
  memcpy(&result, mStorage, sizeof(result));
  return result;
}

void ProfileBufferEntry::CopyCharsInto(char (&aOutArray)[kNumChars]) const {
  memcpy(aOutArray, mStorage, kNumChars);
}

// END ProfileBufferEntry
////////////////////////////////////////////////////////////////////////

// As mentioned in ProfileBufferEntry.h, the JSON format contains many
// arrays whose elements are laid out according to various schemas to help
// de-duplication. This RAII class helps write these arrays by keeping track of
// the last non-null element written and adding the appropriate number of null
// elements when writing new non-null elements. It also automatically opens and
// closes an array element on the given JSON writer.
//
// You grant the AutoArraySchemaWriter exclusive access to the JSONWriter and
// the UniqueJSONStrings objects for the lifetime of AutoArraySchemaWriter. Do
// not access them independently while the AutoArraySchemaWriter is alive.
// If you need to add complex objects, call FreeFormElement(), which will give
// you temporary access to the writer.
//
// Example usage:
//
//     // Define the schema of elements in this type of array: [FOO, BAR, BAZ]
//     enum Schema : uint32_t {
//       FOO = 0,
//       BAR = 1,
//       BAZ = 2
//     };
//
//     AutoArraySchemaWriter writer(someJsonWriter, someUniqueStrings);
//     if (shouldWriteFoo) {
//       writer.IntElement(FOO, getFoo());
//     }
//     ... etc ...
//
//     The elements need to be added in-order.
class MOZ_RAII AutoArraySchemaWriter {
 public:
  AutoArraySchemaWriter(SpliceableJSONWriter& aWriter,
                        UniqueJSONStrings& aStrings)
      : mJSONWriter(aWriter), mStrings(&aStrings), mNextFreeIndex(0) {
    mJSONWriter.StartArrayElement(SpliceableJSONWriter::SingleLineStyle);
  }

  explicit AutoArraySchemaWriter(SpliceableJSONWriter& aWriter)
      : mJSONWriter(aWriter), mStrings(nullptr), mNextFreeIndex(0) {
    mJSONWriter.StartArrayElement(SpliceableJSONWriter::SingleLineStyle);
  }

  ~AutoArraySchemaWriter() { mJSONWriter.EndArray(); }

  template <typename T>
  void IntElement(uint32_t aIndex, T aValue) {
    static_assert(!IsSame<T, uint64_t>::value,
                  "Narrowing uint64 -> int64 conversion not allowed");
    FillUpTo(aIndex);
    mJSONWriter.IntElement(static_cast<int64_t>(aValue));
  }

  void DoubleElement(uint32_t aIndex, double aValue) {
    FillUpTo(aIndex);
    mJSONWriter.DoubleElement(aValue);
  }

  void BoolElement(uint32_t aIndex, bool aValue) {
    FillUpTo(aIndex);
    mJSONWriter.BoolElement(aValue);
  }

  void StringElement(uint32_t aIndex, const char* aValue) {
    MOZ_RELEASE_ASSERT(mStrings);
    FillUpTo(aIndex);
    mStrings->WriteElement(mJSONWriter, aValue);
  }

  // Write an element using a callback that takes a JSONWriter& and a
  // UniqueJSONStrings&.
  template <typename LambdaT>
  void FreeFormElement(uint32_t aIndex, LambdaT aCallback) {
    MOZ_RELEASE_ASSERT(mStrings);
    FillUpTo(aIndex);
    aCallback(mJSONWriter, *mStrings);
  }

 private:
  void FillUpTo(uint32_t aIndex) {
    MOZ_ASSERT(aIndex >= mNextFreeIndex);
    mJSONWriter.NullElements(aIndex - mNextFreeIndex);
    mNextFreeIndex = aIndex + 1;
  }

  SpliceableJSONWriter& mJSONWriter;
  UniqueJSONStrings* mStrings;
  uint32_t mNextFreeIndex;
};

UniqueJSONStrings::UniqueJSONStrings() { mStringTableWriter.StartBareList(); }

UniqueJSONStrings::UniqueJSONStrings(const UniqueJSONStrings& aOther) {
  mStringTableWriter.StartBareList();
  uint32_t count = mStringHashToIndexMap.count();
  if (count != 0) {
    MOZ_RELEASE_ASSERT(mStringHashToIndexMap.reserve(count));
    for (auto iter = aOther.mStringHashToIndexMap.iter(); !iter.done();
         iter.next()) {
      mStringHashToIndexMap.putNewInfallible(iter.get().key(),
                                             iter.get().value());
    }
    UniquePtr<char[]> stringTableJSON =
        aOther.mStringTableWriter.WriteFunc()->CopyData();
    mStringTableWriter.Splice(stringTableJSON.get());
  }
}

uint32_t UniqueJSONStrings::GetOrAddIndex(const char* aStr) {
  uint32_t count = mStringHashToIndexMap.count();
  HashNumber hash = HashString(aStr);
  auto entry = mStringHashToIndexMap.lookupForAdd(hash);
  if (entry) {
    MOZ_ASSERT(entry->value() < count);
    return entry->value();
  }

  MOZ_RELEASE_ASSERT(mStringHashToIndexMap.add(entry, hash, count));
  mStringTableWriter.StringElement(aStr);
  return count;
}

UniqueStacks::StackKey UniqueStacks::BeginStack(const FrameKey& aFrame) {
  return StackKey(GetOrAddFrameIndex(aFrame));
}

UniqueStacks::StackKey UniqueStacks::AppendFrame(const StackKey& aStack,
                                                 const FrameKey& aFrame) {
  return StackKey(aStack, GetOrAddStackIndex(aStack),
                  GetOrAddFrameIndex(aFrame));
}

bool UniqueStacks::FrameKey::NormalFrameData::operator==(
    const NormalFrameData& aOther) const {
  return mLocation == aOther.mLocation &&
         mRelevantForJS == aOther.mRelevantForJS && mLine == aOther.mLine &&
         mColumn == aOther.mColumn && mCategoryPair == aOther.mCategoryPair;
}

UniqueStacks::UniqueStacks() : mUniqueStrings(MakeUnique<UniqueJSONStrings>()) {
  mFrameTableWriter.StartBareList();
  mStackTableWriter.StartBareList();
}

uint32_t UniqueStacks::GetOrAddStackIndex(const StackKey& aStack) {
  uint32_t count = mStackToIndexMap.count();
  auto entry = mStackToIndexMap.lookupForAdd(aStack);
  if (entry) {
    MOZ_ASSERT(entry->value() < count);
    return entry->value();
  }

  MOZ_RELEASE_ASSERT(mStackToIndexMap.add(entry, aStack, count));
  StreamStack(aStack);
  return count;
}

template <typename RangeT, typename PosT>
struct PositionInRangeComparator final {
  bool Equals(const RangeT& aRange, PosT aPos) const {
    return aRange.mRangeStart <= aPos && aPos < aRange.mRangeEnd;
  }

  bool LessThan(const RangeT& aRange, PosT aPos) const {
    return aRange.mRangeEnd <= aPos;
  }
};

uint32_t UniqueStacks::GetOrAddFrameIndex(const FrameKey& aFrame) {
  uint32_t count = mFrameToIndexMap.count();
  auto entry = mFrameToIndexMap.lookupForAdd(aFrame);
  if (entry) {
    MOZ_ASSERT(entry->value() < count);
    return entry->value();
  }

  MOZ_RELEASE_ASSERT(mFrameToIndexMap.add(entry, aFrame, count));
  StreamNonJITFrame(aFrame);
  return count;
}

void UniqueStacks::SpliceFrameTableElements(SpliceableJSONWriter& aWriter) {
  mFrameTableWriter.EndBareList();
  aWriter.TakeAndSplice(mFrameTableWriter.WriteFunc());
}

void UniqueStacks::SpliceStackTableElements(SpliceableJSONWriter& aWriter) {
  mStackTableWriter.EndBareList();
  aWriter.TakeAndSplice(mStackTableWriter.WriteFunc());
}

void UniqueStacks::StreamStack(const StackKey& aStack) {
  enum Schema : uint32_t { PREFIX = 0, FRAME = 1 };

  AutoArraySchemaWriter writer(mStackTableWriter, *mUniqueStrings);
  if (aStack.mPrefixStackIndex.isSome()) {
    writer.IntElement(PREFIX, *aStack.mPrefixStackIndex);
  }
  writer.IntElement(FRAME, aStack.mFrameIndex);
}

void UniqueStacks::StreamNonJITFrame(const FrameKey& aFrame) {
  using NormalFrameData = FrameKey::NormalFrameData;

  enum Schema : uint32_t {
    LOCATION = 0,
    RELEVANT_FOR_JS = 1,
    IMPLEMENTATION = 2,
    OPTIMIZATIONS = 3,
    LINE = 4,
    COLUMN = 5,
    CATEGORY = 6,
    SUBCATEGORY = 7
  };

  AutoArraySchemaWriter writer(mFrameTableWriter, *mUniqueStrings);

  const NormalFrameData& data = aFrame.mData.as<NormalFrameData>();
  writer.StringElement(LOCATION, data.mLocation.c_str());
  writer.BoolElement(RELEVANT_FOR_JS, data.mRelevantForJS);
  if (data.mLine.isSome()) {
    writer.IntElement(LINE, *data.mLine);
  }
  if (data.mColumn.isSome()) {
    writer.IntElement(COLUMN, *data.mColumn);
  }
  if (data.mCategoryPair.isSome()) {
    const ProfilingCategoryPairInfo& info =
        GetProfilingCategoryPairInfo(*data.mCategoryPair);
    writer.IntElement(CATEGORY, uint32_t(info.mCategory));
    writer.IntElement(SUBCATEGORY, info.mSubcategoryIndex);
  }
}

struct CStringWriteFunc : public JSONWriteFunc {
  std::string& mBuffer;  // The struct must not outlive this buffer
  explicit CStringWriteFunc(std::string& aBuffer) : mBuffer(aBuffer) {}

  void Write(const char* aStr) override { mBuffer += aStr; }
};

struct ProfileSample {
  uint32_t mStack;
  double mTime;
  Maybe<double> mResponsiveness;
  Maybe<double> mRSS;
  Maybe<double> mUSS;
};

static void WriteSample(SpliceableJSONWriter& aWriter,
                        UniqueJSONStrings& aUniqueStrings,
                        const ProfileSample& aSample) {
  enum Schema : uint32_t {
    STACK = 0,
    TIME = 1,
    RESPONSIVENESS = 2,
    RSS = 3,
    USS = 4
  };

  AutoArraySchemaWriter writer(aWriter, aUniqueStrings);

  writer.IntElement(STACK, aSample.mStack);

  writer.DoubleElement(TIME, aSample.mTime);

  if (aSample.mResponsiveness.isSome()) {
    writer.DoubleElement(RESPONSIVENESS, *aSample.mResponsiveness);
  }

  if (aSample.mRSS.isSome()) {
    writer.DoubleElement(RSS, *aSample.mRSS);
  }

  if (aSample.mUSS.isSome()) {
    writer.DoubleElement(USS, *aSample.mUSS);
  }
}

class EntryGetter {
 public:
  explicit EntryGetter(const ProfileBuffer& aBuffer,
                       uint64_t aInitialReadPos = 0)
      : mBuffer(aBuffer), mReadPos(aBuffer.mRangeStart) {
    if (aInitialReadPos != 0) {
      MOZ_RELEASE_ASSERT(aInitialReadPos >= aBuffer.mRangeStart &&
                         aInitialReadPos <= aBuffer.mRangeEnd);
      mReadPos = aInitialReadPos;
    }
  }

  bool Has() const { return mReadPos != mBuffer.mRangeEnd; }
  const ProfileBufferEntry& Get() const { return mBuffer.GetEntry(mReadPos); }
  void Next() { mReadPos++; }
  uint64_t CurPos() { return mReadPos; }

 private:
  const ProfileBuffer& mBuffer;
  uint64_t mReadPos;
};

// The following grammar shows legal sequences of profile buffer entries.
// The sequences beginning with a ThreadId entry are known as "samples".
//
// (
//   ( /* Samples */
//     ThreadId
//     Time
//     ( NativeLeafAddr
//     | Label FrameFlags? DynamicStringFragment* LineNumber? CategoryPair?
//     | JitReturnAddr
//     )+
//     Marker*
//     Responsiveness?
//     ResidentMemory?
//     UnsharedMemory?
//   )
//   | ( ResidentMemory UnsharedMemory? Time)  /* Memory */
//   | ( /* Counters */
//       CounterId
//       Time
//       (
//         CounterKey
//         Count
//         Number?
//       )*
//     )
//   | CollectionStart
//   | CollectionEnd
//   | Pause
//   | Resume
//   | ( ProfilerOverheadTime /* Sampling start timestamp */
//       ProfilerOverheadDuration /* Lock acquisition */
//       ProfilerOverheadDuration /* Expired markers cleaning */
//       ProfilerOverheadDuration /* Counters */
//       ProfilerOverheadDuration /* Threads */
//     )
// )*
//
// The most complicated part is the stack entry sequence that begins with
// Label. Here are some examples.
//
// - ProfilingStack frames without a dynamic string:
//
//     Label("js::RunScript")
//     CategoryPair(ProfilingCategoryPair::JS)
//
//     Label("XREMain::XRE_main")
//     LineNumber(4660)
//     CategoryPair(ProfilingCategoryPair::OTHER)
//
//     Label("ElementRestyler::ComputeStyleChangeFor")
//     LineNumber(3003)
//     CategoryPair(ProfilingCategoryPair::CSS)
//
// - ProfilingStack frames with a dynamic string:
//
//     Label("nsObserverService::NotifyObservers")
//     FrameFlags(uint64_t(ProfilingStackFrame::Flags::IS_LABEL_FRAME))
//     DynamicStringFragment("domwindo")
//     DynamicStringFragment("wopened")
//     LineNumber(291)
//     CategoryPair(ProfilingCategoryPair::OTHER)
//
//     Label("")
//     FrameFlags(uint64_t(ProfilingStackFrame::Flags::IS_JS_FRAME))
//     DynamicStringFragment("closeWin")
//     DynamicStringFragment("dow (chr")
//     DynamicStringFragment("ome://gl")
//     DynamicStringFragment("obal/con")
//     DynamicStringFragment("tent/glo")
//     DynamicStringFragment("balOverl")
//     DynamicStringFragment("ay.js:5)")
//     DynamicStringFragment("")          # this string holds the closing '\0'
//     LineNumber(25)
//     CategoryPair(ProfilingCategoryPair::JS)
//
//     Label("")
//     FrameFlags(uint64_t(ProfilingStackFrame::Flags::IS_JS_FRAME))
//     DynamicStringFragment("bound (s")
//     DynamicStringFragment("elf-host")
//     DynamicStringFragment("ed:914)")
//     LineNumber(945)
//     CategoryPair(ProfilingCategoryPair::JS)
//
// - A profiling stack frame with a dynamic string, but with privacy enabled:
//
//     Label("nsObserverService::NotifyObservers")
//     FrameFlags(uint64_t(ProfilingStackFrame::Flags::IS_LABEL_FRAME))
//     DynamicStringFragment("(private")
//     DynamicStringFragment(")")
//     LineNumber(291)
//     CategoryPair(ProfilingCategoryPair::OTHER)
//
// - A profiling stack frame with an overly long dynamic string:
//
//     Label("")
//     FrameFlags(uint64_t(ProfilingStackFrame::Flags::IS_LABEL_FRAME))
//     DynamicStringFragment("(too lon")
//     DynamicStringFragment("g)")
//     LineNumber(100)
//     CategoryPair(ProfilingCategoryPair::NETWORK)
//
// - A wasm JIT frame:
//
//     Label("")
//     FrameFlags(uint64_t(0))
//     DynamicStringFragment("wasm-fun")
//     DynamicStringFragment("ction[87")
//     DynamicStringFragment("36] (blo")
//     DynamicStringFragment("b:http:/")
//     DynamicStringFragment("/webasse")
//     DynamicStringFragment("mbly.org")
//     DynamicStringFragment("/3dc5759")
//     DynamicStringFragment("4-ce58-4")
//     DynamicStringFragment("626-975b")
//     DynamicStringFragment("-08ad116")
//     DynamicStringFragment("30bc1:38")
//     DynamicStringFragment("29856)")
//
// - A JS frame in a synchronous sample:
//
//     Label("")
//     FrameFlags(uint64_t(ProfilingStackFrame::Flags::IS_LABEL_FRAME))
//     DynamicStringFragment("u (https")
//     DynamicStringFragment("://perf-")
//     DynamicStringFragment("html.io/")
//     DynamicStringFragment("ac0da204")
//     DynamicStringFragment("aaa44d75")
//     DynamicStringFragment("a800.bun")
//     DynamicStringFragment("dle.js:2")
//     DynamicStringFragment("5)")

// Because this is a format entirely internal to the Profiler, any parsing
// error indicates a bug in the ProfileBuffer writing or the parser itself,
// or possibly flaky hardware.
#  define ERROR_AND_CONTINUE(msg)                            \
    {                                                        \
      fprintf(stderr, "ProfileBuffer parse error: %s", msg); \
      MOZ_ASSERT(false, msg);                                \
      continue;                                              \
    }

void ProfileBuffer::StreamSamplesToJSON(SpliceableJSONWriter& aWriter,
                                        int aThreadId, double aSinceTime,
                                        UniqueStacks& aUniqueStacks) const {
  UniquePtr<char[]> dynStrBuf = MakeUnique<char[]>(kMaxFrameKeyLength);

  EntryGetter e(*this);

  for (;;) {
    // This block skips entries until we find the start of the next sample.
    // This is useful in three situations.
    //
    // - The circular buffer overwrites old entries, so when we start parsing
    //   we might be in the middle of a sample, and we must skip forward to the
    //   start of the next sample.
    //
    // - We skip samples that don't have an appropriate ThreadId or Time.
    //
    // - We skip range Pause, Resume, CollectionStart, Marker, Counter
    //   and CollectionEnd entries between samples.
    while (e.Has()) {
      if (e.Get().IsThreadId()) {
        break;
      } else {
        e.Next();
      }
    }

    if (!e.Has()) {
      break;
    }

    if (e.Get().IsThreadId()) {
      int threadId = e.Get().GetInt();
      e.Next();

      // Ignore samples that are for the wrong thread.
      if (threadId != aThreadId) {
        continue;
      }
    } else {
      // Due to the skip_to_next_sample block above, if we have an entry here
      // it must be a ThreadId entry.
      MOZ_CRASH();
    }

    ProfileSample sample;

    if (e.Has() && e.Get().IsTime()) {
      sample.mTime = e.Get().GetDouble();
      e.Next();

      // Ignore samples that are too old.
      if (sample.mTime < aSinceTime) {
        continue;
      }
    } else {
      ERROR_AND_CONTINUE("expected a Time entry");
    }

    UniqueStacks::StackKey stack =
        aUniqueStacks.BeginStack(UniqueStacks::FrameKey("(root)"));

    int numFrames = 0;
    while (e.Has()) {
      if (e.Get().IsNativeLeafAddr()) {
        numFrames++;

        void* pc = e.Get().GetPtr();
        e.Next();

        static const uint32_t BUF_SIZE = 256;
        char buf[BUF_SIZE];

        // Bug 753041: We need a double cast here to tell GCC that we don't
        // want to sign extend 32-bit addresses starting with 0xFXXXXXX.
        unsigned long long pcULL = (unsigned long long)(uintptr_t)pc;
        SprintfLiteral(buf, "%#llx", pcULL);

        // If the "MOZ_BASE_PROFILER_SYMBOLICATE" env-var is set, we add a local
        // symbolication description to the PC address. This is off by default,
        // and mainly intended for local development.
        static const bool preSymbolicate = []() {
          const char* symbolicate = getenv("MOZ_BASE_PROFILER_SYMBOLICATE");
          return symbolicate && symbolicate[0] != '\0';
        }();
        if (preSymbolicate) {
          MozCodeAddressDetails details;
          if (MozDescribeCodeAddress(pc, &details)) {
            // Replace \0 terminator with space.
            const uint32_t pcLen = strlen(buf);
            buf[pcLen] = ' ';
            // Add description after space. Note: Using a frame number of 0,
            // as using `numFrames` wouldn't help here, and would prevent
            // combining same function calls that happen at different depths.
            // TODO: Remove unsightly "#00: " if too annoying. :-)
            MozFormatCodeAddressDetails(buf + pcLen + 1, BUF_SIZE - (pcLen + 1),
                                        0, pc, &details);
          }
        }

        stack = aUniqueStacks.AppendFrame(stack, UniqueStacks::FrameKey(buf));

      } else if (e.Get().IsLabel()) {
        numFrames++;

        const char* label = e.Get().GetString();
        e.Next();

        using FrameFlags = ProfilingStackFrame::Flags;
        uint32_t frameFlags = 0;
        if (e.Has() && e.Get().IsFrameFlags()) {
          frameFlags = uint32_t(e.Get().GetUint64());
          e.Next();
        }

        bool relevantForJS = frameFlags & uint32_t(FrameFlags::RELEVANT_FOR_JS);

        // Copy potential dynamic string fragments into dynStrBuf, so that
        // dynStrBuf will then contain the entire dynamic string.
        size_t i = 0;
        dynStrBuf[0] = '\0';
        while (e.Has()) {
          if (e.Get().IsDynamicStringFragment()) {
            char chars[ProfileBufferEntry::kNumChars];
            e.Get().CopyCharsInto(chars);
            for (char c : chars) {
              if (i < kMaxFrameKeyLength) {
                dynStrBuf[i] = c;
                i++;
              }
            }
            e.Next();
          } else {
            break;
          }
        }
        dynStrBuf[kMaxFrameKeyLength - 1] = '\0';
        bool hasDynamicString = (i != 0);

        std::string frameLabel;
        if (label[0] != '\0' && hasDynamicString) {
          if (frameFlags & uint32_t(FrameFlags::STRING_TEMPLATE_METHOD)) {
            frameLabel += label;
            frameLabel += '.';
            frameLabel += dynStrBuf.get();
          } else if (frameFlags &
                     uint32_t(FrameFlags::STRING_TEMPLATE_GETTER)) {
            frameLabel += "get ";
            frameLabel += label;
            frameLabel += '.';
            frameLabel += dynStrBuf.get();
          } else if (frameFlags &
                     uint32_t(FrameFlags::STRING_TEMPLATE_SETTER)) {
            frameLabel += "set ";
            frameLabel += label;
            frameLabel += '.';
            frameLabel += dynStrBuf.get();
          } else {
            frameLabel += label;
            frameLabel += ' ';
            frameLabel += dynStrBuf.get();
          }
        } else if (hasDynamicString) {
          frameLabel += dynStrBuf.get();
        } else {
          frameLabel += label;
        }

        Maybe<unsigned> line;
        if (e.Has() && e.Get().IsLineNumber()) {
          line = Some(unsigned(e.Get().GetInt()));
          e.Next();
        }

        Maybe<unsigned> column;
        if (e.Has() && e.Get().IsColumnNumber()) {
          column = Some(unsigned(e.Get().GetInt()));
          e.Next();
        }

        Maybe<ProfilingCategoryPair> categoryPair;
        if (e.Has() && e.Get().IsCategoryPair()) {
          categoryPair =
              Some(ProfilingCategoryPair(uint32_t(e.Get().GetInt())));
          e.Next();
        }

        stack = aUniqueStacks.AppendFrame(
            stack, UniqueStacks::FrameKey(std::move(frameLabel), relevantForJS,
                                          line, column, categoryPair));

      } else {
        break;
      }
    }

    if (numFrames == 0) {
      // It is possible to have empty stacks if native stackwalking is
      // disabled. Skip samples with empty stacks. (See Bug 1497985).
      // Thus, don't use ERROR_AND_CONTINUE, but just continue.
      continue;
    }

    sample.mStack = aUniqueStacks.GetOrAddStackIndex(stack);

    // Skip over the markers. We process them in StreamMarkersToJSON().
    while (e.Has()) {
      if (e.Get().IsMarker()) {
        e.Next();
      } else {
        break;
      }
    }

    if (e.Has() && e.Get().IsResponsiveness()) {
      sample.mResponsiveness = Some(e.Get().GetDouble());
      e.Next();
    }

    if (e.Has() && e.Get().IsResidentMemory()) {
      sample.mRSS = Some(e.Get().GetDouble());
      e.Next();
    }

    if (e.Has() && e.Get().IsUnsharedMemory()) {
      sample.mUSS = Some(e.Get().GetDouble());
      e.Next();
    }

    WriteSample(aWriter, *aUniqueStacks.mUniqueStrings, sample);
  }
}

void ProfileBuffer::StreamMarkersToJSON(SpliceableJSONWriter& aWriter,
                                        int aThreadId,
                                        const TimeStamp& aProcessStartTime,
                                        double aSinceTime,
                                        UniqueStacks& aUniqueStacks) const {
  EntryGetter e(*this);

  // Stream all markers whose threadId matches aThreadId. We skip other entries,
  // because we process them in StreamSamplesToJSON().
  //
  // NOTE: The ThreadId of a marker is determined by its GetThreadId() method,
  // rather than ThreadId buffer entries, as markers can be added outside of
  // samples.
  while (e.Has()) {
    if (e.Get().IsMarker()) {
      const ProfilerMarker* marker = e.Get().GetMarker();
      if (marker->GetTime() >= aSinceTime &&
          marker->GetThreadId() == aThreadId) {
        marker->StreamJSON(aWriter, aProcessStartTime, aUniqueStacks);
      }
    }
    e.Next();
  }
}

void ProfileBuffer::StreamProfilerOverheadToJSON(
    SpliceableJSONWriter& aWriter, const TimeStamp& aProcessStartTime,
    double aSinceTime) const {
  enum Schema : uint32_t {
    TIME = 0,
    LOCKING = 1,
    MARKER_CLEANING = 2,
    COUNTERS = 3,
    THREADS = 4
  };

  EntryGetter e(*this);

  aWriter.StartObjectProperty("profilerOverhead_UNSTABLE");
  // Stream all sampling overhead data. We skip other entries, because we
  // process them in StreamSamplesToJSON()/etc.
  {
    JSONSchemaWriter schema(aWriter);
    schema.WriteField("time");
    schema.WriteField("locking");
    schema.WriteField("expiredMarkerCleaning");
    schema.WriteField("counters");
    schema.WriteField("threads");
  }

  aWriter.StartArrayProperty("data");
  double firstTime = 0.0;
  double lastTime = 0.0;
  struct Stats {
    unsigned n = 0;
    double sum = 0;
    double min = std::numeric_limits<double>::max();
    double max = 0;
    void Count(double v) {
      ++n;
      sum += v;
      if (v < min) {
        min = v;
      }
      if (v > max) {
        max = v;
      }
    }
  };
  Stats intervals, overheads, lockings, cleanings, counters, threads;
  while (e.Has()) {
    // valid sequence: ProfilerOverheadTime, ProfilerOverheadDuration * 4
    if (e.Get().IsProfilerOverheadTime()) {
      double time = e.Get().GetDouble();
      if (time >= aSinceTime) {
        e.Next();
        if (!e.Has() || !e.Get().IsProfilerOverheadDuration()) {
          ERROR_AND_CONTINUE(
              "expected a ProfilerOverheadDuration entry after "
              "ProfilerOverheadTime");
        }
        double locking = e.Get().GetDouble();
        e.Next();
        if (!e.Has() || !e.Get().IsProfilerOverheadDuration()) {
          ERROR_AND_CONTINUE(
              "expected a ProfilerOverheadDuration entry after "
              "ProfilerOverheadTime,ProfilerOverheadDuration");
        }
        double cleaning = e.Get().GetDouble();
        e.Next();
        if (!e.Has() || !e.Get().IsProfilerOverheadDuration()) {
          ERROR_AND_CONTINUE(
              "expected a ProfilerOverheadDuration entry after "
              "ProfilerOverheadTime,ProfilerOverheadDuration*2");
        }
        double counter = e.Get().GetDouble();
        e.Next();
        if (!e.Has() || !e.Get().IsProfilerOverheadDuration()) {
          ERROR_AND_CONTINUE(
              "expected a ProfilerOverheadDuration entry after "
              "ProfilerOverheadTime,ProfilerOverheadDuration*3");
        }
        double thread = e.Get().GetDouble();

        if (firstTime == 0.0) {
          firstTime = time;
        } else {
          // Note that we'll have 1 fewer interval than other numbers (because
          // we need both ends of an interval to know its duration). The final
          // difference should be insignificant over the expected many thousands
          // of iterations.
          intervals.Count(time - lastTime);
        }
        lastTime = time;
        overheads.Count(locking + cleaning + counter + thread);
        lockings.Count(locking);
        cleanings.Count(cleaning);
        counters.Count(counter);
        threads.Count(thread);

        AutoArraySchemaWriter writer(aWriter);
        writer.DoubleElement(TIME, time);
        writer.DoubleElement(LOCKING, locking);
        writer.DoubleElement(MARKER_CLEANING, cleaning);
        writer.DoubleElement(COUNTERS, counter);
        writer.DoubleElement(THREADS, thread);
      }
    }
    e.Next();
  }
  aWriter.EndArray();  // data

  // Only output statistics if there is at least one full interval (and
  // therefore at least two samplings.)
  if (intervals.n > 0) {
    aWriter.StartObjectProperty("statistics");
    aWriter.DoubleProperty("profiledDuration", lastTime - firstTime);
    aWriter.IntProperty("samplingCount", overheads.n);
    aWriter.DoubleProperty("overheadDurations", overheads.sum);
    aWriter.DoubleProperty("overheadPercentage",
                           overheads.sum / (lastTime - firstTime));
#  define PROFILER_STATS(name, var)                           \
    aWriter.DoubleProperty("mean" name, (var).sum / (var).n); \
    aWriter.DoubleProperty("min" name, (var).min);            \
    aWriter.DoubleProperty("max" name, (var).max);
    PROFILER_STATS("Interval", intervals);
    PROFILER_STATS("Overhead", overheads);
    PROFILER_STATS("Lockings", lockings);
    PROFILER_STATS("Cleaning", cleanings);
    PROFILER_STATS("Counter", counters);
    PROFILER_STATS("Thread", threads);
#  undef PROFILER_STATS
    aWriter.EndObject();  // statistics
  }
  aWriter.EndObject();  // profilerOverhead
}

struct CounterKeyedSample {
  double mTime;
  uint64_t mNumber;
  int64_t mCount;
};

using CounterKeyedSamples = Vector<CounterKeyedSample>;

using CounterMap = HashMap<uint64_t, CounterKeyedSamples>;

// HashMap lookup, if not found, a default value is inserted.
// Returns reference to (existing or new) value inside the HashMap.
template <typename HashM, typename Key>
static auto& LookupOrAdd(HashM& aMap, Key&& aKey) {
  auto addPtr = aMap.lookupForAdd(aKey);
  if (!addPtr) {
    MOZ_RELEASE_ASSERT(aMap.add(addPtr, std::forward<Key>(aKey),
                                typename HashM::Entry::ValueType{}));
    MOZ_ASSERT(!!addPtr);
  }
  return addPtr->value();
}

void ProfileBuffer::StreamCountersToJSON(SpliceableJSONWriter& aWriter,
                                         const TimeStamp& aProcessStartTime,
                                         double aSinceTime) const {
  // Because this is a format entirely internal to the Profiler, any parsing
  // error indicates a bug in the ProfileBuffer writing or the parser itself,
  // or possibly flaky hardware.

  EntryGetter e(*this);
  enum Schema : uint32_t { TIME = 0, NUMBER = 1, COUNT = 2 };

  // Stream all counters. We skip other entries, because we process them in
  // StreamSamplesToJSON()/etc.
  //
  // Valid sequence in the buffer:
  // CounterID
  // Time
  // ( CounterKey Count Number? )*
  //
  // And the JSON (example):
  // "counters": {
  //  "name": "malloc",
  //  "category": "Memory",
  //  "description": "Amount of allocated memory",
  //  "sample_groups": {
  //   "id": 0,
  //   "samples": {
  //    "schema": {"time": 0, "number": 1, "count": 2},
  //    "data": [
  //     [
  //      16117.033968000002,
  //      2446216,
  //      6801320
  //     ],
  //     [
  //      16118.037638,
  //      2446216,
  //      6801320
  //     ],
  //    ],
  //   }
  //  }
  // },

  // Build the map of counters and populate it
  HashMap<void*, CounterMap> counters;

  while (e.Has()) {
    // skip all non-Counters, including if we start in the middle of a counter
    if (e.Get().IsCounterId()) {
      void* id = e.Get().GetPtr();
      CounterMap& counter = LookupOrAdd(counters, id);
      e.Next();
      if (!e.Has() || !e.Get().IsTime()) {
        ERROR_AND_CONTINUE("expected a Time entry");
      }
      double time = e.Get().GetDouble();
      if (time >= aSinceTime) {
        e.Next();
        while (e.Has() && e.Get().IsCounterKey()) {
          uint64_t key = e.Get().GetUint64();
          CounterKeyedSamples& data = LookupOrAdd(counter, key);
          e.Next();
          if (!e.Has() || !e.Get().IsCount()) {
            ERROR_AND_CONTINUE("expected a Count entry");
          }
          int64_t count = e.Get().GetUint64();
          e.Next();
          uint64_t number;
          if (!e.Has() || !e.Get().IsNumber()) {
            number = 0;
          } else {
            number = e.Get().GetInt64();
          }
          CounterKeyedSample sample = {time, number, count};
          MOZ_RELEASE_ASSERT(data.append(sample));
        }
      } else {
        // skip counter sample - only need to skip the initial counter
        // id, then let the loop at the top skip the rest
      }
    }
    e.Next();
  }
  // we have a map of a map of counter entries; dump them to JSON
  if (counters.count() == 0) {
    return;
  }

  aWriter.StartArrayProperty("counters");
  for (auto iter = counters.iter(); !iter.done(); iter.next()) {
    CounterMap& counter = iter.get().value();
    const BaseProfilerCount* base_counter =
        static_cast<const BaseProfilerCount*>(iter.get().key());

    aWriter.Start();
    aWriter.StringProperty("name", base_counter->mLabel);
    aWriter.StringProperty("category", base_counter->mCategory);
    aWriter.StringProperty("description", base_counter->mDescription);

    aWriter.StartObjectProperty("sample_groups");
    for (auto counter_iter = counter.iter(); !counter_iter.done();
         counter_iter.next()) {
      CounterKeyedSamples& samples = counter_iter.get().value();
      uint64_t key = counter_iter.get().key();

      size_t size = samples.length();
      if (size == 0) {
        continue;
      }
      aWriter.IntProperty("id", static_cast<int64_t>(key));
      aWriter.StartObjectProperty("samples");
      {
        // XXX Can we assume a missing count means 0?
        JSONSchemaWriter schema(aWriter);
        schema.WriteField("time");
        schema.WriteField("number");
        schema.WriteField("count");
      }

      aWriter.StartArrayProperty("data");
      uint64_t previousNumber = 0;
      int64_t previousCount = 0;
      for (size_t i = 0; i < size; i++) {
        // Encode as deltas, and only encode if different than the last sample
        if (i == 0 || samples[i].mNumber != previousNumber ||
            samples[i].mCount != previousCount) {
          MOZ_ASSERT(i == 0 || samples[i].mTime >= samples[i - 1].mTime);
          MOZ_ASSERT(samples[i].mNumber >= previousNumber);
          MOZ_ASSERT(samples[i].mNumber - previousNumber <=
                     uint64_t(std::numeric_limits<int64_t>::max()));

          AutoArraySchemaWriter writer(aWriter);
          writer.DoubleElement(TIME, samples[i].mTime);
          writer.IntElement(NUMBER, static_cast<int64_t>(samples[i].mNumber -
                                                         previousNumber));
          writer.IntElement(COUNT, samples[i].mCount - previousCount);
          previousNumber = samples[i].mNumber;
          previousCount = samples[i].mCount;
        }
      }
      aWriter.EndArray();   // data
      aWriter.EndObject();  // samples
    }
    aWriter.EndObject();  // sample groups
    aWriter.End();        // for each counter
  }
  aWriter.EndArray();  // counters
}

void ProfileBuffer::StreamMemoryToJSON(SpliceableJSONWriter& aWriter,
                                       const TimeStamp& aProcessStartTime,
                                       double aSinceTime) const {
  enum Schema : uint32_t { TIME = 0, RSS = 1, USS = 2 };

  EntryGetter e(*this);

  aWriter.StartObjectProperty("memory");
  // Stream all memory (rss/uss) data. We skip other entries, because we
  // process them in StreamSamplesToJSON()/etc.
  aWriter.IntProperty("initial_heap", 0);  // XXX FIX
  aWriter.StartObjectProperty("samples");
  {
    JSONSchemaWriter schema(aWriter);
    schema.WriteField("time");
    schema.WriteField("rss");
    schema.WriteField("uss");
  }

  aWriter.StartArrayProperty("data");
  int64_t previous_rss = 0;
  int64_t previous_uss = 0;
  while (e.Has()) {
    // valid sequence: Resident, Unshared?, Time
    if (e.Get().IsResidentMemory()) {
      int64_t rss = e.Get().GetInt64();
      int64_t uss = 0;
      e.Next();
      if (e.Has()) {
        if (e.Get().IsUnsharedMemory()) {
          uss = e.Get().GetDouble();
          e.Next();
          if (!e.Has()) {
            break;
          }
        }
        if (e.Get().IsTime()) {
          double time = e.Get().GetDouble();
          if (time >= aSinceTime &&
              (previous_rss != rss || previous_uss != uss)) {
            AutoArraySchemaWriter writer(aWriter);
            writer.DoubleElement(TIME, time);
            writer.IntElement(RSS, rss);
            if (uss != 0) {
              writer.IntElement(USS, uss);
            }
            previous_rss = rss;
            previous_uss = uss;
          }
        } else {
          ERROR_AND_CONTINUE("expected a Time entry");
        }
      }
    }
    e.Next();
  }
  aWriter.EndArray();   // data
  aWriter.EndObject();  // samples
  aWriter.EndObject();  // memory
}
#  undef ERROR_AND_CONTINUE

static void AddPausedRange(SpliceableJSONWriter& aWriter, const char* aReason,
                           const Maybe<double>& aStartTime,
                           const Maybe<double>& aEndTime) {
  aWriter.Start();
  if (aStartTime) {
    aWriter.DoubleProperty("startTime", *aStartTime);
  } else {
    aWriter.NullProperty("startTime");
  }
  if (aEndTime) {
    aWriter.DoubleProperty("endTime", *aEndTime);
  } else {
    aWriter.NullProperty("endTime");
  }
  aWriter.StringProperty("reason", aReason);
  aWriter.End();
}

void ProfileBuffer::StreamPausedRangesToJSON(SpliceableJSONWriter& aWriter,
                                             double aSinceTime) const {
  EntryGetter e(*this);

  Maybe<double> currentPauseStartTime;
  Maybe<double> currentCollectionStartTime;

  while (e.Has()) {
    if (e.Get().IsPause()) {
      currentPauseStartTime = Some(e.Get().GetDouble());
    } else if (e.Get().IsResume()) {
      AddPausedRange(aWriter, "profiler-paused", currentPauseStartTime,
                     Some(e.Get().GetDouble()));
      currentPauseStartTime = Nothing();
    } else if (e.Get().IsCollectionStart()) {
      currentCollectionStartTime = Some(e.Get().GetDouble());
    } else if (e.Get().IsCollectionEnd()) {
      AddPausedRange(aWriter, "collecting", currentCollectionStartTime,
                     Some(e.Get().GetDouble()));
      currentCollectionStartTime = Nothing();
    }
    e.Next();
  }

  if (currentPauseStartTime) {
    AddPausedRange(aWriter, "profiler-paused", currentPauseStartTime,
                   Nothing());
  }
  if (currentCollectionStartTime) {
    AddPausedRange(aWriter, "collecting", currentCollectionStartTime,
                   Nothing());
  }
}

bool ProfileBuffer::DuplicateLastSample(int aThreadId,
                                        const TimeStamp& aProcessStartTime,
                                        Maybe<uint64_t>& aLastSample) {
  if (aLastSample && *aLastSample < mRangeStart) {
    // The last sample is no longer within the buffer range, so we cannot use
    // it. Reset the stored buffer position to Nothing().
    aLastSample.reset();
  }

  if (!aLastSample) {
    return false;
  }

  uint64_t lastSampleStartPos = *aLastSample;

  MOZ_RELEASE_ASSERT(GetEntry(lastSampleStartPos).IsThreadId() &&
                     GetEntry(lastSampleStartPos).GetInt() == aThreadId);

  aLastSample = Some(AddThreadIdEntry(aThreadId));

  EntryGetter e(*this, lastSampleStartPos + 1);

  // Go through the whole entry and duplicate it, until we find the next one.
  while (e.Has()) {
    switch (e.Get().GetKind()) {
      case ProfileBufferEntry::Kind::Pause:
      case ProfileBufferEntry::Kind::Resume:
      case ProfileBufferEntry::Kind::CollectionStart:
      case ProfileBufferEntry::Kind::CollectionEnd:
      case ProfileBufferEntry::Kind::ThreadId:
        // We're done.
        return true;
      case ProfileBufferEntry::Kind::Time:
        // Copy with new time
        AddEntry(ProfileBufferEntry::Time(
            (TimeStamp::NowUnfuzzed() - aProcessStartTime).ToMilliseconds()));
        break;
      case ProfileBufferEntry::Kind::Marker:
      case ProfileBufferEntry::Kind::ResidentMemory:
      case ProfileBufferEntry::Kind::UnsharedMemory:
      case ProfileBufferEntry::Kind::CounterKey:
      case ProfileBufferEntry::Kind::Number:
      case ProfileBufferEntry::Kind::Count:
      case ProfileBufferEntry::Kind::Responsiveness:
        // Don't copy anything not part of a thread's stack sample
        break;
      case ProfileBufferEntry::Kind::CounterId:
        // CounterId is normally followed by Time - if so, we'd like
        // to skip it.  If we duplicate Time, it won't hurt anything, just
        // waste buffer space (and this can happen if the CounterId has
        // fallen off the end of the buffer, but Time (and Number/Count)
        // are still in the buffer).
        e.Next();
        if (e.Has() && e.Get().GetKind() != ProfileBufferEntry::Kind::Time) {
          // this would only happen if there was an invalid sequence
          // in the buffer.  Don't skip it.
          continue;
        }
        // we've skipped Time
        break;
      case ProfileBufferEntry::Kind::ProfilerOverheadTime:
        // ProfilerOverheadTime is normally followed by
        // ProfilerOverheadDuration*4 - if so, we'd like to skip it. Don't
        // duplicate, as we are in the middle of a sampling and will soon
        // capture its own overhead.
        e.Next();
        // A missing Time would only happen if there was an invalid sequence
        // in the buffer. Don't skip unexpected entry.
        if (e.Has() && e.Get().GetKind() !=
                           ProfileBufferEntry::Kind::ProfilerOverheadDuration) {
          continue;
        }
        e.Next();
        if (e.Has() && e.Get().GetKind() !=
                           ProfileBufferEntry::Kind::ProfilerOverheadDuration) {
          continue;
        }
        e.Next();
        if (e.Has() && e.Get().GetKind() !=
                           ProfileBufferEntry::Kind::ProfilerOverheadDuration) {
          continue;
        }
        e.Next();
        if (e.Has() && e.Get().GetKind() !=
                           ProfileBufferEntry::Kind::ProfilerOverheadDuration) {
          continue;
        }
        // we've skipped ProfilerOverheadTime and ProfilerOverheadDuration*4.
        break;
      default: {
        // Copy anything else we don't know about.
        ProfileBufferEntry entry = e.Get();
        AddEntry(entry);
        break;
      }
    }
    e.Next();
  }
  return true;
}

void ProfileBuffer::DiscardSamplesBeforeTime(double aTime) {
  EntryGetter e(*this);
  for (;;) {
    // This block skips entries until we find the start of the next sample.
    // This is useful in three situations.
    //
    // - The circular buffer overwrites old entries, so when we start parsing
    //   we might be in the middle of a sample, and we must skip forward to the
    //   start of the next sample.
    //
    // - We skip samples that don't have an appropriate ThreadId or Time.
    //
    // - We skip range Pause, Resume, CollectionStart, Marker, and CollectionEnd
    //   entries between samples.
    while (e.Has()) {
      if (e.Get().IsThreadId()) {
        break;
      } else {
        e.Next();
      }
    }

    if (!e.Has()) {
      break;
    }

    MOZ_RELEASE_ASSERT(e.Get().IsThreadId());
    uint64_t sampleStartPos = e.CurPos();
    e.Next();

    if (e.Has() && e.Get().IsTime()) {
      double sampleTime = e.Get().GetDouble();

      if (sampleTime >= aTime) {
        // This is the first sample within the window of time that we want to
        // keep. Throw away all samples before sampleStartPos and return.
        mRangeStart = sampleStartPos;
        return;
      }
    }
  }
}

// END ProfileBuffer
////////////////////////////////////////////////////////////////////////

}  // namespace baseprofiler
}  // namespace mozilla

#endif  // MOZ_BASE_PROFILER
