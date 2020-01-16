/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Utilities for animation of computed style values */

#ifndef mozilla_StyleAnimationValue_h_
#define mozilla_StyleAnimationValue_h_

#include "mozilla/gfx/MatrixFwd.h"
#include "mozilla/gfx/Point.h"
#include "mozilla/RefPtr.h"
#include "mozilla/ServoBindingTypes.h"
#include "mozilla/ServoStyleConsts.h"  // Servo_AnimationValue_Dump
#include "mozilla/DbgMacro.h"
#include "nsStringFwd.h"
#include "nsStringBuffer.h"
#include "nsCoord.h"
#include "nsColor.h"
#include "nsCSSPropertyID.h"
#include "nsCSSValue.h"
#include "nsStyleConsts.h"
#include "nsStyleTransformMatrix.h"

class nsIFrame;
class gfx3DMatrix;

namespace mozilla {

class ComputedStyle;

namespace css {
class StyleRule;
}  // namespace css

namespace dom {
class Element;
}  // namespace dom

namespace layers {
class Animatable;
}  // namespace layers

enum class PseudoStyleType : uint8_t;
struct PropertyStyleAnimationValuePair;

struct AnimationValue {
  explicit AnimationValue(const RefPtr<RawServoAnimationValue>& aValue)
      : mServo(aValue) {}
  AnimationValue() = default;

  AnimationValue(const AnimationValue& aOther) : mServo(aOther.mServo) {}
  AnimationValue(AnimationValue&& aOther) : mServo(std::move(aOther.mServo)) {}

  AnimationValue& operator=(const AnimationValue& aOther) {
    if (this != &aOther) {
      mServo = aOther.mServo;
    }
    return *this;
  }
  AnimationValue& operator=(AnimationValue&& aOther) {
    MOZ_ASSERT(this != &aOther, "Do not move itself");
    if (this != &aOther) {
      mServo = std::move(aOther.mServo);
    }
    return *this;
  }

  bool operator==(const AnimationValue& aOther) const;
  bool operator!=(const AnimationValue& aOther) const;

  bool IsNull() const { return !mServo; }

  float GetOpacity() const;

  // Returns nscolor value in this AnimationValue.
  // Currently only background-color is supported.
  nscolor GetColor(nscolor aForegroundColor) const;

  // Return a transform list for the transform property.
  const mozilla::StyleTransform& GetTransformProperty() const;
  const mozilla::StyleScale& GetScaleProperty() const;
  const mozilla::StyleTranslate& GetTranslateProperty() const;
  const mozilla::StyleRotate& GetRotateProperty() const;

  // Motion path properties.
  const mozilla::StyleOffsetPath& GetOffsetPathProperty() const;
  const mozilla::LengthPercentage& GetOffsetDistanceProperty() const;
  const mozilla::StyleOffsetRotate& GetOffsetRotateProperty() const;
  const mozilla::StylePositionOrAuto& GetOffsetAnchorProperty() const;

  // Return the scale for mServo, which is calculated with reference to aFrame.
  mozilla::gfx::Size GetScaleValue(const nsIFrame* aFrame) const;

  // Uncompute this AnimationValue and then serialize it.
  void SerializeSpecifiedValue(nsCSSPropertyID aProperty,
                               const RawServoStyleSet* aRawSet,
                               nsAString& aString) const;

  // Check if |*this| and |aToValue| can be interpolated.
  bool IsInterpolableWith(nsCSSPropertyID aProperty,
                          const AnimationValue& aToValue) const;

  // Compute the distance between *this and aOther.
  // If |aComputedStyle| is nullptr, we will return 0.0 if we have mismatched
  // transform lists.
  double ComputeDistance(nsCSSPropertyID aProperty,
                         const AnimationValue& aOther,
                         ComputedStyle* aComputedStyle) const;

  // Create an AnimaitonValue from a string. This method flushes style, so we
  // should use this carefully. Now, it is only used by
  // nsDOMWindowUtils::ComputeAnimationDistance.
  static AnimationValue FromString(nsCSSPropertyID aProperty,
                                   const nsAString& aValue,
                                   dom::Element* aElement);

  // Create an already_AddRefed<RawServoAnimationValue> from a
  // layers::Animatable. Basically, this function should return AnimationValue,
  // but it seems the caller, AnimationHelper, only needs
  // RawServoAnimationValue, so we return its already_AddRefed<> to avoid
  // adding/removing a redundant ref-count.
  static already_AddRefed<RawServoAnimationValue> FromAnimatable(
      nsCSSPropertyID aProperty, const layers::Animatable& aAnimatable);

  RefPtr<RawServoAnimationValue> mServo;
};

inline std::ostream& operator<<(std::ostream& aOut,
                                const AnimationValue& aValue) {
  MOZ_ASSERT(aValue.mServo);
  nsString s;
  Servo_AnimationValue_Dump(aValue.mServo, &s);
  return aOut << s;
}

struct PropertyStyleAnimationValuePair {
  nsCSSPropertyID mProperty;
  AnimationValue mValue;
};
}  // namespace mozilla

#endif
