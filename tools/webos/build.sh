#!/bin/bash
# webOS TV build orchestration: configure -> build -> install -> ares-package.
#
# Runs under the arm-webos-linux-gnueabi buildroot cross-toolchain.
#
# Needs:
#   - WEBOS_SDK toolchain (relocate-sdk.sh already run)
#   - ares-package on PATH (from webosbrew/ares-cli-rs)
#
# Usage (from repo root):
#   bash tools/webos/build.sh
#
# Env overrides:
#   WEBOS_SDK         webOS buildroot toolchain root.
#                     Default: /opt/arm-webos-linux-gnueabi_sdk-buildroot
#   BUILD_DIR         CMake build tree. Default: build/webos
#   DIST_DIR          Where the assembled app root + .ipk land. Default: dist
#   PVZP_APP_VERSION  appinfo.json version, and hence the .ipk filename.
#                     Unset: derived from git tag/CMake. CI sets it.

set -eo pipefail

PROJ="$(cd "$(dirname "$0")/../.." && pwd)"

WEBOS_SDK="${WEBOS_SDK:-/opt/arm-webos-linux-gnueabi_sdk-buildroot}"
BUILD_DIR="${BUILD_DIR:-$PROJ/build/webos}"
DIST_DIR="${DIST_DIR:-$PROJ/dist}"
APP_DIR="$BUILD_DIR/approot"

TOOLCHAIN_FILE="$WEBOS_SDK/share/buildroot/toolchainfile.cmake"
if [ ! -f "$TOOLCHAIN_FILE" ]; then
    echo "ERROR: webOS toolchain file not found at $TOOLCHAIN_FILE" >&2
    echo "Set WEBOS_SDK to the extracted+relocated buildroot SDK root." >&2
    exit 1
fi

if ! command -v ares-package >/dev/null 2>&1; then
    echo "ERROR: ares-package not found on PATH (webosbrew/ares-cli-rs)." >&2
    exit 1
fi

# Ensure libopenmpt is available in the SDK sysroot; if not, build it.
SYSROOT_USR="$WEBOS_SDK/arm-webos-linux-gnueabi/sysroot/usr"
if [ ! -f "$SYSROOT_USR/lib/libopenmpt.a" ]; then
    echo "libopenmpt not found in SDK sysroot. Building static libopenmpt..."
    OPENMPT_BUILD_DIR="${OPENMPT_BUILD_DIR:-/tmp/libopenmpt-src}"
    if [ ! -d "$OPENMPT_BUILD_DIR" ]; then
        git clone --depth=1 --branch "libopenmpt-0.8.4" https://github.com/OpenMPT/openmpt.git "$OPENMPT_BUILD_DIR"
    fi
    make -C "$OPENMPT_BUILD_DIR" CONFIG=gcc \
        CC="$WEBOS_SDK/bin/arm-webos-linux-gnueabi-gcc" \
        CXX="$WEBOS_SDK/bin/arm-webos-linux-gnueabi-g++" \
        AR="$WEBOS_SDK/bin/arm-webos-linux-gnueabi-ar" \
        RANLIB="$WEBOS_SDK/bin/arm-webos-linux-gnueabi-ranlib" \
        STATIC_LIB=1 SHARED_LIB=0 \
        EXAMPLES=0 OPENMPT123=0 TEST=0 \
        NO_ZLIB=1 NO_MPG123=1 NO_OGG=1 NO_VORBIS=1 NO_VORBISFILE=1 \
        NO_PORTAUDIO=1 NO_PORTAUDIOCPP=1 NO_PULSEAUDIO=1 \
        NO_SDL2=1 NO_FLAC=1 NO_SNDFILE=1 \
        -j"$(nproc)"
    make -C "$OPENMPT_BUILD_DIR" CONFIG=gcc \
        CC="$WEBOS_SDK/bin/arm-webos-linux-gnueabi-gcc" \
        CXX="$WEBOS_SDK/bin/arm-webos-linux-gnueabi-g++" \
        AR="$WEBOS_SDK/bin/arm-webos-linux-gnueabi-ar" \
        RANLIB="$WEBOS_SDK/bin/arm-webos-linux-gnueabi-ranlib" \
        STATIC_LIB=1 SHARED_LIB=0 \
        EXAMPLES=0 OPENMPT123=0 TEST=0 \
        PREFIX="$SYSROOT_USR" install
fi

CMAKE_EXTRA_ARGS=()
if [ -n "${PVZP_APP_VERSION:-}" ]; then
    CMAKE_EXTRA_ARGS+=(-DPVZP_APP_VERSION="$PVZP_APP_VERSION")
fi

cmake -B "$BUILD_DIR" \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
    -DCMAKE_BUILD_TYPE=Release \
    -DWEBOS=ON \
    "${CMAKE_EXTRA_ARGS[@]}"

cmake --build "$BUILD_DIR" --target pvz-portable -j"$(nproc)"

rm -rf "$APP_DIR"
cmake --install "$BUILD_DIR" --prefix "$APP_DIR" --component pvzportable

mkdir -p "$DIST_DIR"
ares-package "$APP_DIR" -o "$DIST_DIR"

IPK="$(find "$DIST_DIR" -maxdepth 1 -name 'io.github.wszqkzqk.pvzportable_*_arm.ipk' -newer "$APP_DIR/appinfo.json" 2>/dev/null | head -n1 || true)"
if [ -z "$IPK" ]; then
    IPK="$(find "$DIST_DIR" -maxdepth 1 -name 'io.github.wszqkzqk.pvzportable_*_arm.ipk' 2>/dev/null | sort | tail -n1 || true)"
fi

echo
echo "App root:     $APP_DIR"
echo "Package:      ${IPK:-$DIST_DIR (see above for the .ipk filename)}"
echo
echo "To install on a device: bash tools/webos/deploy-dev.sh \"${IPK:-<path-to-ipk>}\""
