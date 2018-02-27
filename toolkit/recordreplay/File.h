/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_toolkit_recordreplay_File_h
#define mozilla_toolkit_recordreplay_File_h

#include "InfallibleVector.h"
#include "ProcessRecordReplay.h"
#include "SpinLock.h"

#include "mozilla/PodOperations.h"
#include "mozilla/RecordReplay.h"

namespace mozilla {
namespace recordreplay {

// Structure managing file I/O. Each file contains an index for a set of named
// streams, whose contents are compressed and interleaved throughout the file.
// Additionally, we directly manage the file handle and all associated memory.
// This makes it easier to restore memory snapshots without getting confused
// about the state of the file handles which the process has opened. Data
// written and read from files is automatically compressed with LZ4.
//
// File is threadsafe, but Stream is not.

// A location of a chunk of a stream within a file.
struct StreamChunkLocation
{
  // Offset into the file of the start of the chunk.
  uint64_t mOffset;

  // Compressed (stored) size of the chunk.
  uint32_t mCompressedSize;

  // Decompressed size of the chunk.
  uint32_t mDecompressedSize;

  inline bool operator == (const StreamChunkLocation& aOther) const {
    return mOffset == aOther.mOffset
        && mCompressedSize == aOther.mCompressedSize
        && mDecompressedSize == aOther.mDecompressedSize;
  }
};

enum class StreamName
{
  Main,
  Lock,
  Event,
  Assert,
  WeakPointer,
  Count
};

template <AllocatedMemoryKind Kind>
class StreamTemplate
{
  friend class FileTemplate<Kind>;

  // File this stream belongs to.
  FileTemplate<Kind>* mFile;

  // Prefix name for this stream.
  StreamName mName;

  // Index which, when combined to mName, uniquely identifies this stream in
  // the file.
  size_t mNameIndex;

  // When writing, all chunks that have been flushed to disk. When reading, all
  // chunks in the entire stream.
  InfallibleVector<StreamChunkLocation, 1, AllocPolicy<Kind>> mChunks;

  // Data buffer.
  char* mBuffer;

  // The maximum number of bytes to buffer before compressing and writing to
  // disk, and the maximum number of bytes that can be decompressed at once.
  static const size_t BUFFER_MAX = 1024 * 1024;

  // The capacity of mBuffer, at most BUFFER_MAX.
  size_t mBufferSize;

  // During reading, the number of accessible bytes in mBuffer.
  size_t mBufferLength;

  // The number of bytes read or written from mBuffer.
  size_t mBufferPos;

  // The number of uncompressed bytes read or written from the stream.
  size_t mStreamPos;

  // Any buffer available for use when decompressing or compressing data.
  char* mBallast;
  size_t mBallastSize;

  // The number of chunks that have been completely read or written. When
  // writing, this equals mChunks.length().
  size_t mChunkIndex;

  // Flag set if the file is open for reading, but the stream is setup as if
  // for writing.
  bool mNeedsFixupAfterRecordingRewind;

  StreamTemplate(FileTemplate<Kind>* aFile, StreamName aName, size_t aNameIndex)
    : mFile(aFile)
    , mName(aName)
    , mNameIndex(aNameIndex)
    , mBuffer(nullptr)
    , mBufferSize(0)
    , mBufferLength(0)
    , mBufferPos(0)
    , mStreamPos(0)
    , mBallast(nullptr)
    , mBallastSize(0)
    , mChunkIndex(0)
    , mNeedsFixupAfterRecordingRewind(false)
  {}

  ~StreamTemplate() {
    if (mBuffer) {
      mFile->DeallocateMemory(mBuffer, mBufferSize);
    }
    if (mBallast) {
      mFile->DeallocateMemory(mBallast, mBallastSize);
    }
  }

public:
  void ReadBytes(void* aData, size_t aSize);
  void WriteBytes(const void* aData, size_t aSize);
  size_t ReadScalar();
  void WriteScalar(size_t aValue);
  bool AtEnd();

  inline void RecordOrReplayBytes(void* aData, size_t aSize) {
    if (IsRecording()) {
      WriteBytes(aData, aSize);
    } else {
      ReadBytes(aData, aSize);
    }
  }

  template <typename T>
  inline void RecordOrReplayScalar(T* aPtr) {
    if (IsRecording()) {
      WriteScalar((size_t)*aPtr);
    } else {
      *aPtr = (T)ReadScalar();
    }
  }

