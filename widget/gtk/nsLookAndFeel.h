/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim:expandtab:shiftwidth=4:tabstop=4:
 */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef __nsLookAndFeel
#define __nsLookAndFeel

#include "nsXPLookAndFeel.h"
#include "nsCOMPtr.h"
#include "gfxFont.h"

struct _GtkStyle;

class nsLookAndFeel final : public nsXPLookAndFeel
{
public:
    nsLookAndFeel();
    virtual ~nsLookAndFeel();

    virtual nsresult NativeGetColor(ColorID aID, nscolor &aResult);
    virtual void NativeInit() final;
    virtual nsresult GetIntImpl(IntID aID, int32_t &aResult);
    virtual nsresult GetFloatImpl(FloatID aID, float &aResult);
    virtual bool GetFontImpl(FontID aID, nsString& aFontName,
                             gfxFontStyle& aFontStyle,
                             float aDevPixPerCSSPixel);

    virtual void RefreshImpl();
    virtual char16_t GetPasswordCharacterImpl();
    virtual bool GetEchoPasswordImpl();

protected:

    // Cached fonts
    bool mDefaultFontCached;
    bool mButtonFontCached;
    bool mFieldFontCached;
    bool mMenuFontCached;
    nsString mDefaultFontName;
    nsString mButtonFontName;
    nsString mFieldFontName;
    nsString mMenuFontName;
    gfxFontStyle mDefaultFontStyle;
    gfxFontStyle mButtonFontStyle;
    gfxFontStyle mFieldFontStyle;
    gfxFontStyle mMenuFontStyle;

    // Cached colors
    nscolor sInfoBackground;
    nscolor sInfoText;
    nscolor sMenuBackground;
    nscolor sMenuBarText;
    nscolor sMenuBarHoverText;
    nscolor sMenuText;
    nscolor sMenuTextInactive;
    nscolor sMenuHover;
    nscolor sMenuHoverText;
    nscolor sButtonDefault;
    nscolor sButtonText;
    nscolor sButtonHoverText;
    nscolor sButtonHoverFace;
    nscolor sFrameOuterLightBorder;
    nscolor sFrameInnerDarkBorder;
    nscolor sOddCellBackground;
    nscolor sNativeHyperLinkText;
    nscolor sComboBoxText;
    nscolor sComboBoxBackground;
    nscolor sMozFieldText;
    nscolor sMozFieldBackground;
    nscolor sMozWindowText;
    nscolor sMozWindowBackground;
    nscolor sMozWindowActiveBorder;
    nscolor sMozWindowInactiveBorder;
    nscolor sMozWindowInactiveCaption;
    nscolor sTextSelectedText;
    nscolor sTextSelectedBackground;
    nscolor sMozScrollbar;
    nscolor sInfoBarText;
    char16_t sInvisibleCharacter;
    float   sCaretRatio;
    bool    sMenuSupportsDrag;
    bool    mInitialized;

    void EnsureInit();
};

#endif
