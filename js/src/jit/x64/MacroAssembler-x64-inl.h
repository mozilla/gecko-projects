/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef jit_x64_MacroAssembler_x64_inl_h
#define jit_x64_MacroAssembler_x64_inl_h

#include "jit/x64/MacroAssembler-x64.h"

#include "jit/x86-shared/MacroAssembler-x86-shared-inl.h"

namespace js {
namespace jit {

//{{{ check_macroassembler_style
// ===============================================================

void
MacroAssembler::andPtr(Register src, Register dest)
{
    andq(src, dest);
}

void
MacroAssembler::andPtr(Imm32 imm, Register dest)
{
    andq(imm, dest);
}

void
MacroAssembler::orPtr(Register src, Register dest)
{
    orq(src, dest);
}

void
MacroAssembler::orPtr(Imm32 imm, Register dest)
{
    orq(imm, dest);
}

void
MacroAssembler::xorPtr(Register src, Register dest)
{
    xorq(src, dest);
}

void
MacroAssembler::xorPtr(Imm32 imm, Register dest)
{
    xorq(imm, dest);
}

//}}} check_macroassembler_style
// ===============================================================

} // namespace jit
} // namespace js

#endif /* jit_x64_MacroAssembler_x64_inl_h */
