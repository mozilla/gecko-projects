#!/bin/bash
set -x -e -v

# This script is for building libdmg-hfsplus to get the `dmg` and `hfsplus`
# tools for producing DMG archives on Linux.

WORKSPACE=$HOME/workspace
HOME_DIR=$WORKSPACE/build
STAGE=$WORKSPACE/dmg
UPLOAD_DIR=$WORKSPACE/artifacts

# There's no single well-maintained fork of libdmg-hfsplus, but this
# branch currently has some fixes we need.
: LIBDMG_REPOSITORY    ${LIBDMG_REPOSITORY:=https://github.com/andreas56/libdmg-hfsplus}
# This is the last known working rev that doesn't produce corrupted DMGs
: LIBDMG_REV           ${LIBDMG_REV:=1d72dd62a13632134667a378f926e8904f70bf25}

mkdir -p $UPLOAD_DIR $STAGE

cd $WORKSPACE
tc-vcs checkout --force-clone libdmg-hfsplus $LIBDMG_REPOSITORY $LIBDMG_REPOSITORY $LIBDMG_REV
cd libdmg-hfsplus
patches=""
for i in $HOME_DIR/src/taskcluster/scripts/misc/libdmg-patches/*.patch; do
	patches="$patches $i"
	git -c user.name="Taskcluster build-libdmg-hfsplus.sh" -c user.email="<tools-taskcluster@lists.mozilla.org>" am $i
done
# Make a source archive
git archive HEAD | xz > $UPLOAD_DIR/libdmg-hfsplus.tar.xz
cmake .
make -j$(getconf _NPROCESSORS_ONLN)

# We only need the dmg and hfsplus tools.
strip dmg/dmg hfs/hfsplus
cp dmg/dmg hfs/hfsplus $STAGE

cat >$STAGE/README<<EOF
Built from ${LIBDMG_REPOSITORY} rev `git rev-parse ${LIBDMG_REV}`.
With patches:$patches
Source is available in tooltool, digest `sha512sum $UPLOAD_DIR/libdmg-hfsplus.tar.xz`.
EOF
tar cf - -C $WORKSPACE `basename $STAGE` | xz > $UPLOAD_DIR/dmg.tar.xz
