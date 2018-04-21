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

  // When writing, the number of chunks in this stream when the file was last
  // flushed.
  size_t mFlushedChunks;

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
    , mFlushedChunks(0)
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
  StreamName Name() const { return mName; }
  size_t NameIndex() const { return mNameIndex; }

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
  // recording.
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
  void Flush(bool aTakeLock);

  static size_t BallastMaxSize();
};

typedef StreamTemplate<TrackedMemoryKind> Stream;
typedef StreamTemplate<UntrackedMemoryKind::File> UntrackedStream;

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

  // The offset of the last index read or written to the file.
  uint64_t mLastIndexOffset;

  // All streams in this file, indexed by stream name and name index.
  typedef InfallibleVector<StreamTemplate<Kind>*, 1, AllocPolicy<Kind>> StreamVector;
  StreamVector mStreams[(size_t) StreamName::Count];

  // Lock protecting access to this file.
  SpinLock mLock;

  // When writing, lock for synchronizing file flushes (writer) with other
  // threads writing to streams in this file (readers).
  ReadWriteSpinLock mStreamLock;

  void Clear() {
    mFilename = nullptr;
    mFd = 0;
    mMode = READ;
    mWriteOffset = 0;
    mLastIndexOffset = 0;
    for (auto& vector : mStreams) {
      vector.clear();
    }
    PodZero(&mLock);
    PodZero(&mStreamLock);
  }

public:
  FileTemplate() { Clear(); }
  ~FileTemplate() { Close(); }

  bool Open(const char* aFilename, Mode aMode);
  void Close();

  bool OpenForWriting() const { return mFd && mMode == WRITE; }
  bool OpenForReading() const { return mFd && mMode == READ; }

  StreamTemplate<Kind>* OpenStream(StreamName aName, size_t aNameIndex);

  const char* Filename() { return mFilename; }

  // Prevent/allow other threads to write to streams in this file.
  void PreventStreamWrites() { mStreamLock.Lock(/* aRead = */ false); }
  void AllowStreamWrites() { mStreamLock.Unlock(/* aRead = */ false); }

  // Flush any changes since the last Flush() call to disk, returning whether
  // there were such changes.
  bool Flush();

  enum class ReadIndexResult {
    InvalidFile,
    EndOfFile,
    FoundIndex
  };

  // Read any data added to the file by a Flush() call. aUpdatedStreams is
  // optional and filled in with streams whose contents have changed, and may
  // have duplicates.
  ReadIndexResult ReadNextIndex(InfallibleVector<StreamTemplate<Kind>*>* aUpdatedStreams);

private:
  void SetFilename(const char* aFilename);

  StreamChunkLocation WriteChunk(const char* aStart,
                                 size_t aCompressedSize, size_t aDecompressedSize,
                                 bool aTakeLock);
  void ReadChunk(char* aDest, const StreamChunkLocation& aChunk);

  char* AllocateMemory(size_t aSize);
  void DeallocateMemory(void* aBuf, size_t aSize);
};

typedef FileTemplate<TrackedMemoryKind> File;
typedef FileTemplate<UntrackedMemoryKind::File> UntrackedFile;

void InitializeFiles(const char* aTempFile);

} // namespace recordreplay
} // namespace mozilla

#endif // mozilla_toolkit_recordreplay_File_h
