/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_DeclarationBlockInlines_h
#define mozilla_DeclarationBlockInlines_h

#include "mozilla/css/Declaration.h"
#include "mozilla/ServoDeclarationBlock.h"

namespace mozilla {

MOZ_DEFINE_STYLO_METHODS(DeclarationBlock, css::Declaration, ServoDeclarationBlock)

MozExternalRefCountType
DeclarationBlock::AddRef()
{
  MOZ_STYLO_FORWARD(AddRef, ())
}

MozExternalRefCountType
DeclarationBlock::Release()
{
  MOZ_STYLO_FORWARD(Release, ())
}

} // namespace mozilla

#endif // mozilla_DeclarationBlockInlines_h
