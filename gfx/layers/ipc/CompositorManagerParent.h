/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_COMPOSITORMANAGERPARENT_H
#define MOZILLA_GFX_COMPOSITORMANAGERPARENT_H

#include <stdint.h>                     // for uint32_t
#include "mozilla/Attributes.h"         // for override
#include "mozilla/StaticPtr.h"          // for StaticRefPtr
#include "mozilla/StaticMutex.h"        // for StaticMutex
#include "mozilla/RefPtr.h"             // for already_AddRefed
#include "mozilla/layers/PCompositorManagerParent.h"
#include "nsTArray.h"                   // for AutoTArray

namespace mozilla {
namespace layers {

class CompositorBridgeParent;
class CompositorThreadHolder;

class CompositorManagerParent final : public PCompositorManagerParent
{
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(CompositorManagerParent)

public:
  static already_AddRefed<CompositorManagerParent> CreateSameProcess();
  static void Create(Endpoint<PCompositorManagerParent>&& aEndpoint);

  static already_AddRefed<CompositorBridgeParent>
  CreateSameProcessWidgetCompositorBridge(CSSToLayoutDeviceScale aScale,
                                          const CompositorOptions& aOptions,
                                          bool aUseExternalSurfaceSize,
                                          const gfx::IntSize& aSurfaceSize);

  void ActorDestroy(ActorDestroyReason aReason) override;

  bool DeallocPCompositorBridgeParent(PCompositorBridgeParent* aActor) override;
  PCompositorBridgeParent* AllocPCompositorBridgeParent(const CompositorBridgeOptions& aOpt) override;

private:
  static StaticRefPtr<CompositorManagerParent> sInstance;
  static StaticMutex sMutex;

  CompositorManagerParent();
  ~CompositorManagerParent() override;

  void Bind(Endpoint<PCompositorManagerParent>&& aEndpoint);

  void DeallocPCompositorManagerParent() override;

  RefPtr<CompositorThreadHolder> mCompositorThreadHolder;

  AutoTArray<RefPtr<CompositorBridgeParent>, 1> mPendingCompositorBridges;
};

} // namespace layers
} // namespace mozilla

#endif
