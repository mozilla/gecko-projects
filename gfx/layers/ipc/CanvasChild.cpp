/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "CanvasChild.h"

#include "MainThreadUtils.h"
#include "mozilla/gfx/DrawTargetRecording.h"
#include "mozilla/gfx/Tools.h"
#include "mozilla/layers/CanvasDrawEventRecorder.h"
#include "RecordedCanvasEventImpl.h"

namespace mozilla {
namespace layers {

static const TimeDuration kLockWaitTimeout =
    TimeDuration::FromMilliseconds(100);
static const TimeDuration kGetDataTimeout = TimeDuration::FromMilliseconds(500);

CanvasChild::CanvasChild(Endpoint<PCanvasChild>&& aEndpoint) {
  aEndpoint.Bind(this);
  mCanSend = true;
}

CanvasChild::~CanvasChild() {}

void CanvasChild::EnsureRecorder(TextureType aTextureType) {
  if (!mRecorder) {
    MOZ_ASSERT(mTextureType == TextureType::Unknown);
    mTextureType = aTextureType;
    mRecorder = MakeAndAddRef<CanvasDrawEventRecorder>();
    SharedMemoryBasic::Handle handle;
    CrossProcessSemaphoreHandle readerSem;
    CrossProcessSemaphoreHandle writerSem;
    RefPtr<CanvasChild> thisRef = this;
    mRecorder->Init(OtherPid(), &handle, &readerSem, &writerSem,
                    [cc = std::move(thisRef)] { cc->ResumeTranslation(); });

    if (mCanSend) {
      Unused << SendCreateTranslator(mTextureType, handle, readerSem,
                                     writerSem);
    }
  }

  MOZ_RELEASE_ASSERT(mTextureType == aTextureType,
                     "We only support one remote TextureType currently.");
}

void CanvasChild::ActorDestroy(ActorDestroyReason aWhy) {
  mCanSend = false;

  // Explicitly drop our reference to the recorder, because it holds a reference
  // to us via the ResumeTranslation callback.
  mRecorder = nullptr;
}

void CanvasChild::ResumeTranslation() {
  if (mCanSend) {
    SendResumeTranslation();
  }
}

void CanvasChild::Destroy() { Close(); }

void CanvasChild::OnTextureWriteLock() {
  mHasOutstandingWriteLock = true;
  mLastWriteLockCheckpoint = mRecorder->CreateCheckpoint();
}

void CanvasChild::OnTextureForwarded() {
  if (mHasOutstandingWriteLock) {
    mRecorder->RecordEvent(RecordedCanvasFlush());
    if (!mRecorder->WaitForCheckpoint(mLastWriteLockCheckpoint,
                                      kLockWaitTimeout)) {
      gfxWarning() << "Timed out waiting for last write lock to be processed.";
    }

    mHasOutstandingWriteLock = false;
  }
}

void CanvasChild::EnsureBeginTransaction() {
  if (!mIsInTransaction) {
    mRecorder->RecordEvent(RecordedCanvasBeginTransaction());
    mIsInTransaction = true;
  }
}

void CanvasChild::EndTransaction() {
  if (mIsInTransaction) {
    mRecorder->RecordEvent(RecordedCanvasEndTransaction());
    mIsInTransaction = false;
  }

  ++mTransactionsSinceGetDataSurface;
}

already_AddRefed<gfx::DrawTarget> CanvasChild::CreateDrawTarget(
    gfx::IntSize aSize, gfx::SurfaceFormat aFormat) {
  MOZ_ASSERT(mRecorder);

  RefPtr<gfx::DrawTarget> dummyDt = gfx::Factory::CreateDrawTarget(
      gfx::BackendType::SKIA, gfx::IntSize(1, 1), aFormat);
  RefPtr<gfx::DrawTarget> dt =
      MakeAndAddRef<gfx::DrawTargetRecording>(mRecorder, dummyDt, aSize);
  return dt.forget();
}

void CanvasChild::RecordEvent(const gfx::RecordedEvent& aEvent) {
  mRecorder->RecordEvent(aEvent);
}

already_AddRefed<gfx::DataSourceSurface> CanvasChild::GetDataSurface(
    const gfx::SourceSurface* aSurface) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aSurface);

  mTransactionsSinceGetDataSurface = 0;
  EnsureBeginTransaction();
  mRecorder->RecordEvent(RecordedPrepareDataForSurface(aSurface));
  uint32_t checkpoint = mRecorder->CreateCheckpoint();

  gfx::IntSize ssSize = aSurface->GetSize();
  gfx::SurfaceFormat ssFormat = aSurface->GetFormat();
  size_t dataFormatWidth = ssSize.width * BytesPerPixel(ssFormat);
  RefPtr<gfx::DataSourceSurface> dataSurface =
      gfx::Factory::CreateDataSourceSurfaceWithStride(ssSize, ssFormat,
                                                      dataFormatWidth);
  if (!dataSurface) {
    gfxWarning() << "Failed to create DataSourceSurface.";
    return nullptr;
  }
  gfx::DataSourceSurface::ScopedMap map(dataSurface,
                                        gfx::DataSourceSurface::READ_WRITE);
  char* dest = reinterpret_cast<char*>(map.GetData());
  if (!mRecorder->WaitForCheckpoint(checkpoint, kGetDataTimeout)) {
    gfxWarning() << "Timed out preparing data for DataSourceSurface.";
    return dataSurface.forget();
  }

  mRecorder->RecordEvent(RecordedGetDataForSurface(aSurface));
  mRecorder->ReturnRead(dest, ssSize.height * dataFormatWidth);

  return dataSurface.forget();
}

}  // namespace layers
}  // namespace mozilla
