/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* some utilities for stylo */

#ifndef mozilla_ServoUtils_h
#define mozilla_ServoUtils_h

#include "mozilla/Assertions.h"
#include "mozilla/TypeTraits.h"
#include "MainThreadUtils.h"

namespace mozilla {

// Defined in ServoBindings.cpp.
void AssertIsMainThreadOrServoFontMetricsLocked();

class ServoStyleSet;
extern ServoStyleSet* sInServoTraversal;
inline bool IsInServoTraversal()
{
  // The callers of this function are generally main-thread-only _except_
  // for potentially running during the Servo traversal, in which case they may
  // take special paths that avoid writing to caches and the like. In order
  // to allow those callers to branch efficiently without checking TLS, we
  // maintain this static boolean. However, the danger is that those callers
  // are generally unprepared to deal with non-Servo-but-also-non-main-thread
  // callers, and are likely to take the main-thread codepath if this function
  // returns false. So we assert against other non-main-thread callers here.
  MOZ_ASSERT(sInServoTraversal || NS_IsMainThread());
  return sInServoTraversal;
}
} // namespace mozilla

#if defined(MOZ_STYLO) && defined(MOZ_OLD_STYLE)
# define MOZ_DECL_STYLO_CHECK_METHODS \
  bool IsGecko() const { return !IsServo(); } \
  bool IsServo() const { return mType == StyleBackendType::Servo; }
#elif defined(MOZ_STYLO)
# define MOZ_DECL_STYLO_CHECK_METHODS \
  bool IsGecko() const { return false; } \
  bool IsServo() const { return true; }
#else
# define MOZ_DECL_STYLO_CHECK_METHODS \
  bool IsGecko() const { return true; } \
  bool IsServo() const { return false; }
#endif

#define MOZ_DECL_STYLO_CONVERT_METHODS_SERVO(servotype_) \
  inline servotype_* AsServo();                         \
  inline const servotype_* AsServo() const;             \
  inline servotype_* GetAsServo();                      \
  inline const servotype_* GetAsServo() const;

#define MOZ_DECL_STYLO_CONVERT_METHODS_GECKO(geckotype_) \
  inline geckotype_* AsGecko();                         \
  inline const geckotype_* AsGecko() const;             \
  inline geckotype_* GetAsGecko();                      \
  inline const geckotype_* GetAsGecko() const;

#ifdef MOZ_OLD_STYLE
#define MOZ_DECL_STYLO_CONVERT_METHODS(geckotype_, servotype_) \
  MOZ_DECL_STYLO_CONVERT_METHODS_SERVO(servotype_) \
  MOZ_DECL_STYLO_CONVERT_METHODS_GECKO(geckotype_)
#else
#define MOZ_DECL_STYLO_CONVERT_METHODS(geckotype_, servotype_) \
  MOZ_DECL_STYLO_CONVERT_METHODS_SERVO(servotype_)
#endif

/**
 * Macro used in a base class of |geckotype_| and |servotype_|.
 * The class should define |StyleBackendType mType;| itself.
 */
#define MOZ_DECL_STYLO_METHODS(geckotype_, servotype_)  \
  MOZ_DECL_STYLO_CHECK_METHODS                          \
  MOZ_DECL_STYLO_CONVERT_METHODS(geckotype_, servotype_)

#define MOZ_DEFINE_STYLO_METHODS_GECKO(type_, geckotype_) \
  geckotype_* type_::AsGecko() {                                \
    MOZ_ASSERT(IsGecko());                                      \
    return static_cast<geckotype_*>(this);                      \
  }                                                             \
  const geckotype_* type_::AsGecko() const {                    \
    MOZ_ASSERT(IsGecko());                                      \
    return static_cast<const geckotype_*>(this);                \
  }                                                             \
  geckotype_* type_::GetAsGecko() {                             \
    return IsGecko() ? AsGecko() : nullptr;                     \
  }                                                             \
  const geckotype_* type_::GetAsGecko() const {                 \
    return IsGecko() ? AsGecko() : nullptr;                     \
  }

#define MOZ_DEFINE_STYLO_METHODS_SERVO(type_, servotype_) \
  servotype_* type_::AsServo() {                                \
    MOZ_ASSERT(IsServo());                                      \
    return static_cast<servotype_*>(this);                      \
  }                                                             \
  const servotype_* type_::AsServo() const {                    \
    MOZ_ASSERT(IsServo());                                      \
    return static_cast<const servotype_*>(this);                \
  }                                                             \
  servotype_* type_::GetAsServo() {                             \
    return IsServo() ? AsServo() : nullptr;                     \
  }                                                             \
  const servotype_* type_::GetAsServo() const {                 \
    return IsServo() ? AsServo() : nullptr;                     \
  }


/**
 * Macro used in inline header of class |type_| with its Gecko and Servo
 * subclasses named |geckotype_| and |servotype_| correspondingly for
 * implementing the inline methods defined by MOZ_DECL_STYLO_METHODS.
 */
#ifdef MOZ_OLD_STYLE
#define MOZ_DEFINE_STYLO_METHODS(type_, geckotype_, servotype_) \
  MOZ_DEFINE_STYLO_METHODS_SERVO(type_, servotype_) \
  MOZ_DEFINE_STYLO_METHODS_GECKO(type_, geckotype_)
#else
#define MOZ_DEFINE_STYLO_METHODS(type_, geckotype_, servotype_) \
  MOZ_DEFINE_STYLO_METHODS_SERVO(type_, servotype_)
#endif

#define MOZ_STYLO_THIS_TYPE  mozilla::RemovePointer<decltype(this)>::Type
#define MOZ_STYLO_GECKO_TYPE mozilla::RemovePointer<decltype(AsGecko())>::Type
#define MOZ_STYLO_SERVO_TYPE mozilla::RemovePointer<decltype(AsServo())>::Type

/**
 * Macro used to forward a method call to the concrete method defined by
 * the Servo or Gecko implementation. The class of the method using it
 * should use MOZ_DECL_STYLO_METHODS to define basic stylo methods.
 */
#ifdef MOZ_OLD_STYLE
#define MOZ_STYLO_FORWARD_CONCRETE(method_, geckoargs_, servoargs_)         \
  static_assert(!mozilla::IsSame<decltype(&MOZ_STYLO_THIS_TYPE::method_),   \
                                 decltype(&MOZ_STYLO_GECKO_TYPE::method_)>  \
                ::value, "Gecko subclass should define its own " #method_); \
  static_assert(!mozilla::IsSame<decltype(&MOZ_STYLO_THIS_TYPE::method_),   \
                                 decltype(&MOZ_STYLO_SERVO_TYPE::method_)>  \
                ::value, "Servo subclass should define its own " #method_); \
  if (IsServo()) {                                                          \
    return AsServo()->method_ servoargs_;                                   \
  }                                                                         \
  return AsGecko()->method_ geckoargs_;
#else
#define MOZ_STYLO_FORWARD_CONCRETE(method_, geckoargs_, servoargs_)         \
  return AsServo()->method_ servoargs_;
#endif

#define MOZ_STYLO_FORWARD(method_, args_) \
  MOZ_STYLO_FORWARD_CONCRETE(method_, args_, args_)

#endif // mozilla_ServoUtils_h
