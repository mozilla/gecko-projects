/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/ServoStyleContext.h"

#include "nsCSSAnonBoxes.h"
#include "nsStyleConsts.h"
#include "nsStyleStruct.h"
#include "nsPresContext.h"
#include "nsCSSRuleProcessor.h"
#include "mozilla/dom/HTMLBodyElement.h"

#include "mozilla/ServoBindings.h"

namespace mozilla {

ServoStyleContext::ServoStyleContext(
    nsPresContext* aPresContext,
    nsIAtom* aPseudoTag,
    CSSPseudoElementType aPseudoType,
    ServoComputedDataForgotten aComputedValues)
  : nsStyleContext(aPseudoTag, aPseudoType)
  , mPresContext(aPresContext)
  , mSource(aComputedValues)
{
  AddStyleBit(Servo_ComputedValues_GetStyleBits(this));
  MOZ_ASSERT(ComputedData());

  // No need to call ApplyStyleFixups here, since fixups are handled by Servo when
  // producing the ServoComputedData.
}

ServoStyleContext*
ServoStyleContext::GetCachedInheritingAnonBoxStyle(nsIAtom* aAnonBox) const
{
  MOZ_ASSERT(nsCSSAnonBoxes::IsInheritingAnonBox(aAnonBox));

  // See the reasoning in SetCachedInheritingAnonBoxStyle to understand why we
  // can't use the cache in this case.
  if (IsInheritingAnonBox()) {
    return nullptr;
  }

  auto* current = mNextInheritingAnonBoxStyle.get();

  while (current && current->GetPseudo() != aAnonBox) {
    current = current->mNextInheritingAnonBoxStyle.get();
  }

  return current;
}

ServoStyleContext*
ServoStyleContext::GetCachedLazyPseudoStyle(CSSPseudoElementType aPseudo) const
{
  MOZ_ASSERT(aPseudo != CSSPseudoElementType::NotPseudo &&
             aPseudo != CSSPseudoElementType::InheritingAnonBox &&
             aPseudo != CSSPseudoElementType::NonInheritingAnonBox);
  MOZ_ASSERT(!IsLazilyCascadedPseudoElement(), "Lazy pseudos can't inherit lazy pseudos");

  if (nsCSSPseudoElements::PseudoElementSupportsUserActionState(aPseudo)) {
    return nullptr;
  }

  auto* current = mNextLazyPseudoStyle.get();

  while (current && current->GetPseudoType() != aPseudo) {
    current = current->mNextLazyPseudoStyle.get();
  }

  return current;
}

} // namespace mozilla
