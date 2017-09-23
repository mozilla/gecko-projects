/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_WR_IPCRESOURCEUPDATEQUEUE_H
#define GFX_WR_IPCRESOURCEUPDATEQUEUE_H

#include "mozilla/layers/WebRenderMessages.h"
#include "mozilla/webrender/WebRenderTypes.h"

namespace mozilla {
namespace ipc {
class IShmemAllocator;
}
namespace wr {

/// ShmSegmentsWriter pushes bytes in a sequence of fixed size shmems for small
/// allocations and creates dedicated shmems for large allocations.
class ShmSegmentsWriter {
public:
  ShmSegmentsWriter(ipc::IShmemAllocator* aAllocator, size_t aChunkSize);
  ~ShmSegmentsWriter();

  layers::OffsetRange Write(Range<uint8_t> aBytes);

  template<typename T>
  layers::OffsetRange WriteAsBytes(Range<T> aValues)
  {
    return Write(Range<uint8_t>((uint8_t*)aValues.begin().get(), aValues.length() * sizeof(T)));
  }

  void Flush(nsTArray<ipc::Shmem>& aSmallAllocs, nsTArray<ipc::Shmem>& aLargeAllocs);

  void Clear();

protected:
  void AllocChunk();
  layers::OffsetRange AllocLargeChunk(size_t aSize);

  nsTArray<ipc::Shmem> mSmallAllocs;
  nsTArray<ipc::Shmem> mLargeAllocs;
  ipc::IShmemAllocator* mShmAllocator;
  size_t mCursor;
  size_t mChunkSize;
};

class ShmSegmentsReader {
public:
  ShmSegmentsReader(const nsTArray<ipc::Shmem>& aSmallShmems,
                    const nsTArray<ipc::Shmem>& aLargeShmems);

  bool Read(const layers::OffsetRange& aRange, wr::Vec_u8& aInto);

protected:
  bool ReadLarge(const layers::OffsetRange& aRange, wr::Vec_u8& aInto);

  const nsTArray<ipc::Shmem>& mSmallAllocs;
  const nsTArray<ipc::Shmem>& mLargeAllocs;
  size_t mChunkSize;
};

class IpcResourceUpdateQueue {
public:
  // TODO: 32768 is completely arbitrary, needs some adjustments.
  // Because we are using shmems, the size should be a multiple of the page size, and keep in mind
  // that each shmem has guard pages, one of which contains meta-data so at least an extra page
  // is mapped under the hood (so having a lot of smaller shmems = overhead).
  explicit IpcResourceUpdateQueue(ipc::IShmemAllocator* aAllocator, size_t aChunkSize = 32768);

  void AddImage(wr::ImageKey aKey,
                const ImageDescriptor& aDescriptor,
                Range<uint8_t> aBytes);

  void AddBlobImage(wr::ImageKey aKey,
                    const ImageDescriptor& aDescriptor,
                    Range<uint8_t> aBytes);

  void AddExternalImage(wr::ExternalImageId aExtId, wr::ImageKey aKey);

  void UpdateImageBuffer(wr::ImageKey aKey,
                         const ImageDescriptor& aDescriptor,
                         Range<uint8_t> aBytes);

  void UpdateBlobImage(wr::ImageKey aKey,
                       const ImageDescriptor& aDescriptor,
                       Range<uint8_t> aBytes);

  void UpdateExternalImage(ImageKey aKey,
                           const ImageDescriptor& aDescriptor,
                           ExternalImageId aExtID,
                           wr::WrExternalImageBufferType aBufferType,
                           uint8_t aChannelIndex = 0);

  void DeleteImage(wr::ImageKey aKey);

  void AddRawFont(wr::FontKey aKey, Range<uint8_t> aBytes, uint32_t aIndex);

  void DeleteFont(wr::FontKey aKey);

  void AddFontInstance(wr::FontInstanceKey aKey,
                       wr::FontKey aFontKey,
                       float aGlyphSize,
                       const wr::FontInstanceOptions* aOptions,
                       const wr::FontInstancePlatformOptions* aPlatformOptions,
                       Range<const gfx::FontVariation> aVariations);

  void DeleteFontInstance(wr::FontInstanceKey aKey);

  void Clear();

  void Flush(nsTArray<layers::OpUpdateResource>& aUpdates,
             nsTArray<ipc::Shmem>& aSmallAllocs,
             nsTArray<ipc::Shmem>& aLargeAllocs);

protected:
  ShmSegmentsWriter mWriter;
  nsTArray<layers::OpUpdateResource> mUpdates;
};

} // namespace
} // namespace

#endif
