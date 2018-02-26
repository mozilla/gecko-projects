/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * Inlined methods for nsStyleContext. Will just redirect to
 * GeckoStyleContext methods when compiled without stylo, but will do
 * virtual dispatch (by checking which kind of container it is)
 * in stylo mode.
 */

#ifndef nsStyleContextInlines_h
#define nsStyleContextInlines_h

#include "nsStyleContext.h"
#include "mozilla/ServoStyleContext.h"
#ifdef MOZ_OLD_STYLE
#include "mozilla/GeckoStyleContext.h"
#endif
#include "mozilla/ServoUtils.h"
#include "mozilla/ServoBindings.h"

MOZ_DEFINE_STYLO_METHODS(nsStyleContext,
                         mozilla::GeckoStyleContext,
                         mozilla::ServoStyleContext);

#ifdef MOZ_OLD_STYLE
nsRuleNode*
nsStyleContext::RuleNode()
{
  MOZ_RELEASE_ASSERT(IsGecko());
  return AsGecko()->RuleNode();
}
#endif

const ServoComputedData*
nsStyleContext::ComputedData()
{
    MOZ_RELEASE_ASSERT(IsServo());
    return AsServo()->ComputedData();
}

void
nsStyleContext::AddRef()
{
  MOZ_STYLO_FORWARD(AddRef, ())
}

void
nsStyleContext::Release()
{
  MOZ_STYLO_FORWARD(Release, ())
}

#define STYLE_STRUCT(name_, checkdata_cb_)                      \
const nsStyle##name_ *                                          \
nsStyleContext::Style##name_() {                                \
  return DoGetStyle##name_<true>();                             \
}                                                               \
const nsStyle##name_ *                                          \
nsStyleContext::ThreadsafeStyle##name_() {                      \
  if (mozilla::ServoStyleSet::IsInServoTraversal()) {           \
    return AsServo()->ComputedData()->GetStyle##name_();        \
  }                                                             \
  return Style##name_();                                        \
}                                                               \
const nsStyle##name_ * nsStyleContext::PeekStyle##name_() {     \
  return DoGetStyle##name_<false>();                            \
}
#include "nsStyleStructList.h"
#undef STYLE_STRUCT

