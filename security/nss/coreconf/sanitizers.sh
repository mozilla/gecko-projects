#!/usr/bin/env bash
# This file is used by build.sh to setup sanitizers.

sanitizer_flags=""

# This tracks what sanitizers are enabled, and their options.
declare -A sanitizers
enable_sanitizer()
{
    local san="$1"
    [ -n "${sanitizers[$san]}" ] && return
    sanitizers[$san]="${2:-1}"

    if [ -z "$sanitizer_flags" ]; then
        gyp_params+=(-Dno_zdefs=1)
    fi

    local cflags=$(python $cwd/coreconf/sanitizers.py "$@")
    sanitizer_flags="$sanitizer_flags $cflags"
}

enable_sancov()
{
    local clang_version=$($CC --version | grep -oE 'clang version (3\.9\.|4\.)')
    if [ -z "$clang_version" ]; then
        echo "Need at least clang-3.9 (better 4.0) for sancov." 1>&2
        exit 1
    fi

    local sancov
    if [ -n "$1" ]; then
        sancov="$1"
    elif [ "$clang_version" = "clang version 3.9." ]; then
        sancov=edge,indirect-calls,8bit-counters
    else
        sancov=trace-pc-guard,trace-cmp
    fi
    enable_sanitizer sancov "$sancov"
}

enable_ubsan()
{
    local ubsan
    if [ -n "$1" ]; then
        ubsan="$1"
    else
        ubsan=bool,signed-integer-overflow,shift,vptr
    fi
    enable_sanitizer ubsan "$ubsan"
}

# Not strictly a sanitizer, but the pattern fits
scanbuild=()
enable_scanbuild()
{
    [ "${#scanbuild[@]}" -gt 0 ] && return

    scanbuild=(scan-build)
    if [ -n "$1" ]; then
        scanbuild+=(-o "$1")
    fi
    # pass on CC and CCC to scanbuild
    if [ -n "$CC" ]; then
        scanbuild+=(--use-cc="$CC")
    fi
    if [ -n "$CCC" ]; then
        scanbuild+=(--use-c++="$CCC")
    fi
}

run_scanbuild()
{
    "${scanbuild[@]}" "$@"
}
