/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gfxFT2FontBase.h"
#include "gfxFT2Utils.h"
#include "harfbuzz/hb.h"
#include "mozilla/Likely.h"
#include "gfxFontConstants.h"
#include "gfxFontUtils.h"
#include <algorithm>
#include <dlfcn.h>

#include FT_TRUETYPE_TAGS_H
#include FT_TRUETYPE_TABLES_H
#include FT_ADVANCES_H
#include FT_MULTIPLE_MASTERS_H

#ifndef FT_LOAD_COLOR
#  define FT_LOAD_COLOR (1L << 20)
#endif
#ifndef FT_FACE_FLAG_COLOR
#  define FT_FACE_FLAG_COLOR (1L << 14)
#endif

using namespace mozilla::gfx;

gfxFT2FontBase::gfxFT2FontBase(
    const RefPtr<UnscaledFontFreeType>& aUnscaledFont,
    cairo_scaled_font_t* aScaledFont,
    RefPtr<mozilla::gfx::SharedFTFace>&& aFTFace, gfxFontEntry* aFontEntry,
    const gfxFontStyle* aFontStyle, int aLoadFlags, bool aEmbolden)
    : gfxFont(aUnscaledFont, aFontEntry, aFontStyle, kAntialiasDefault,
              aScaledFont),
      mFTFace(std::move(aFTFace)),
      mSpaceGlyph(0),
      mFTLoadFlags(aLoadFlags | FT_LOAD_IGNORE_GLOBAL_ADVANCE_WIDTH |
                   FT_LOAD_COLOR),
      mEmbolden(aEmbolden),
      mFTSize(1.0) {
  cairo_scaled_font_reference(mScaledFont);
}

gfxFT2FontBase::~gfxFT2FontBase() { cairo_scaled_font_destroy(mScaledFont); }

FT_Face gfxFT2FontBase::LockFTFace() {
  if (!mFTFace->Lock(this)) {
    FT_Set_Transform(mFTFace->GetFace(), nullptr, nullptr);

    FT_F26Dot6 charSize = NS_lround(mFTSize * 64.0);
    FT_Set_Char_Size(mFTFace->GetFace(), charSize, charSize, 0, 0);
  }
  return mFTFace->GetFace();
}

void gfxFT2FontBase::UnlockFTFace() { mFTFace->Unlock(); }

uint32_t gfxFT2FontBase::GetGlyph(uint32_t aCharCode) {
  // FcFreeTypeCharIndex needs to lock the FT_Face and can end up searching
  // through all the postscript glyph names in the font.  Therefore use a
  // lightweight cache, which is stored on the cairo_font_face_t.

  cairo_font_face_t* face =
      cairo_scaled_font_get_font_face(GetCairoScaledFont());

  if (cairo_font_face_status(face) != CAIRO_STATUS_SUCCESS) return 0;

  // This cache algorithm and size is based on what is done in
  // cairo_scaled_font_text_to_glyphs and pango_fc_font_real_get_glyph.  I
  // think the concept is that adjacent characters probably come mostly from
  // one Unicode block.  This assumption is probably not so valid with
  // scripts with large character sets as used for East Asian languages.

  struct CmapCacheSlot {
    uint32_t mCharCode;
    uint32_t mGlyphIndex;
  };
  const uint32_t kNumSlots = 256;
  static cairo_user_data_key_t sCmapCacheKey;

  CmapCacheSlot* slots = static_cast<CmapCacheSlot*>(
      cairo_font_face_get_user_data(face, &sCmapCacheKey));

  if (!slots) {
    // cairo's caches can keep some cairo_font_faces alive past our last
    // destroy, so the destroy function (free) for the cache must be
    // callable from cairo without any assumptions about what other
    // modules have not been shutdown.
    slots =
        static_cast<CmapCacheSlot*>(calloc(kNumSlots, sizeof(CmapCacheSlot)));
    if (!slots) return 0;

    cairo_status_t status =
        cairo_font_face_set_user_data(face, &sCmapCacheKey, slots, free);
    if (status != CAIRO_STATUS_SUCCESS) {  // OOM
      free(slots);
      return 0;
    }

    // Invalidate slot 0 by setting its char code to something that would
    // never end up in slot 0.  All other slots are already invalid
    // because they have mCharCode = 0 and a glyph for char code 0 will
    // always be in the slot 0.
    slots[0].mCharCode = 1;
  }

  CmapCacheSlot* slot = &slots[aCharCode % kNumSlots];
  if (slot->mCharCode != aCharCode) {
    slot->mCharCode = aCharCode;
    slot->mGlyphIndex = gfxFT2LockedFace(this).GetGlyph(aCharCode);
  }

  return slot->mGlyphIndex;
}

