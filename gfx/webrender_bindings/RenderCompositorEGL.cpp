/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "RenderCompositorEGL.h"

#include "GLContext.h"
#include "GLContextEGL.h"
#include "GLContextProvider.h"
#include "GLLibraryEGL.h"
#include "mozilla/webrender/RenderThread.h"
#include "mozilla/widget/CompositorWidget.h"

#ifdef MOZ_WAYLAND
#  include "mozilla/widget/GtkCompositorWidget.h"
#  include <gdk/gdk.h>
#  include <gdk/gdkx.h>
#endif

#ifdef MOZ_WIDGET_ANDROID
#  include "GeneratedJNIWrappers.h"
#endif

namespace mozilla {
namespace wr {

/* static */
UniquePtr<RenderCompositor> RenderCompositorEGL::Create(
    RefPtr<widget::CompositorWidget> aWidget) {
#ifdef MOZ_WAYLAND
  if (GDK_IS_X11_DISPLAY(gdk_display_get_default())) {
    return nullptr;
  }
#endif
  if (!RenderThread::Get()->SharedGL()) {
    gfxCriticalNote << "Failed to get shared GL context";
    return nullptr;
  }
  return MakeUnique<RenderCompositorEGL>(aWidget);
}

EGLSurface RenderCompositorEGL::CreateEGLSurface() {
  EGLSurface surface = EGL_NO_SURFACE;
  surface = gl::GLContextEGL::CreateEGLSurfaceForCompositorWidget(
      mWidget, gl::GLContextEGL::Cast(gl())->mConfig);
  if (surface == EGL_NO_SURFACE) {
    gfxCriticalNote << "Failed to create EGLSurface";
  }
  return surface;
}

RenderCompositorEGL::RenderCompositorEGL(
    RefPtr<widget::CompositorWidget> aWidget)
    : RenderCompositor(std::move(aWidget)), mEGLSurface(EGL_NO_SURFACE) {}

RenderCompositorEGL::~RenderCompositorEGL() { DestroyEGLSurface(); }

bool RenderCompositorEGL::BeginFrame() {
#ifdef MOZ_WAYLAND
  if (mWidget->AsX11() &&
      mWidget->AsX11()->WaylandRequestsUpdatingEGLSurface()) {
    // Destroy EGLSurface if it exists.
    DestroyEGLSurface();
    mEGLSurface = CreateEGLSurface();
    if (mEGLSurface) {
      const auto* egl = gl::GLLibraryEGL::Get();
      // Make eglSwapBuffers() non-blocking on wayland
      egl->fSwapInterval(gl::EGL_DISPLAY(), 0);
    }
  }
#endif
  if (!MakeCurrent()) {
    gfxCriticalNote << "Failed to make render context current, can't draw.";
    return false;
  }

#ifdef MOZ_WIDGET_ANDROID
  java::GeckoSurfaceTexture::DestroyUnused((int64_t)gl());
#endif

  return true;
}

void RenderCompositorEGL::EndFrame() {
  if (mEGLSurface != EGL_NO_SURFACE) {
    gl()->SwapBuffers();
  }
}

void RenderCompositorEGL::WaitForGPU() {}

void RenderCompositorEGL::Pause() {
#ifdef MOZ_WIDGET_ANDROID
  java::GeckoSurfaceTexture::DestroyUnused((int64_t)gl());
  java::GeckoSurfaceTexture::DetachAllFromGLContext((int64_t)gl());
  DestroyEGLSurface();
#endif
}

bool RenderCompositorEGL::Resume() {
#ifdef MOZ_WIDGET_ANDROID
  // Destroy EGLSurface if it exists.
  DestroyEGLSurface();
  mEGLSurface = CreateEGLSurface();
  gl::GLContextEGL::Cast(gl())->SetEGLSurfaceOverride(mEGLSurface);
#endif
  return true;
}

gl::GLContext* RenderCompositorEGL::gl() const {
  return RenderThread::Get()->SharedGL();
}

bool RenderCompositorEGL::MakeCurrent() {
  gl::GLContextEGL::Cast(gl())->SetEGLSurfaceOverride(mEGLSurface);
  return gl()->MakeCurrent();
}

void RenderCompositorEGL::DestroyEGLSurface() {
  auto* egl = gl::GLLibraryEGL::Get();

  // Release EGLSurface of back buffer before calling ResizeBuffers().
  if (mEGLSurface) {
    gl::GLContextEGL::Cast(gl())->SetEGLSurfaceOverride(EGL_NO_SURFACE);
    egl->fDestroySurface(egl->Display(), mEGLSurface);
    mEGLSurface = nullptr;
  }
}

LayoutDeviceIntSize RenderCompositorEGL::GetBufferSize() {
  return mWidget->GetClientSize();
}

}  // namespace wr
}  // namespace mozilla
