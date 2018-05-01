/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "File.h"

#include "mozilla/Compression.h"
#include "mozilla/Sprintf.h"
#include "ProcessRewind.h"

#include <algorithm>

namespace mozilla {
namespace recordreplay {

///////////////////////////////////////////////////////////////////////////////
// StreamTemplate
///////////////////////////////////////////////////////////////////////////////

template <AllocatedMemoryKind Kind>
void
StreamTemplate<Kind>::ReadBytes(void* aData, size_t aSize)
{
  MOZ_RELEASE_ASSERT(mFile->OpenForReading());

  size_t totalRead = 0;

  while (true) {
    // Read what we can from the data buffer.
    MOZ_RELEASE_ASSERT(mBufferPos <= mBufferLength);
    size_t bufAvailable = mBufferLength - mBufferPos;
    size_t bufRead = std::min(bufAvailable, aSize);
    if (aData) {
      memcpy(aData, &mBuffer[mBufferPos], bufRead);
      aData = (char*)aData + bufRead;
    }
    mBufferPos += bufRead;
    mStreamPos += bufRead;
    totalRead += bufRead;
    aSize -= bufRead;

    if (!aSize) {
      return;
    }

    MOZ_RELEASE_ASSERT(mBufferPos == mBufferLength);

    // If we try to read off the end of a stream then we must have hit the end
    // of the replay for this thread.
    while (mChunkIndex == mChunks.length()) {
      MOZ_RELEASE_ASSERT(mName == StreamName::Event || mName == StreamName::Assert);
      HitEndOfRecording();
    }

    const StreamChunkLocation& chunk = mChunks[mChunkIndex++];

    EnsureMemory(&mBallast, &mBallastSize, chunk.mCompressedSize, BallastMaxSize(),
                 DontCopyExistingData);
    mFile->ReadChunk(mBallast, chunk);

    EnsureMemory(&mBuffer, &mBufferSize, chunk.mDecompressedSize, BUFFER_MAX,
                 DontCopyExistingData);

    size_t bytesWritten;
    if (!Compression::LZ4::decompress((const char*) mBallast, chunk.mCompressedSize,
                                      (char*) mBuffer, chunk.mDecompressedSize, &bytesWritten) ||
        bytesWritten != chunk.mDecompressedSize)
    {
      MOZ_CRASH();
    }

    mBufferPos = 0;
    mBufferLength = chunk.mDecompressedSize;
  }
}

template <AllocatedMemoryKind Kind>
bool
StreamTemplate<Kind>::AtEnd()
{
  MOZ_RELEASE_ASSERT(mFile->OpenForReading());

  return mBufferPos == mBufferLength && mChunkIndex == mChunks.length();
}

template <AllocatedMemoryKind Kind>
void
StreamTemplate<Kind>::WriteBytes(const void* aData, size_t aSize)
{
  MOZ_RELEASE_ASSERT(mFile->OpenForWriting());

  // Prevent the entire file from being flushed while we write this data.
  AutoReadSpinLock streamLock(mFile->mStreamLock);

  while (true) {
    // Fill up the data buffer first.
    MOZ_RELEASE_ASSERT(mBufferPos <= mBufferSize);
    size_t bufAvailable = mBufferSize - mBufferPos;
    size_t bufWrite = (bufAvailable < aSize) ? bufAvailable : aSize;
    memcpy(&mBuffer[mBufferPos], aData, bufWrite);
    mBufferPos += bufWrite;
    mStreamPos += bufWrite;
    if (bufWrite == aSize) {
      return;
    }
    aData = (char*)aData + bufWrite;
    aSize -= bufWrite;

    // Grow the file's buffer if it is not at its maximum size.
    if (mBufferSize < BUFFER_MAX) {
      EnsureMemory(&mBuffer, &mBufferSize, mBufferSize + 1, BUFFER_MAX, CopyExistingData);
      continue;
    }

    Flush(/* aTakeLock = */ true);
  }
}

template <AllocatedMemoryKind Kind>
size_t
StreamTemplate<Kind>::ReadScalar()
{
  // Read back a pointer sized value using the same encoding as WriteScalar.
  size_t value = 0, shift = 0;
  while (true) {
    uint8_t bits;
    ReadBytes(&bits, 1);
    value |= (size_t)(bits & 127) << shift;
    if (!(bits & 128)) {
      break;
    }
    shift += 7;
  }
  return value;
}

template <AllocatedMemoryKind Kind>
void
StreamTemplate<Kind>::WriteScalar(size_t aValue)
{
  // Pointer sized values are written out as unsigned values with an encoding
  // optimized for small values. Each written byte successively captures 7 bits
  // of data from the value, starting at the low end, with the high bit in the
  // byte indicating whether there are any more non-zero bits in the value.
  //
  // With this encoding, values less than 2^7 (128) require one byte, values
  // less than 2^14 (16384) require two bytes, and so forth, but negative
  // numbers end up requiring ten bytes on a 64 bit architecture.
  do {
    uint8_t bits = aValue & 127;
    aValue = aValue >> 7;
    if (aValue) {
      bits |= 128;
    }
    WriteBytes(&bits, 1);
  } while (aValue);
}

template <AllocatedMemoryKind Kind>
void
StreamTemplate<Kind>::CheckInput(size_t aValue)
{
  size_t oldValue = aValue;
  RecordOrReplayScalar(&oldValue);
  if (oldValue != aValue) {
    child::ReportFatalError("Input Mismatch: Recorded: %zu Replayed %zu\n", oldValue, aValue);
    Unreachable();
  }
}

template <AllocatedMemoryKind Kind>
void
StreamTemplate<Kind>::EnsureMemory(char** aBuf, size_t* aSize,
                                   size_t aNeededSize, size_t aMaxSize, ShouldCopy aCopy)
{
  // Once a stream buffer grows, it never shrinks again. Buffers start out
  // small because most streams are very small.
  MOZ_RELEASE_ASSERT(!!*aBuf == !!*aSize);
  MOZ_RELEASE_ASSERT(aNeededSize <= aMaxSize);
  if (*aSize < aNeededSize) {
    size_t newSize = std::min(std::max<size_t>(256, aNeededSize * 2), aMaxSize);
    char* newBuf = mFile->AllocateMemory(newSize);
    if (*aBuf) {
      if (aCopy == CopyExistingData) {
        memcpy(newBuf, *aBuf, *aSize);
      }
      mFile->DeallocateMemory(*aBuf, *aSize);
    }
    *aBuf = newBuf;
    *aSize = newSize;
  }
}

template <AllocatedMemoryKind Kind>
void
StreamTemplate<Kind>::Flush(bool aTakeLock)
{
  MOZ_RELEASE_ASSERT(mFile && mFile->OpenForWriting());

  if (!mBufferPos) {
    return;
  }

  size_t bound = Compression::LZ4::maxCompressedSize(mBufferPos);
  EnsureMemory(&mBallast, &mBallastSize, bound, BallastMaxSize(),
               DontCopyExistingData);

  size_t compressedSize = Compression::LZ4::compress((const char*) mBuffer, mBufferPos,
                                                     (char*) mBallast);
  MOZ_RELEASE_ASSERT(compressedSize != 0);
  MOZ_RELEASE_ASSERT((size_t)compressedSize <= bound);

  StreamChunkLocation chunk = mFile->WriteChunk(mBallast, compressedSize, mBufferPos, aTakeLock);
  mChunks.append(chunk);
  MOZ_ALWAYS_TRUE(++mChunkIndex == mChunks.length());

  mBufferPos = 0;
}

template <AllocatedMemoryKind Kind>
/* static */ size_t
StreamTemplate<Kind>::BallastMaxSize()
{
  return Compression::LZ4::maxCompressedSize(BUFFER_MAX);
}

template class StreamTemplate<TrackedMemoryKind>;
template class StreamTemplate<UntrackedMemoryKind::File>;

///////////////////////////////////////////////////////////////////////////////
// FileTemplate
///////////////////////////////////////////////////////////////////////////////

// We expect to find this at every index in a file.
static const uint64_t MagicValue = 0xd3e7f5fa + 0;

// Information in a file index about a chunk.
struct FileIndexChunk
{
  uint32_t /* StreamName */ mName;
  uint32_t mNameIndex;
  StreamChunkLocation mChunk;