// aScale is intended for a 16.16 x/y_scale of an FT_Size_Metrics
static inline FT_Long ScaleRoundDesignUnits(FT_Short aDesignMetric,
                                            FT_Fixed aScale) {
  FT_Long fixed26dot6 = FT_MulFix(aDesignMetric, aScale);
  return ROUND_26_6_TO_INT(fixed26dot6);
}

// Snap a line to pixels while keeping the center and size of the line as
// close to the original position as possible.
//
// Pango does similar snapping for underline and strikethrough when fonts are
// hinted, but nsCSSRendering::GetTextDecorationRectInternal always snaps the
// top and size of lines.  Optimizing the distance between the line and
// baseline is probably good for the gap between text and underline, but
// optimizing the center of the line is better for positioning strikethough.
static void SnapLineToPixels(gfxFloat& aOffset, gfxFloat& aSize) {
  gfxFloat snappedSize = std::max(floor(aSize + 0.5), 1.0);
  // Correct offset for change in size
  gfxFloat offset = aOffset - 0.5 * (aSize - snappedSize);
  // Snap offset
  aOffset = floor(offset + 0.5);
  aSize = snappedSize;
}

/**
 * Get extents for a simple character representable by a single glyph.
 * The return value is the glyph id of that glyph or zero if no such glyph
 * exists.  aExtents is only set when this returns a non-zero glyph id.
 */
uint32_t gfxFT2FontBase::GetCharExtents(char aChar, gfxFloat* aWidth,
                                        gfxFloat* aHeight) {
  FT_UInt gid = GetGlyph(aChar);
  int32_t width;
  int32_t height;
  if (gid && GetFTGlyphExtents(gid, &width, &height)) {
    *aWidth = FLOAT_FROM_16_16(width);
    *aHeight = FLOAT_FROM_26_6(height);
    return gid;
  } else {
    return 0;
  }
}

/**
 * Get glyph id and width for a simple character.
 * The return value is the glyph id of that glyph or zero if no such glyph
 * exists.  aWidth is only set when this returns a non-zero glyph id.
 * This is just for use during initialization, and doesn't use the width cache.
 */
uint32_t gfxFT2FontBase::GetCharWidth(char aChar, gfxFloat* aWidth) {
  FT_UInt gid = GetGlyph(aChar);
  int32_t width;
  if (gid && GetFTGlyphExtents(gid, &width)) {
    *aWidth = FLOAT_FROM_16_16(width);
    return gid;
  } else {
    return 0;
  }
}

/**
 * Find the closest available fixed strike size, if applicable, to the
 * desired font size.
 */
static double FindClosestSize(FT_Face aFace, double aSize) {
  // FT size selection does not actually support sizes smaller than 1 and will
  // clamp this internally, regardless of what is requested. Do the clamp here
  // instead so that glyph extents/font matrix scaling will compensate it, as
  // Cairo normally would.
  if (aSize < 1.0) {
    aSize = 1.0;
  }
  if (FT_IS_SCALABLE(aFace)) {
    return aSize;
  }
  double bestDist = DBL_MAX;
  FT_Int bestSize = -1;
  for (FT_Int i = 0; i < aFace->num_fixed_sizes; i++) {
    double dist = aFace->available_sizes[i].y_ppem / 64.0 - aSize;
    // If the previous best is smaller than the desired size, prefer
    // a bigger size. Otherwise, just choose whatever size is closest.
    if (bestDist < 0 ? dist >= bestDist : fabs(dist) <= bestDist) {
      bestDist = dist;
      bestSize = i;
    }
  }
  if (bestSize < 0) {
    return aSize;
  }
  return aFace->available_sizes[bestSize].y_ppem / 64.0;
}

