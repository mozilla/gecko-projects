/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_SOURCESURFACESKIA_H_
#define MOZILLA_GFX_SOURCESURFACESKIA_H_

#include "2D.h"
#include <vector>
#include "mozilla/Mutex.h"
#include "skia/include/core/SkCanvas.h"
#include "skia/include/core/SkImage.h"

namespace mozilla {

namespace gfx {

class DrawTargetSkia;
class SnapshotLock;

class SourceSurfaceSkia : public DataSourceSurface
{
public:
  MOZ_DECLARE_REFCOUNTED_VIRTUAL_TYPENAME(DataSourceSurfaceSkia)
  SourceSurfaceSkia();
  ~SourceSurfaceSkia();

  virtual SurfaceType GetType() const { return SurfaceType::SKIA; }
  virtual IntSize GetSize() const;
  virtual SurfaceFormat GetFormat() const;

  sk_sp<SkImage> GetImage();

  bool InitFromData(unsigned char* aData,
                    const IntSize &aSize,
                    int32_t aStride,
                    SurfaceFormat aFormat);

  bool InitFromImage(const sk_sp<SkImage>& aImage,
                     SurfaceFormat aFormat = SurfaceFormat::UNKNOWN,
                     DrawTargetSkia* aOwner = nullptr,
                     std::shared_ptr<Mutex> aSnapshotLock = std::shared_ptr<Mutex>{});

  virtual uint8_t* GetData();

  /**
   * The caller is responsible for ensuring aMappedSurface is not null.
   */
  virtual bool Map(MapType, MappedSurface *aMappedSurface);

  virtual void Unmap();

  virtual int32_t Stride() { return mStride; }

private:
  friend class DrawTargetSkia;

  void DrawTargetWillChange();

  sk_sp<SkImage> mImage;
  SurfaceFormat mFormat;
  IntSize mSize;
  int32_t mStride;
  RefPtr<DrawTargetSkia> mDrawTarget;
  std::shared_ptr<Mutex> mSnapshotLock;
  Mutex mChangeMutex;
};

} // namespace gfx
} // namespace mozilla

#endif /* MOZILLA_GFX_SOURCESURFACESKIA_H_ */
