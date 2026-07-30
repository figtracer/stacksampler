#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT/build"
DIST_DIR="$ROOT/dist"
PACKAGE_NAME="StackSampler-macOS-universal"
PACKAGE_DIR="$DIST_DIR/$PACKAGE_NAME"
ZIP="$DIST_DIR/$PACKAGE_NAME.zip"
CHECKSUMS="$DIST_DIR/SHA256SUMS"
JUCE_DIR="$BUILD_DIR/_deps/juce-src"

if [[ ! -f "$ROOT/CMakeLists.txt" ]]; then
    printf 'error: CMakeLists.txt not found at %s\n' "$ROOT" >&2
    exit 1
fi

cmake \
    -S "$ROOT" \
    -B "$BUILD_DIR" \
    -G "Unix Makefiles" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
    -DBUILD_TESTING=ON

cmake --build "$BUILD_DIR" --parallel
ctest --test-dir "$BUILD_DIR" -C Release --output-on-failure

VST3="$BUILD_DIR/StackSampler_artefacts/Release/VST3/StackSampler.vst3"
STANDALONE="$BUILD_DIR/StackSampler_artefacts/Release/Standalone/StackSampler.app"
VST3_EXECUTABLE="$VST3/Contents/MacOS/StackSampler"
STANDALONE_EXECUTABLE="$STANDALONE/Contents/MacOS/StackSampler"

for artifact in "$VST3" "$STANDALONE"; do
    if [[ ! -e "$artifact" ]]; then
        printf 'error: expected build artifact not found: %s\n' "$artifact" >&2
        exit 1
    fi
done

lipo "$VST3_EXECUTABLE" -verify_arch arm64 x86_64
lipo "$STANDALONE_EXECUTABLE" -verify_arch arm64 x86_64

codesign --force --deep --sign - "$VST3"
codesign --force --deep --sign - "$STANDALONE"

verify_signature() {
    local artifact="$1"

    if codesign --verify --deep --strict --verbose=2 "$artifact"; then
        return
    fi

    # APFS can briefly expose the JUCE-generated resource seal before all
    # metadata is visible to a second codesign process.
    sleep 1
    codesign --verify --deep --strict --verbose=2 "$artifact"
}

verify_signature "$VST3"
verify_signature "$STANDALONE"

rm -rf "$PACKAGE_DIR"
rm -f "$ZIP" "$CHECKSUMS"
mkdir -p "$PACKAGE_DIR"
mkdir -p "$PACKAGE_DIR/Third-Party Licenses"

ditto "$VST3" "$PACKAGE_DIR/StackSampler.vst3"
ditto "$STANDALONE" "$PACKAGE_DIR/StackSampler.app"
cp "$ROOT/packaging/INSTALL.txt" "$PACKAGE_DIR/INSTALL.txt"
cp "$ROOT/LICENSE" "$PACKAGE_DIR/LICENSE"
cp "$ROOT/THIRD_PARTY_NOTICES.md" "$PACKAGE_DIR/THIRD_PARTY_NOTICES.md"

copy_notice() {
    local source="$1"
    local destination="$2"

    if [[ ! -f "$source" ]]; then
        printf 'error: required licence notice not found: %s\n' "$source" >&2
        exit 1
    fi

    cp "$source" "$PACKAGE_DIR/Third-Party Licenses/$destination"
}

copy_notice "$JUCE_DIR/LICENSE.md" "JUCE-LICENSE.md"
copy_notice \
    "$JUCE_DIR/modules/juce_audio_formats/codecs/flac/Flac Licence.txt" \
    "FLAC-LICENSE.txt"
copy_notice \
    "$JUCE_DIR/modules/juce_audio_formats/codecs/oggvorbis/Ogg Vorbis Licence.txt" \
    "OGG-VORBIS-LICENSE.txt"
copy_notice \
    "$JUCE_DIR/modules/juce_graphics/image_formats/jpglib/README" \
    "JPEG-README.txt"
copy_notice \
    "$JUCE_DIR/modules/juce_graphics/image_formats/pnglib/LICENSE" \
    "PNG-LICENSE.txt"
copy_notice \
    "$JUCE_DIR/modules/juce_core/zip/zlib/LICENSE" \
    "ZLIB-LICENSE.txt"
copy_notice \
    "$JUCE_DIR/modules/juce_graphics/fonts/harfbuzz/COPYING" \
    "HARFBUZZ-COPYING.txt"
copy_notice \
    "$JUCE_DIR/modules/juce_graphics/unicode/sheenbidi/LICENSE" \
    "SHEENBIDI-LICENSE.txt"
copy_notice \
    "$JUCE_DIR/modules/juce_audio_processors_headless/format_types/VST3_SDK/LICENSE.txt" \
    "VST3-SDK-LICENSE.txt"
copy_notice \
    "$JUCE_DIR/modules/juce_audio_processors_headless/format_types/VST3_SDK/base/LICENSE.txt" \
    "VST3-BASE-LICENSE.txt"
copy_notice \
    "$JUCE_DIR/modules/juce_audio_processors_headless/format_types/VST3_SDK/pluginterfaces/LICENSE.txt" \
    "VST3-PLUGINTERFACES-LICENSE.txt"
copy_notice \
    "$JUCE_DIR/modules/juce_audio_processors_headless/format_types/VST3_SDK/public.sdk/LICENSE.txt" \
    "VST3-PUBLIC-SDK-LICENSE.txt"

(
    cd "$DIST_DIR"
    ditto -c -k --norsrc --noextattr --noqtn --noacl \
        --keepParent "$PACKAGE_NAME" "$PACKAGE_NAME.zip"
    shasum -a 256 "$PACKAGE_NAME.zip" > SHA256SUMS
)

printf '%s\n%s\n' "$ZIP" "$CHECKSUMS"