void gfxFT2FontBase::InitMetrics() {
  mFUnitsConvFactor = 0.0;

  if (MOZ_UNLIKELY(GetStyle()->size <= 0.0) ||
      MOZ_UNLIKELY(GetStyle()->sizeAdjust == 0.0)) {
    memset(&mMetrics, 0, sizeof(mMetrics));  // zero initialize
    mSpaceGlyph = GetGlyph(' ');
    return;
  }

  // Cairo metrics are normalized to em-space, so that whatever fixed size
  // might actually be chosen is factored out. They are then later scaled by
  // the font matrix to the target adjusted size. Stash the chosen closest
  // size here for later scaling of the metrics.
  mFTSize = FindClosestSize(mFTFace->GetFace(), GetAdjustedSize());

  // Explicitly lock the face so we can release it early before calling
  // back into Cairo below.
  FT_Face face = LockFTFace();

  if (MOZ_UNLIKELY(!face)) {
    // No face.  This unfortunate situation might happen if the font
    // file is (re)moved at the wrong time.
    const gfxFloat emHeight = GetAdjustedSize();
    mMetrics.emHeight = emHeight;
    mMetrics.maxAscent = mMetrics.emAscent = 0.8 * emHeight;
    mMetrics.maxDescent = mMetrics.emDescent = 0.2 * emHeight;
    mMetrics.maxHeight = emHeight;
    mMetrics.internalLeading = 0.0;
    mMetrics.externalLeading = 0.2 * emHeight;
    const gfxFloat spaceWidth = 0.5 * emHeight;
    mMetrics.spaceWidth = spaceWidth;
    mMetrics.maxAdvance = spaceWidth;
    mMetrics.aveCharWidth = spaceWidth;
    mMetrics.zeroWidth = spaceWidth;
    const gfxFloat xHeight = 0.5 * emHeight;
    mMetrics.xHeight = xHeight;
    mMetrics.capHeight = mMetrics.maxAscent;
    const gfxFloat underlineSize = emHeight / 14.0;
    mMetrics.underlineSize = underlineSize;
    mMetrics.underlineOffset = -underlineSize;
    mMetrics.strikeoutOffset = 0.25 * emHeight;
    mMetrics.strikeoutSize = underlineSize;

    SanitizeMetrics(&mMetrics, false);
    return;
  }

  const FT_Size_Metrics& ftMetrics = face->size->metrics;

  mMetrics.maxAscent = FLOAT_FROM_26_6(ftMetrics.ascender);
  mMetrics.maxDescent = -FLOAT_FROM_26_6(ftMetrics.descender);
  mMetrics.maxAdvance = FLOAT_FROM_26_6(ftMetrics.max_advance);
  gfxFloat lineHeight = FLOAT_FROM_26_6(ftMetrics.height);

  gfxFloat emHeight;
  // Scale for vertical design metric conversion: pixels per design unit.
  // If this remains at 0.0, we can't use metrics from OS/2 etc.
  gfxFloat yScale = 0.0;
  if (FT_IS_SCALABLE(face)) {
    // Prefer FT_Size_Metrics::x_scale to x_ppem as x_ppem does not
    // have subpixel accuracy.
    //
    // FT_Size_Metrics::y_scale is in 16.16 fixed point format.  Its
    // (fractional) value is a factor that converts vertical metrics from
    // design units to units of 1/64 pixels, so that the result may be
    // interpreted as pixels in 26.6 fixed point format.
    mFUnitsConvFactor = FLOAT_FROM_26_6(FLOAT_FROM_16_16(ftMetrics.x_scale));
    yScale = FLOAT_FROM_26_6(FLOAT_FROM_16_16(ftMetrics.y_scale));
    emHeight = face->units_per_EM * yScale;
  } else {  // Not scalable.
    emHeight = ftMetrics.y_ppem;
    // FT_Face doc says units_per_EM and a bunch of following fields
    // are "only relevant to scalable outlines". If it's an sfnt,
    // we can get units_per_EM from the 'head' table instead; otherwise,
    // we don't have a unitsPerEm value so we can't compute/use yScale or
    // mFUnitsConvFactor (x scale).
    const TT_Header* head =
        static_cast<TT_Header*>(FT_Get_Sfnt_Table(face, ft_sfnt_head));
    if (head) {
      // Bug 1267909 - Even if the font is not explicitly scalable,
      // if the face has color bitmaps, it should be treated as scalable
      // and scaled to the desired size. Metrics based on y_ppem need
      // to be rescaled for the adjusted size. This makes metrics agree
      // with the scales we pass to Cairo for Fontconfig fonts.
      if (face->face_flags & FT_FACE_FLAG_COLOR) {
        emHeight = GetAdjustedSize();
        gfxFloat adjustScale = emHeight / ftMetrics.y_ppem;
        mMetrics.maxAscent *= adjustScale;
        mMetrics.maxDescent *= adjustScale;
        mMetrics.maxAdvance *= adjustScale;
        lineHeight *= adjustScale;
      }
      gfxFloat emUnit = head->Units_Per_EM;
      mFUnitsConvFactor = ftMetrics.x_ppem / emUnit;
      yScale = emHeight / emUnit;
    }
  }

  TT_OS2* os2 = static_cast<TT_OS2*>(FT_Get_Sfnt_Table(face, ft_sfnt_os2));

  if (os2 && os2->sTypoAscender && yScale > 0.0) {
    mMetrics.emAscent = os2->sTypoAscender * yScale;
    mMetrics.emDescent = -os2->sTypoDescender * yScale;
    FT_Short typoHeight =
        os2->sTypoAscender - os2->sTypoDescender + os2->sTypoLineGap;
    lineHeight = typoHeight * yScale;

    // If the OS/2 fsSelection USE_TYPO_METRICS bit is set,
    // set maxAscent/Descent from the sTypo* fields instead of hhea.
    const uint16_t kUseTypoMetricsMask = 1 << 7;
    if ((os2->fsSelection & kUseTypoMetricsMask) ||
        // maxAscent/maxDescent get used for frame heights, and some fonts
        // don't have the HHEA table ascent/descent set (bug 279032).
        (mMetrics.maxAscent == 0.0 && mMetrics.maxDescent == 0.0)) {
      // We use NS_round here to parallel the pixel-rounded values that
      // freetype gives us for ftMetrics.ascender/descender.
      mMetrics.maxAscent = NS_round(mMetrics.emAscent);
      mMetrics.maxDescent = NS_round(mMetrics.emDescent);
    }
  } else {
    mMetrics.emAscent = mMetrics.maxAscent;
    mMetrics.emDescent = mMetrics.maxDescent;
  }

  // gfxFont::Metrics::underlineOffset is the position of the top of the
  // underline.
  //
  // FT_FaceRec documentation describes underline_position as "the
  // center of the underlining stem".  This was the original definition
  // of the PostScript metric, but in the PostScript table of OpenType
  // fonts the metric is "the top of the underline"
  // (http://www.microsoft.com/typography/otspec/post.htm), and FreeType
  // (up to version 2.3.7) doesn't make any adjustment.
  //
  // Therefore get the underline position directly from the table
  // ourselves when this table exists.  Use FreeType's metrics for
  // other (including older PostScript) fonts.
  if (face->underline_position && face->underline_thickness && yScale > 0.0) {
    mMetrics.underlineSize = face->underline_thickness * yScale;
    TT_Postscript* post =
        static_cast<TT_Postscript*>(FT_Get_Sfnt_Table(face, ft_sfnt_post));
    if (post && post->underlinePosition) {
      mMetrics.underlineOffset = post->underlinePosition * yScale;
    } else {
      mMetrics.underlineOffset =
          face->underline_position * yScale + 0.5 * mMetrics.underlineSize;
    }
  } else {  // No underline info.
    // Imitate Pango.
    mMetrics.underlineSize = emHeight / 14.0;
    mMetrics.underlineOffset = -mMetrics.underlineSize;
  }

  if (os2 && os2->yStrikeoutSize && os2->yStrikeoutPosition && yScale > 0.0) {
    mMetrics.strikeoutSize = os2->yStrikeoutSize * yScale;
    mMetrics.strikeoutOffset = os2->yStrikeoutPosition * yScale;
  } else {  // No strikeout info.
    mMetrics.strikeoutSize = mMetrics.underlineSize;
    // Use OpenType spec's suggested position for Roman font.
    mMetrics.strikeoutOffset =
        emHeight * 409.0 / 2048.0 + 0.5 * mMetrics.strikeoutSize;
  }
  SnapLineToPixels(mMetrics.strikeoutOffset, mMetrics.strikeoutSize);

  if (os2 && os2->sxHeight && yScale > 0.0) {
    mMetrics.xHeight = os2->sxHeight * yScale;
  } else {
    // CSS 2.1, section 4.3.2 Lengths: "In the cases where it is
    // impossible or impractical to determine the x-height, a value of
    // 0.5em should be used."
    mMetrics.xHeight = 0.5 * emHeight;
  }

  // aveCharWidth is used for the width of text input elements so be
  // liberal rather than conservative in the estimate.
  if (os2 && os2->xAvgCharWidth) {
    // Round to pixels as this is compared with maxAdvance to guess
    // whether this is a fixed width font.
    mMetrics.aveCharWidth =
        ScaleRoundDesignUnits(os2->xAvgCharWidth, ftMetrics.x_scale);
  } else {
    mMetrics.aveCharWidth = 0.0;  // updated below
  }

  if (os2 && os2->sCapHeight && yScale > 0.0) {
    mMetrics.capHeight = os2->sCapHeight * yScale;
  } else {
    mMetrics.capHeight = mMetrics.maxAscent;
  }

  // Release the face lock to safely load glyphs with GetCharExtents if
  // necessary without recursively locking.
  UnlockFTFace();

  gfxFloat width;
  mSpaceGlyph = GetCharWidth(' ', &width);
  if (mSpaceGlyph) {
    mMetrics.spaceWidth = width;
  } else {
    mMetrics.spaceWidth = mMetrics.maxAdvance;  // guess
  }

  if (GetCharWidth('0', &width)) {
    mMetrics.zeroWidth = width;
  } else {
    mMetrics.zeroWidth = -1.0;  // indicates not found
  }

  // Prefering a measured x over sxHeight because sxHeight doesn't consider
  // hinting, but maybe the x extents are not quite right in some fancy
  // script fonts.  CSS 2.1 suggests possibly using the height of an "o",
  // which would have a more consistent glyph across fonts.
  gfxFloat xWidth;
  gfxFloat xHeight;
  if (GetCharExtents('x', &xWidth, &xHeight) && xHeight < 0.0) {
    mMetrics.xHeight = -xHeight;
    mMetrics.aveCharWidth = std::max(mMetrics.aveCharWidth, xWidth);
  }

  if (GetCharExtents('H', &xWidth, &xHeight) && xHeight < 0.0) {
    mMetrics.capHeight = -xHeight;
  }

  mMetrics.aveCharWidth = std::max(mMetrics.aveCharWidth, mMetrics.zeroWidth);
  if (mMetrics.aveCharWidth == 0.0) {
    mMetrics.aveCharWidth = mMetrics.spaceWidth;
  }
  // Apparently hinting can mean that max_advance is not always accurate.
  mMetrics.maxAdvance = std::max(mMetrics.maxAdvance, mMetrics.aveCharWidth);

  mMetrics.maxHeight = mMetrics.maxAscent + mMetrics.maxDescent;

  // Make the line height an integer number of pixels so that lines will be
  // equally spaced (rather than just being snapped to pixels, some up and
  // some down).  Layout calculates line height from the emHeight +
  // internalLeading + externalLeading, but first each of these is rounded
  // to layout units.  To ensure that the result is an integer number of
  // pixels, round each of the components to pixels.
  mMetrics.emHeight = floor(emHeight + 0.5);

  // maxHeight will normally be an integer, but round anyway in case
  // FreeType is configured differently.
  mMetrics.internalLeading =
      floor(mMetrics.maxHeight - mMetrics.emHeight + 0.5);

  // Text input boxes currently don't work well with lineHeight
  // significantly less than maxHeight (with Verdana, for example).
  lineHeight = floor(std::max(lineHeight, mMetrics.maxHeight) + 0.5);
  mMetrics.externalLeading =
      lineHeight - mMetrics.internalLeading - mMetrics.emHeight;

  // Ensure emAscent + emDescent == emHeight
  gfxFloat sum = mMetrics.emAscent + mMetrics.emDescent;
  mMetrics.emAscent =
      sum > 0.0 ? mMetrics.emAscent * mMetrics.emHeight / sum : 0.0;
  mMetrics.emDescent = mMetrics.emHeight - mMetrics.emAscent;

  SanitizeMetrics(&mMetrics, false);

#if 0
    //    printf("font name: %s %f\n", NS_ConvertUTF16toUTF8(GetName()).get(), GetStyle()->size);
    //    printf ("pango font %s\n", pango_font_description_to_string (pango_font_describe (font)));

    fprintf (stderr, "Font: %s\n", NS_ConvertUTF16toUTF8(GetName()).get());
    fprintf (stderr, "    emHeight: %f emAscent: %f emDescent: %f\n", mMetrics.emHeight, mMetrics.emAscent, mMetrics.emDescent);
    fprintf (stderr, "    maxAscent: %f maxDescent: %f\n", mMetrics.maxAscent, mMetrics.maxDescent);
    fprintf (stderr, "    internalLeading: %f externalLeading: %f\n", mMetrics.externalLeading, mMetrics.internalLeading);
    fprintf (stderr, "    spaceWidth: %f aveCharWidth: %f xHeight: %f\n", mMetrics.spaceWidth, mMetrics.aveCharWidth, mMetrics.xHeight);
    fprintf (stderr, "    uOff: %f uSize: %f stOff: %f stSize: %f\n", mMetrics.underlineOffset, mMetrics.underlineSize, mMetrics.strikeoutOffset, mMetrics.strikeoutSize);
#endif
}

