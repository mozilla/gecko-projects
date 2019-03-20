/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_SVGFETurbulenceElement_h
#define mozilla_dom_SVGFETurbulenceElement_h

#include "SVGEnum.h"
#include "SVGFilters.h"
#include "nsSVGNumber2.h"
#include "SVGInteger.h"
#include "SVGString.h"

nsresult NS_NewSVGFETurbulenceElement(
    nsIContent** aResult, already_AddRefed<mozilla::dom::NodeInfo>&& aNodeInfo);

namespace mozilla {
namespace dom {

typedef SVGFE SVGFETurbulenceElementBase;

class SVGFETurbulenceElement : public SVGFETurbulenceElementBase {
  friend nsresult(::NS_NewSVGFETurbulenceElement(
      nsIContent** aResult,
      already_AddRefed<mozilla::dom::NodeInfo>&& aNodeInfo));

 protected:
  explicit SVGFETurbulenceElement(
      already_AddRefed<mozilla::dom::NodeInfo>&& aNodeInfo)
      : SVGFETurbulenceElementBase(std::move(aNodeInfo)) {}
  virtual JSObject* WrapNode(JSContext* aCx,
                             JS::Handle<JSObject*> aGivenProto) override;

 public:
  virtual bool SubregionIsUnionOfRegions() override { return false; }

  virtual FilterPrimitiveDescription GetPrimitiveDescription(
      nsSVGFilterInstance* aInstance, const IntRect& aFilterSubregion,
      const nsTArray<bool>& aInputsAreTainted,
      nsTArray<RefPtr<SourceSurface>>& aInputImages) override;
  virtual bool AttributeAffectsRendering(int32_t aNameSpaceID,
                                         nsAtom* aAttribute) const override;
  virtual SVGString& GetResultImageName() override {
    return mStringAttributes[RESULT];
  }

  virtual nsresult Clone(dom::NodeInfo*, nsINode** aResult) const override;

  // WebIDL
  already_AddRefed<SVGAnimatedNumber> BaseFrequencyX();
  already_AddRefed<SVGAnimatedNumber> BaseFrequencyY();
  already_AddRefed<SVGAnimatedInteger> NumOctaves();
  already_AddRefed<SVGAnimatedNumber> Seed();
  already_AddRefed<SVGAnimatedEnumeration> StitchTiles();
  already_AddRefed<SVGAnimatedEnumeration> Type();

 protected:
  virtual NumberAttributesInfo GetNumberInfo() override;
  virtual NumberPairAttributesInfo GetNumberPairInfo() override;
  virtual IntegerAttributesInfo GetIntegerInfo() override;
  virtual EnumAttributesInfo GetEnumInfo() override;
  virtual StringAttributesInfo GetStringInfo() override;

  enum { SEED };  // floating point seed?!
  nsSVGNumber2 mNumberAttributes[1];
  static NumberInfo sNumberInfo[1];

  enum { BASE_FREQ };
  SVGNumberPair mNumberPairAttributes[1];
  static NumberPairInfo sNumberPairInfo[1];

  enum { OCTAVES };
  SVGInteger mIntegerAttributes[1];
  static IntegerInfo sIntegerInfo[1];

  enum { TYPE, STITCHTILES };
  SVGEnum mEnumAttributes[2];
  static SVGEnumMapping sTypeMap[];
  static SVGEnumMapping sStitchTilesMap[];
  static EnumInfo sEnumInfo[2];

  enum { RESULT };
  SVGString mStringAttributes[1];
  static StringInfo sStringInfo[1];
};

}  // namespace dom
}  // namespace mozilla

#endif  // mozilla_dom_SVGFETurbulenceElement_h
