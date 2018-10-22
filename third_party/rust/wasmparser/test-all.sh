#!/bin/bash

# This is the top-level test script:
#
# - Build documentation for Rust code in 'src/tools/target/doc'.
# - Run unit tests for all Rust crates.
# - Make a debug build of all crates.
#
# All tests run by this script should be passing at all times.

# Exit immediately on errors.
set -e

# Repository top-level directory.
cd $(dirname "$0")
topdir=$(pwd)

function banner() {
    echo "======  $@  ======"
}

# Run rustfmt if we have it.
if $topdir/check-rustfmt.sh; then
    banner "Rust formatting"
    $topdir/format-all.sh --write-mode=diff
fi

PKGS="wasmparser"
cd "$topdir"
for PKG in $PKGS
do
    banner "Rust $PKG unit tests"
    cargo test -p $PKG
done

cd "$topdir"
banner "Rust documentation"
echo "open $topdir/target/doc/wasmparser.rs/index.html"
cargo doc
banner "Rust release build"
cargo build --release

banner "OK"
