#!/bin/bash

set -xe

# Required env variables

#test "$DATE" # YYYY-MM-DD - get it from in-tree for release date
test "$VERSION"
test "$BUILD_NUMBER"
test "$CANDIDATES_DIR"
test "$L10N_CHANGESETS"

# Optional env variables
: WORKSPACE                     "${WORKSPACE:=/home/worker/workspace}"
: ARTIFACTS_DIR                 "${ARTIFACTS_DIR:=/home/worker/artifacts}"

DATE=$(date +%Y-%m-%d)
SCRIPT_DIRECTORY="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
TARGET="target.flatpak"
TARGET_TAR_GZ="target.flatpak.tar.gz"
TARGET_FULL_PATH="$ARTIFACTS_DIR/$TARGET"
TARGET_TAR_GZ_FULL_PATH="$ARTIFACTS_DIR/$TARGET_TAR_GZ"
SOURCE_DEST="${WORKSPACE}/source"

mkdir -p "$ARTIFACTS_DIR"
rm -rf "$SOURCE_DEST" && mkdir -p "$SOURCE_DEST"

CURL="curl --location --retry 10 --retry-delay 10"

# Download en-US linux64 binary
$CURL -o "${WORKSPACE}/firefox.tar.bz2" \
    "${CANDIDATES_DIR}/${VERSION}-candidates/build${BUILD_NUMBER}/linux-x86_64/en-US/firefox-${VERSION}.tar.bz2"

# Make sure the downloaded bits are correct
$CURL -o "${WORKSPACE}/SHA512SUMS" \
    "${CANDIDATES_DIR}/${VERSION}-candidates/build${BUILD_NUMBER}/SHA512SUMS"
UPSTREAM_SHA=`cat $WORKSPACE/SHA512SUMS | grep linux-x86_64/en-US/firefox-${VERSION}.tar.bz2 | cut -d\  -f1`
LOCAL_SHA=$(sha512sum "$WORKSPACE/firefox.tar.bz2" | awk '{print $1}')

if [ "$UPSTREAM_SHA" != "$LOCAL_SHA" ]; then
    echo 'Local sha512 of the en-US binary differs from the upstream one, bailing ...'
    exit 1
fi

# Use list of locales to fetch L10N XPIs
$CURL -o "${WORKSPACE}/l10n_changesets.json" "$L10N_CHANGESETS"
locales=$(python3 "$SCRIPT_DIRECTORY/extract_locales_from_l10n_json.py" "${WORKSPACE}/l10n_changesets.json")

DISTRIBUTION_DIR="$SOURCE_DEST/distribution"
mkdir -p "$DISTRIBUTION_DIR/extensions"
for locale in $locales; do
    $CURL -o "$DISTRIBUTION_DIR/extensions/langpack-${locale}@firefox.mozilla.org.xpi" \
        "$CANDIDATES_DIR/${VERSION}-candidates/build${BUILD_NUMBER}/linux-x86_64/xpi/${locale}.xpi"

    LANGPACKS="$LANGPACKS
              {
                  'type': 'file',
                  'path': '$DISTRIBUTION_DIR/extensions/langpack-${locale}@firefox.mozilla.org.xpi',
                  'dest': 'langpacks/'
              },"
done
export LANGPACKS

# Generate flatpak manifest
envsubst '$LANGPACKS'< "$SCRIPT_DIRECTORY/flatpak.json.in" > "${WORKSPACE}/org.mozilla.firefox.json"
envsubst < "$SCRIPT_DIRECTORY/org.mozilla.firefox.appdata.xml.in" > "${WORKSPACE}/org.mozilla.firefox.appdata.xml"
cp -v "$SCRIPT_DIRECTORY/Makefile" "$WORKSPACE"
cp -v "$SCRIPT_DIRECTORY/org.mozilla.firefox.desktop" "$WORKSPACE"
cp -v "$SCRIPT_DIRECTORY/distribution.ini" "$WORKSPACE"
cp -v "$SCRIPT_DIRECTORY/default-preferences.js" "$WORKSPACE"
cp -v "$SCRIPT_DIRECTORY/launch-script.sh" "$WORKSPACE"
cd "${WORKSPACE}"

flatpak-builder --install-deps-from=flathub --assumeyes --force-clean --disable-rofiles-fuse --disable-cache --repo="$WORKSPACE"/repo --verbose "$WORKSPACE"/app org.mozilla.firefox.json
flatpak build-bundle "$WORKSPACE"/repo org.mozilla.firefox.flatpak org.mozilla.firefox
flatpak build-update-repo repo
tar cvfz flatpak.tar.gz repo

mv -- *.flatpak "$TARGET_FULL_PATH"
mv -- flatpak.tar.gz "$TARGET_TAR_GZ_FULL_PATH"

cd "$ARTIFACTS_DIR"

# Generate checksums file
size=$(stat --printf="%s" "$TARGET_FULL_PATH")
sha=$(sha512sum "$TARGET_FULL_PATH" | awk '{print $1}')
echo "$sha sha512 $size $TARGET" > "$TARGET.checksums"

echo "Generating signing manifest"
hash=$(sha512sum "$TARGET.checksums" | awk '{print $1}')

cat << EOF > signing_manifest.json
[{"file_to_sign": "$TARGET.checksums", "hash": "$hash"}]
EOF

# For posterity
find . -ls
cat "$TARGET.checksums"
cat signing_manifest.json
