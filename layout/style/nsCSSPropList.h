/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * a list of all CSS properties with considerable data about them, for
 * preprocessing
 */

/******

  This file contains the list of all parsed CSS properties.  It is
  designed to be used as inline input through the magic of C
  preprocessing.  All entries must be enclosed in the appropriate
  CSS_PROP_* macro which will have cruel and unusual things done to it.
  It is recommended (but not strictly necessary) to keep all entries in
  alphabetical order.

  The arguments to CSS_PROP, CSS_PROP_LOGICAL and CSS_PROP_* are:

  -. 'name' entries represent a CSS property name and *must* use only
  lowercase characters.

  -. 'id' should be the same as 'name' except that all hyphens ('-')
  in 'name' are converted to underscores ('_') in 'id'. For properties
  on a standards track, any '-moz-' prefix is removed in 'id'. This
  lets us do nice things with the macros without having to copy/convert
  strings at runtime.  These are the names used for the enum values of
  the nsCSSPropertyID enumeration defined in nsCSSProps.h.

  -. 'method' is designed to be as input for CSS2Properties and similar
  callers.  It must always be the same as 'name' except it must use
  InterCaps and all hyphens ('-') must be removed.  Callers using this
  parameter must also define the CSS_PROP_PUBLIC_OR_PRIVATE(publicname_,
  privatename_) macro to yield either publicname_ or privatename_.
  The names differ in that publicname_ has Moz prefixes where they are
  used, and also in CssFloat vs. Float.  The caller's choice depends on
  whether the use is for internal use such as eCSSProperty_* or
  nsRuleData::ValueFor* or external use such as exposing DOM properties.

  -. 'flags', a bitfield containing CSS_PROPERTY_* flags.

  -. 'pref' is the name of a pref that controls whether the property
  is enabled.  The property is enabled if 'pref' is an empty string,
  or if the boolean property whose name is 'pref' is set to true.

  -. 'parsevariant', to be passed to ParseVariant in the parser.

  -. 'kwtable', which is either nullptr or the name of the appropriate
  keyword table member of class nsCSSProps, for use in
  nsCSSProps::LookupPropertyValue.

  -. 'stylestruct_' [used only for CSS_PROP and CSS_PROP_LOGICAL, not
  CSS_PROP_*] gives the name of the style struct.  Can be used to make
  nsStyle##stylestruct_ and eStyleStruct_##stylestruct_

  -. 'animtype_' gives the animation type (see nsStyleAnimType) of this
  property.

  CSS_PROP_SHORTHAND only takes 1-5.

  CSS_PROP_LOGICAL should be used instead of CSS_PROP_struct when
  defining logical properties.  Logical shorthand properties should
  still be defined with CSS_PROP_SHORTHAND.

 ******/


/*************************************************************************/


// All includers must explicitly define CSS_PROP_SHORTHAND if they
// want it.
#ifndef CSS_PROP_SHORTHAND
#define CSS_PROP_SHORTHAND(name_, id_, method_, flags_, pref_) /* nothing */
#define DEFINED_CSS_PROP_SHORTHAND
#endif

