/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_SVGFEDropShadowElement_h
#define mozilla_dom_SVGFEDropShadowElement_h

#include "SVGFilters.h"
#include "nsSVGNumber2.h"
#include "SVGNumberPair.h"
#include "SVGString.h"

nsresult NS_NewSVGFEDropShadowElement(
    nsIContent** aResult, already_AddRefed<mozilla::dom::NodeInfo>&& aNodeInfo);

namespace mozilla {
namespace dom {

typedef SVGFE SVGFEDropShadowElementBase;

class SVGFEDropShadowElement : public SVGFEDropShadowElementBase {
  friend nsresult(::NS_NewSVGFEDropShadowElement(
      nsIContent** aResult,
      already_AddRefed<mozilla::dom::NodeInfo>&& aNodeInfo));

 protected:
  explicit SVGFEDropShadowElement(
      already_AddRefed<mozilla::dom::NodeInfo>&& aNodeInfo)
      : SVGFEDropShadowElementBase(std::move(aNodeInfo)) {}
  virtual JSObject* WrapNode(JSContext* aCx,
                             JS::Handle<JSObject*> aGivenProto) override;

 public:
  virtual FilterPrimitiveDescription GetPrimitiveDescription(
      nsSVGFilterInstance* aInstance, const IntRect& aFilterSubregion,
      const nsTArray<bool>& aInputsAreTainted,
      nsTArray<RefPtr<SourceSurface>>& aInputImages) override;
  virtual bool AttributeAffectsRendering(int32_t aNameSpaceID,
                                         nsAtom* aAttribute) const override;
  virtual SVGString& GetResultImageName() override {
    return mStringAttributes[RESULT];
  }
  virtual void GetSourceImageNames(nsTArray<SVGStringInfo>& aSources) override;

  // nsIContent interface
  NS_IMETHOD_(bool) IsAttributeMapped(const nsAtom* aAttribute) const override;

  virtual nsresult Clone(dom::NodeInfo*, nsINode** aResult) const override;

  // WebIDL
  already_AddRefed<SVGAnimatedString> In1();
  already_AddRefed<SVGAnimatedNumber> Dx();
  already_AddRefed<SVGAnimatedNumber> Dy();
  already_AddRefed<SVGAnimatedNumber> StdDeviationX();
  already_AddRefed<SVGAnimatedNumber> StdDeviationY();
  void SetStdDeviation(float stdDeviationX, float stdDeviationY);

 protected:
  virtual NumberAttributesInfo GetNumberInfo() override;
  virtual NumberPairAttributesInfo GetNumberPairInfo() override;
  virtual StringAttributesInfo GetStringInfo() override;

  enum { DX, DY };
  nsSVGNumber2 mNumberAttributes[2];
  static NumberInfo sNumberInfo[2];

  enum { STD_DEV };
  SVGNumberPair mNumberPairAttributes[1];
  static NumberPairInfo sNumberPairInfo[1];

  enum { RESULT, IN1 };
  SVGString mStringAttributes[2];
  static StringInfo sStringInfo[2];
};

}  // namespace dom
}  // namespace mozilla

#endif  // mozilla_dom_SVGFEDropShadowElement_h
