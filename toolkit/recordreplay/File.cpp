/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "File.h"

#include "mozilla/Compression.h"
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

  MaybeFixupAfterRecordingRewind();

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
    MOZ_RELEASE_ASSERT(mChunkIndex < mChunks.length());

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

  MaybeFixupAfterRecordingRewind();
  return mBufferPos == mBufferLength && mChunkIndex == mChunks.length();
}

template <AllocatedMemoryKind Kind>
void
StreamTemplate<Kind>::WriteBytes(const void* aData, size_t aSize)
{
  MOZ_RELEASE_ASSERT(mFile->OpenForWriting());

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

    Flush();
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
    char buf[4096];
    snprintf(buf, sizeof(buf),
             "Input Mismatch: Recorded: %d Replayed %d\n", (int) oldValue, (int) aValue);
    buf[sizeof(buf) - 1] = 0;
    child::ReportFatalError(buf);
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
StreamTemplate<Kind>::Flush()
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

  StreamChunkLocation chunk = mFile->WriteChunk(mBallast, compressedSize, mBufferPos);
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

template <AllocatedMemoryKind Kind>
void
StreamTemplate<Kind>::MaybeFixupAfterRecordingRewind()
{
  MOZ_RELEASE_ASSERT(mFile->OpenForReading());

  if (!mNeedsFixupAfterRecordingRewind) {
    return;
  }
  mNeedsFixupAfterRecordingRewind = false;

  // Remember where we are in the current chunk.
  size_t pos = mBufferPos;

  // Reset state so that we are at the start of the chunk.
  mStreamPos -= mBufferPos;
  mBufferPos = 0;
  mBufferLength = 0;

  // Advance to the position we were in earlier.
  ReadBytes(nullptr, pos);
}

template class StreamTemplate<AllocatedMemoryKind::Tracked>;
template class StreamTemplate<AllocatedMemoryKind::Untracked>;

///////////////////////////////////////////////////////////////////////////////
// FileTemplate
///////////////////////////////////////////////////////////////////////////////

// We expect to find this at the start of every generated file.
static const uint64_t MagicValue = 0xd3e7f5fa + 0;

struct FileHeader
{
  uint32_t mMagic;
  uint32_t mIndexLength;
  uint64_t mIndexOffset;
};

template <AllocatedMemoryKind Kind>
template <typename T>
/* static */ bool
FileTemplate<Kind>::ReadForIndex(char** aBuf, char* aLimit, T* aRes)
{
  if (*aBuf + sizeof(T) > aLimit) {
    return false;
  }
  memcpy(aRes, *aBuf, sizeof(T));
  (*aBuf) += sizeof(T);
  return true;
}

template <AllocatedMemoryKind Kind>
template <typename T>
/* static */ void
FileTemplate<Kind>::WriteForIndex(IndexBuffer& aBuf, const T& aSrc)
{
  aBuf.append((const char*) &aSrc, sizeof(T));
}

template <AllocatedMemoryKind Kind>
bool
FileTemplate<Kind>::ReadStreamFromIndex(char** aBuf, char* aLimit)
{
  StreamName name;
  uint32_t nameIndex;
  if (!ReadForIndex<StreamName>(aBuf, aLimit, &name) ||
      !ReadForIndex<uint32_t>(aBuf, aLimit, &nameIndex)) {
    return false;
  }

  StreamTemplate<Kind>* stream = OpenStream(name, nameIndex);

  uint32_t numChunks;
  if (!ReadForIndex<uint32_t>(aBuf, aLimit, &numChunks)) {
    return false;
  }
  MOZ_RELEASE_ASSERT(numChunks >= stream->mChunks.length());

  for (size_t i = 0; i < numChunks; i++) {
    StreamChunkLocation chunk;
    if (!ReadForIndex<StreamChunkLocation>(aBuf, aLimit, &chunk)) {
      return false;
    }
    if (i < stream->mChunks.length()) {
      MOZ_RELEASE_ASSERT(chunk == stream->mChunks[i]);
    } else {
      stream->mChunks.append(chunk);
    }
  }

  return true;
}

template <AllocatedMemoryKind Kind>
/* static */ void
FileTemplate<Kind>::WriteStreamToIndex(IndexBuffer& aBuf, const StreamTemplate<Kind>& aStream)
{
  WriteForIndex<StreamName>(aBuf, aStream.mName);
  WriteForIndex<uint32_t>(aBuf, aStream.mNameIndex);

  WriteForIndex<uint32_t>(aBuf, aStream.mChunks.length());
  for (auto chunk : aStream.mChunks) {
    WriteForIndex<StreamChunkLocation>(aBuf, chunk);
  }
}

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
FileTemplate<Kind>::Open(const char* aName, size_t aIndex, Mode aMode)
{
  MOZ_RELEASE_ASSERT(!mFd);
  MOZ_RELEASE_ASSERT(aName);

  if (aIndex == (size_t) -1) {
    SetFilename(aName);
  } else {
    char filename[1024];
    size_t nchars = snprintf(filename, sizeof(filename), "%s_%d", aName, (int) aIndex);
    MOZ_RELEASE_ASSERT(nchars < sizeof(filename));
    SetFilename(filename);
  }

  mMode = aMode;
  mFd = DirectOpenFile(mFilename, mMode == WRITE);

  if (OpenForWriting()) {
    // Write a dummy header at the start of the file.
    FileHeader header;
    PodZero(&header);
    DirectWrite(mFd, &header, sizeof(header));
    mWriteOffset += sizeof(header);
    return true;
  }

  return ReadIndex();
}

template <AllocatedMemoryKind Kind>
void
FileTemplate<Kind>::Close()
{
  if (!mFd) {
    return;
  }

  if (OpenForWriting()) {
    size_t numStreams = 0;
    ForEachStream([&](StreamTemplate<Kind>* aStream) { aStream->Flush(); numStreams++; });

    IndexBuffer buffer;
    WriteForIndex<uint32_t>(buffer, numStreams);
    ForEachStream([&](StreamTemplate<Kind>* aStream) { WriteStreamToIndex(buffer, *aStream); });

    DirectWrite(mFd, buffer.begin(), buffer.length());
    DirectSeekFile(mFd, 0);

    FileHeader header;
    header.mMagic = MagicValue;
    header.mIndexLength = buffer.length();
    header.mIndexOffset = mWriteOffset;
    DirectWrite(mFd, &header, sizeof(header));
  }

  DirectCloseFile(mFd);
  DeallocateMemory(mFilename, strlen(mFilename) + 1);

  Clear();
}

template <AllocatedMemoryKind Kind>
bool
FileTemplate<Kind>::ReadIndex()
{
  MOZ_RELEASE_ASSERT(OpenForReading());

  FileHeader header;
  DirectSeekFile(mFd, 0);
  DirectRead(mFd, &header, sizeof(header));
  if (header.mMagic != MagicValue) {
    return false;
  }

  char* indexBuffer = AllocateMemory(header.mIndexLength);
  DirectSeekFile(mFd, header.mIndexOffset);
  if (DirectRead(mFd, indexBuffer, header.mIndexLength) != header.mIndexLength) {
    return false;
  }

  char* limit = indexBuffer + header.mIndexLength;
  uint32_t numStreams;
  if (!ReadForIndex<uint32_t>(&indexBuffer, limit, &numStreams)) {
    return false;
  }
  for (size_t i = 0; i < numStreams; i++) {
    if (!ReadStreamFromIndex(&indexBuffer, limit)) {
      return false;
    }
  }
  if (indexBuffer != limit) {
    return false;
  }

  return true;
}

template <AllocatedMemoryKind Kind>
void
FileTemplate<Kind>::CloneFrom(const FileTemplate<Kind>& aOther)
{
  MOZ_RELEASE_ASSERT(OpenForWriting());
  for (auto& vector : mStreams) {
    MOZ_RELEASE_ASSERT(vector.empty());
  }

  InfallibleVector<char> buffer;

  FileHandle readFd = DirectOpenFile(aOther.mFilename, false);

  aOther.ForEachStream([&](StreamTemplate<Kind>* aOtherStream) {
      StreamTemplate<Kind>* stream = OpenStream(aOtherStream->mName, aOtherStream->mNameIndex);

      for (auto otherChunk : aOtherStream->mChunks) {
        if (otherChunk.mCompressedSize > buffer.length()) {
          buffer.appendN(0, otherChunk.mCompressedSize - buffer.length());
        }

        DirectSeekFile(readFd, otherChunk.mOffset);
        size_t res = DirectRead(readFd, buffer.begin(), otherChunk.mCompressedSize);
        if (res != otherChunk.mCompressedSize) {
          MOZ_CRASH();
        }

        StreamChunkLocation newChunk =
          WriteChunk(buffer.begin(), otherChunk.mCompressedSize, otherChunk.mDecompressedSize);
        stream->mChunks.append(newChunk);
        MOZ_ALWAYS_TRUE(++stream->mChunkIndex == stream->mChunks.length());
      }

      if (aOther.OpenForWriting()) {
        stream->WriteBytes(aOtherStream->mBuffer, aOtherStream->mBufferPos);
        stream->mStreamPos = aOtherStream->mStreamPos;
      }
    });

  DirectCloseFile(readFd);
}

template <AllocatedMemoryKind Kind>
void
FileTemplate<Kind>::FixupAfterRecordingRewind(FileHandle aFd)
{
  MOZ_RELEASE_ASSERT(OpenForWriting());

  ForEachStream([&](StreamTemplate<Kind>* aStream) {
      aStream->mNeedsFixupAfterRecordingRewind = true;
    });

  mFd = aFd;
  mMode = READ;
  mWriteOffset = 0;

  ReadIndex();
}

template <AllocatedMemoryKind Kind>
void
FileTemplate<Kind>::ForEachStream(std::function<void(StreamTemplate<Kind>*)> aCallback) const
{
  for (auto& vector : mStreams) {
    for (auto stream : vector) {
      if (stream) {
        aCallback(stream);
      }
    }
  }
}

template <AllocatedMemoryKind Kind>
StreamChunkLocation
FileTemplate<Kind>::WriteChunk(const char* aStart,
                               size_t aCompressedSize, size_t aDecompressedSize)
{
  AutoSpinLock lock(mLock);

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
FileTemplate<Kind>::DeallocateMemory(char* aBuf, size_t aSize)
{
  recordreplay::DeallocateMemory(aBuf, aSize, Kind);
}

template class FileTemplate<AllocatedMemoryKind::Tracked>;
template class FileTemplate<AllocatedMemoryKind::Untracked>;

void
InitializeFiles(const char* aTempFile)
{
  // Make sure that all the symbols we will use for writing and reading files
  // are instantiated, so we don't get lazy loads at unexpected places later in
  // execution.
  {
    File file;
    file.Open(aTempFile, (size_t) -1, File::WRITE);
    Stream* stream = file.OpenStream(StreamName::Main, 0);
    uint32_t token = 0xDEADBEEF;
    stream->WriteBytes(&token, sizeof(uint32_t));
  }
  {
    File file;
    file.Open(aTempFile, (size_t) -1, File::READ);
    Stream* stream = file.OpenStream(StreamName::Main, 0);
    uint32_t token;
    stream->ReadBytes(&token, sizeof(uint32_t));
    MOZ_RELEASE_ASSERT(token == 0xDEADBEEF);
  }
}

} // namespace recordreplay
} // namespace mozilla
