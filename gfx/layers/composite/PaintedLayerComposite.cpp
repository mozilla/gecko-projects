/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "PaintedLayerComposite.h"
#include "CompositableHost.h"           // for TiledLayerProperties, etc
#include "FrameMetrics.h"               // for FrameMetrics
#include "Units.h"                      // for CSSRect, LayerPixel, etc
#include "gfxUtils.h"                   // for gfxUtils, etc
#include "mozilla/Assertions.h"         // for MOZ_ASSERT, etc
#include "mozilla/gfx/Matrix.h"         // for Matrix4x4
#include "mozilla/gfx/Point.h"          // for Point
#include "mozilla/gfx/Rect.h"           // for RoundedToInt, Rect
#include "mozilla/gfx/Types.h"          // for Filter::Filter::LINEAR
#include "mozilla/layers/Compositor.h"  // for Compositor
#include "mozilla/layers/ContentHost.h"  // for ContentHost
#include "mozilla/layers/Effects.h"     // for EffectChain
#include "mozilla/mozalloc.h"           // for operator delete
#include "nsAString.h"
#include "mozilla/nsRefPtr.h"                   // for nsRefPtr
#include "nsISupportsImpl.h"            // for MOZ_COUNT_CTOR, etc
#include "nsMathUtils.h"                // for NS_lround
#include "nsString.h"                   // for nsAutoCString
#include "TextRenderer.h"
#include "GeckoProfiler.h"

namespace mozilla {
namespace layers {

PaintedLayerComposite::PaintedLayerComposite(LayerManagerComposite *aManager)
  : PaintedLayer(aManager, nullptr)
  , LayerComposite(aManager)
  , mBuffer(nullptr)
{
  MOZ_COUNT_CTOR(PaintedLayerComposite);
  mImplData = static_cast<LayerComposite*>(this);
}

PaintedLayerComposite::~PaintedLayerComposite()
{
  MOZ_COUNT_DTOR(PaintedLayerComposite);
  CleanupResources();
}

bool
PaintedLayerComposite::SetCompositableHost(CompositableHost* aHost)
{
  switch (aHost->GetType()) {
    case CompositableType::CONTENT_TILED:
    case CompositableType::CONTENT_SINGLE:
    case CompositableType::CONTENT_DOUBLE:
      mBuffer = static_cast<ContentHost*>(aHost);
      return true;
    default:
      return false;
  }
}

void
PaintedLayerComposite::Disconnect()
{
  Destroy();
}

void
PaintedLayerComposite::Destroy()
{
  if (!mDestroyed) {
    CleanupResources();
    mDestroyed = true;
  }
}

Layer*
PaintedLayerComposite::GetLayer()
{
  return this;
}

void
PaintedLayerComposite::SetLayerManager(LayerManagerComposite* aManager)
{
  LayerComposite::SetLayerManager(aManager);
  mManager = aManager;
  if (mBuffer && mCompositor) {
    mBuffer->SetCompositor(mCompositor);
  }
}

LayerRenderState
PaintedLayerComposite::GetRenderState()
{
  if (!mBuffer || !mBuffer->IsAttached() || mDestroyed) {
    return LayerRenderState();
  }
  return mBuffer->GetRenderState();
}

void
PaintedLayerComposite::RenderLayer(const gfx::IntRect& aClipRect)
{
  if (!mBuffer || !mBuffer->IsAttached()) {
    return;
  }
  PROFILER_LABEL("PaintedLayerComposite", "RenderLayer",
    js::ProfileEntry::Category::GRAPHICS);

  Compositor* compositor = mCompositeManager->GetCompositor();

  MOZ_ASSERT(mBuffer->GetCompositor() == compositor &&
             mBuffer->GetLayer() == this,
             "buffer is corrupted");

  const nsIntRegion& visibleRegion = GetEffectiveVisibleRegion();
  gfx::Rect clipRect(aClipRect.x, aClipRect.y, aClipRect.width, aClipRect.height);

#ifdef MOZ_DUMP_PAINTING
  if (gfxUtils::sDumpPainting) {
    RefPtr<gfx::DataSourceSurface> surf = mBuffer->GetAsSurface();
    if (surf) {
      WriteSnapshotToDumpFile(this, surf);
    }
  }
#endif

  if (gfxUtils::sDumpDebug) {
    nsIntRect lbounds = GetLayerBounds();
    nsIntRect bounds = visibleRegion.GetBounds();
    const gfx::Matrix4x4& xform = GetEffectiveTransform();
    printf_stderr("PaintedLayer[%p]: bounds: [%d %d %d %d] visible: [%d %d %d %d] clip: [%.2f %.2f %.2f %.2f]\n",
                  this,
                  lbounds.X(), lbounds.Y(), lbounds.Width(), lbounds.Height(),
                  bounds.X(), bounds.Y(), bounds.Width(), bounds.Height(),
                  clipRect.X(), clipRect.Y(), clipRect.Width(), clipRect.Height());
    if (xform.IsTranslation()) {
      printf_stderr("                  xform: [translate %.2f %.2f %.2f]\n", xform._41, xform._42, xform._43);
    } else {
      printf_stderr("   xform: [%7.6f %7.6f %7.6f %7.6f]\n", xform._11, xform._12, xform._13, xform._14);
      printf_stderr("          [%7.6f %7.6f %7.6f %7.6f]\n", xform._21, xform._22, xform._23, xform._24);
      printf_stderr("          [%7.6f %7.6f %7.6f %7.6f]\n", xform._31, xform._32, xform._33, xform._34);
      printf_stderr("          [%7.6f %7.6f %7.6f %7.6f]\n", xform._41, xform._42, xform._43, xform._44);
    }
  }

 RenderWithAllMasks(this, compositor, aClipRect,
                     [&](EffectChain& effectChain, const Rect& clipRect) {
    mBuffer->SetPaintWillResample(MayResample());

    mBuffer->Composite(this, effectChain,
                       GetEffectiveOpacity(),
                       GetEffectiveTransform(),
                       GetEffectFilter(),
                       clipRect,
                       &visibleRegion);
  });

  mBuffer->BumpFlashCounter();

  compositor->MakeCurrent();
}

CompositableHost*
PaintedLayerComposite::GetCompositableHost()
{
  if (mBuffer && mBuffer->IsAttached()) {
    return mBuffer.get();
  }

  return nullptr;
}

void
PaintedLayerComposite::CleanupResources()
{
  if (mBuffer) {
    mBuffer->Detach(this);
  }
  mBuffer = nullptr;
}

void
PaintedLayerComposite::GenEffectChain(EffectChain& aEffect)
{
  aEffect.mLayerRef = this;
  aEffect.mPrimaryEffect = mBuffer->GenEffect(GetEffectFilter());
}

void
PaintedLayerComposite::PrintInfo(std::stringstream& aStream, const char* aPrefix)
{
  PaintedLayer::PrintInfo(aStream, aPrefix);
  if (mBuffer && mBuffer->IsAttached()) {
    aStream << "\n";
    nsAutoCString pfx(aPrefix);
    pfx += "  ";
    mBuffer->PrintInfo(aStream, pfx.get());
  }
}

} // namespace layers
} // namespace mozilla