const gfxFont::Metrics& gfxFT2FontBase::GetHorizontalMetrics() {
  return mMetrics;
}

// Get the glyphID of a space
uint32_t gfxFT2FontBase::GetSpaceGlyph() { return mSpaceGlyph; }

uint32_t gfxFT2FontBase::GetGlyph(uint32_t unicode,
                                  uint32_t variation_selector) {
  if (variation_selector) {
    uint32_t id =
        gfxFT2LockedFace(this).GetUVSGlyph(unicode, variation_selector);
    if (id) {
      return id;
    }
    unicode = gfxFontUtils::GetUVSFallback(unicode, variation_selector);
    if (unicode) {
      return GetGlyph(unicode);
    }
    return 0;
  }

  return GetGlyph(unicode);
}

FT_Fixed gfxFT2FontBase::GetEmboldenAdvance(FT_Face aFace, FT_Fixed aAdvance) {
  // If freetype emboldening is being used, and it's not a zero-width glyph,
  // adjust the advance to account for the increased width.
  if (!mEmbolden || !aAdvance) {
    return 0;
  }
  // This is the embolden "strength" used by FT_GlyphSlot_Embolden,
  // converted from 26.6 to 16.16
  FT_Fixed strength =
      FT_MulFix(aFace->units_per_EM, aFace->size->metrics.y_scale) / 24;
  if (aFace->glyph->format == FT_GLYPH_FORMAT_BITMAP) {
    strength &= -64;
    if (!strength) {
      strength = 64;
    }
  }
  return strength << 10;
}

