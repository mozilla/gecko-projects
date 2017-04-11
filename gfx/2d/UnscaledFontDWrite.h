/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_UNSCALEDFONTDWRITE_H_
#define MOZILLA_GFX_UNSCALEDFONTDWRITE_H_

#include <dwrite.h>

#include "2D.h"

namespace mozilla {
namespace gfx {

class UnscaledFontDWrite final : public UnscaledFont
{
public:
  MOZ_DECLARE_REFCOUNTED_VIRTUAL_TYPENAME(UnscaledFontDWrite, override)
  explicit UnscaledFontDWrite(const RefPtr<IDWriteFontFace>& aFontFace,
                              DWRITE_FONT_SIMULATIONS aSimulations =
                                DWRITE_FONT_SIMULATIONS_NONE)
    : mFontFace(aFontFace),
      mSimulations(aSimulations)
  {}

  FontType GetType() const override { return FontType::DWRITE; }

  const RefPtr<IDWriteFontFace> GetFontFace() const { return mFontFace; }
  DWRITE_FONT_SIMULATIONS GetSimulations() const { return mSimulations; }

private:
  RefPtr<IDWriteFontFace> mFontFace;
  DWRITE_FONT_SIMULATIONS mSimulations;
};

} // namespace gfx
} // namespace mozilla

#endif /* MOZILLA_GFX_UNSCALEDFONTDWRITE_H_ */

