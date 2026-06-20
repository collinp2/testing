#!/usr/bin/env bash
# ============================================================================
#  Build a self-contained macOS installer (.pkg) for NECRONAM.
#  Packages the VST3 + AU (and optionally the Standalone app) into a single
#  double-clickable installer that drops them into the system plug-in folders.
#
#  Prereqs: Xcode command-line tools (pkgbuild/productbuild ship with them).
#  Build the plugin first:  cmake -B build -DCMAKE_BUILD_TYPE=Release && \
#                           cmake --build build --config Release
#
#  Usage:
#    packaging/macos/build_installer.sh
#
#  Optional env vars:
#    VERSION=1.0.0                 # installer version string
#    BUILD_DIR=build               # where the artefacts are
#    INCLUDE_STANDALONE=1          # also install the Standalone .app to /Applications
#    INSTALLER_SIGN_ID="Developer ID Installer: Name (TEAMID)"   # sign the pkg
# ============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"            # the NECRONAM project dir

VERSION="${VERSION:-1.0.0}"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
ART="$BUILD_DIR/NECRONAM_artefacts/Release"
OUT="$ROOT/dist"
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT

VST3="$ART/VST3/NECRONAM.vst3"
AU="$ART/AU/NECRONAM.component"
APP="$ART/Standalone/NECRONAM.app"

if [[ ! -d "$VST3" ]]; then
    echo "ERROR: $VST3 not found. Build first:" >&2
    echo "  cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --config Release" >&2
    exit 1
fi

mkdir -p "$OUT"
mkdir -p "$STAGE/Library/Audio/Plug-Ins/VST3"
mkdir -p "$STAGE/Library/Audio/Plug-Ins/Components"

echo "Staging VST3..."
cp -R "$VST3" "$STAGE/Library/Audio/Plug-Ins/VST3/"
if [[ -d "$AU" ]]; then
    echo "Staging AU..."
    cp -R "$AU" "$STAGE/Library/Audio/Plug-Ins/Components/"
fi
if [[ "${INCLUDE_STANDALONE:-0}" == "1" && -d "$APP" ]]; then
    echo "Staging Standalone app..."
    mkdir -p "$STAGE/Applications"
    cp -R "$APP" "$STAGE/Applications/"
fi

PKG="$OUT/NECRONAM-$VERSION-macOS.pkg"
ARGS=(--root "$STAGE"
      --identifier com.cpsoftware.necronam.pkg
      --version "$VERSION"
      --install-location /)
if [[ -n "${INSTALLER_SIGN_ID:-}" ]]; then
    ARGS+=(--sign "$INSTALLER_SIGN_ID")
fi

echo "Building installer..."
pkgbuild "${ARGS[@]}" "$PKG"

echo
echo "Done: $PKG"
if [[ -z "${INSTALLER_SIGN_ID:-}" ]]; then
    echo "NOTE: unsigned. For distribution, sign with a Developer ID Installer"
    echo "      identity (INSTALLER_SIGN_ID=...) and notarize (xcrun notarytool)."
fi
