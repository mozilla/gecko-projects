dnl This Source Code Form is subject to the terms of the Mozilla Public
dnl License, v. 2.0. If a copy of the MPL was not distributed with this
dnl file, You can obtain one at http://mozilla.org/MPL/2.0/.

dnl Set MOZ_FRAMEPTR_FLAGS to the flags that should be used for enabling or
dnl disabling frame pointers in this architecture based on the configure
dnl options

AC_DEFUN([MOZ_SET_FRAMEPTR_FLAGS], [
  if test "$GNU_CC"; then
    MOZ_ENABLE_FRAME_PTR="-fno-omit-frame-pointer -funwind-tables"
    MOZ_DISABLE_FRAME_PTR="-fomit-frame-pointer -funwind-tables"
  else
    case "$target" in
    dnl some versions of clang-cl don't support -Oy-; accommodate them.
    aarch64-windows*)
      if test "$CC_TYPE" = "clang-cl"; then
        MOZ_ENABLE_FRAME_PTR="-Xclang -mdisable-fp-elim"
        MOZ_DISABLE_FRAME_PTR="-Xclang -mdisable-fp-elim"
      else
        MOZ_ENABLE_FRAME_PTR="-Oy-"
        MOZ_DISABLE_FRAME_PTR="-Oy"
      fi
    ;;
    dnl Oy (Frame-Pointer Omission) is only support on x86 compilers
    *-mingw32*)
      MOZ_ENABLE_FRAME_PTR="-Oy-"
      MOZ_DISABLE_FRAME_PTR="-Oy"
    ;;
    esac
  fi

  # If we are debugging, profiling, using sanitizers, or on win32 we want a
  # frame pointer.  It is not required to enable frame pointers on AArch64
  # Windows, but we enable it for compatibility with ETW.
  if test -z "$MOZ_OPTIMIZE" -o \
          -n "$MOZ_PROFILING" -o \
          -n "$MOZ_DEBUG" -o \
          -n "$MOZ_MSAN" -o \
          -n "$MOZ_ASAN" -o \
          -n "$MOZ_UBSAN" -o \
          "$OS_ARCH:$CPU_ARCH" = "WINNT:x86" -o \
	  "$OS_ARCH:$CPU_ARCH" = "WINNT:aarch64"; then
    MOZ_FRAMEPTR_FLAGS="$MOZ_ENABLE_FRAME_PTR"
  else
    MOZ_FRAMEPTR_FLAGS="$MOZ_DISABLE_FRAME_PTR"
  fi
])