#define CSS_PROP_DOMPROP_PREFIXED(name_) \
  CSS_PROP_PUBLIC_OR_PRIVATE(Moz ## name_, name_)

// Callers may define CSS_PROP_LIST_EXCLUDE_INTERNAL if they want to
// exclude internal properties that are not represented in the DOM (only
// the DOM style code defines this).  All properties defined in an
// #ifndef CSS_PROP_LIST_EXCLUDE_INTERNAL section must have the
// CSS_PROPERTY_INTERNAL flag set.

// When capturing all properties by defining CSS_PROP, callers must also
// define one of the following three macros:
//
//   CSS_PROP_LIST_EXCLUDE_LOGICAL
//     Does not include logical properties (defined with CSS_PROP_LOGICAL,
//     such as margin-inline-start) when capturing properties to CSS_PROP.
//
//   CSS_PROP_LIST_INCLUDE_LOGICAL
//     Does include logical properties when capturing properties to
//     CSS_PROP.
//
//   CSS_PROP_LOGICAL
//     Captures logical properties separately to CSS_PROP_LOGICAL.
//
// (CSS_PROP_LIST_EXCLUDE_LOGICAL is used for example to ensure
// gPropertyCountInStruct and gPropertyIndexInStruct do not allocate any
// storage to logical properties, since the result of the cascade, stored
// in an nsRuleData, does not need to store both logical and physical
// property values.)

// Callers may also define CSS_PROP_LIST_ONLY_COMPONENTS_OF_ALL_SHORTHAND
// to exclude properties that are not considered to be components of the 'all'
// shorthand property.  Currently this excludes 'direction' and 'unicode-bidi',
// as required by the CSS Cascading and Inheritance specification, and any
// internal properties that cannot be changed by using CSS syntax.  For example,
// the internal '-moz-system-font' property is not excluded, as it is set by the
// 'font' shorthand, while '-x-lang' is excluded as there is no way to set this
// internal property from a style sheet.

// A caller who wants all the properties can define the |CSS_PROP|
// macro.
#ifdef CSS_PROP

#define USED_CSS_PROP
#define CSS_PROP_FONT(name_, id_, method_, flags_, pref_, parsevariant_, kwtable_, animtype_) CSS_PROP(name_, id_, method_, flags_, pref_, parsevariant_, kwtable_, Font, animtype_)
#define CSS_PROP_COLOR(name_, id_, method_, flags_, pref_, parsevariant_, kwtable_, animtype_) CSS_PROP(name_, id_, method_, flags_, pref_, parsevariant_, kwtable_, Color, animtype_)
#define CSS_PROP_BACKGROUND(name_, id_, method_, flags_, pref_, parsevariant_, kwtable_, animtype_) CSS_PROP(name_, id_, method_, flags_, pref_, parsevariant_, kwtable_, Background, animtype_)
#define CSS_PROP_LIST(name_, id_, method_, flags_, pref_, parsevariant_, kwtable_, animtype_) CSS_PROP(name_, id_, method_, flags_, pref_, parsevariant_, kwtable_, List, animtype_)
#define CSS_PROP_POSITION(name_, id_, method_, flags_, pref_, parsevariant_, kwtable_, animtype_) CSS_PROP(name_, id_, method_, flags_, pref_, parsevariant_, kwtable_, Position, animtype_)
#define CSS_PROP_TEXT(name_, id_, method_, flags_, pref_, parsevariant_, kwtable_, animtype_) CSS_PROP(name_, id_, method_, flags_, pref_, parsevariant_, kwtable_, Text, animtype_)
#define CSS_PROP_TEXTRESET(name_, id_, method_, flags_, pref_, parsevariant_, kwtable_, animtype_) CSS_PROP(name_, id_, method_, flags_, pref_, parsevariant_, kwtable_, TextReset, animtype_)
#define CSS_PROP_DISPLAY(name_, id_, method_, flags_, pref_, parsevariant_, kwtable_, animtype_) CSS_PROP(name_, id_, method_, flags_, pref_, parsevariant_, kwtable_, Display, animtype_)
#define CSS_PROP_VISIBILITY(name_, id_, method_, flags_, pref_, parsevariant_, kwtable_, animtype_) CSS_PROP(name_, id_, method_, flags_, pref_, parsevariant_, kwtable_, Visibility, animtype_)
#define CSS_PROP_CONTENT(name_, id_, method_, flags_, pref_, parsevariant_, kwtable_, animtype_) CSS_PROP(name_, id_, method_, flags_, pref_, parsevariant_, kwtable_, Content, animtype_)
#define CSS_PROP_USERINTERFACE(name_, id_, method_, flags_, pref_, parsevariant_, kwtable_, animtype_) CSS_PROP(name_, id_, method_, flags_, pref_, parsevariant_, kwtable_, UserInterface, animtype_)
#define CSS_PROP_UIRESET(name_, id_, method_, flags_, pref_, parsevariant_, kwtable_, animtype_) CSS_PROP(name_, id_, method_, flags_, pref_, parsevariant_, kwtable_, UIReset, animtype_)
#define CSS_PROP_TABLE(name_, id_, method_, flags_, pref_, parsevariant_, kwtable_, animtype_) CSS_PROP(name_, id_, method_, flags_, pref_, parsevariant_, kwtable_, Table, animtype_)
#define CSS_PROP_TABLEBORDER(name_, id_, method_, flags_, pref_, parsevariant_, kwtable_, animtype_) CSS_PROP(name_, id_, method_, flags_, pref_, parsevariant_, kwtable_, TableBorder, animtype_)
#define CSS_PROP_MARGIN(name_, id_, method_, flags_, pref_, parsevariant_, kwtable_, animtype_) CSS_PROP(name_, id_, method_, flags_, pref_, parsevariant_, kwtable_, Margin, animtype_)
#define CSS_PROP_PADDING(name_, id_, method_, flags_, pref_, parsevariant_, kwtable_, animtype_) CSS_PROP(name_, id_, method_, flags_, pref_, parsevariant_, kwtable_, Padding, animtype_)
#define CSS_PROP_BORDER(name_, id_, method_, flags_, pref_, parsevariant_, kwtable_, animtype_) CSS_PROP(name_, id_, method_, flags_, pref_, parsevariant_, kwtable_, Border, animtype_)
#define CSS_PROP_OUTLINE(name_, id_, method_, flags_, pref_, parsevariant_, kwtable_, animtype_) CSS_PROP(name_, id_, method_, flags_, pref_, parsevariant_, kwtable_, Outline, animtype_)
#define CSS_PROP_XUL(name_, id_, method_, flags_, pref_, parsevariant_, kwtable_, animtype_) CSS_PROP(name_, id_, method_, flags_, pref_, parsevariant_, kwtable_, XUL, animtype_)
#define CSS_PROP_COLUMN(name_, id_, method_, flags_, pref_, parsevariant_, kwtable_, animtype_) CSS_PROP(name_, id_, method_, flags_, pref_, parsevariant_, kwtable_, Column, animtype_)
#define CSS_PROP_SVG(name_, id_, method_, flags_, pref_, parsevariant_, kwtable_, animtype_) CSS_PROP(name_, id_, method_, flags_, pref_, parsevariant_, kwtable_, SVG, animtype_)
#define CSS_PROP_SVGRESET(name_, id_, method_, flags_, pref_, parsevariant_, kwtable_, animtype_) CSS_PROP(name_, id_, method_, flags_, pref_, parsevariant_, kwtable_, SVGReset, animtype_)
#define CSS_PROP_VARIABLES(name_, id_, method_, flags_, pref_, parsevariant_, kwtable_, animtype_) CSS_PROP(name_, id_, method_, flags_, pref_, parsevariant_, kwtable_, Variables, animtype_)
#define CSS_PROP_EFFECTS(name_, id_, method_, flags_, pref_, parsevariant_, kwtable_, animtype_) CSS_PROP(name_, id_, method_, flags_, pref_, parsevariant_, kwtable_, Effects, animtype_)

// And similarly for logical properties.  An includer can define
// CSS_PROP_LOGICAL to capture all logical properties, but otherwise they
// are included in CSS_PROP (as long as CSS_PROP_LIST_INCLUDE_LOGICAL is
// defined).
#if defined(CSS_PROP_LOGICAL) && defined(CSS_PROP_LIST_EXCLUDE_LOGICAL) || defined(CSS_PROP_LOGICAL) && defined(CSS_PROP_LIST_INCLUDE_LOGICAL) || defined(CSS_PROP_LIST_EXCLUDE_LOGICAL) && defined(CSS_PROP_LIST_INCLUDE_LOGICAL)
#error Do not define more than one of CSS_PROP_LOGICAL, CSS_PROP_LIST_EXCLUDE_LOGICAL and CSS_PROP_LIST_INCLUDE_LOGICAL when capturing properties using CSS_PROP.
#endif

#ifndef CSS_PROP_LOGICAL
#ifdef CSS_PROP_LIST_INCLUDE_LOGICAL
#define CSS_PROP_LOGICAL(name_, id_, method_, flags_, pref_, parsevariant_, kwtable_, struct_, animtype_) CSS_PROP(name_, id_, method_, flags_, pref_, parsevariant_, kwtable_, struct_, animtype_)
#else
#ifndef CSS_PROP_LIST_EXCLUDE_LOGICAL
#error Must define exactly one of CSS_PROP_LOGICAL, CSS_PROP_LIST_EXCLUDE_LOGICAL and CSS_PROP_LIST_INCLUDE_LOGICAL when capturing properties using CSS_PROP.
#endif
#define CSS_PROP_LOGICAL(name_, id_, method_, flags_, pref_, parsevariant_, kwtable_, struct_, animtype_) /* nothing */
#endif
#define DEFINED_CSS_PROP_LOGICAL
#endif

#else /* !defined(CSS_PROP) */

// An includer who does not define CSS_PROP can define any or all of the
// per-struct macros that are equivalent to it, and the rest will be
// ignored.

#if defined(CSS_PROP_LIST_EXCLUDE_LOGICAL) || defined(CSS_PROP_LIST_INCLUDE_LOGICAL)
#error Do not define CSS_PROP_LIST_EXCLUDE_LOGICAL or CSS_PROP_LIST_INCLUDE_LOGICAL when not capturing properties using CSS_PROP.
#endif

#ifndef CSS_PROP_FONT
#define CSS_PROP_FONT(name_, id_, method_, flags_, pref_, parsevariant_, kwtable_, animtype_) /* nothing */
#define DEFINED_CSS_PROP_FONT
#endif
#ifndef CSS_PROP_COLOR
#define CSS_PROP_COLOR(name_, id_, method_, flags_, pref_, parsevariant_, kwtable_, animtype_) /* nothing */
#define DEFINED_CSS_PROP_COLOR
#endif
#ifndef CSS_PROP_BACKGROUND
#define CSS_PROP_BACKGROUND(name_, id_, method_, flags_, pref_, parsevariant_, kwtable_, animtype_) /* nothing */
#define DEFINED_CSS_PROP_BACKGROUND
#endif
#ifndef CSS_PROP_LIST
#define CSS_PROP_LIST(name_, id_, method_, flags_, pref_, parsevariant_, kwtable_, animtype_) /* nothing */
#define DEFINED_CSS_PROP_LIST
#endif
#ifndef CSS_PROP_POSITION
#define CSS_PROP_POSITION(name_, id_, method_, flags_, pref_, parsevariant_, kwtable_, animtype_) /* nothing */
#define DEFINED_CSS_PROP_POSITION
#endif
#ifndef CSS_PROP_TEXT
#define CSS_PROP_TEXT(name_, id_, method_, flags_, pref_, parsevariant_, kwtable_, animtype_) /* nothing */
#define DEFINED_CSS_PROP_TEXT
#endif
#ifndef CSS_PROP_TEXTRESET
#define CSS_PROP_TEXTRESET(name_, id_, method_, flags_, pref_, parsevariant_, kwtable_, animtype_) /* nothing */
#define DEFINED_CSS_PROP_TEXTRESET
#endif
#ifndef CSS_PROP_DISPLAY
#define CSS_PROP_DISPLAY(name_, id_, method_, flags_, pref_, parsevariant_, kwtable_, animtype_) /* nothing */
#define DEFINED_CSS_PROP_DISPLAY
#endif
#ifndef CSS_PROP_VISIBILITY
#define CSS_PROP_VISIBILITY(name_, id_, method_, flags_, pref_, parsevariant_, kwtable_, animtype_) /* nothing */
#define DEFINED_CSS_PROP_VISIBILITY
#endif
#ifndef CSS_PROP_CONTENT
#define CSS_PROP_CONTENT(name_, id_, method_, flags_, pref_, parsevariant_, kwtable_, animtype_) /* nothing */
#define DEFINED_CSS_PROP_CONTENT
#endif
#ifndef CSS_PROP_USERINTERFACE
#define CSS_PROP_USERINTERFACE(name_, id_, method_, flags_, pref_, parsevariant_, kwtable_, animtype_) /* nothing */
#define DEFINED_CSS_PROP_USERINTERFACE
#endif
#ifndef CSS_PROP_UIRESET
#define CSS_PROP_UIRESET(name_, id_, method_, flags_, pref_, parsevariant_, kwtable_, animtype_) /* nothing */
#define DEFINED_CSS_PROP_UIRESET
#endif
#ifndef CSS_PROP_TABLE
#define CSS_PROP_TABLE(name_, id_, method_, flags_, pref_, parsevariant_, kwtable_, animtype_) /* nothing */
#define DEFINED_CSS_PROP_TABLE
#endif
#ifndef CSS_PROP_TABLEBORDER
#define CSS_PROP_TABLEBORDER(name_, id_, method_, flags_, pref_, parsevariant_, kwtable_, animtype_) /* nothing */
#define DEFINED_CSS_PROP_TABLEBORDER
#endif
#ifndef CSS_PROP_MARGIN
#define CSS_PROP_MARGIN(name_, id_, method_, flags_, pref_, parsevariant_, kwtable_, animtype_) /* nothing */
#define DEFINED_CSS_PROP_MARGIN
#endif
#ifndef CSS_PROP_PADDING
#define CSS_PROP_PADDING(name_, id_, method_, flags_, pref_, parsevariant_, kwtable_, animtype_) /* nothing */
#define DEFINED_CSS_PROP_PADDING
#endif
#ifndef CSS_PROP_BORDER
#define CSS_PROP_BORDER(name_, id_, method_, flags_, pref_, parsevariant_, kwtable_, animtype_) /* nothing */
#define DEFINED_CSS_PROP_BORDER
#endif
#ifndef CSS_PROP_OUTLINE
#define CSS_PROP_OUTLINE(name_, id_, method_, flags_, pref_, parsevariant_, kwtable_, animtype_) /* nothing */
#define DEFINED_CSS_PROP_OUTLINE
#endif
#ifndef CSS_PROP_XUL
#define CSS_PROP_XUL(name_, id_, method_, flags_, pref_, parsevariant_, kwtable_, animtype_) /* nothing */
#define DEFINED_CSS_PROP_XUL
#endif
#ifndef CSS_PROP_COLUMN
#define CSS_PROP_COLUMN(name_, id_, method_, flags_, pref_, parsevariant_, kwtable_, animtype_) /* nothing */
#define DEFINED_CSS_PROP_COLUMN
#endif
#ifndef CSS_PROP_SVG
#define CSS_PROP_SVG(name_, id_, method_, flags_, pref_, parsevariant_, kwtable_, animtype_) /* nothing */
#define DEFINED_CSS_PROP_SVG
#endif
#ifndef CSS_PROP_SVGRESET
#define CSS_PROP_SVGRESET(name_, id_, method_, flags_, pref_, parsevariant_, kwtable_, animtype_) /* nothing */
#define DEFINED_CSS_PROP_SVGRESET
#endif
#ifndef CSS_PROP_VARIABLES
#define CSS_PROP_VARIABLES(name_, id_, method_, flags_, pref_, parsevariant_, kwtable_, animtype_) /* nothing */
#define DEFINED_CSS_PROP_VARIABLES
#endif
#ifndef CSS_PROP_EFFECTS
#define CSS_PROP_EFFECTS(name_, id_, method_, flags_, pref_, parsevariant_, kwtable_, animtype_) /* nothing */
#define DEFINED_CSS_PROP_EFFECTS
#endif

#ifndef CSS_PROP_LOGICAL
#define CSS_PROP_LOGICAL(name_, id_, method_, flags_, pref_, parsevariant_, kwtable_, struct_, animtype_) /* nothing */
#define DEFINED_CSS_PROP_LOGICAL
#endif

#endif /* !defined(CSS_PROP) */

/*************************************************************************/

// For notes XXX bug 3935 below, the names being parsed do not correspond
// to the constants used internally.  It would be nice to bring the
// constants into line sometime.

// The parser will refuse to parse properties marked with -x-.

// Those marked XXX bug 48973 are CSS2 properties that we support
// differently from the spec for UI requirements.  If we ever
// support them correctly the old constants need to be renamed and
// new ones should be entered.

// CSS2.1 section 5.12.1 says that the properties that apply to
// :first-line are: font properties, color properties, background
// properties, 'word-spacing', 'letter-spacing', 'text-decoration',
// 'vertical-align', 'text-transform', and 'line-height'.
//
// We also allow 'text-shadow', which was listed in CSS2 (where the
// property existed).

// CSS2.1 section 5.12.2 says that the properties that apply to
// :first-letter are: font properties, 'text-decoration',
// 'text-transform', 'letter-spacing', 'word-spacing' (when
// appropriate), 'line-height', 'float', 'vertical-align' (only if
// 'float' is 'none'), margin properties, padding properties, border
// properties, 'color', and background properties.  We also allow
// 'text-shadow' (see above) and 'box-shadow' (which is like the
// border properties).

// Please keep these sorted by property name, ignoring any "-moz-",
// "-webkit-" or "-x-" prefix.

CSS_PROP_POSITION(
    align-content,
    align_content,
    AlignContent,
    CSS_PROPERTY_PARSE_FUNCTION,
    "",
    VARIANT_HK,
    kAutoCompletionAlignJustifyContent,
    eStyleAnimType_Discrete)
CSS_PROP_POSITION(
    align-items,
    align_items,
    AlignItems,
    CSS_PROPERTY_PARSE_FUNCTION,
    "",
    VARIANT_HK,
    kAutoCompletionAlignItems,
    eStyleAnimType_Discrete)
CSS_PROP_POSITION(
    align-self,
    align_self,
    AlignSelf,
    CSS_PROPERTY_PARSE_FUNCTION,
    "",
    VARIANT_HK,
    kAutoCompletionAlignJustifySelf,
    eStyleAnimType_Discrete)
CSS_PROP_SHORTHAND(
    all,
    all,
    All,
    CSS_PROPERTY_PARSE_FUNCTION,
    "layout.css.all-shorthand.enabled")
CSS_PROP_SHORTHAND(
    animation,
    animation,
    Animation,
    CSS_PROPERTY_PARSE_FUNCTION,
    "")
CSS_PROP_DISPLAY(
    animation-delay,
    animation_delay,
    AnimationDelay,
    CSS_PROPERTY_VALUE_LIST_USES_COMMAS,
    "",
    VARIANT_TIME, // used by list parsing
    nullptr,
    eStyleAnimType_None)
CSS_PROP_DISPLAY(
    animation-direction,
    animation_direction,
    AnimationDirection,
    CSS_PROPERTY_VALUE_LIST_USES_COMMAS,
    "",
    VARIANT_KEYWORD, // used by list parsing
    kAnimationDirectionKTable,
    eStyleAnimType_None)
CSS_PROP_DISPLAY(
    animation-duration,
    animation_duration,
    AnimationDuration,
    CSS_PROPERTY_VALUE_LIST_USES_COMMAS,
    "",
    VARIANT_TIME | VARIANT_NONNEGATIVE_DIMENSION, // used by list parsing
    nullptr,
    eStyleAnimType_None)
CSS_PROP_DISPLAY(
    animation-fill-mode,
    animation_fill_mode,
    AnimationFillMode,
    CSS_PROPERTY_VALUE_LIST_USES_COMMAS,
    "",
    VARIANT_KEYWORD, // used by list parsing
    kAnimationFillModeKTable,
    eStyleAnimType_None)
CSS_PROP_DISPLAY(
    animation-iteration-count,
    animation_iteration_count,
    AnimationIterationCount,
    CSS_PROPERTY_VALUE_LIST_USES_COMMAS,
    "",
    VARIANT_KEYWORD | VARIANT_NUMBER, // used by list parsing
    kAnimationIterationCountKTable,
    eStyleAnimType_None)
CSS_PROP_DISPLAY(
    animation-name,
    animation_name,
    AnimationName,
    CSS_PROPERTY_VALUE_LIST_USES_COMMAS,
    "",
    // FIXME: The spec should say something about 'inherit' and 'initial'
    // not being allowed.
    VARIANT_NONE | VARIANT_IDENTIFIER_NO_INHERIT | VARIANT_STRING, // used by list parsing
    nullptr,
    eStyleAnimType_None)
CSS_PROP_DISPLAY(
    animation-play-state,
    animation_play_state,
    AnimationPlayState,
    CSS_PROPERTY_VALUE_LIST_USES_COMMAS,
    "",
    VARIANT_KEYWORD, // used by list parsing
    kAnimationPlayStateKTable,
    eStyleAnimType_None)
CSS_PROP_DISPLAY(
    animation-timing-function,
    animation_timing_function,
    AnimationTimingFunction,
    CSS_PROPERTY_VALUE_LIST_USES_COMMAS,
    "",
    VARIANT_KEYWORD | VARIANT_TIMING_FUNCTION, // used by list parsing
    kTransitionTimingFunctionKTable,
    eStyleAnimType_None)
CSS_PROP_DISPLAY(
    -moz-appearance,
    _moz_appearance,
    CSS_PROP_DOMPROP_PREFIXED(Appearance),
    0,
    "",
    VARIANT_HK,
    kAppearanceKTable,
    eStyleAnimType_Discrete)
CSS_PROP_DISPLAY(
    backface-visibility,
    backface_visibility,
    BackfaceVisibility,
    0,
    "",
    VARIANT_HK,
    kBackfaceVisibilityKTable,
    eStyleAnimType_Discrete)
CSS_PROP_SHORTHAND(
    background,
    background,
    Background,
    CSS_PROPERTY_PARSE_FUNCTION,
    "")
CSS_PROP_BACKGROUND(
    background-attachment,
    background_attachment,
    BackgroundAttachment,
    CSS_PROPERTY_VALUE_LIST_USES_COMMAS,
    "",
    VARIANT_KEYWORD, // used by list parsing
    kImageLayerAttachmentKTable,
    eStyleAnimType_Discrete)
CSS_PROP_BACKGROUND(
    background-blend-mode,
    background_blend_mode,
    BackgroundBlendMode,
    CSS_PROPERTY_VALUE_LIST_USES_COMMAS,
    "layout.css.background-blend-mode.enabled",
    VARIANT_KEYWORD, // used by list parsing
    kBlendModeKTable,
    eStyleAnimType_Discrete)
CSS_PROP_BACKGROUND(
    background-clip,
    background_clip,
    BackgroundClip,
    CSS_PROPERTY_VALUE_LIST_USES_COMMAS,
    "",
    VARIANT_KEYWORD, // used by list parsing
    kBackgroundClipKTable,
    eStyleAnimType_Discrete)
CSS_PROP_BACKGROUND(
    background-color,
    background_color,
    BackgroundColor,
    0,
    "",
    VARIANT_HC,
    nullptr,
    eStyleAnimType_ComplexColor)
CSS_PROP_BACKGROUND(
    background-image,
    background_image,
    BackgroundImage,
    CSS_PROPERTY_VALUE_LIST_USES_COMMAS,
    "",
    VARIANT_IMAGE, // used by list parsing
    nullptr,
    eStyleAnimType_Discrete)
CSS_PROP_BACKGROUND(
    background-origin,
    background_origin,
    BackgroundOrigin,
    CSS_PROPERTY_VALUE_LIST_USES_COMMAS,
    "",
    VARIANT_KEYWORD, // used by list parsing
    kBackgroundOriginKTable,
    eStyleAnimType_Discrete)
CSS_PROP_SHORTHAND(
    background-position,
    background_position,
    BackgroundPosition,
    CSS_PROPERTY_PARSE_FUNCTION,
    "")
CSS_PROP_BACKGROUND(
    background-position-x,
    background_position_x,
    BackgroundPositionX,
    CSS_PROPERTY_PARSE_FUNCTION |
        CSS_PROPERTY_VALUE_LIST_USES_COMMAS,
    "",
    0,
    kImageLayerPositionKTable,
    eStyleAnimType_Custom)
CSS_PROP_BACKGROUND(
    background-position-y,
    background_position_y,
    BackgroundPositionY,
    CSS_PROPERTY_PARSE_FUNCTION |
        CSS_PROPERTY_VALUE_LIST_USES_COMMAS,
    "",
    0,
    kImageLayerPositionKTable,
    eStyleAnimType_Custom)
CSS_PROP_BACKGROUND(
    background-repeat,
    background_repeat,
    BackgroundRepeat,
    CSS_PROPERTY_PARSE_FUNCTION |
        CSS_PROPERTY_VALUE_LIST_USES_COMMAS,
    "",
    VARIANT_KEYWORD, // used by list parsing
    kImageLayerRepeatKTable,
    eStyleAnimType_Discrete)
CSS_PROP_BACKGROUND(
    background-size,
    background_size,
    BackgroundSize,
    CSS_PROPERTY_PARSE_FUNCTION |
        CSS_PROPERTY_VALUE_LIST_USES_COMMAS,
    "",
    0,
    kImageLayerSizeKTable,
    eStyleAnimType_Custom)
CSS_PROP_DISPLAY(
    -moz-binding,
    _moz_binding,
    CSS_PROP_DOMPROP_PREFIXED(Binding),
    0,
    "",
    VARIANT_HUO,
    nullptr,
    eStyleAnimType_None) // XXX bug 3935
CSS_PROP_LOGICAL(
    block-size,
    block_size,
    BlockSize,
    CSS_PROPERTY_GETCS_NEEDS_LAYOUT_FLUSH,
    "",
    VARIANT_AHLP | VARIANT_CALC,
    nullptr,
    Position,
    eStyleAnimType_None)
CSS_PROP_SHORTHAND(
    border,
    border,
    Border,
    CSS_PROPERTY_PARSE_FUNCTION,
    "")
CSS_PROP_SHORTHAND(
    border-block-end,
    border_block_end,
    BorderBlockEnd,
    CSS_PROPERTY_PARSE_FUNCTION,
    "")
CSS_PROP_LOGICAL(
    border-block-end-color,
    border_block_end_color,
    BorderBlockEndColor,
    0,
    "",
    VARIANT_HC,
    nullptr,
    Border,
    eStyleAnimType_None)
CSS_PROP_LOGICAL(
    border-block-end-style,
    border_block_end_style,
    BorderBlockEndStyle,
    0,
    "",
    VARIANT_HK,
    kBorderStyleKTable,
    Border,
    eStyleAnimType_None)
CSS_PROP_LOGICAL(
    border-block-end-width,
    border_block_end_width,
    BorderBlockEndWidth,
    CSS_PROPERTY_GETCS_NEEDS_LAYOUT_FLUSH,
    "",
    VARIANT_HKL | VARIANT_CALC,
    kBorderWidthKTable,
    Border,
    eStyleAnimType_None)
CSS_PROP_SHORTHAND(
    border-block-start,
    border_block_start,
    BorderBlockStart,
    CSS_PROPERTY_PARSE_FUNCTION,
    "")
CSS_PROP_LOGICAL(
    border-block-start-color,
    border_block_start_color,
    BorderBlockStartColor,
    0,
    "",
    VARIANT_HC,
    nullptr,
    Border,
    eStyleAnimType_None)
CSS_PROP_LOGICAL(
    border-block-start-style,
    border_block_start_style,
    BorderBlockStartStyle,
    0,
    "",
    VARIANT_HK,
    kBorderStyleKTable,
    Border,
    eStyleAnimType_None)
CSS_PROP_LOGICAL(
    border-block-start-width,
    border_block_start_width,
    BorderBlockStartWidth,
    CSS_PROPERTY_GETCS_NEEDS_LAYOUT_FLUSH,
    "",
    VARIANT_HKL | VARIANT_CALC,
    kBorderWidthKTable,
    Border,
    eStyleAnimType_None)
CSS_PROP_SHORTHAND(
    border-bottom,
    border_bottom,
    BorderBottom,
    CSS_PROPERTY_PARSE_FUNCTION,
    "")
CSS_PROP_BORDER(
    border-bottom-color,
    border_bottom_color,
    BorderBottomColor,
    0,
    "",
    VARIANT_HC,
    nullptr,
    eStyleAnimType_ComplexColor)
CSS_PROP_BORDER(
    border-bottom-left-radius,
    border_bottom_left_radius,
    BorderBottomLeftRadius,
    CSS_PROPERTY_PARSE_FUNCTION,
    "",
    0,
    nullptr,
    eStyleAnimType_Corner_BottomLeft)
CSS_PROP_BORDER(
    border-bottom-right-radius,
    border_bottom_right_radius,
    BorderBottomRightRadius,
    CSS_PROPERTY_PARSE_FUNCTION,
    "",
    0,
    nullptr,
    eStyleAnimType_Corner_BottomRight)
CSS_PROP_BORDER(
    border-bottom-style,
    border_bottom_style,
    BorderBottomStyle,
    0,
    "",
    VARIANT_HK,
    kBorderStyleKTable,
    eStyleAnimType_Discrete)  // on/off will need reflow
CSS_PROP_BORDER(
    border-bottom-width,
    border_bottom_width,
    BorderBottomWidth,
    CSS_PROPERTY_GETCS_NEEDS_LAYOUT_FLUSH,
    "",
    VARIANT_HKL | VARIANT_CALC,
    kBorderWidthKTable,
    eStyleAnimType_Custom)
CSS_PROP_TABLEBORDER(
    border-collapse,
    border_collapse,
    BorderCollapse,
    0,
    "",
    VARIANT_HK,
    kBorderCollapseKTable,
    eStyleAnimType_Discrete)
CSS_PROP_SHORTHAND(
    border-color,
    border_color,
    BorderColor,
    CSS_PROPERTY_PARSE_FUNCTION,
    "")
CSS_PROP_SHORTHAND(
    border-image,
    border_image,
    BorderImage,
    CSS_PROPERTY_PARSE_FUNCTION,
    "")
CSS_PROP_BORDER(
    border-image-outset,
    border_image_outset,
    BorderImageOutset,
    CSS_PROPERTY_PARSE_FUNCTION,
    "",
    0,
    nullptr,
    eStyleAnimType_Discrete)
CSS_PROP_BORDER(
    border-image-repeat,
    border_image_repeat,
    BorderImageRepeat,
    CSS_PROPERTY_PARSE_FUNCTION,
    "",
    0,
    kBorderImageRepeatKTable,
    eStyleAnimType_Discrete)
CSS_PROP_BORDER(
    border-image-slice,
    border_image_slice,
    BorderImageSlice,
    CSS_PROPERTY_PARSE_FUNCTION,
    "",
    0,
    kBorderImageSliceKTable,
    eStyleAnimType_Discrete)
CSS_PROP_BORDER(
    border-image-source,
    border_image_source,
    BorderImageSource,
    0,
    "",
    VARIANT_IMAGE | VARIANT_INHERIT,
    nullptr,
    eStyleAnimType_Discrete)
CSS_PROP_BORDER(
    border-image-width,
    border_image_width,
    BorderImageWidth,
    CSS_PROPERTY_PARSE_FUNCTION,
    "",
    0,
    nullptr,
    eStyleAnimType_Discrete)
CSS_PROP_SHORTHAND(
    border-inline-end,
    border_inline_end,
    BorderInlineEnd,
    CSS_PROPERTY_PARSE_FUNCTION,
    "")
CSS_PROP_LOGICAL(
    border-inline-end-color,
    border_inline_end_color,
    BorderInlineEndColor,
    0,
    "",
    VARIANT_HC,
    nullptr,
    Border,
    eStyleAnimType_None)
CSS_PROP_LOGICAL(
    border-inline-end-style,
    border_inline_end_style,
    BorderInlineEndStyle,
    0,
    "",
    VARIANT_HK,
    kBorderStyleKTable,
    Border,
    eStyleAnimType_None)
CSS_PROP_LOGICAL(
    border-inline-end-width,
    border_inline_end_width,
    BorderInlineEndWidth,
    CSS_PROPERTY_GETCS_NEEDS_LAYOUT_FLUSH,
    "",
    VARIANT_HKL | VARIANT_CALC,
    kBorderWidthKTable,
    Border,
    eStyleAnimType_None)
CSS_PROP_SHORTHAND(
    border-inline-start,
    border_inline_start,
    BorderInlineStart,
    CSS_PROPERTY_PARSE_FUNCTION,
    "")
CSS_PROP_LOGICAL(
    border-inline-start-color,
    border_inline_start_color,
    BorderInlineStartColor,
    0,
    "",
    VARIANT_HC,
    nullptr,
    Border,
    eStyleAnimType_None)
CSS_PROP_LOGICAL(
    border-inline-start-style,
    border_inline_start_style,
    BorderInlineStartStyle,
    0,
    "",
    VARIANT_HK,
    kBorderStyleKTable,
    Border,
    eStyleAnimType_None)
CSS_PROP_LOGICAL(
    border-inline-start-width,
    border_inline_start_width,
    BorderInlineStartWidth,
    CSS_PROPERTY_GETCS_NEEDS_LAYOUT_FLUSH,
    "",
    VARIANT_HKL | VARIANT_CALC,
    kBorderWidthKTable,
    Border,
    eStyleAnimType_None)
CSS_PROP_SHORTHAND(
    border-left,
    border_left,
    BorderLeft,
    CSS_PROPERTY_PARSE_FUNCTION,
    "")
CSS_PROP_BORDER(
    border-left-color,
    border_left_color,
    BorderLeftColor,
    0,
    "",
    VARIANT_HC,
    nullptr,
    eStyleAnimType_ComplexColor)
CSS_PROP_BORDER(
    border-left-style,
    border_left_style,
    BorderLeftStyle,
    0,
    "",
    VARIANT_HK,
    kBorderStyleKTable,
    eStyleAnimType_Discrete)
CSS_PROP_BORDER(
    border-left-width,
    border_left_width,
    BorderLeftWidth,
    CSS_PROPERTY_GETCS_NEEDS_LAYOUT_FLUSH,
    "",
    VARIANT_HKL | VARIANT_CALC,
    kBorderWidthKTable,
    eStyleAnimType_Custom)
CSS_PROP_SHORTHAND(
    border-radius,
    border_radius,
    BorderRadius,
    CSS_PROPERTY_PARSE_FUNCTION,
    "")
CSS_PROP_SHORTHAND(
    border-right,
    border_right,
    BorderRight,
    CSS_PROPERTY_PARSE_FUNCTION,
    "")
CSS_PROP_BORDER(
    border-right-color,
    border_right_color,
    BorderRightColor,
    0,
    "",
    VARIANT_HC,
    nullptr,
    eStyleAnimType_ComplexColor)
CSS_PROP_BORDER(
    border-right-style,
    border_right_style,
    BorderRightStyle,
    0,
    "",
    VARIANT_HK,
    kBorderStyleKTable,
    eStyleAnimType_Discrete)
CSS_PROP_BORDER(
    border-right-width,
    border_right_width,
    BorderRightWidth,
    CSS_PROPERTY_GETCS_NEEDS_LAYOUT_FLUSH,
    "",
    VARIANT_HKL | VARIANT_CALC,
    kBorderWidthKTable,
    eStyleAnimType_Custom)
CSS_PROP_TABLEBORDER(
    border-spacing,
    border_spacing,
    BorderSpacing,
    CSS_PROPERTY_PARSE_FUNCTION,
    "",
    0,
    nullptr,
    eStyleAnimType_Custom)
CSS_PROP_SHORTHAND(
    border-style,
    border_style,
    BorderStyle,
    CSS_PROPERTY_PARSE_FUNCTION,
    "")  // on/off will need reflow
CSS_PROP_SHORTHAND(
    border-top,
    border_top,
    BorderTop,
    CSS_PROPERTY_PARSE_FUNCTION,
    "")
CSS_PROP_BORDER(
    border-top-color,
    border_top_color,
    BorderTopColor,
    0,
    "",
    VARIANT_HC,
    nullptr,
    eStyleAnimType_ComplexColor)
CSS_PROP_BORDER(
    border-top-left-radius,
    border_top_left_radius,
    BorderTopLeftRadius,
    CSS_PROPERTY_PARSE_FUNCTION,
    "",
    0,
    nullptr,
    eStyleAnimType_Corner_TopLeft)
CSS_PROP_BORDER(
    border-top-right-radius,
    border_top_right_radius,
    BorderTopRightRadius,
    CSS_PROPERTY_PARSE_FUNCTION,
    "",
    0,
    nullptr,
    eStyleAnimType_Corner_TopRight)
CSS_PROP_BORDER(
    border-top-style,
    border_top_style,
    BorderTopStyle,
    0,
    "",
    VARIANT_HK,
    kBorderStyleKTable,
    eStyleAnimType_Discrete)  // on/off will need reflow
CSS_PROP_BORDER(
    border-top-width,
    border_top_width,
    BorderTopWidth,
    CSS_PROPERTY_GETCS_NEEDS_LAYOUT_FLUSH,
    "",
    VARIANT_HKL | VARIANT_CALC,
    kBorderWidthKTable,
    eStyleAnimType_Custom)
CSS_PROP_SHORTHAND(
    border-width,
    border_width,
    BorderWidth,
    CSS_PROPERTY_PARSE_FUNCTION,
    "")
CSS_PROP_POSITION(
    bottom,
    bottom,
    Bottom,
    CSS_PROPERTY_GETCS_NEEDS_LAYOUT_FLUSH,
    "",
    VARIANT_AHLP | VARIANT_CALC,
    nullptr,
    eStyleAnimType_Sides_Bottom)
CSS_PROP_XUL(
    -moz-box-align,
    _moz_box_align,
    CSS_PROP_DOMPROP_PREFIXED(BoxAlign),
    0,
    "",
    VARIANT_HK,
    kBoxAlignKTable,
    eStyleAnimType_Discrete) // XXX bug 3935
CSS_PROP_BORDER(
    box-decoration-break,
    box_decoration_break,
    BoxDecorationBreak,
    0,
    "layout.css.box-decoration-break.enabled",
    VARIANT_HK,
    kBoxDecorationBreakKTable,
    eStyleAnimType_Discrete)
CSS_PROP_XUL(
    -moz-box-direction,
    _moz_box_direction,
    CSS_PROP_DOMPROP_PREFIXED(BoxDirection),
    0,
    "",
    VARIANT_HK,
    kBoxDirectionKTable,
    eStyleAnimType_Discrete) // XXX bug 3935
CSS_PROP_XUL(
    -moz-box-flex,
    _moz_box_flex,
    CSS_PROP_DOMPROP_PREFIXED(BoxFlex),
    0,
    "",
    VARIANT_HN,
    nullptr,
    eStyleAnimType_float) // XXX bug 3935
CSS_PROP_XUL(
    -moz-box-ordinal-group,
    _moz_box_ordinal_group,
    CSS_PROP_DOMPROP_PREFIXED(BoxOrdinalGroup),
    0,
    "",
    VARIANT_HI,
    nullptr,
    eStyleAnimType_Discrete)
CSS_PROP_XUL(
    -moz-box-orient,
    _moz_box_orient,
    CSS_PROP_DOMPROP_PREFIXED(BoxOrient),
    0,
    "",
    VARIANT_HK,
    kBoxOrientKTable,
    eStyleAnimType_Discrete) // XXX bug 3935
CSS_PROP_XUL(
    -moz-box-pack,
    _moz_box_pack,
    CSS_PROP_DOMPROP_PREFIXED(BoxPack),
    0,
    "",
    VARIANT_HK,
    kBoxPackKTable,
    eStyleAnimType_Discrete) // XXX bug 3935
CSS_PROP_EFFECTS(
    box-shadow,
    box_shadow,
    BoxShadow,
    CSS_PROPERTY_PARSE_FUNCTION |
        CSS_PROPERTY_VALUE_LIST_USES_COMMAS,
        // NOTE: some components must be nonnegative
    "",
    VARIANT_COLOR | VARIANT_LENGTH | VARIANT_CALC | VARIANT_INHERIT | VARIANT_NONE,
    kBoxShadowTypeKTable,
    eStyleAnimType_Shadow)
CSS_PROP_POSITION(
    box-sizing,
    box_sizing,
    BoxSizing,
    0,
    "",
    VARIANT_HK,
    kBoxSizingKTable,
    eStyleAnimType_Discrete)
CSS_PROP_TABLEBORDER(
    caption-side,
    caption_side,
    CaptionSide,
    0,
    "",
    VARIANT_HK,
    kCaptionSideKTable,
    eStyleAnimType_Discrete)
CSS_PROP_USERINTERFACE(
    caret-color,
    caret_color,
    CaretColor,
    0,
    "",
    VARIANT_AUTO | VARIANT_HC,
    nullptr,
    eStyleAnimType_ComplexColor)
CSS_PROP_DISPLAY(
    clear,
    clear,
    Clear,
    0,
    "",
    VARIANT_HK,
    kClearKTable,
    eStyleAnimType_Discrete)
CSS_PROP_EFFECTS(
    clip,
    clip,
    Clip,
    CSS_PROPERTY_PARSE_FUNCTION,
    "",
    VARIANT_AH,
    nullptr,
    eStyleAnimType_Custom)
CSS_PROP_SVGRESET(
    clip-path,
    clip_path,
    ClipPath,
    CSS_PROPERTY_VALUE_PARSER_FUNCTION,
    "",
    VARIANT_HUO,
    nullptr,
    eStyleAnimType_Custom)
CSS_PROP_SVG(
    clip-rule,
    clip_rule,
    ClipRule,
    0,
    "",
    VARIANT_HK,
    kFillRuleKTable,
    eStyleAnimType_Discrete)
CSS_PROP_COLOR(
    color,
    color,
    Color,
    0,
    "",
    VARIANT_HC,
    nullptr,
    eStyleAnimType_Color)
CSS_PROP_VISIBILITY(
    color-adjust,
    color_adjust,
    ColorAdjust,
    0,
    "layout.css.color-adjust.enabled",
    VARIANT_HK,
    kColorAdjustKTable,
    eStyleAnimType_Discrete)
CSS_PROP_SVG(
    color-interpolation,
    color_interpolation,
    ColorInterpolation,
    0,
    "",
    VARIANT_HK,
    kColorInterpolationKTable,
    eStyleAnimType_Discrete)
CSS_PROP_SVG(
    color-interpolation-filters,
    color_interpolation_filters,
    ColorInterpolationFilters,
    0,
    "",
    VARIANT_HK,
    kColorInterpolationKTable,
    eStyleAnimType_Discrete)
CSS_PROP_COLUMN(
    column-count,
    column_count,
    ColumnCount,
    0,
    "",
    VARIANT_AHI,
    nullptr,
    eStyleAnimType_Custom)
CSS_PROP_COLUMN(
    column-fill,
    column_fill,
    ColumnFill,
    0,
    "",
    VARIANT_HK,
    kColumnFillKTable,
    eStyleAnimType_Discrete)
CSS_PROP_COLUMN(
    column-gap,
    column_gap,
    ColumnGap,
    0,
    "",
    VARIANT_HLP | VARIANT_NORMAL | VARIANT_CALC,
    nullptr,
    eStyleAnimType_Coord)
CSS_PROP_SHORTHAND(
    column-rule,
    column_rule,
    ColumnRule,
    CSS_PROPERTY_PARSE_FUNCTION,
    "")
CSS_PROP_COLUMN(
    column-rule-color,
    column_rule_color,
    ColumnRuleColor,
    0,
    "",
    VARIANT_HC,
    nullptr,
    eStyleAnimType_ComplexColor)
CSS_PROP_COLUMN(
    column-rule-style,
    column_rule_style,
    ColumnRuleStyle,
    0,
    "",
    VARIANT_HK,
    kBorderStyleKTable,
    eStyleAnimType_Discrete)
CSS_PROP_COLUMN(
    column-rule-width,
    column_rule_width,
    ColumnRuleWidth,
    0,
    "",
    VARIANT_HKL | VARIANT_CALC,
    kBorderWidthKTable,
    eStyleAnimType_Custom)
CSS_PROP_COLUMN(
    column-span,
    column_span,
    ColumnSpan,
    0,
    "layout.css.column-span.enabled",
    VARIANT_HK,
    kColumnSpanKTable,
    eStyleAnimType_Discrete)
CSS_PROP_COLUMN(
    column-width,
    column_width,
    ColumnWidth,
    0,
    "",
    VARIANT_AHL | VARIANT_CALC,
    nullptr,
    eStyleAnimType_Coord)
CSS_PROP_SHORTHAND(
    columns,
    columns,
    Columns,
    CSS_PROPERTY_PARSE_FUNCTION,
    "")
CSS_PROP_DISPLAY(
    contain,
    contain,
    Contain,
    CSS_PROPERTY_VALUE_PARSER_FUNCTION,
    "layout.css.contain.enabled",
    // Does not affect parsing, but is needed for tab completion in devtools:
    VARIANT_HK | VARIANT_NONE,
    kContainKTable,
    eStyleAnimType_Discrete)
CSS_PROP_CONTENT(
    content,
    content,
    Content,
    CSS_PROPERTY_PARSE_FUNCTION,
    "",
    VARIANT_HMK | VARIANT_NONE | VARIANT_URL | VARIANT_COUNTER | VARIANT_ATTR,
    kContentKTable,
    eStyleAnimType_Discrete)
#ifndef CSS_PROP_LIST_EXCLUDE_INTERNAL
CSS_PROP_SVG(
    // Only intended to be used internally by Mozilla, so prefixed.
    -moz-context-properties,
    _moz_context_properties,
    CSS_PROP_DOMPROP_PREFIXED(ContextProperties),
    CSS_PROPERTY_PARSE_FUNCTION |
        CSS_PROPERTY_VALUE_LIST_USES_COMMAS |
        CSS_PROPERTY_INTERNAL,
    "",
    0,
    nullptr,
    eStyleAnimType_None)
CSS_PROP_TEXT(
    -moz-control-character-visibility,
    _moz_control_character_visibility,
    CSS_PROP_DOMPROP_PREFIXED(ControlCharacterVisibility),
    CSS_PROPERTY_INTERNAL,
    "",
    VARIANT_HK,
    kControlCharacterVisibilityKTable,
    eStyleAnimType_None)
#endif // CSS_PROP_LIST_EXCLUDE_INTERNAL
CSS_PROP_CONTENT(
    counter-increment,
    counter_increment,
    CounterIncrement,
    CSS_PROPERTY_PARSE_FUNCTION,
    "",
    VARIANT_INHERIT | VARIANT_NONE,
    nullptr,
    eStyleAnimType_Discrete) // XXX bug 137285
CSS_PROP_CONTENT(
    counter-reset,
    counter_reset,
    CounterReset,
    CSS_PROPERTY_PARSE_FUNCTION,
    "",
    VARIANT_INHERIT | VARIANT_NONE,
    nullptr,
    eStyleAnimType_Discrete) // XXX bug 137285
CSS_PROP_USERINTERFACE(
    cursor,
    cursor,
    Cursor,
    CSS_PROPERTY_PARSE_FUNCTION |
        CSS_PROPERTY_VALUE_LIST_USES_COMMAS,
    "",
    0,
    kCursorKTable,
    eStyleAnimType_Discrete)
#ifndef CSS_PROP_LIST_ONLY_COMPONENTS_OF_ALL_SHORTHAND
CSS_PROP_VISIBILITY(
    direction,
    direction,
    Direction,
    0,
    "",
    VARIANT_HK,
    kDirectionKTable,
    eStyleAnimType_Discrete)
#endif // !defined(CSS_PROP_LIST_ONLY_COMPONENTS_OF_ALL_SHORTHAND)
CSS_PROP_DISPLAY(
    display,
    display,
    Display,
    0,
    "",
    VARIANT_HK,
    kDisplayKTable,
    eStyleAnimType_None)
CSS_PROP_SVGRESET(
    dominant-baseline,
    dominant_baseline,
    DominantBaseline,
    0,
    "",
    VARIANT_HK,
    kDominantBaselineKTable,
    eStyleAnimType_Discrete)
CSS_PROP_TABLEBORDER(
    empty-cells,
    empty_cells,
    EmptyCells,
    0,
    "",
    VARIANT_HK,
    kEmptyCellsKTable,
    eStyleAnimType_Discrete)
CSS_PROP_SVG(
    fill,
    fill,
    Fill,
    CSS_PROPERTY_PARSE_FUNCTION,
    "",
    0,
    kContextPatternKTable,
    eStyleAnimType_PaintServer)
CSS_PROP_SVG(
    fill-opacity,
    fill_opacity,
    FillOpacity,
    0,
    "",
    VARIANT_HN | VARIANT_KEYWORD,
    kContextOpacityKTable,
    eStyleAnimType_float)
CSS_PROP_SVG(
    fill-rule,
    fill_rule,
    FillRule,
    0,
    "",
    VARIANT_HK,
    kFillRuleKTable,
    eStyleAnimType_Discrete)
CSS_PROP_EFFECTS(
    filter,
    filter,
    Filter,
    CSS_PROPERTY_PARSE_FUNCTION,
    "",
    0,
    nullptr,
    eStyleAnimType_Custom)
CSS_PROP_SHORTHAND(
    flex,
    flex,
    Flex,
    CSS_PROPERTY_PARSE_FUNCTION,
    "")
CSS_PROP_POSITION(
    flex-basis,
    flex_basis,
    FlexBasis,
    0,
    "",
    // NOTE: The parsing implementation for the 'flex' shorthand property has
    // its own code to parse each subproperty. It does not depend on the
    // longhand parsing defined here.
    VARIANT_AHKLP | VARIANT_CALC,
    kFlexBasisKTable,
    eStyleAnimType_Coord)
CSS_PROP_POSITION(
    flex-direction,
    flex_direction,
    FlexDirection,
    0,
    "",
    VARIANT_HK,
    kFlexDirectionKTable,
    eStyleAnimType_Discrete)
CSS_PROP_SHORTHAND(
    flex-flow,
    flex_flow,
    FlexFlow,
    CSS_PROPERTY_PARSE_FUNCTION,
    "")
CSS_PROP_POSITION(
    flex-grow,
    flex_grow,
    FlexGrow,
    0,
    "",
    // NOTE: The parsing implementation for the 'flex' shorthand property has
    // its own code to parse each subproperty. It does not depend on the
    // longhand parsing defined here.
    VARIANT_HN,
    nullptr,
    eStyleAnimType_float)
CSS_PROP_POSITION(
    flex-shrink,
    flex_shrink,
    FlexShrink,
    0,
    "",
    // NOTE: The parsing implementation for the 'flex' shorthand property has
    // its own code to parse each subproperty. It does not depend on the
    // longhand parsing defined here.
    VARIANT_HN,
    nullptr,
    eStyleAnimType_float)
CSS_PROP_POSITION(
    flex-wrap,
    flex_wrap,
    FlexWrap,
    0,
    "",
    VARIANT_HK,
    kFlexWrapKTable,
    eStyleAnimType_Discrete)
CSS_PROP_DISPLAY(
    float,
    float_,
    CSS_PROP_PUBLIC_OR_PRIVATE(CssFloat, Float),
    0,
    "",
    VARIANT_HK,
    kFloatKTable,
    eStyleAnimType_Discrete)
CSS_PROP_BORDER(
    -moz-float-edge,
    _moz_float_edge,
    CSS_PROP_DOMPROP_PREFIXED(FloatEdge),
    0,
    "",
    VARIANT_HK,
    kFloatEdgeKTable,
    eStyleAnimType_Discrete) // XXX bug 3935
CSS_PROP_SVGRESET(
    flood-color,
    flood_color,
    FloodColor,
    0,
    "",
    VARIANT_HC,
    nullptr,
    eStyleAnimType_Color)
CSS_PROP_SVGRESET(
    flood-opacity,
    flood_opacity,
    FloodOpacity,
    0,
    "",
    VARIANT_HN,
    nullptr,
    eStyleAnimType_float)
CSS_PROP_SHORTHAND(
    font,
    font,
    Font,
    CSS_PROPERTY_PARSE_FUNCTION,
    "")
CSS_PROP_FONT(
    font-family,
    font_family,
    FontFamily,
    CSS_PROPERTY_VALUE_PARSER_FUNCTION,
    "",
    0,
    nullptr,
    eStyleAnimType_Discrete)
CSS_PROP_FONT(
    font-feature-settings,
    font_feature_settings,
    FontFeatureSettings,
    CSS_PROPERTY_VALUE_PARSER_FUNCTION,
    "",
    0,
    nullptr,
    eStyleAnimType_Discrete)
CSS_PROP_FONT(
    font-kerning,
    font_kerning,
    FontKerning,
    0,
    "",
    VARIANT_HK,
    kFontKerningKTable,
    eStyleAnimType_Discrete)
CSS_PROP_FONT(
    font-language-override,
    font_language_override,
    FontLanguageOverride,
    0,
    "",
    VARIANT_NORMAL | VARIANT_INHERIT | VARIANT_STRING,
    nullptr,
    eStyleAnimType_Discrete)
CSS_PROP_FONT(
    font-optical-sizing,
    font_optical_sizing,
    FontOpticalSizing,
    0,
    "layout.css.font-variations.enabled",
    VARIANT_HK,
    kFontOpticalSizingKTable,
    eStyleAnimType_None)
CSS_PROP_FONT(
    font-size,
    font_size,
    FontSize,
    0,
    "",
    VARIANT_HKLP | VARIANT_SYSFONT | VARIANT_CALC,
    kFontSizeKTable,
    // Note that mSize is the correct place for *reading* the computed value,
    // but setting it requires setting mFont.size as well.
    eStyleAnimType_nscoord)
CSS_PROP_FONT(
    font-size-adjust,
    font_size_adjust,
    FontSizeAdjust,
    0,
    "",
    VARIANT_HON | VARIANT_SYSFONT,
    nullptr,
    eStyleAnimType_float)
#ifndef CSS_PROP_LIST_EXCLUDE_INTERNAL
CSS_PROP_FONT(
    -moz-font-smoothing-background-color,
    _moz_font_smoothing_background_color,
    CSS_PROP_DOMPROP_PREFIXED(FontSmoothingBackgroundColor),
    CSS_PROPERTY_INTERNAL |
        CSS_PROPERTY_ENABLED_IN_UA_SHEETS_AND_CHROME,
    "",
    VARIANT_HC,
    nullptr,
    eStyleAnimType_Color)
#endif // CSS_PROP_LIST_EXCLUDE_INTERNAL
CSS_PROP_FONT(
    font-stretch,
    font_stretch,
    FontStretch,
    0,
    "",
    VARIANT_HK | VARIANT_SYSFONT,
    kFontStretchKTable,
    eStyleAnimType_Custom)
CSS_PROP_FONT(
    font-style,
    font_style,
    FontStyle,
    0,
    "",
    VARIANT_HK | VARIANT_SYSFONT,
    kFontStyleKTable,
    eStyleAnimType_Discrete)
CSS_PROP_FONT(
    font-synthesis,
    font_synthesis,
    FontSynthesis,
    CSS_PROPERTY_VALUE_PARSER_FUNCTION,
    "",
    0,
    kFontSynthesisKTable,
    eStyleAnimType_Discrete)
CSS_PROP_SHORTHAND(
    font-variant,
    font_variant,
    FontVariant,
    CSS_PROPERTY_PARSE_FUNCTION,
    "")
CSS_PROP_FONT(
    font-variant-alternates,
    font_variant_alternates,
    FontVariantAlternates,
    CSS_PROPERTY_VALUE_PARSER_FUNCTION,
    "",
    0,
    kFontVariantAlternatesKTable,
    eStyleAnimType_Discrete)
CSS_PROP_FONT(
    font-variant-caps,
    font_variant_caps,
    FontVariantCaps,
    0,
    "",
    VARIANT_HMK,
    kFontVariantCapsKTable,
    eStyleAnimType_Discrete)
CSS_PROP_FONT(
    font-variant-east-asian,
    font_variant_east_asian,
    FontVariantEastAsian,
    CSS_PROPERTY_VALUE_PARSER_FUNCTION,
    "",
    0,
    kFontVariantEastAsianKTable,
    eStyleAnimType_Discrete)
CSS_PROP_FONT(
    font-variant-ligatures,
    font_variant_ligatures,
    FontVariantLigatures,
    CSS_PROPERTY_VALUE_PARSER_FUNCTION,
    "",
    0,
    kFontVariantLigaturesKTable,
    eStyleAnimType_Discrete)
CSS_PROP_FONT(
    font-variant-numeric,
    font_variant_numeric,
    FontVariantNumeric,
    CSS_PROPERTY_VALUE_PARSER_FUNCTION,
    "",
    0,
    kFontVariantNumericKTable,
    eStyleAnimType_Discrete)
CSS_PROP_FONT(
    font-variant-position,
    font_variant_position,
    FontVariantPosition,
    0,
    "",
    VARIANT_HMK,
    kFontVariantPositionKTable,
    eStyleAnimType_Discrete)
CSS_PROP_FONT(
    font-variation-settings,
    font_variation_settings,
    FontVariationSettings,
    CSS_PROPERTY_VALUE_PARSER_FUNCTION |
        CSS_PROPERTY_VALUE_LIST_USES_COMMAS,
    "layout.css.font-variations.enabled",
    0,
    nullptr,
    eStyleAnimType_Custom)
CSS_PROP_FONT(
    font-weight,
    font_weight,
    FontWeight,
    CSS_PROPERTY_VALUE_PARSER_FUNCTION,
        // NOTE: This property has range restrictions on interpolation!
    "",
    0,
    kFontWeightKTable,
    eStyleAnimType_Custom)
CSS_PROP_UIRESET(
    -moz-force-broken-image-icon,
    _moz_force_broken_image_icon,
    CSS_PROP_DOMPROP_PREFIXED(ForceBrokenImageIcon),
    0,
    "",
    VARIANT_HI,
    nullptr,
    eStyleAnimType_Discrete) // bug 58646
CSS_PROP_SHORTHAND(
    grid,
    grid,
    Grid,
    CSS_PROPERTY_PARSE_FUNCTION,
    "")
CSS_PROP_SHORTHAND(
    grid-area,
    grid_area,
    GridArea,
    CSS_PROPERTY_PARSE_FUNCTION,
    "")
CSS_PROP_POSITION(
    grid-auto-columns,
    grid_auto_columns,
    GridAutoColumns,
    CSS_PROPERTY_PARSE_FUNCTION,
    "",
    0,
    kGridTrackBreadthKTable,
    eStyleAnimType_Discrete)
CSS_PROP_POSITION(
    grid-auto-flow,
    grid_auto_flow,
    GridAutoFlow,
    CSS_PROPERTY_PARSE_FUNCTION,
    "",
    0,
    kGridAutoFlowKTable,
    eStyleAnimType_Discrete)
CSS_PROP_POSITION(
    grid-auto-rows,
    grid_auto_rows,
    GridAutoRows,
    CSS_PROPERTY_PARSE_FUNCTION,
    "",
    0,
    kGridTrackBreadthKTable,
    eStyleAnimType_Discrete)
CSS_PROP_SHORTHAND(
    grid-column,
    grid_column,
    GridColumn,
    CSS_PROPERTY_PARSE_FUNCTION,
    "")
CSS_PROP_POSITION(
    grid-column-end,
    grid_column_end,
    GridColumnEnd,
    CSS_PROPERTY_PARSE_FUNCTION,
    "",
    0,
    nullptr,
    eStyleAnimType_Discrete)
CSS_PROP_POSITION(
    grid-column-gap,
    grid_column_gap,
    GridColumnGap,
    0,
    "",
    VARIANT_HLP | VARIANT_CALC,
    nullptr,
    eStyleAnimType_Coord)
CSS_PROP_POSITION(
    grid-column-start,
    grid_column_start,
    GridColumnStart,
    CSS_PROPERTY_PARSE_FUNCTION,
    "",
    0,
    nullptr,
    eStyleAnimType_Discrete)
CSS_PROP_SHORTHAND(
    grid-gap,
    grid_gap,
    GridGap,
    CSS_PROPERTY_PARSE_FUNCTION,
    "")
CSS_PROP_SHORTHAND(
    grid-row,
    grid_row,
    GridRow,
    CSS_PROPERTY_PARSE_FUNCTION,
    "")
CSS_PROP_POSITION(
    grid-row-end,
    grid_row_end,
    GridRowEnd,
    CSS_PROPERTY_PARSE_FUNCTION,
    "",
    0,
    nullptr,
    eStyleAnimType_Discrete)
CSS_PROP_POSITION(
    grid-row-gap,
    grid_row_gap,
    GridRowGap,
    0,
    "",
    VARIANT_HLP | VARIANT_CALC,
    nullptr,
    eStyleAnimType_Coord)
CSS_PROP_POSITION(
    grid-row-start,
    grid_row_start,
    GridRowStart,
    CSS_PROPERTY_PARSE_FUNCTION,
    "",
    0,
    nullptr,
    eStyleAnimType_Discrete)
CSS_PROP_SHORTHAND(
    grid-template,
    grid_template,
    GridTemplate,
    CSS_PROPERTY_PARSE_FUNCTION,
    "")
CSS_PROP_POSITION(
    grid-template-areas,
    grid_template_areas,
    GridTemplateAreas,
    CSS_PROPERTY_PARSE_FUNCTION,
    "",
    0,
    nullptr,
    eStyleAnimType_Discrete)
CSS_PROP_POSITION(
    grid-template-columns,
    grid_template_columns,
    GridTemplateColumns,
    CSS_PROPERTY_PARSE_FUNCTION |
        CSS_PROPERTY_GETCS_NEEDS_LAYOUT_FLUSH,
    "",
    0,
    kGridTrackBreadthKTable,
    eStyleAnimType_Discrete)
CSS_PROP_POSITION(
    grid-template-rows,
    grid_template_rows,
    GridTemplateRows,
    CSS_PROPERTY_PARSE_FUNCTION |
        CSS_PROPERTY_GETCS_NEEDS_LAYOUT_FLUSH,
    "",
    0,
    kGridTrackBreadthKTable,
    eStyleAnimType_Discrete)
CSS_PROP_POSITION(
    height,
    height,
    Height,
    CSS_PROPERTY_GETCS_NEEDS_LAYOUT_FLUSH,
    "",
    VARIANT_AHKLP | VARIANT_CALC,
    kWidthKTable,
    eStyleAnimType_Coord)
CSS_PROP_TEXT(
    hyphens,
    hyphens,
    Hyphens,
    0,
    "",
    VARIANT_HK,
    kHyphensKTable,
    eStyleAnimType_Discrete)
CSS_PROP_TEXTRESET(
    initial-letter,
    initial_letter,
    InitialLetter,
    CSS_PROPERTY_PARSE_FUNCTION,
    "layout.css.initial-letter.enabled",
    0,
    nullptr,
    eStyleAnimType_Discrete)
CSS_PROP_VISIBILITY(
    image-orientation,
    image_orientation,
    ImageOrientation,
    CSS_PROPERTY_VALUE_PARSER_FUNCTION,
    "layout.css.image-orientation.enabled",
    0,
    kImageOrientationKTable,
    eStyleAnimType_Discrete)
CSS_PROP_LIST(
    -moz-image-region,
    _moz_image_region,
    CSS_PROP_DOMPROP_PREFIXED(ImageRegion),
    CSS_PROPERTY_PARSE_FUNCTION,
    "",
    0,
    nullptr,
    eStyleAnimType_Custom)
CSS_PROP_VISIBILITY(
    image-rendering,
    image_rendering,
    ImageRendering,
    0,
    "",
    VARIANT_HK,
    kImageRenderingKTable,
    eStyleAnimType_Discrete)
CSS_PROP_UIRESET(
    ime-mode,
    ime_mode,
    ImeMode,
    0,
    "",
    VARIANT_HK,
    kIMEModeKTable,
    eStyleAnimType_Discrete)
CSS_PROP_LOGICAL(
    inline-size,
    inline_size,
    InlineSize,
    CSS_PROPERTY_GETCS_NEEDS_LAYOUT_FLUSH,
    "",
    VARIANT_AHKLP | VARIANT_CALC,
    kWidthKTable,
    Position,
    eStyleAnimType_None)
CSS_PROP_DISPLAY(
    isolation,
    isolation,
    Isolation,
    0,
    "layout.css.isolation.enabled",
    VARIANT_HK,
    kIsolationKTable,
    eStyleAnimType_Discrete)
CSS_PROP_POSITION(
    justify-content,
    justify_content,
    JustifyContent,
    CSS_PROPERTY_PARSE_FUNCTION,
    "",
    VARIANT_HK,
    kAutoCompletionAlignJustifyContent,
    eStyleAnimType_Discrete)
CSS_PROP_POSITION(
    justify-items,
    justify_items,
    JustifyItems,
    CSS_PROPERTY_PARSE_FUNCTION,
    "",
    VARIANT_HK,
    // for auto-completion we use same values as justify-self:
    kAutoCompletionAlignJustifySelf,
    eStyleAnimType_Discrete)
CSS_PROP_POSITION(
    justify-self,
    justify_self,
    JustifySelf,
    CSS_PROPERTY_PARSE_FUNCTION,
    "",
    VARIANT_HK,
    kAutoCompletionAlignJustifySelf,
    eStyleAnimType_Discrete)
#ifndef CSS_PROP_LIST_ONLY_COMPONENTS_OF_ALL_SHORTHAND
#ifndef CSS_PROP_LIST_EXCLUDE_INTERNAL
CSS_PROP_FONT(
    -x-lang,
    _x_lang,
    Lang,
    CSS_PROPERTY_INTERNAL |
        CSS_PROPERTY_PARSE_INACCESSIBLE,
    "",
    0,
    nullptr,
    eStyleAnimType_None)
#endif // CSS_PROP_LIST_EXCLUDE_INTERNAL
#endif // CSS_PROP_LIST_ONLY_COMPONENTS_OF_ALL_SHORTHAND
CSS_PROP_POSITION(
    left,
    left,
    Left,
    CSS_PROPERTY_GETCS_NEEDS_LAYOUT_FLUSH,
    "",
    VARIANT_AHLP | VARIANT_CALC,
    nullptr,
    eStyleAnimType_Sides_Left)
CSS_PROP_TEXT(
    letter-spacing,
    letter_spacing,
    LetterSpacing,
    0,
    "",
    VARIANT_HL | VARIANT_NORMAL | VARIANT_CALC,
    nullptr,
    eStyleAnimType_Coord)
CSS_PROP_SVGRESET(
    lighting-color,
    lighting_color,
    LightingColor,
    0,
    "",
    VARIANT_HC,
    nullptr,
    eStyleAnimType_Color)
CSS_PROP_TEXT(
    line-height,
    line_height,
    LineHeight,
    CSS_PROPERTY_GETCS_NEEDS_LAYOUT_FLUSH,
    "",
    VARIANT_HLPN | VARIANT_KEYWORD | VARIANT_NORMAL | VARIANT_SYSFONT | VARIANT_CALC,
    kLineHeightKTable,
    eStyleAnimType_Coord)
CSS_PROP_SHORTHAND(
    list-style,
    list_style,
    ListStyle,
    CSS_PROPERTY_PARSE_FUNCTION,
    "")
CSS_PROP_LIST(
    list-style-image,
    list_style_image,
    ListStyleImage,
    0,
    "",
    VARIANT_HUO,
    nullptr,
    eStyleAnimType_Discrete)
CSS_PROP_LIST(
    list-style-position,
    list_style_position,
    ListStylePosition,
    0,
    "",
    VARIANT_HK,
    kListStylePositionKTable,
    eStyleAnimType_Discrete)
CSS_PROP_LIST(
    list-style-type,
    list_style_type,
    ListStyleType,
    CSS_PROPERTY_VALUE_PARSER_FUNCTION,
    "",
    0,
    nullptr,
    eStyleAnimType_Discrete)
CSS_PROP_SHORTHAND(
    margin,
    margin,
    Margin,
    CSS_PROPERTY_PARSE_FUNCTION,
    "")
CSS_PROP_LOGICAL(
    margin-block-end,
    margin_block_end,
    MarginBlockEnd,
    CSS_PROPERTY_GETCS_NEEDS_LAYOUT_FLUSH,
    "",
    VARIANT_AHLP | VARIANT_CALC,
    nullptr,
    Margin,
    eStyleAnimType_None)
CSS_PROP_LOGICAL(
    margin-block-start,
    margin_block_start,
    MarginBlockStart,
    CSS_PROPERTY_GETCS_NEEDS_LAYOUT_FLUSH,
    "",
    VARIANT_AHLP | VARIANT_CALC,
    nullptr,
    Margin,
    eStyleAnimType_None)
CSS_PROP_MARGIN(
    margin-bottom,
    margin_bottom,
    MarginBottom,
    CSS_PROPERTY_GETCS_NEEDS_LAYOUT_FLUSH,
    "",
    VARIANT_AHLP | VARIANT_CALC,
    nullptr,
    eStyleAnimType_Sides_Bottom)
CSS_PROP_LOGICAL(
    margin-inline-end,
    margin_inline_end,
    MarginInlineEnd,
    0,
    "",
    VARIANT_AHLP | VARIANT_CALC,
    nullptr,
    Margin,
    eStyleAnimType_None)
CSS_PROP_LOGICAL(
    margin-inline-start,
    margin_inline_start,
    MarginInlineStart,
    0,
    "",
    VARIANT_AHLP | VARIANT_CALC,
    nullptr,
    Margin,
    eStyleAnimType_None)
CSS_PROP_MARGIN(
    margin-left,
    margin_left,
    MarginLeft,
    CSS_PROPERTY_GETCS_NEEDS_LAYOUT_FLUSH,
    "",
    VARIANT_AHLP | VARIANT_CALC,
    nullptr,
    eStyleAnimType_Sides_Left)
CSS_PROP_MARGIN(
    margin-right,
    margin_right,
    MarginRight,
    CSS_PROPERTY_GETCS_NEEDS_LAYOUT_FLUSH,
    "",
    VARIANT_AHLP | VARIANT_CALC,
    nullptr,
    eStyleAnimType_Sides_Right)
CSS_PROP_MARGIN(
    margin-top,
    margin_top,
    MarginTop,
    CSS_PROPERTY_GETCS_NEEDS_LAYOUT_FLUSH,
    "",
    VARIANT_AHLP | VARIANT_CALC,
    nullptr,
    eStyleAnimType_Sides_Top)
CSS_PROP_SHORTHAND(
    marker,
    marker,
    Marker,
    CSS_PROPERTY_PARSE_FUNCTION,
    "")
CSS_PROP_SVG(
    marker-end,
    marker_end,
    MarkerEnd,
    0,
    "",
    VARIANT_HUO,
    nullptr,
    eStyleAnimType_Discrete)
CSS_PROP_SVG(
    marker-mid,
    marker_mid,
    MarkerMid,
    0,
    "",
    VARIANT_HUO,
    nullptr,
    eStyleAnimType_Discrete)
CSS_PROP_SVG(
    marker-start,
    marker_start,
    MarkerStart,
    0,
    "",
    VARIANT_HUO,
    nullptr,
    eStyleAnimType_Discrete)
CSS_PROP_SHORTHAND(
    mask,
    mask,
    Mask,
    CSS_PROPERTY_PARSE_FUNCTION,
    "")
CSS_PROP_SVGRESET(
    mask-clip,
    mask_clip,
    MaskClip,
    CSS_PROPERTY_VALUE_LIST_USES_COMMAS,
    "",
    VARIANT_KEYWORD, // used by list parsing
    kMaskClipKTable,
    eStyleAnimType_Discrete)
CSS_PROP_SVGRESET(
    mask-composite,
    mask_composite,
    MaskComposite,
    CSS_PROPERTY_VALUE_LIST_USES_COMMAS,
    "",
    VARIANT_KEYWORD, // used by list parsing
    kImageLayerCompositeKTable,
    eStyleAnimType_Discrete)
CSS_PROP_SVGRESET(
    mask-image,
    mask_image,
    MaskImage,
    CSS_PROPERTY_VALUE_LIST_USES_COMMAS,
    "",
    VARIANT_IMAGE, // used by list parsing
    nullptr,
    eStyleAnimType_Discrete)
CSS_PROP_SVGRESET(
    mask-mode,
    mask_mode,
    MaskMode,
    CSS_PROPERTY_VALUE_LIST_USES_COMMAS,
    "",
    VARIANT_KEYWORD, // used by list parsing
    kImageLayerModeKTable,
    eStyleAnimType_Discrete)
CSS_PROP_SVGRESET(
    mask-origin,
    mask_origin,
    MaskOrigin,
    CSS_PROPERTY_VALUE_LIST_USES_COMMAS,
    "",
    VARIANT_KEYWORD, // used by list parsing
    kMaskOriginKTable,
    eStyleAnimType_Discrete)
CSS_PROP_SHORTHAND(
    mask-position,
    mask_position,
    MaskPosition,
    CSS_PROPERTY_PARSE_FUNCTION,
    "")
CSS_PROP_SVGRESET(
    mask-position-x,
    mask_position_x,
    MaskPositionX,
    CSS_PROPERTY_PARSE_FUNCTION |
        CSS_PROPERTY_VALUE_LIST_USES_COMMAS,
    "",
    0,
    kImageLayerPositionKTable,
    eStyleAnimType_Custom)
CSS_PROP_SVGRESET(
    mask-position-y,
    mask_position_y,
    MaskPositionY,
    CSS_PROPERTY_PARSE_FUNCTION |
        CSS_PROPERTY_VALUE_LIST_USES_COMMAS,
    "",
    0,
    kImageLayerPositionKTable,
    eStyleAnimType_Custom)
CSS_PROP_SVGRESET(
    mask-repeat,
    mask_repeat,
    MaskRepeat,
    CSS_PROPERTY_PARSE_FUNCTION |
        CSS_PROPERTY_VALUE_LIST_USES_COMMAS,
    "",
    VARIANT_KEYWORD, // used by list parsing
    kImageLayerRepeatKTable,
    eStyleAnimType_Discrete)
CSS_PROP_SVGRESET(
    mask-size,
    mask_size,
    MaskSize,
    CSS_PROPERTY_PARSE_FUNCTION |
        CSS_PROPERTY_VALUE_LIST_USES_COMMAS,
    "",
    0,
    kImageLayerSizeKTable,
    eStyleAnimType_Custom)
CSS_PROP_SVGRESET(
    mask-type,
    mask_type,
    MaskType,
    0,
    "",
    VARIANT_HK,
    kMaskTypeKTable,
    eStyleAnimType_Discrete)
#ifndef CSS_PROP_LIST_ONLY_COMPONENTS_OF_ALL_SHORTHAND
#ifndef CSS_PROP_LIST_EXCLUDE_INTERNAL
CSS_PROP_FONT(
    -moz-math-display,
    _moz_math_display,
    MathDisplay,
    CSS_PROPERTY_INTERNAL |
        CSS_PROPERTY_ENABLED_IN_UA_SHEETS,
    "",
    VARIANT_HK,
    kMathDisplayKTable,
    eStyleAnimType_None)
CSS_PROP_FONT(
    -moz-math-variant,
    _moz_math_variant,
    MathVariant,
    CSS_PROPERTY_INTERNAL |
        CSS_PROPERTY_PARSE_INACCESSIBLE,
    "",
    VARIANT_HK,
    kMathVariantKTable,
    eStyleAnimType_None)
#endif // CSS_PROP_LIST_EXCLUDE_INTERNAL
#endif // CSS_PROP_LIST_ONLY_COMPONENTS_OF_ALL_SHORTHAND
CSS_PROP_LOGICAL(
    max-block-size,
    max_block_size,
    MaxBlockSize,
    CSS_PROPERTY_GETCS_NEEDS_LAYOUT_FLUSH,
    "",
    VARIANT_HLPO | VARIANT_CALC,
    nullptr,
    Position,
    eStyleAnimType_None)
CSS_PROP_POSITION(
    max-height,
    max_height,
    MaxHeight,
    0,
    "",
    VARIANT_HKLPO | VARIANT_CALC,
    kWidthKTable,
    eStyleAnimType_Coord)
CSS_PROP_LOGICAL(
    max-inline-size,
    max_inline_size,
    MaxInlineSize,
    CSS_PROPERTY_GETCS_NEEDS_LAYOUT_FLUSH,
    "",
    VARIANT_HKLPO | VARIANT_CALC,
    kWidthKTable,
    Position,
    eStyleAnimType_None)
CSS_PROP_POSITION(
    max-width,
    max_width,
    MaxWidth,
    0,
    "",
    VARIANT_HKLPO | VARIANT_CALC,
    kWidthKTable,
    eStyleAnimType_Coord)
CSS_PROP_LOGICAL(
    min-block-size,
    min_block_size,
    MinBlockSize,
    CSS_PROPERTY_GETCS_NEEDS_LAYOUT_FLUSH,
    "",
    VARIANT_AHLP | VARIANT_CALC,
    nullptr,
    Position,
    eStyleAnimType_None)
#ifndef CSS_PROP_LIST_EXCLUDE_INTERNAL
CSS_PROP_FONT(
    -moz-min-font-size-ratio,
    _moz_min_font_size_ratio,
    CSS_PROP_DOMPROP_PREFIXED(MinFontSizeRatio),
    CSS_PROPERTY_INTERNAL |
        CSS_PROPERTY_ENABLED_IN_UA_SHEETS,
    "",
    VARIANT_INHERIT | VARIANT_PERCENT,
    nullptr,
    eStyleAnimType_None)
#endif // CSS_PROP_LIST_EXCLUDE_INTERNAL
CSS_PROP_POSITION(
    min-height,
    min_height,
    MinHeight,
    0,
    "",
    VARIANT_AHKLP | VARIANT_CALC,
    kWidthKTable,
    eStyleAnimType_Coord)
CSS_PROP_LOGICAL(
    min-inline-size,
    min_inline_size,
    MinInlineSize,
    CSS_PROPERTY_GETCS_NEEDS_LAYOUT_FLUSH,
    "",
    VARIANT_AHKLP | VARIANT_CALC,
    kWidthKTable,
    Position,
    eStyleAnimType_None)
CSS_PROP_POSITION(
    min-width,
    min_width,
    MinWidth,
    0,
    "",
    VARIANT_AHKLP | VARIANT_CALC,
    kWidthKTable,
    eStyleAnimType_Coord)
CSS_PROP_EFFECTS(
    mix-blend-mode,
    mix_blend_mode,
    MixBlendMode,
    0,
    "layout.css.mix-blend-mode.enabled",
    VARIANT_HK,
    kBlendModeKTable,
    eStyleAnimType_Discrete)
CSS_PROP_POSITION(
    object-fit,
    object_fit,
    ObjectFit,
    0,
    "",
    VARIANT_HK,
    kObjectFitKTable,
    eStyleAnimType_Discrete)
CSS_PROP_POSITION(
    object-position,
    object_position,
    ObjectPosition,
    CSS_PROPERTY_PARSE_FUNCTION,
    "",
    VARIANT_CALC,
    kImageLayerPositionKTable,
    eStyleAnimType_Custom)
CSS_PROP_LOGICAL(
    offset-block-end,
    offset_block_end,
    OffsetBlockEnd,
    CSS_PROPERTY_GETCS_NEEDS_LAYOUT_FLUSH,
    "",
    VARIANT_AHLP | VARIANT_CALC,
    nullptr,
    Position,
    eStyleAnimType_None)
CSS_PROP_LOGICAL(
    offset-block-start,
    offset_block_start,
    OffsetBlockStart,
    CSS_PROPERTY_GETCS_NEEDS_LAYOUT_FLUSH,
    "",
    VARIANT_AHLP | VARIANT_CALC,
    nullptr,
    Position,
    eStyleAnimType_None)
CSS_PROP_LOGICAL(
    offset-inline-end,
    offset_inline_end,
    OffsetInlineEnd,
    CSS_PROPERTY_GETCS_NEEDS_LAYOUT_FLUSH,
    "",
    VARIANT_AHLP | VARIANT_CALC,
    nullptr,
    Position,
    eStyleAnimType_None)
CSS_PROP_LOGICAL(
    offset-inline-start,
    offset_inline_start,
    OffsetInlineStart,
    CSS_PROPERTY_GETCS_NEEDS_LAYOUT_FLUSH,
    "",
    VARIANT_AHLP | VARIANT_CALC,
    nullptr,
    Position,
    eStyleAnimType_None)
CSS_PROP_EFFECTS(
    opacity,
    opacity,
    Opacity,
    CSS_PROPERTY_CAN_ANIMATE_ON_COMPOSITOR,
    "",
    VARIANT_HN,
    nullptr,
    eStyleAnimType_float)
CSS_PROP_POSITION(
    order,
    order,
    Order,
    0,
    "",
    VARIANT_HI,
    nullptr,
    eStyleAnimType_Custom) // <integer>
CSS_PROP_DISPLAY(
    -moz-orient,
    _moz_orient,
    CSS_PROP_DOMPROP_PREFIXED(Orient),
    0,
    "",
    VARIANT_HK,
    kOrientKTable,
    eStyleAnimType_Discrete)
CSS_PROP_FONT(
    -moz-osx-font-smoothing,
    _moz_osx_font_smoothing,
    CSS_PROP_DOMPROP_PREFIXED(OsxFontSmoothing),
    0,
    "layout.css.osx-font-smoothing.enabled",
    VARIANT_HK,
    kFontSmoothingKTable,
    eStyleAnimType_Discrete)
CSS_PROP_SHORTHAND(
    outline,
    outline,
    Outline,
    CSS_PROPERTY_PARSE_FUNCTION,
    "")
CSS_PROP_OUTLINE(
    outline-color,
    outline_color,
    OutlineColor,
    0,
    "",
    VARIANT_HC,
    nullptr,
    eStyleAnimType_ComplexColor)
CSS_PROP_OUTLINE(
    outline-offset,
    outline_offset,
    OutlineOffset,
    0,
    "",
    VARIANT_HL | VARIANT_CALC,
    nullptr,
    eStyleAnimType_nscoord)
CSS_PROP_SHORTHAND(
    -moz-outline-radius,
    _moz_outline_radius,
    CSS_PROP_DOMPROP_PREFIXED(OutlineRadius),
    CSS_PROPERTY_PARSE_FUNCTION,
    "")
CSS_PROP_OUTLINE(
    -moz-outline-radius-bottomleft,
    _moz_outline_radius_bottomleft,
    CSS_PROP_DOMPROP_PREFIXED(OutlineRadiusBottomleft),
    CSS_PROPERTY_PARSE_FUNCTION,
    "",
    0,
    nullptr,
    eStyleAnimType_Corner_BottomLeft)
CSS_PROP_OUTLINE(
    -moz-outline-radius-bottomright,
    _moz_outline_radius_bottomright,
    CSS_PROP_DOMPROP_PREFIXED(OutlineRadiusBottomright),
    CSS_PROPERTY_PARSE_FUNCTION,
    "",
    0,
    nullptr,
    eStyleAnimType_Corner_BottomRight)
CSS_PROP_OUTLINE(
    -moz-outline-radius-topleft,
    _moz_outline_radius_topleft,
    CSS_PROP_DOMPROP_PREFIXED(OutlineRadiusTopleft),
    CSS_PROPERTY_PARSE_FUNCTION,
    "",
    0,
    nullptr,
    eStyleAnimType_Corner_TopLeft)
CSS_PROP_OUTLINE(
    -moz-outline-radius-topright,
    _moz_outline_radius_topright,
    CSS_PROP_DOMPROP_PREFIXED(OutlineRadiusTopright),
    CSS_PROPERTY_PARSE_FUNCTION,
    "",
    0,
    nullptr,
    eStyleAnimType_Corner_TopRight)
CSS_PROP_OUTLINE(
    outline-style,
    outline_style,
    OutlineStyle,
    0,
    "",
    VARIANT_HK,
    kOutlineStyleKTable,
    eStyleAnimType_Discrete)
CSS_PROP_OUTLINE(
    outline-width,
    outline_width,
    OutlineWidth,
    0,
    "",
    VARIANT_HKL | VARIANT_CALC,
    kBorderWidthKTable,
    eStyleAnimType_nscoord)
CSS_PROP_SHORTHAND(
    overflow,
    overflow,
    Overflow,
    CSS_PROPERTY_PARSE_FUNCTION,
    "")
CSS_PROP_SHORTHAND(
    overflow-clip-box,
    overflow_clip_box,
    OverflowClipBox,
    CSS_PROPERTY_PARSE_FUNCTION |
        CSS_PROPERTY_ENABLED_IN_UA_SHEETS,
    "layout.css.overflow-clip-box.enabled")
CSS_PROP_DISPLAY(
    overflow-clip-box-block,
    overflow_clip_box_block,
    OverflowClipBoxBlock,
    CSS_PROPERTY_ENABLED_IN_UA_SHEETS,
    "layout.css.overflow-clip-box.enabled",
    VARIANT_HK,
    kOverflowClipBoxKTable,
    eStyleAnimType_Discrete)
CSS_PROP_DISPLAY(
    overflow-clip-box-inline,
    overflow_clip_box_inline,
    OverflowClipBoxInline,
    CSS_PROPERTY_ENABLED_IN_UA_SHEETS,
    "layout.css.overflow-clip-box.enabled",
    VARIANT_HK,
    kOverflowClipBoxKTable,
    eStyleAnimType_Discrete)
CSS_PROP_DISPLAY(
    overflow-x,
    overflow_x,
    OverflowX,
    0,
    "",
    VARIANT_HK,
    kOverflowSubKTable,
    eStyleAnimType_Discrete)
CSS_PROP_DISPLAY(
    overflow-y,
    overflow_y,
    OverflowY,
    0,
    "",
    VARIANT_HK,
    kOverflowSubKTable,
    eStyleAnimType_Discrete)
CSS_PROP_SHORTHAND(
    padding,
    padding,
    Padding,
    CSS_PROPERTY_PARSE_FUNCTION,
    "")
CSS_PROP_LOGICAL(
    padding-block-end,
    padding_block_end,
    PaddingBlockEnd,
    CSS_PROPERTY_GETCS_NEEDS_LAYOUT_FLUSH,
    "",
    VARIANT_HLP | VARIANT_CALC,
    nullptr,
    Padding,
    eStyleAnimType_None)
CSS_PROP_LOGICAL(
    padding-block-start,
    padding_block_start,
    PaddingBlockStart,
    CSS_PROPERTY_GETCS_NEEDS_LAYOUT_FLUSH,
    "",
    VARIANT_HLP | VARIANT_CALC,
    nullptr,
    Padding,
    eStyleAnimType_None)
CSS_PROP_PADDING(
    padding-bottom,
    padding_bottom,
    PaddingBottom,
    CSS_PROPERTY_GETCS_NEEDS_LAYOUT_FLUSH,
    "",
    VARIANT_HLP | VARIANT_CALC,
    nullptr,
    eStyleAnimType_Sides_Bottom)
CSS_PROP_LOGICAL(
    padding-inline-end,
    padding_inline_end,
    PaddingInlineEnd,
    CSS_PROPERTY_GETCS_NEEDS_LAYOUT_FLUSH,
    "",
    VARIANT_HLP | VARIANT_CALC,
    nullptr,
    Padding,
    eStyleAnimType_None)
CSS_PROP_LOGICAL(
    padding-inline-start,
    padding_inline_start,
    PaddingInlineStart,
    CSS_PROPERTY_GETCS_NEEDS_LAYOUT_FLUSH,
    "",
    VARIANT_HLP | VARIANT_CALC,
    nullptr,
    Padding,
    eStyleAnimType_None)
CSS_PROP_PADDING(
    padding-left,
    padding_left,
    PaddingLeft,
    CSS_PROPERTY_GETCS_NEEDS_LAYOUT_FLUSH,
    "",
    VARIANT_HLP | VARIANT_CALC,
    nullptr,
    eStyleAnimType_Sides_Left)
CSS_PROP_PADDING(
    padding-right,
    padding_right,
    PaddingRight,
    CSS_PROPERTY_GETCS_NEEDS_LAYOUT_FLUSH,
    "",
    VARIANT_HLP | VARIANT_CALC,
    nullptr,
    eStyleAnimType_Sides_Right)
CSS_PROP_PADDING(
    padding-top,
    padding_top,
    PaddingTop,
    CSS_PROPERTY_GETCS_NEEDS_LAYOUT_FLUSH,
    "",
    VARIANT_HLP | VARIANT_CALC,
    nullptr,
    eStyleAnimType_Sides_Top)
CSS_PROP_DISPLAY(
    page-break-after,
    page_break_after,
    PageBreakAfter,
    0,
    "",
    VARIANT_HK,
    kPageBreakKTable,
    eStyleAnimType_Discrete) // temp fix for bug 24000
CSS_PROP_DISPLAY(
    page-break-before,
    page_break_before,
    PageBreakBefore,
    0,
    "",
    VARIANT_HK,
    kPageBreakKTable,
    eStyleAnimType_Discrete) // temp fix for bug 24000
CSS_PROP_DISPLAY(
    page-break-inside,
    page_break_inside,
    PageBreakInside,
    0,
    "",
    VARIANT_HK,
    kPageBreakInsideKTable,
    eStyleAnimType_Discrete)
CSS_PROP_SVG(
    paint-order,
    paint_order,
    PaintOrder,
    CSS_PROPERTY_PARSE_FUNCTION,
    "",
    0,
    nullptr,
    eStyleAnimType_Discrete)
CSS_PROP_DISPLAY(
    perspective,
    perspective,
    Perspective,
    0,
    "",
    VARIANT_NONE | VARIANT_INHERIT | VARIANT_LENGTH |
      VARIANT_NONNEGATIVE_DIMENSION,
    nullptr,
    eStyleAnimType_Coord)
CSS_PROP_DISPLAY(
    perspective-origin,
    perspective_origin,
    PerspectiveOrigin,
    CSS_PROPERTY_PARSE_FUNCTION |
        CSS_PROPERTY_GETCS_NEEDS_LAYOUT_FLUSH,
    "",
    VARIANT_CALC,
    kImageLayerPositionKTable,
    eStyleAnimType_Custom)
CSS_PROP_SHORTHAND(
    place-content,
    place_content,
    PlaceContent,
    CSS_PROPERTY_PARSE_FUNCTION,
    "")
CSS_PROP_SHORTHAND(
    place-items,
    place_items,
    PlaceItems,
    CSS_PROPERTY_PARSE_FUNCTION,
    "")
CSS_PROP_SHORTHAND(
    place-self,
    place_self,
    PlaceSelf,
    CSS_PROPERTY_PARSE_FUNCTION,
    "")
CSS_PROP_USERINTERFACE(
    pointer-events,
    pointer_events,
    PointerEvents,
    0,
    "",
    VARIANT_HK,
    kPointerEventsKTable,
    eStyleAnimType_Discrete)
CSS_PROP_DISPLAY(
    position,
    position,
    Position,
    0,
    "",
    VARIANT_HK,
    kPositionKTable,
    eStyleAnimType_Discrete)
CSS_PROP_LIST(
    quotes,
    quotes,
    Quotes,
    CSS_PROPERTY_PARSE_FUNCTION,
    "",
    VARIANT_HOS,
    nullptr,
    eStyleAnimType_Discrete)
CSS_PROP_DISPLAY(
    resize,
    resize,
    Resize,
    0,
    "",
    VARIANT_HK,
    kResizeKTable,
    eStyleAnimType_Discrete)
CSS_PROP_POSITION(
    right,
    right,
    Right,
    CSS_PROPERTY_GETCS_NEEDS_LAYOUT_FLUSH,
    "",
    VARIANT_AHLP | VARIANT_CALC,
    nullptr,
    eStyleAnimType_Sides_Right)
CSS_PROP_DISPLAY(
    rotate,
    rotate,
    Rotate,
    CSS_PROPERTY_PARSE_FUNCTION |
        CSS_PROPERTY_GETCS_NEEDS_LAYOUT_FLUSH,
    "layout.css.individual-transform.enabled",
    0,
    nullptr,
    eStyleAnimType_None)
CSS_PROP_TEXT(
    ruby-align,
    ruby_align,
    RubyAlign,
    0,
    "",
    VARIANT_HK,
    kRubyAlignKTable,
    eStyleAnimType_Discrete)
CSS_PROP_TEXT(
    ruby-position,
    ruby_position,
    RubyPosition,
    0,
    "",
    VARIANT_HK,
    kRubyPositionKTable,
    eStyleAnimType_Discrete)
#ifndef CSS_PROP_LIST_ONLY_COMPONENTS_OF_ALL_SHORTHAND
#ifndef CSS_PROP_LIST_EXCLUDE_INTERNAL
CSS_PROP_FONT(
    -moz-script-level,
    _moz_script_level,
    ScriptLevel,
    // We only allow 'script-level' when unsafe rules are enabled, because
    // otherwise it could interfere with rulenode optimizations if used in
    // a non-MathML-enabled document.
    CSS_PROPERTY_INTERNAL |
        CSS_PROPERTY_ENABLED_IN_UA_SHEETS,
    "",
    // script-level can take Auto, Integer and Number values, but only Auto
    // ("increment if parent is not in displaystyle") and Integer
    // ("relative") values can be specified in a style sheet.
    VARIANT_AHI,
    nullptr,
    eStyleAnimType_None)
CSS_PROP_FONT(
    -moz-script-min-size,
    _moz_script_min_size,
    ScriptMinSize,
    CSS_PROPERTY_INTERNAL |
        CSS_PROPERTY_PARSE_INACCESSIBLE,
    "",
    0,
    nullptr,
    eStyleAnimType_None)
CSS_PROP_FONT(
    -moz-script-size-multiplier,
    _moz_script_size_multiplier,
    ScriptSizeMultiplier,
    CSS_PROPERTY_INTERNAL |
        CSS_PROPERTY_PARSE_INACCESSIBLE,
    "",
    0,
    nullptr,
    eStyleAnimType_None)
#endif // CSS_PROP_LIST_EXCLUDE_INTERNAL
#endif // CSS_PROP_LIST_ONLY_COMPONENTS_OF_ALL_SHORTHAND
CSS_PROP_DISPLAY(
    scroll-behavior,
    scroll_behavior,
    ScrollBehavior,
    0,
    "layout.css.scroll-behavior.property-enabled",
    VARIANT_HK,
    kScrollBehaviorKTable,
    eStyleAnimType_Discrete)
CSS_PROP_SHORTHAND(
    overscroll-behavior,
    overscroll_behavior,
    OverscrollBehavior,
    CSS_PROPERTY_PARSE_FUNCTION,
    "layout.css.overscroll-behavior.enabled")
CSS_PROP_DISPLAY(
    overscroll-behavior-x,
    overscroll_behavior_x,
    OverscrollBehaviorX,
    0,
    "layout.css.overscroll-behavior.enabled",
    VARIANT_HK,
    kOverscrollBehaviorKTable,
    eStyleAnimType_Discrete)
CSS_PROP_DISPLAY(
    overscroll-behavior-y,
    overscroll_behavior_y,
    OverscrollBehaviorY,
    0,
    "layout.css.overscroll-behavior.enabled",
    VARIANT_HK,
    kOverscrollBehaviorKTable,
    eStyleAnimType_Discrete)
CSS_PROP_DISPLAY(
    scroll-snap-coordinate,
    scroll_snap_coordinate,
    ScrollSnapCoordinate,
    CSS_PROPERTY_VALUE_PARSER_FUNCTION |
        CSS_PROPERTY_VALUE_LIST_USES_COMMAS,
    "layout.css.scroll-snap.enabled",
    0,
    kImageLayerPositionKTable,
    eStyleAnimType_Discrete)
CSS_PROP_DISPLAY(
    scroll-snap-destination,
    scroll_snap_destination,
    ScrollSnapDestination,
    CSS_PROPERTY_VALUE_PARSER_FUNCTION,
    "layout.css.scroll-snap.enabled",
    0,
    kImageLayerPositionKTable,
    eStyleAnimType_Discrete)
CSS_PROP_DISPLAY(
    scroll-snap-points-x,
    scroll_snap_points_x,
    ScrollSnapPointsX,
    CSS_PROPERTY_VALUE_PARSER_FUNCTION,
    "layout.css.scroll-snap.enabled",
    0,
    nullptr,
    eStyleAnimType_Discrete)
CSS_PROP_DISPLAY(
    scroll-snap-points-y,
    scroll_snap_points_y,
    ScrollSnapPointsY,
    CSS_PROPERTY_VALUE_PARSER_FUNCTION,
    "layout.css.scroll-snap.enabled",
    0,
    nullptr,
    eStyleAnimType_Discrete)
CSS_PROP_SHORTHAND(
    scroll-snap-type,
    scroll_snap_type,
    ScrollSnapType,
    CSS_PROPERTY_PARSE_FUNCTION,
    "layout.css.scroll-snap.enabled")
CSS_PROP_DISPLAY(
    scroll-snap-type-x,
    scroll_snap_type_x,
    ScrollSnapTypeX,
    0,
    "layout.css.scroll-snap.enabled",
    VARIANT_HK,
    kScrollSnapTypeKTable,
    eStyleAnimType_Discrete)
CSS_PROP_DISPLAY(
    scroll-snap-type-y,
    scroll_snap_type_y,
    ScrollSnapTypeY,
    0,
    "layout.css.scroll-snap.enabled",
    VARIANT_HK,
    kScrollSnapTypeKTable,
    eStyleAnimType_Discrete)
CSS_PROP_DISPLAY(
    shape-image-threshold,
    shape_image_threshold,
    ShapeImageThreshold,
    0,
    "layout.css.shape-outside.enabled",
    VARIANT_HN,
    nullptr,
    eStyleAnimType_float)
CSS_PROP_DISPLAY(
    shape-outside,
    shape_outside,
    ShapeOutside,
    CSS_PROPERTY_VALUE_PARSER_FUNCTION,
    "layout.css.shape-outside.enabled",
    0,
    nullptr,
    eStyleAnimType_Custom)
CSS_PROP_SVG(
    shape-rendering,
    shape_rendering,
    ShapeRendering,
    0,
    "",
    VARIANT_HK,
    kShapeRenderingKTable,
    eStyleAnimType_Discrete)
#ifndef CSS_PROP_LIST_ONLY_COMPONENTS_OF_ALL_SHORTHAND
#ifndef CSS_PROP_LIST_EXCLUDE_INTERNAL
CSS_PROP_TABLE(
    -x-span,
    _x_span,
    Span,
    CSS_PROPERTY_INTERNAL |
        CSS_PROPERTY_PARSE_INACCESSIBLE,
    "",
    0,
    nullptr,
    eStyleAnimType_None)
#endif // CSS_PROP_LIST_EXCLUDE_INTERNAL
#endif // CSS_PROP_LIST_ONLY_COMPONENTS_OF_ALL_SHORTHAND
CSS_PROP_XUL(
    -moz-stack-sizing,
    _moz_stack_sizing,
    CSS_PROP_DOMPROP_PREFIXED(StackSizing),
    0,
    "",
    VARIANT_HK,
    kStackSizingKTable,
    eStyleAnimType_Discrete)
CSS_PROP_SVGRESET(
    stop-color,
    stop_color,
    StopColor,
    0,
    "",
    VARIANT_HC,
    nullptr,
    eStyleAnimType_Color)
CSS_PROP_SVGRESET(
    stop-opacity,
    stop_opacity,
    StopOpacity,
    0,
    "",
    VARIANT_HN,
    nullptr,
    eStyleAnimType_float)
CSS_PROP_SVG(
    stroke,
    stroke,
    Stroke,
    CSS_PROPERTY_PARSE_FUNCTION,
    "",
    0,
    kContextPatternKTable,
    eStyleAnimType_PaintServer)
CSS_PROP_SVG(
    stroke-dasharray,
    stroke_dasharray,
    StrokeDasharray,
    CSS_PROPERTY_PARSE_FUNCTION |
        CSS_PROPERTY_VALUE_LIST_USES_COMMAS,
        // NOTE: Internal values have range restrictions.
    "",
    0,
    kStrokeContextValueKTable,
    eStyleAnimType_Custom)
CSS_PROP_SVG(
    stroke-dashoffset,
    stroke_dashoffset,
    StrokeDashoffset,
    0,
    "",
    VARIANT_HLPN | VARIANT_OPENTYPE_SVG_KEYWORD,
    kStrokeContextValueKTable,
    eStyleAnimType_Coord)
CSS_PROP_SVG(
    stroke-linecap,
    stroke_linecap,
    StrokeLinecap,
    0,
    "",
    VARIANT_HK,
    kStrokeLinecapKTable,
    eStyleAnimType_Discrete)
CSS_PROP_SVG(
    stroke-linejoin,
    stroke_linejoin,
    StrokeLinejoin,
    0,
    "",
    VARIANT_HK,
    kStrokeLinejoinKTable,
    eStyleAnimType_Discrete)
CSS_PROP_SVG(
    stroke-miterlimit,
    stroke_miterlimit,
    StrokeMiterlimit,
    0,
    "",
    VARIANT_HN,
    nullptr,
    eStyleAnimType_float)
CSS_PROP_SVG(
    stroke-opacity,
    stroke_opacity,
    StrokeOpacity,
    0,
    "",
    VARIANT_HN | VARIANT_KEYWORD,
    kContextOpacityKTable,
    eStyleAnimType_float)
CSS_PROP_SVG(
    stroke-width,
    stroke_width,
    StrokeWidth,
    0,
    "",
    VARIANT_HLPN | VARIANT_OPENTYPE_SVG_KEYWORD,
    kStrokeContextValueKTable,
    eStyleAnimType_Coord)
#ifndef CSS_PROP_LIST_EXCLUDE_INTERNAL
CSS_PROP_FONT(
    -x-system-font,
    _x_system_font,
    CSS_PROP_DOMPROP_PREFIXED(SystemFont),
    CSS_PROPERTY_INTERNAL |
        CSS_PROPERTY_PARSE_INACCESSIBLE,
    "",
    0,
    kFontKTable,
    eStyleAnimType_None)
#endif // CSS_PROP_LIST_EXCLUDE_INTERNAL
CSS_PROP_TEXT(
    -moz-tab-size,
    _moz_tab_size,
    CSS_PROP_DOMPROP_PREFIXED(TabSize),
    0,
    "",
    VARIANT_INHERIT | VARIANT_LNCALC,
    nullptr,
    eStyleAnimType_Coord)
CSS_PROP_TABLE(
    table-layout,
    table_layout,
    TableLayout,
    0,
    "",
    VARIANT_HK,
    kTableLayoutKTable,
    eStyleAnimType_Discrete)
CSS_PROP_TEXT(
    text-align,
    text_align,
    TextAlign,
    CSS_PROPERTY_VALUE_PARSER_FUNCTION,
    "",
    // When we support aligning on a string, we can parse text-align
    // as a string....
    VARIANT_HK /* | VARIANT_STRING */,
    kTextAlignKTable,
    eStyleAnimType_Discrete)
CSS_PROP_TEXT(
    text-align-last,
    text_align_last,
    TextAlignLast,
    CSS_PROPERTY_VALUE_PARSER_FUNCTION,
    "",
    VARIANT_HK,
    kTextAlignLastKTable,
    eStyleAnimType_Discrete)
CSS_PROP_SVG(
    text-anchor,
    text_anchor,
    TextAnchor,
    0,
    "",
    VARIANT_HK,
    kTextAnchorKTable,
    eStyleAnimType_Discrete)
CSS_PROP_TEXT(
    text-combine-upright,
    text_combine_upright,
    TextCombineUpright,
    CSS_PROPERTY_VALUE_PARSER_FUNCTION,
    "layout.css.text-combine-upright.enabled",
    0,
    kTextCombineUprightKTable,
    eStyleAnimType_Discrete)
CSS_PROP_SHORTHAND(
    text-decoration,
    text_decoration,
    TextDecoration,
    CSS_PROPERTY_PARSE_FUNCTION,
    "")
CSS_PROP_TEXTRESET(
    text-decoration-color,
    text_decoration_color,
    TextDecorationColor,
    0,
    "",
    VARIANT_HC,
    nullptr,
    eStyleAnimType_ComplexColor)
CSS_PROP_TEXTRESET(
    text-decoration-line,
    text_decoration_line,
    TextDecorationLine,
    CSS_PROPERTY_VALUE_PARSER_FUNCTION,
    "",
    0,
    kTextDecorationLineKTable,
    eStyleAnimType_Discrete)
CSS_PROP_TEXTRESET(
    text-decoration-style,
    text_decoration_style,
    TextDecorationStyle,
    0,
    "",
    VARIANT_HK,
    kTextDecorationStyleKTable,
    eStyleAnimType_Discrete)
CSS_PROP_SHORTHAND(
    text-emphasis,
    text_emphasis,
    TextEmphasis,
    CSS_PROPERTY_PARSE_FUNCTION,
    "")
CSS_PROP_TEXT(
    text-emphasis-color,
    text_emphasis_color,
    TextEmphasisColor,
    0,
    "",
    VARIANT_HC,
    nullptr,
    eStyleAnimType_ComplexColor)
CSS_PROP_TEXT(
    text-emphasis-position,
    text_emphasis_position,
    TextEmphasisPosition,
    CSS_PROPERTY_VALUE_PARSER_FUNCTION,
    "",
    0,
    kTextEmphasisPositionKTable,
    eStyleAnimType_Discrete)
CSS_PROP_TEXT(
    text-emphasis-style,
    text_emphasis_style,
    TextEmphasisStyle,
    CSS_PROPERTY_VALUE_PARSER_FUNCTION,
    "",
    0,
    nullptr,
    eStyleAnimType_Discrete)
CSS_PROP_TEXT(
    -webkit-text-fill-color,
    _webkit_text_fill_color,
    WebkitTextFillColor,
    0,
    "layout.css.prefixes.webkit",
    VARIANT_HC,
    nullptr,
    eStyleAnimType_ComplexColor)
CSS_PROP_TEXT(
    text-indent,
    text_indent,
    TextIndent,
    0,
    "",
    VARIANT_HLP | VARIANT_CALC,
    nullptr,
    eStyleAnimType_Coord)
CSS_PROP_TEXT(
    text-justify,
    text_justify,
    TextJustify,
    0,
    "layout.css.text-justify.enabled",
    VARIANT_HK,
    kTextJustifyKTable,
    eStyleAnimType_Discrete)
CSS_PROP_VISIBILITY(
    text-orientation,
    text_orientation,
    TextOrientation,
    0,
    "",
    VARIANT_HK,
    kTextOrientationKTable,
    eStyleAnimType_Discrete)
CSS_PROP_TEXTRESET(
    text-overflow,
    text_overflow,
    TextOverflow,
    CSS_PROPERTY_VALUE_PARSER_FUNCTION,
    "",
    0,
    kTextOverflowKTable,
    eStyleAnimType_Discrete)
CSS_PROP_TEXT(
    text-rendering,
    text_rendering,
    TextRendering,
    0,
    "",
    VARIANT_HK,
    kTextRenderingKTable,
    eStyleAnimType_Discrete)
CSS_PROP_TEXT(
    text-shadow,
    text_shadow,
    TextShadow,
    CSS_PROPERTY_PARSE_FUNCTION |
        CSS_PROPERTY_VALUE_LIST_USES_COMMAS,
        // NOTE: some components must be nonnegative
    "",
    VARIANT_COLOR | VARIANT_LENGTH | VARIANT_CALC | VARIANT_INHERIT | VARIANT_NONE,
    nullptr,
    eStyleAnimType_Shadow)
CSS_PROP_TEXT(
    -moz-text-size-adjust,
    _moz_text_size_adjust,
    CSS_PROP_DOMPROP_PREFIXED(TextSizeAdjust),
    0,
    "",
    VARIANT_HK,
    kTextSizeAdjustKTable,
    eStyleAnimType_Discrete)
CSS_PROP_SHORTHAND(
    -webkit-text-stroke,
    _webkit_text_stroke,
    WebkitTextStroke,
    CSS_PROPERTY_PARSE_FUNCTION,
    "layout.css.prefixes.webkit")
CSS_PROP_TEXT(
    -webkit-text-stroke-color,
    _webkit_text_stroke_color,
    WebkitTextStrokeColor,
    0,
    "layout.css.prefixes.webkit",
    VARIANT_HC,
    nullptr,
    eStyleAnimType_ComplexColor)
CSS_PROP_TEXT(
    -webkit-text-stroke-width,
    _webkit_text_stroke_width,
    WebkitTextStrokeWidth,
    0,
    "layout.css.prefixes.webkit",
    VARIANT_HKL | VARIANT_CALC,
    kBorderWidthKTable,
    eStyleAnimType_Discrete)
CSS_PROP_DISPLAY(
    scale,
    scale,
    Scale,
    CSS_PROPERTY_PARSE_FUNCTION |
        CSS_PROPERTY_GETCS_NEEDS_LAYOUT_FLUSH,
    "layout.css.individual-transform.enabled",
    0,
    nullptr,
    eStyleAnimType_None)
CSS_PROP_TEXT(
    text-transform,
    text_transform,
    TextTransform,
    0,
    "",
    VARIANT_HK,
    kTextTransformKTable,
    eStyleAnimType_Discrete)
#ifndef CSS_PROP_LIST_ONLY_COMPONENTS_OF_ALL_SHORTHAND
#ifndef CSS_PROP_LIST_EXCLUDE_INTERNAL
CSS_PROP_FONT(
    -x-text-zoom,
    _x_text_zoom,
    TextZoom,
    CSS_PROPERTY_INTERNAL |
        CSS_PROPERTY_PARSE_INACCESSIBLE,
    "",
    0,
    nullptr,
    eStyleAnimType_None)
#endif // CSS_PROP_LIST_EXCLUDE_INTERNAL
#endif // CSS_PROP_LIST_ONLY_COMPONENTS_OF_ALL_SHORTHAND
CSS_PROP_POSITION(
    top,
    top,
    Top,
    CSS_PROPERTY_GETCS_NEEDS_LAYOUT_FLUSH,
    "",
    VARIANT_AHLP | VARIANT_CALC,
    nullptr,
    eStyleAnimType_Sides_Top)
#ifndef CSS_PROP_LIST_EXCLUDE_INTERNAL
CSS_PROP_DISPLAY(
    -moz-top-layer,
    _moz_top_layer,
    CSS_PROP_DOMPROP_PREFIXED(TopLayer),
    CSS_PROPERTY_INTERNAL |
        CSS_PROPERTY_ENABLED_IN_UA_SHEETS,
    "",
    VARIANT_HK,
    kTopLayerKTable,
    eStyleAnimType_None)
#endif // CSS_PROP_LIST_EXCLUDE_INTERNAL
CSS_PROP_DISPLAY(
    touch-action,
    touch_action,
    TouchAction,
    CSS_PROPERTY_VALUE_PARSER_FUNCTION,
    "layout.css.touch_action.enabled",
    VARIANT_HK,
    kTouchActionKTable,
    eStyleAnimType_Discrete)
CSS_PROP_DISPLAY(
    transform,
    transform,
    Transform,
    CSS_PROPERTY_PARSE_FUNCTION |
        CSS_PROPERTY_GETCS_NEEDS_LAYOUT_FLUSH |
        CSS_PROPERTY_CAN_ANIMATE_ON_COMPOSITOR,
    "",
    0,
    nullptr,
    eStyleAnimType_Custom)
CSS_PROP_DISPLAY(
    transform-box,
    transform_box,
    TransformBox,
    0,
    "svg.transform-box.enabled",
    VARIANT_HK,
    kTransformBoxKTable,
    eStyleAnimType_Discrete)
CSS_PROP_DISPLAY(
    transform-origin,
    transform_origin,
    TransformOrigin,
    CSS_PROPERTY_PARSE_FUNCTION |
        CSS_PROPERTY_GETCS_NEEDS_LAYOUT_FLUSH,
    "",
    0,
    kImageLayerPositionKTable,
    eStyleAnimType_Custom)
CSS_PROP_DISPLAY(
    transform-style,
    transform_style,
    TransformStyle,
    0,
    "",
    VARIANT_HK,
    kTransformStyleKTable,
    eStyleAnimType_Discrete)
CSS_PROP_SHORTHAND(
    transition,
    transition,
    Transition,
    CSS_PROPERTY_PARSE_FUNCTION,
    "")
CSS_PROP_DISPLAY(
    transition-delay,
    transition_delay,
    TransitionDelay,
        CSS_PROPERTY_VALUE_LIST_USES_COMMAS,
    "",
    VARIANT_TIME, // used by list parsing
    nullptr,
    eStyleAnimType_None)
CSS_PROP_DISPLAY(
    transition-duration,
    transition_duration,
    TransitionDuration,
    CSS_PROPERTY_VALUE_LIST_USES_COMMAS,
    "",
    VARIANT_TIME | VARIANT_NONNEGATIVE_DIMENSION, // used by list parsing
    nullptr,
    eStyleAnimType_None)
CSS_PROP_DISPLAY(
    transition-property,
    transition_property,
    TransitionProperty,
    CSS_PROPERTY_PARSE_FUNCTION |
        CSS_PROPERTY_VALUE_LIST_USES_COMMAS,
    "",
    VARIANT_IDENTIFIER | VARIANT_NONE | VARIANT_ALL, // used only in shorthand
    nullptr,
    eStyleAnimType_None)
CSS_PROP_DISPLAY(
    transition-timing-function,
    transition_timing_function,
    TransitionTimingFunction,
    CSS_PROPERTY_VALUE_LIST_USES_COMMAS,
    "",
    VARIANT_KEYWORD | VARIANT_TIMING_FUNCTION, // used by list parsing
    kTransitionTimingFunctionKTable,
    eStyleAnimType_None)
CSS_PROP_DISPLAY(
    translate,
    translate,
    Translate,
    CSS_PROPERTY_PARSE_FUNCTION |
        CSS_PROPERTY_GETCS_NEEDS_LAYOUT_FLUSH,
    "layout.css.individual-transform.enabled",
    0,
    nullptr,
    eStyleAnimType_None)
#ifndef CSS_PROP_LIST_ONLY_COMPONENTS_OF_ALL_SHORTHAND
CSS_PROP_TEXTRESET(
    unicode-bidi,
    unicode_bidi,
    UnicodeBidi,
    0,
    "",
    VARIANT_HK,
    kUnicodeBidiKTable,
    eStyleAnimType_Discrete)
#endif // CSS_PROP_LIST_ONLY_COMPONENTS_OF_ALL_SHORTHAND
CSS_PROP_USERINTERFACE(
    -moz-user-focus,
    _moz_user_focus,
    CSS_PROP_DOMPROP_PREFIXED(UserFocus),
    0,
    "",
    VARIANT_HK,
    kUserFocusKTable,
    eStyleAnimType_Discrete) // XXX bug 3935
CSS_PROP_USERINTERFACE(
    -moz-user-input,
    _moz_user_input,
    CSS_PROP_DOMPROP_PREFIXED(UserInput),
    0,
    "",
    VARIANT_HK,
    kUserInputKTable,
    eStyleAnimType_Discrete) // XXX ??? // XXX bug 3935
CSS_PROP_USERINTERFACE(
    -moz-user-modify,
    _moz_user_modify,
    CSS_PROP_DOMPROP_PREFIXED(UserModify),
    0,
    "",
    VARIANT_HK,
    kUserModifyKTable,
    eStyleAnimType_Discrete) // XXX bug 3935
CSS_PROP_UIRESET(
    -moz-user-select,
    _moz_user_select,
    CSS_PROP_DOMPROP_PREFIXED(UserSelect),
    0,
    "",
    VARIANT_HK,
    kUserSelectKTable,
    eStyleAnimType_Discrete) // XXX bug 3935
CSS_PROP_SVGRESET(
    vector-effect,
    vector_effect,
    VectorEffect,
    0,
    "",
    VARIANT_HK,
    kVectorEffectKTable,
    eStyleAnimType_Discrete)
// NOTE: vertical-align is only supposed to apply to :first-letter when
// 'float' is 'none', but we don't worry about that since it has no
// effect otherwise
CSS_PROP_DISPLAY(
    vertical-align,
    vertical_align,
    VerticalAlign,
    0,
    "",
    VARIANT_HKLP | VARIANT_CALC,
    kVerticalAlignKTable,
    eStyleAnimType_Coord)
CSS_PROP_VISIBILITY(
    visibility,
    visibility,
    Visibility,
    0,
    "",
    VARIANT_HK,
    kVisibilityKTable,
    eStyleAnimType_Discrete)  // reflow for collapse
CSS_PROP_TEXT(
    white-space,
    white_space,
    WhiteSpace,
    0,
    "",
    VARIANT_HK,
    kWhitespaceKTable,
    eStyleAnimType_Discrete)
CSS_PROP_POSITION(
    width,
    width,
    Width,
    CSS_PROPERTY_GETCS_NEEDS_LAYOUT_FLUSH,
    "",
    VARIANT_AHKLP | VARIANT_CALC,
    kWidthKTable,
    eStyleAnimType_Coord)
CSS_PROP_DISPLAY(
    will-change,
    will_change,
    WillChange,
    CSS_PROPERTY_PARSE_FUNCTION |
        CSS_PROPERTY_VALUE_LIST_USES_COMMAS,
    "",
    0,
    nullptr,
    eStyleAnimType_Discrete)
CSS_PROP_UIRESET(
    -moz-window-dragging,
    _moz_window_dragging,
    CSS_PROP_DOMPROP_PREFIXED(WindowDragging),
    0,
    "",
    VARIANT_HK,
    kWindowDraggingKTable,
    eStyleAnimType_Discrete)
#ifndef CSS_PROP_LIST_EXCLUDE_INTERNAL
CSS_PROP_UIRESET(
    -moz-window-shadow,
    _moz_window_shadow,
    CSS_PROP_DOMPROP_PREFIXED(WindowShadow),
    CSS_PROPERTY_INTERNAL |
        CSS_PROPERTY_ENABLED_IN_UA_SHEETS_AND_CHROME,
    "",
    VARIANT_HK,
    kWindowShadowKTable,
    eStyleAnimType_None)
CSS_PROP_UIRESET(
    -moz-window-opacity,
    _moz_window_opacity,
    CSS_PROP_DOMPROP_PREFIXED(WindowOpacity),
    CSS_PROPERTY_INTERNAL | 0,
    "",
    VARIANT_HN,
    nullptr,
    eStyleAnimType_float)
CSS_PROP_UIRESET(
    -moz-window-transform,
    _moz_window_transform,
    CSS_PROP_DOMPROP_PREFIXED(WindowTransform),
    CSS_PROPERTY_INTERNAL |
        CSS_PROPERTY_PARSE_FUNCTION |
        CSS_PROPERTY_GETCS_NEEDS_LAYOUT_FLUSH,
    "",
    0,
    nullptr,
    eStyleAnimType_Custom)
CSS_PROP_UIRESET(
    -moz-window-transform-origin,
    _moz_window_transform_origin,
    CSS_PROP_DOMPROP_PREFIXED(WindowTransformOrigin),
    CSS_PROPERTY_INTERNAL |
        CSS_PROPERTY_PARSE_FUNCTION |
        CSS_PROPERTY_GETCS_NEEDS_LAYOUT_FLUSH,
    "",
    0,
    kImageLayerPositionKTable,
    eStyleAnimType_Custom)
#endif // CSS_PROP_LIST_EXCLUDE_INTERNAL
CSS_PROP_TEXT(
    word-break,
    word_break,
    WordBreak,
    0,
    "",
    VARIANT_HK,
    kWordBreakKTable,
    eStyleAnimType_Discrete)
CSS_PROP_TEXT(
    word-spacing,
    word_spacing,
    WordSpacing,
    0,
    "",
    VARIANT_HLP | VARIANT_NORMAL | VARIANT_CALC,
    nullptr,
    eStyleAnimType_Coord)
CSS_PROP_TEXT(
    overflow-wrap,
    overflow_wrap,
    OverflowWrap,
    0,
    "",
    VARIANT_HK,
    kOverflowWrapKTable,
    eStyleAnimType_Discrete)
CSS_PROP_VISIBILITY(
    writing-mode,
    writing_mode,
    WritingMode,
    0,
    "",
    VARIANT_HK,
    kWritingModeKTable,
    eStyleAnimType_Discrete)
CSS_PROP_POSITION(
    z-index,
    z_index,
    ZIndex,
    0,
    "",
    VARIANT_AHI,
    nullptr,
    eStyleAnimType_Coord)

#ifdef USED_CSS_PROP

#undef USED_CSS_PROP
#undef CSS_PROP_FONT
#undef CSS_PROP_COLOR
#undef CSS_PROP_BACKGROUND
#undef CSS_PROP_LIST
#undef CSS_PROP_POSITION
#undef CSS_PROP_TEXT
#undef CSS_PROP_TEXTRESET
#undef CSS_PROP_DISPLAY
#undef CSS_PROP_VISIBILITY
#undef CSS_PROP_CONTENT
#undef CSS_PROP_USERINTERFACE
#undef CSS_PROP_UIRESET
#undef CSS_PROP_TABLE
#undef CSS_PROP_TABLEBORDER
#undef CSS_PROP_MARGIN
#undef CSS_PROP_PADDING
#undef CSS_PROP_BORDER
#undef CSS_PROP_OUTLINE
#undef CSS_PROP_XUL
#undef CSS_PROP_COLUMN
#undef CSS_PROP_SVG
#undef CSS_PROP_SVGRESET
#undef CSS_PROP_VARIABLES
#undef CSS_PROP_EFFECTS

#else /* !defined(USED_CSS_PROP) */

#ifdef DEFINED_CSS_PROP_FONT
#undef CSS_PROP_FONT
#undef DEFINED_CSS_PROP_FONT
#endif
#ifdef DEFINED_CSS_PROP_COLOR
#undef CSS_PROP_COLOR
#undef DEFINED_CSS_PROP_COLOR
#endif
#ifdef DEFINED_CSS_PROP_BACKGROUND
#undef CSS_PROP_BACKGROUND
#undef DEFINED_CSS_PROP_BACKGROUND
#endif
#ifdef DEFINED_CSS_PROP_LIST
#undef CSS_PROP_LIST
#undef DEFINED_CSS_PROP_LIST
#endif
#ifdef DEFINED_CSS_PROP_POSITION
#undef CSS_PROP_POSITION
#undef DEFINED_CSS_PROP_POSITION
#endif
#ifdef DEFINED_CSS_PROP_TEXT
#undef CSS_PROP_TEXT
#undef DEFINED_CSS_PROP_TETEXTRESETT
#endif
#ifdef DEFINED_CSS_PROP_TEXTRESET
#undef CSS_PROP_TEXTRESET
#undef DEFINED_CSS_PROP_TEDISPLAYTRESET
#endif
#ifdef DEFINED_CSS_PROP_DISPLAY
#undef CSS_PROP_DISPLAY
#undef DEFINED_CSS_PROP_DISPLAY
#endif
#ifdef DEFINED_CSS_PROP_VISIBILITY
#undef CSS_PROP_VISIBILITY
#undef DEFINED_CSS_PROP_VISIBILITY
#endif
#ifdef DEFINED_CSS_PROP_CONTENT
#undef CSS_PROP_CONTENT
#undef DEFINED_CSS_PROP_CONTENT
#endif
#ifdef DEFINED_CSS_PROP_USERINTERFACE
#undef CSS_PROP_USERINTERFACE
#undef DEFINED_CSS_PROP_USERINTERFACE
#endif
#ifdef DEFINED_CSS_PROP_UIRESET
#undef CSS_PROP_UIRESET
#undef DEFINED_CSS_PROP_UIRESET
#endif
#ifdef DEFINED_CSS_PROP_TABLE
#undef CSS_PROP_TABLE
#undef DEFINED_CSS_PROP_TABLE
#endif
#ifdef DEFINED_CSS_PROP_TABLEBORDER
#undef CSS_PROP_TABLEBORDER
#undef DEFINED_CSS_PROP_TABLEBORDER
#endif
#ifdef DEFINED_CSS_PROP_MARGIN
#undef CSS_PROP_MARGIN
#undef DEFINED_CSS_PROP_MARGIN
#endif
#ifdef DEFINED_CSS_PROP_PADDING
#undef CSS_PROP_PADDING
#undef DEFINED_CSS_PROP_PADDING
#endif
#ifdef DEFINED_CSS_PROP_BORDER
#undef CSS_PROP_BORDER
#undef DEFINED_CSS_PROP_BORDER
#endif
#ifdef DEFINED_CSS_PROP_OUTLINE
#undef CSS_PROP_OUTLINE
#undef DEFINED_CSS_PROP_OUTLINE
#endif
#ifdef DEFINED_CSS_PROP_XUL
#undef CSS_PROP_XUL
#undef DEFINED_CSS_PROP_XUL
#endif
#ifdef DEFINED_CSS_PROP_COLUMN
#undef CSS_PROP_COLUMN
#undef DEFINED_CSS_PROP_COLUMN
#endif
#ifdef DEFINED_CSS_PROP_SVG
#undef CSS_PROP_SVG
#undef DEFINED_CSS_PROP_SVG
#endif
#ifdef DEFINED_CSS_PROP_SVGRESET
#undef CSS_PROP_SVGRESET
#undef DEFINED_CSS_PROP_SVGRESET
#endif
#ifdef DEFINED_CSS_PROP_VARIABLES
#undef CSS_PROP_VARIABLES
#undef DEFINED_CSS_PROP_VARIABLES
#endif
#ifdef DEFINED_CSS_PROP_EFFECTS
#undef CSS_PROP_EFFECTS
#undef DEFINED_CSS_PROP_EFFECTS
#endif

#endif /* !defined(USED_CSS_PROP) */

#ifdef DEFINED_CSS_PROP_SHORTHAND
#undef CSS_PROP_SHORTHAND
#undef DEFINED_CSS_PROP_SHORTHAND
#endif
#ifdef DEFINED_CSS_PROP_LOGICAL
#undef CSS_PROP_LOGICAL
#undef DEFINED_CSS_PROP_LOGICAL
#endif

#undef CSS_PROP_DOMPROP_PREFIXED
