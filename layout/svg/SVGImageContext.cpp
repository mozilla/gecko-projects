/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */


// Main header first:
#include "SVGImageContext.h"

// Keep others in (case-insensitive) order:
#include "gfxUtils.h"
#include "mozilla/Preferences.h"
#include "nsPresContext.h"

namespace mozilla {

bool
SVGImageContext::MaybeStoreContextPaint(nsIFrame* aFromFrame)
{
  static bool sEnabledForContent = false;
  static bool sEnabledForContentCached = false;

  if (!sEnabledForContentCached) {
    Preferences::AddBoolVarCache(&sEnabledForContent,
                                 "svg.context-properties.content.enabled", false);
    sEnabledForContentCached = true;
  }

  if (!sEnabledForContent &&
      !aFromFrame->PresContext()->IsChrome()) {
    // Context paint is pref'ed off for content and this is a content doc.
    return false;
  }

  // XXX return early if the 'context-properties' property is not set.

  bool haveContextPaint = false;

  RefPtr<SVGEmbeddingContextPaint> contextPaint = new SVGEmbeddingContextPaint();

  const nsStyleSVG* style = aFromFrame->StyleSVG();

  // XXX don't set values for properties not listed in 'context-properties'.

  if (style->mFill.Type() == eStyleSVGPaintType_Color) {
    haveContextPaint = true;
    contextPaint->SetFill(style->mFill.GetColor());
  }
  if (style->mStroke.Type() == eStyleSVGPaintType_Color) {
    haveContextPaint = true;
    contextPaint->SetStroke(style->mStroke.GetColor());
  }

  if (haveContextPaint) {
    mContextPaint = contextPaint.forget();
  }

  return mContextPaint != nullptr;
}

} // namespace mozilla