bool gfxFT2FontBase::GetFTGlyphExtents(uint16_t aGID, int32_t* aAdvance,
                                       int32_t* aHeight) {
  gfxFT2LockedFace face(this);
  MOZ_ASSERT(face.get());
  if (!face.get()) {
    // Failed to get the FT_Face? Give up already.
    NS_WARNING("failed to get FT_Face!");
    return false;
  }

  FT_Error ftError = Factory::LoadFTGlyph(face.get(), aGID, mFTLoadFlags);
  if (ftError != FT_Err_Ok) {
    // FT_Face was somehow broken/invalid? Don't try to access glyph slot.
    // This probably shouldn't happen, but does: see bug 1440938.
    NS_WARNING("failed to load glyph!");
    return false;
  }

  bool hintMetrics = ShouldHintMetrics();

  // Normalize out the loaded FT glyph size and then scale to the actually
  // desired size, in case these two sizes differ.
  gfxFloat extentsScale = GetAdjustedSize() / mFTSize;

  // Due to freetype bug 52683 we MUST use the linearHoriAdvance field when
  // dealing with a variation font; also use it for scalable fonts when not
  // applying hinting. Otherwise, prefer hinted width from glyph->advance.x.
  FT_Fixed advance;
  if (face.get()->glyph->format == FT_GLYPH_FORMAT_OUTLINE &&
      (!hintMetrics || FT_HAS_MULTIPLE_MASTERS(face.get()))) {
    advance = face.get()->glyph->linearHoriAdvance;
  } else {
    advance = face.get()->glyph->advance.x << 10;  // convert 26.6 to 16.16
  }
  advance += GetEmboldenAdvance(face.get(), advance);
  // Hinting was requested, but FT did not apply any hinting to the metrics.
  // Round the advance here to approximate hinting as Cairo does. This must
  // happen BEFORE we apply the glyph extents scale, just like FT hinting
  // would.
  if (hintMetrics && (mFTLoadFlags & FT_LOAD_NO_HINTING)) {
    advance = (advance + 0x8000) & 0xffff0000u;
  }
  *aAdvance = NS_lround(advance * extentsScale);

  if (aHeight) {
    FT_F26Dot6 height = -face.get()->glyph->metrics.horiBearingY;
    if (hintMetrics && (mFTLoadFlags & FT_LOAD_NO_HINTING)) {
      height &= -64;
    }
    *aHeight = NS_lround(height * extentsScale);
  }
  return true;
}