#ifdef MOZ_OLD_STYLE
#define DO_GET_STYLE_INHERITED_GECKO(name_, checkdata_cb_)          \
  {                                                                 \
    auto gecko = AsGecko();                                         \
    const nsStyle##name_ * cachedData =                             \
      static_cast<nsStyle##name_*>(                                 \
        gecko->mCachedInheritedData                                 \
        .mStyleStructs[eStyleStruct_##name_]);                      \
    if (cachedData) /* Have it cached already, yay */               \
      return cachedData;                                            \
    if (!aComputeData) {                                            \
      /* We always cache inherited structs on the context when we */\
      /* compute them. */                                           \
      return nullptr;                                               \
    }                                                               \
    /* Have the rulenode deal */                                    \
    AUTO_CHECK_DEPENDENCY(gecko, eStyleStruct_##name_);             \
    const nsStyle##name_ * newData =                                \
      gecko->RuleNode()->                                           \
        GetStyle##name_<aComputeData>(gecko, mBits);                \
    /* always cache inherited data on the style context; the rule */\
    /* node set the bit in mBits for us if needed. */               \
    gecko->mCachedInheritedData                                     \
      .mStyleStructs[eStyleStruct_##name_] =                        \
      const_cast<nsStyle##name_ *>(newData);                        \
    return newData;                                                 \
  }
#else
#define DO_GET_STYLE_INHERITED_GECKO(name_, checkdata_cb_)          \
  MOZ_CRASH("old style system disabled");
#endif

// Helper functions for GetStyle* and PeekStyle*
#define STYLE_STRUCT_INHERITED(name_, checkdata_cb_)                         \
template<bool aComputeData>                                                  \
const nsStyle##name_ * nsStyleContext::DoGetStyle##name_() {                 \
  if (IsGecko()) {                                                           \
    DO_GET_STYLE_INHERITED_GECKO(name_, checkdata_cb_)                       \
  }                                                                          \
  const bool needToCompute = !(mBits & NS_STYLE_INHERIT_BIT(name_));         \
  if (!aComputeData && needToCompute) {                                      \
    return nullptr;                                                          \
  }                                                                          \
                                                                             \
  const nsStyle##name_* data = ComputedData()->GetStyle##name_();            \
                                                                             \
  /* perform any remaining main thread work on the struct */                 \
  if (needToCompute) {                                                       \
    MOZ_ASSERT(NS_IsMainThread());                                           \
    MOZ_ASSERT(!mozilla::ServoStyleSet::IsInServoTraversal());               \
    const_cast<nsStyle##name_*>(data)->FinishStyle(PresContext(), nullptr);  \
    /* the ServoStyleContext owns the struct */                              \
    AddStyleBit(NS_STYLE_INHERIT_BIT(name_));                                \
  }                                                                          \
  return data;                                                               \
}

#ifdef MOZ_OLD_STYLE
#define DO_GET_STYLE_RESET_GECKO(name_, checkdata_cb_)                        \
  {                                                                           \
    auto gecko = AsGecko();                                                   \
    if (gecko->mCachedResetData) {                                            \
      const nsStyle##name_ * cachedData =                                     \
        static_cast<nsStyle##name_*>(                                         \
          gecko->mCachedResetData->mStyleStructs[eStyleStruct_##name_]);      \
      if (cachedData) /* Have it cached already, yay */                       \
        return cachedData;                                                    \
    }                                                                         \
    /* Have the rulenode deal */                                              \
    AUTO_CHECK_DEPENDENCY(gecko, eStyleStruct_##name_);                       \
    return gecko->RuleNode()->GetStyle##name_<aComputeData>(gecko);           \
  }
#else
#define DO_GET_STYLE_RESET_GECKO(name_, checkdata_cb_)                        \
  MOZ_CRASH("old style system disabled");
#endif

#define STYLE_STRUCT_RESET(name_, checkdata_cb_)                              \
template<bool aComputeData>                                                   \
const nsStyle##name_ * nsStyleContext::DoGetStyle##name_() {                  \
  if (IsGecko()) {                                                            \
    DO_GET_STYLE_RESET_GECKO(name_, checkdata_cb_)                            \
  }                                                                           \
  const bool needToCompute = !(mBits & NS_STYLE_INHERIT_BIT(name_));          \
  if (!aComputeData && needToCompute) {                                       \
    return nullptr;                                                           \
  }                                                                           \
  const nsStyle##name_* data = ComputedData()->GetStyle##name_();             \
  /* perform any remaining main thread work on the struct */                  \
  if (needToCompute) {                                                        \
    const_cast<nsStyle##name_*>(data)->FinishStyle(PresContext(), nullptr);   \
    /* the ServoStyleContext owns the struct */                               \
    AddStyleBit(NS_STYLE_INHERIT_BIT(name_));                                 \
  }                                                                           \
  return data;                                                                \
}
#include "nsStyleStructList.h"
#undef STYLE_STRUCT_RESET
#undef STYLE_STRUCT_INHERITED


nsPresContext*
nsStyleContext::PresContext() const
{
    MOZ_STYLO_FORWARD(PresContext, ())
}


nsStyleContext*
nsStyleContext::GetStyleIfVisited() const
{
  MOZ_STYLO_FORWARD(GetStyleIfVisited, ())
}

void
nsStyleContext::StartBackgroundImageLoads()
{
  // Just get our background struct; that should do the trick
  StyleBackground();
}

#ifdef MOZ_OLD_STYLE
/* static */ already_AddRefed<mozilla::GeckoStyleContext>
mozilla::GeckoStyleContext::TakeRef(
  already_AddRefed<nsStyleContext> aStyleContext)
{
  auto* context = aStyleContext.take();
  MOZ_ASSERT(context);

  return already_AddRefed<mozilla::GeckoStyleContext>(context->AsGecko());
}
#endif

#endif // nsStyleContextInlines_h
