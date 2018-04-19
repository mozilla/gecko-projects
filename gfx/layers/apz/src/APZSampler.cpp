/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/layers/APZSampler.h"

#include "APZCTreeManager.h"
#include "mozilla/layers/CompositorThread.h"

namespace mozilla {
namespace layers {

APZSampler::APZSampler(const RefPtr<APZCTreeManager>& aApz)
  : mApz(aApz)
{
}

APZSampler::~APZSampler()
{
}

void
APZSampler::ClearTree()
{
  MOZ_ASSERT(CompositorThreadHolder::IsInCompositorThread());
  mApz->ClearTree();
}

void
APZSampler::UpdateFocusState(uint64_t aRootLayerTreeId,
                             uint64_t aOriginatingLayersId,
                             const FocusTarget& aFocusTarget)
{
  MOZ_ASSERT(CompositorThreadHolder::IsInCompositorThread());
  mApz->UpdateFocusState(aRootLayerTreeId, aOriginatingLayersId, aFocusTarget);
}

void
APZSampler::UpdateHitTestingTree(uint64_t aRootLayerTreeId,
                                 Layer* aRoot,
                                 bool aIsFirstPaint,
                                 uint64_t aOriginatingLayersId,
                                 uint32_t aPaintSequenceNumber)
{
  MOZ_ASSERT(CompositorThreadHolder::IsInCompositorThread());
  mApz->UpdateHitTestingTree(aRootLayerTreeId, aRoot, aIsFirstPaint,
      aOriginatingLayersId, aPaintSequenceNumber);
}

void
APZSampler::UpdateHitTestingTree(uint64_t aRootLayerTreeId,
                                 const WebRenderScrollData& aScrollData,
                                 bool aIsFirstPaint,
                                 uint64_t aOriginatingLayersId,
                                 uint32_t aPaintSequenceNumber)
{
  MOZ_ASSERT(CompositorThreadHolder::IsInCompositorThread());
  mApz->UpdateHitTestingTree(aRootLayerTreeId, aScrollData, aIsFirstPaint,
      aOriginatingLayersId, aPaintSequenceNumber);
}

void
APZSampler::NotifyLayerTreeAdopted(uint64_t aLayersId,
                                   const RefPtr<APZSampler>& aOldSampler)
{
  MOZ_ASSERT(CompositorThreadHolder::IsInCompositorThread());
  mApz->NotifyLayerTreeAdopted(aLayersId, aOldSampler ? aOldSampler->mApz : nullptr);
}

void
APZSampler::NotifyLayerTreeRemoved(uint64_t aLayersId)
{
  MOZ_ASSERT(CompositorThreadHolder::IsInCompositorThread());
  mApz->NotifyLayerTreeRemoved(aLayersId);
}

bool
APZSampler::PushStateToWR(wr::TransactionBuilder& aTxn,
                          const TimeStamp& aSampleTime,
                          nsTArray<wr::WrTransformProperty>& aTransformArray)
{
  // This function will be removed eventually since we'll have WR pull
  // the transforms from APZ instead.
  return mApz->PushStateToWR(aTxn, aSampleTime, aTransformArray);
}

bool
APZSampler::GetAPZTestData(uint64_t aLayersId,
                           APZTestData* aOutData)
{
  MOZ_ASSERT(CompositorThreadHolder::IsInCompositorThread());
  return mApz->GetAPZTestData(aLayersId, aOutData);
}

void
APZSampler::SetTestAsyncScrollOffset(uint64_t aLayersId,
                                     const FrameMetrics::ViewID& aScrollId,
                                     const CSSPoint& aOffset)
{
  MOZ_ASSERT(CompositorThreadHolder::IsInCompositorThread());
  RefPtr<AsyncPanZoomController> apzc = mApz->GetTargetAPZC(aLayersId, aScrollId);
  if (apzc) {
    apzc->SetTestAsyncScrollOffset(aOffset);
  } else {
    NS_WARNING("Unable to find APZC in SetTestAsyncScrollOffset");
  }
}

void
APZSampler::SetTestAsyncZoom(uint64_t aLayersId,
                             const FrameMetrics::ViewID& aScrollId,
                             const LayerToParentLayerScale& aZoom)
{
  MOZ_ASSERT(CompositorThreadHolder::IsInCompositorThread());
  RefPtr<AsyncPanZoomController> apzc = mApz->GetTargetAPZC(aLayersId, aScrollId);
  if (apzc) {
    apzc->SetTestAsyncZoom(aZoom);
  } else {
    NS_WARNING("Unable to find APZC in SetTestAsyncZoom");
  }
}

} // namespace layers
} // namespace mozilla
