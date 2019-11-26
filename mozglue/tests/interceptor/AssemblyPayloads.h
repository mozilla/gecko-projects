/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

/* These assembly functions represent patterns that were already hooked by
 * another application before our detour.
 */

#ifndef mozilla_AssemblyPayloads_h
#define mozilla_AssemblyPayloads_h

extern "C" {

#if defined(__clang__)
#  if defined(_M_X64)
constexpr uintptr_t JumpDestination = 0x7fff00000000;

__declspec(dllexport) __attribute__((naked)) void MovPushRet() {
  asm volatile(
      "mov %0, %%rax;"
      "push %%rax;"
      "ret;"
      :
      : "i"(JumpDestination));
}

__declspec(dllexport) __attribute__((naked)) void MovRaxJump() {
  asm volatile(
      "mov %0, %%rax;"
      "jmpq *%%rax;"
      :
      : "i"(JumpDestination));
}
#  elif defined(_M_IX86)
constexpr uintptr_t JumpDestination = 0x7fff0000;

__declspec(dllexport) __attribute__((naked)) void PushRet() {
  asm volatile(
      "push %0;"
      "ret;"
      :
      : "i"(JumpDestination));
}

__declspec(dllexport) __attribute__((naked)) void MovEaxJump() {
  asm volatile(
      "mov %0, %%eax;"
      "jmp *%%eax;"
      :
      : "i"(JumpDestination));
}
#  endif
#endif  // defined(__clang__)

}  // extern "C"

#endif  // mozilla_AssemblyPayloads_h
