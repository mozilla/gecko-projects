#!/bin/bash
set -x -e -v

# This script is for building libdmg-hfsplus to get the `dmg` and `hfsplus`
# tools for producing DMG archives on Linux.

WORKSPACE=$HOME/workspace
STAGE=$WORKSPACE/dmg
UPLOAD_DIR=$WORKSPACE/artifacts

# There's no single well-maintained fork of libdmg-hfsplus, so we forked
# https://github.com/andreas56/libdmg-hfsplus/ to get a specific version and
# backport some patches.
: LIBDMG_REPOSITORY    ${LIBDMG_REPOSITORY:=https://github.com/mozilla/libdmg-hfsplus}
# The `mozilla` branch contains our fork.
: LIBDMG_REV           ${LIBDMG_REV:=mozilla}

mkdir -p $UPLOAD_DIR $STAGE

cd $WORKSPACE
git clone --no-checkout $LIBDMG_REPOSITORY libdmg-hfsplus
cd libdmg-hfsplus
git checkout $LIBDMG_REV

# Make a source archive
git archive ${LIBDMG_REV} | xz > $UPLOAD_DIR/libdmg-hfsplus.tar.xz
cmake .
make -j$(getconf _NPROCESSORS_ONLN)

# We only need the dmg and hfsplus tools.
strip dmg/dmg hfs/hfsplus
cp dmg/dmg hfs/hfsplus $STAGE

cat >$STAGE/README<<EOF
Built from ${LIBDMG_REPOSITORY} rev `git rev-parse ${LIBDMG_REV}`.
Source is available in tooltool, digest `sha512sum $UPLOAD_DIR/libdmg-hfsplus.tar.xz`.
EOF
tar cf - -C $WORKSPACE `basename $STAGE` | xz > $UPLOAD_DIR/dmg.tar.xz
