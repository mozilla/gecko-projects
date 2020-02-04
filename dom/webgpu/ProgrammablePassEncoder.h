/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GPU_ProgrammablePassEncoder_H_
#define GPU_ProgrammablePassEncoder_H_

#include "mozilla/dom/TypedArray.h"
#include "ObjectModel.h"

namespace mozilla {
namespace dom {
template <typename T>
class Sequence;
}  // namespace dom
namespace webgpu {

class BindGroup;
class CommandEncoder;

class ProgrammablePassEncoder : public nsISupports, public ObjectBase {
 public:
  // Note: here and in derived classes, there is no need for SCRIPT_HOLDER if
  // it doesn't hold any JS::Value or JSObject members. That way, we could be
  // using a simpler `NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED` macro.
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_CLASS(ProgrammablePassEncoder)

  ProgrammablePassEncoder();

 protected:
  ~ProgrammablePassEncoder() = default;
  void Cleanup() {}

 public:
  // TODO: remove all of this, see
  // https://bugzilla.mozilla.org/show_bug.cgi?id=1611024
  virtual void SetBindGroup(uint32_t aSlot, const BindGroup& aBindGroup,
                            const dom::Sequence<uint32_t>& aDynamicOffsets) = 0;
};

}  // namespace webgpu
}  // namespace mozilla

#endif  // GPU_ProgrammablePassEncoder_H_