  FileIndexChunk(StreamName aName, uint32_t aNameIndex, const StreamChunkLocation& aChunk)
    : mName((uint32_t) aName), mNameIndex(aNameIndex), mChunk(aChunk)
  {}
};

// Index of chunks in a file. There is an index at the start of the file
// (which is always empty) and at various places within the file itself.
struct FileIndex
{
  // This should match MagicValue.
  uint32_t mMagic;

  // How many FileIndexChunk instances follow this structure.
  uint32_t mNumChunks;

  // The location of the next index in the file, or zero.
  uint64_t mNextIndexOffset;

  explicit FileIndex(uint32_t aNumChunks)
    : mMagic(MagicValue), mNumChunks(aNumChunks), mNextIndexOffset(0)
  {}
};

template <AllocatedMemoryKind Kind>
void
FileTemplate<Kind>::SetFilename(const char* aFilename)
{
  MOZ_RELEASE_ASSERT(!mFilename);
  mFilename = AllocateMemory(strlen(aFilename) + 1);
  strcpy(mFilename, aFilename);
}

template <AllocatedMemoryKind Kind>
bool
FileTemplate<Kind>::Open(const char* aName, Mode aMode)
{
  MOZ_RELEASE_ASSERT(!mFd);
  MOZ_RELEASE_ASSERT(aName);

  SetFilename(aName);

  mMode = aMode;
  mFd = DirectOpenFile(mFilename, mMode == WRITE);

  if (OpenForWriting()) {
    // Write an empty index at the start of the file.
    FileIndex index(0);
    DirectWrite(mFd, &index, sizeof(index));
    mWriteOffset += sizeof(index);
    return true;
  }

  // Read in every index in the file.
  ReadIndexResult result;
  do {
    result = ReadNextIndex(nullptr);
    if (result == ReadIndexResult::InvalidFile) {
      return false;
    }
  } while (result == ReadIndexResult::FoundIndex);

  return true;
}

template <AllocatedMemoryKind Kind>
void
FileTemplate<Kind>::Close()
{
  if (!mFd) {
    return;
  }

  if (OpenForWriting()) {
    Flush();
  }

  DirectCloseFile(mFd);
  DeallocateMemory(mFilename, strlen(mFilename) + 1);

  Clear();
}

template <AllocatedMemoryKind Kind>
typename FileTemplate<Kind>::ReadIndexResult
FileTemplate<Kind>::ReadNextIndex(InfallibleVector<StreamTemplate<Kind>*>* aUpdatedStreams)
{
  // Unlike in the Flush() case, we don't have to worry about other threads
  // attempting to read data from streams in this file while we are reading
  // the new index.
  MOZ_ASSERT(OpenForReading());

  // Read in the last index to see if there is another one.
  DirectSeekFile(mFd, mLastIndexOffset + offsetof(FileIndex, mNextIndexOffset));
  uint64_t nextIndexOffset;
  if (DirectRead(mFd, &nextIndexOffset, sizeof(nextIndexOffset)) != sizeof(nextIndexOffset)) {
    return ReadIndexResult::InvalidFile;
  }
  if (!nextIndexOffset) {
    return ReadIndexResult::EndOfFile;
  }

  mLastIndexOffset = nextIndexOffset;

  FileIndex index(0);
  DirectSeekFile(mFd, nextIndexOffset);
  if (DirectRead(mFd, &index, sizeof(index)) != sizeof(index)) {
    return ReadIndexResult::InvalidFile;
  }
  if (index.mMagic != MagicValue) {
    return ReadIndexResult::InvalidFile;
  }

  MOZ_RELEASE_ASSERT(index.mNumChunks);

  size_t indexBytes = index.mNumChunks * sizeof(FileIndexChunk);
  FileIndexChunk* chunks = (FileIndexChunk*) AllocateMemory(indexBytes);
  if (DirectRead(mFd, chunks, indexBytes) != indexBytes) {
    return ReadIndexResult::InvalidFile;
  }
  for (size_t i = 0; i < index.mNumChunks; i++) {
    const FileIndexChunk& indexChunk = chunks[i];
    StreamTemplate<Kind>* stream =
      OpenStream((StreamName) indexChunk.mName, indexChunk.mNameIndex);
    stream->mChunks.append(indexChunk.mChunk);
    if (aUpdatedStreams) {
      aUpdatedStreams->append(stream);
    }
  }
  DeallocateMemory(chunks, indexBytes);

  return ReadIndexResult::FoundIndex;
}

template <AllocatedMemoryKind Kind>
bool
FileTemplate<Kind>::Flush()
{
  MOZ_ASSERT(OpenForWriting());
  AutoSpinLock lock(mLock);

  InfallibleVector<FileIndexChunk, 0, AllocPolicy<Kind>> newChunks;
  for (auto& vector : mStreams) {
    for (StreamTemplate<Kind>* stream : vector) {
      if (stream) {
        stream->Flush(/* aTakeLock = */ false);
        for (size_t i = stream->mFlushedChunks; i < stream->mChunkIndex; i++) {
          newChunks.emplaceBack(stream->mName, stream->mNameIndex, stream->mChunks[i]);
        }
        stream->mFlushedChunks = stream->mChunkIndex;
      }
    }
  }

  if (newChunks.empty()) {
    return false;
  }

  // Write the new index information at the end of the file.
  uint64_t indexOffset = mWriteOffset;
  size_t indexBytes = newChunks.length() * sizeof(FileIndexChunk);
  FileIndex index(newChunks.length());
  DirectWrite(mFd, &index, sizeof(index));
  DirectWrite(mFd, newChunks.begin(), indexBytes);
  mWriteOffset += sizeof(index) + indexBytes;

  // Update the next index offset for the last index written.
  MOZ_RELEASE_ASSERT(sizeof(index.mNextIndexOffset) == sizeof(indexOffset));
  DirectSeekFile(mFd, mLastIndexOffset + offsetof(FileIndex, mNextIndexOffset));
  DirectWrite(mFd, &indexOffset, sizeof(indexOffset));
  DirectSeekFile(mFd, mWriteOffset);

  mLastIndexOffset = indexOffset;

  return true;
}

template <AllocatedMemoryKind Kind>
StreamChunkLocation
FileTemplate<Kind>::WriteChunk(const char* aStart,
                               size_t aCompressedSize, size_t aDecompressedSize,
                               bool aTakeLock)
{
  Maybe<AutoSpinLock> lock;
  if (aTakeLock) {
    lock.emplace(mLock);
  }

  StreamChunkLocation chunk;
  chunk.mOffset = mWriteOffset;
  chunk.mCompressedSize = aCompressedSize;
  chunk.mDecompressedSize = aDecompressedSize;

  DirectWrite(mFd, aStart, aCompressedSize);
  mWriteOffset += aCompressedSize;

  return chunk;
}

template <AllocatedMemoryKind Kind>
void
FileTemplate<Kind>::ReadChunk(char* aDest, const StreamChunkLocation& aChunk)
{
  AutoSpinLock lock(mLock);
  DirectSeekFile(mFd, aChunk.mOffset);
  size_t res = DirectRead(mFd, aDest, aChunk.mCompressedSize);
  if (res != aChunk.mCompressedSize) {
    MOZ_CRASH();
  }
}

template <AllocatedMemoryKind Kind>
StreamTemplate<Kind>*
FileTemplate<Kind>::OpenStream(StreamName aName, size_t aNameIndex)
{
  AutoSpinLock lock(mLock);

  auto& vector = mStreams[(size_t)aName];

  if (aNameIndex >= vector.length()) {
    vector.appendN(nullptr, aNameIndex + 1 - vector.length());
  }

  StreamTemplate<Kind>*& stream = vector[aNameIndex];
  if (!stream) {
    void* ptr = AllocateMemory(sizeof(StreamTemplate<Kind>));
    stream = new (ptr) StreamTemplate<Kind>(this, aName, aNameIndex);
  }
  return stream;
}

template <AllocatedMemoryKind Kind>
char*
FileTemplate<Kind>::AllocateMemory(size_t aSize)
{
  return (char*) recordreplay::AllocateMemory(aSize, Kind);
}

template <AllocatedMemoryKind Kind>
void
FileTemplate<Kind>::DeallocateMemory(void* aBuf, size_t aSize)
{
  recordreplay::DeallocateMemory(aBuf, aSize, Kind);
}

template class FileTemplate<TrackedMemoryKind>;
template class FileTemplate<UntrackedMemoryKind::File>;

void
InitializeFiles(const char* aTempFile)
{
  // Make sure that all the symbols we will use for writing and reading files
  // are instantiated, so we don't get lazy loads at unexpected places later in
  // execution.
  {
    File file;
    file.Open(aTempFile, File::WRITE);
    Stream* stream = file.OpenStream(StreamName::Main, 0);
    uint32_t token = 0xDEADBEEF;
    stream->WriteBytes(&token, sizeof(uint32_t));
  }
  {
    File file;
    file.Open(aTempFile, File::READ);
    Stream* stream = file.OpenStream(StreamName::Main, 0);
    uint32_t token;
    stream->ReadBytes(&token, sizeof(uint32_t));
    MOZ_RELEASE_ASSERT(token == 0xDEADBEEF);
  }
  DirectDeleteFile(aTempFile);
}

} // namespace recordreplay
} // namespace mozilla