  template <typename T>
  inline void RecordOrReplayValue(T* aPtr) {
    RecordOrReplayBytes(aPtr, sizeof(T));
  }

  // Make sure that a value is the same while replaying as it was while
  // recording. If a failure occurs then a last ditch snapshot restore is
  // attempted (see ProcessRewind.h).
  void CheckInput(size_t aValue);

  // Add a thread event to this file. Each thread event in a file is followed
  // by additional data specific to that event. Generally, CheckInput should be
  // used while recording or replaying the data for a thread event so that any
  // discrepancies with the recording are found immediately.
  inline void RecordOrReplayThreadEvent(ThreadEvent aEvent) {
    CheckInput((size_t)aEvent);
  }

  inline size_t StreamPosition() {
    return mStreamPos;
  }

private:
  enum ShouldCopy {
    DontCopyExistingData,
    CopyExistingData
  };

  void EnsureMemory(char** aBuf, size_t* aSize, size_t aNeededSize, size_t aMaxSize,
                    ShouldCopy aCopy);
  void Flush();

  static size_t BallastMaxSize();

  void MaybeFixupAfterRecordingRewind();
};

typedef StreamTemplate<AllocatedMemoryKind::Tracked> Stream;
typedef StreamTemplate<AllocatedMemoryKind::Untracked> UntrackedStream;

template <AllocatedMemoryKind Kind>
class FileTemplate
{
public:
  enum Mode {
    WRITE,
    READ
  };

  friend class StreamTemplate<Kind>;

private:
  // Name of the file being accessed.
  char* mFilename;

  // Open file handle, or 0 if closed.
  FileHandle mFd;

  // Whether this file is open for writing or reading.
  Mode mMode;

  // When writing, the current offset into the file.
  uint64_t mWriteOffset;

  // All streams in this file, indexed by stream name and name index.
  typedef InfallibleVector<StreamTemplate<Kind>*, 1, AllocPolicy<Kind>> StreamVector;
  StreamVector mStreams[(size_t) StreamName::Count];

  // Lock protecting access to this file.
  SpinLock mLock;

  void Clear() {
    mFilename = nullptr;
    mFd = 0;
    mMode = READ;
    mWriteOffset = 0;
    for (auto& vector : mStreams) {
      vector.clear();
    }
    PodZero(&mLock);
  }

public:
  FileTemplate() { Clear(); }
  ~FileTemplate() { Close(); }

  bool Open(const char* aFilename, size_t aIndex, Mode aMode);
  void Close();

  void CloneFrom(const FileTemplate<Kind>& aOther);
  void FixupAfterRecordingRewind(FileHandle aFd);

  bool OpenForWriting() const { return mFd && mMode == WRITE; }
  bool OpenForReading() const { return mFd && mMode == READ; }

  StreamTemplate<Kind>* OpenStream(StreamName aName, size_t aNameIndex);

  const char* Filename() { return mFilename; }

private:
  void SetFilename(const char* aFilename);

  void ForEachStream(const std::function<void(StreamTemplate<Kind>*)>& aCallback) const;

  bool ReadIndex();

  StreamChunkLocation WriteChunk(const char* aStart,
                                 size_t aCompressedSize, size_t aDecompressedSize);
  void ReadChunk(char* aDest, const StreamChunkLocation& aChunk);

  using IndexBuffer = InfallibleVector<char, 4096, AllocPolicy<Kind>>;

  template <typename T>
  static bool ReadForIndex(char** aBuf, char* aLimit, T* aRes);

  template <typename T>
  static void WriteForIndex(IndexBuffer& aBuf, const T& aSrc);

  bool ReadStreamFromIndex(char** aBuf, char* aLimit);
  static void WriteStreamToIndex(IndexBuffer& aBuf, const StreamTemplate<Kind>& aStream);

  char* AllocateMemory(size_t aSize);
  void DeallocateMemory(char* aBuf, size_t aSize);
};

typedef FileTemplate<AllocatedMemoryKind::Tracked> File;
typedef FileTemplate<AllocatedMemoryKind::Untracked> UntrackedFile;

void InitializeFiles(const char* aTempFile);

} // namespace recordreplay
} // namespace mozilla

#endif // mozilla_toolkit_recordreplay_File_h