int32_t gfxFT2FontBase::GetGlyphWidth(uint16_t aGID) {
  if (!mGlyphWidths) {
    mGlyphWidths =
        mozilla::MakeUnique<nsDataHashtable<nsUint32HashKey, int32_t>>(128);
  }

  int32_t width;
  if (mGlyphWidths->Get(aGID, &width)) {
    return width;
  }

  if (!GetFTGlyphExtents(aGID, &width)) {
    width = 0;
  }
  mGlyphWidths->Put(aGID, width);

  return width;
}

// For variation fonts, figure out the variation coordinates to be applied
// for each axis, in freetype's order (which may not match the order of
// axes in mStyle.variationSettings, so we need to search by axis tag).
/*static*/
void gfxFT2FontBase::SetupVarCoords(
    FT_MM_Var* aMMVar, const nsTArray<gfxFontVariation>& aVariations,
    FT_Face aFTFace) {
  if (!aMMVar) {
    return;
  }

  nsTArray<FT_Fixed> coords;
  for (unsigned i = 0; i < aMMVar->num_axis; ++i) {
    coords.AppendElement(aMMVar->axis[i].def);
    for (const auto& v : aVariations) {
      if (aMMVar->axis[i].tag == v.mTag) {
        FT_Fixed val = v.mValue * 0x10000;
        val = std::min(val, aMMVar->axis[i].maximum);
        val = std::max(val, aMMVar->axis[i].minimum);
        coords[i] = val;
        break;
      }
    }
  }

  if (!coords.IsEmpty()) {
#if MOZ_TREE_FREETYPE
    FT_Set_Var_Design_Coordinates(aFTFace, coords.Length(), coords.Elements());
#else
    typedef FT_Error (*SetCoordsFunc)(FT_Face, FT_UInt, FT_Fixed*);
    static SetCoordsFunc setCoords;
    static bool firstTime = true;
    if (firstTime) {
      firstTime = false;
      setCoords =
          (SetCoordsFunc)dlsym(RTLD_DEFAULT, "FT_Set_Var_Design_Coordinates");
    }
    if (setCoords) {
      (*setCoords)(aFTFace, coords.Length(), coords.Elements());
    }
#endif
  }
}

already_AddRefed<SharedFTFace> FTUserFontData::CloneFace(int aFaceIndex) {
  RefPtr<SharedFTFace> face = Factory::NewSharedFTFaceFromData(
      nullptr, mFontData, mLength, aFaceIndex, this);
  if (!face ||
      (FT_Select_Charmap(face->GetFace(), FT_ENCODING_UNICODE) != FT_Err_Ok &&
       FT_Select_Charmap(face->GetFace(), FT_ENCODING_MS_SYMBOL) !=
           FT_Err_Ok)) {
    return nullptr;
  }
  return face.forget();
}
