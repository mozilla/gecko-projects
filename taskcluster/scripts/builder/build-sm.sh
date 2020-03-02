#!/bin/bash

set -x

source $(dirname $0)/sm-tooltool-config.sh

: ${PYTHON3:=python3}

# Run the script
export MOZ_UPLOAD_DIR="$(cd "$UPLOAD_DIR"; pwd)"
AUTOMATION=1 $PYTHON3 $SRCDIR/js/src/devtools/automation/autospider.py ${SPIDERMONKEY_PLATFORM:+--platform=$SPIDERMONKEY_PLATFORM} $SPIDERMONKEY_VARIANT
BUILD_STATUS=$?

# Ensure upload dir exists
mkdir -p $UPLOAD_DIR

# Copy artifacts for upload by TaskCluster
cp -rL $SRCDIR/obj-spider/dist/bin/{js,jsapi-tests,js-gdb.py} $UPLOAD_DIR

# Fuzzing users would really like to have llvm-symbolizer available in the same
# directory as the built output.
gzip -c $MOZ_FETCHES_DIR/clang/bin/llvm-symbolizer > $UPLOAD_DIR/llvm-symbolizer.gz || true

# Fuzzing also uses a few fields in target.json file for automated downloads to
# identify what was built.
if [ -n "$MOZ_BUILD_DATE" ] && [ -n "$GECKO_HEAD_REV" ]; then
    cat >$UPLOAD_DIR/target.json <<EOF
{
  "buildid": "$MOZ_BUILD_DATE",
  "moz_source_stamp": "$GECKO_HEAD_REV"
}
EOF
fi

exit $BUILD_STATUS
