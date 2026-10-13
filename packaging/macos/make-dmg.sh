#!/usr/bin/env bash
# ---------------------------------------------------------------------------
#  UartX - macOS disk image
#
#  Turns a configured, built tree into a drag-to-install .dmg:
#
#    packaging/macos/make-dmg.sh <build-dir>
#
#  1. `cmake --install` stages UartX.app; the install step runs macdeployqt,
#     which copies the Qt frameworks and plugins into the bundle so the app
#     needs no Qt on the target Mac.
#  2. The bundle is signed: with $UARTX_CODESIGN_IDENTITY when it is set
#     (a "Developer ID Application: ..." identity), ad hoc otherwise.
#     Apple Silicon refuses to run code with no signature at all, and
#     macdeployqt's install_name_tool edits break the linker's own signature,
#     so this step is not optional even for an unsigned release.
#  3. The bundle and an /Applications shortcut go into a compressed image.
#
#  Output: <build-dir>/UartX-<version>-macos.dmg
# ---------------------------------------------------------------------------
set -euo pipefail

if [ $# -ne 1 ]; then
    echo "usage: $0 <build-dir>" >&2
    exit 2
fi

# Absolute, so a relative argument still works after any cd below.
BUILD_DIR="$(cd "$1" && pwd)"
CACHE="$BUILD_DIR/CMakeCache.txt"
[ -f "$CACHE" ] || { echo "ERROR: $BUILD_DIR is not a configured CMake build." >&2; exit 1; }

VERSION=$(sed -n 's/^CMAKE_PROJECT_VERSION:STATIC=//p' "$CACHE")
[ -n "$VERSION" ] || { echo "ERROR: could not read the project version from $CACHE" >&2; exit 1; }

STAGE="$BUILD_DIR/dmg-stage"
APP="$STAGE/UartX.app"
DMG="$BUILD_DIR/UartX-$VERSION-macos.dmg"

echo "=== Staging UartX.app (macdeployqt) ==="
rm -rf "$STAGE"
cmake --install "$BUILD_DIR" --prefix "$STAGE"
[ -d "$APP" ] || { echo "ERROR: the install step produced no $APP" >&2; exit 1; }

# A bundle that still reaches for a Qt outside itself works on this machine
# and nowhere else. Every load command has to point inside the bundle or at
# the operating system.
echo "=== Checking the bundle is self-contained ==="
leaks=$(find "$APP/Contents" -type f \( -perm -u+x -o -name '*.dylib' \) -print0 \
        | xargs -0 otool -L 2>/dev/null \
        | awk '/^\t/ {print $1}' \
        | grep -vE '^(@rpath|@executable_path|@loader_path|/System/Library/|/usr/lib/)' \
        | sort -u || true)
if [ -n "$leaks" ]; then
    echo "ERROR: the bundle links against libraries it does not carry:" >&2
    echo "$leaks" >&2
    exit 1
fi
echo "ok"

echo "=== Signing ==="
IDENTITY="${UARTX_CODESIGN_IDENTITY:--}"
if [ "$IDENTITY" = "-" ]; then
    echo "ad hoc (set UARTX_CODESIGN_IDENTITY to sign with a Developer ID)"
    codesign --force --deep --sign - "$APP"
else
    echo "with: $IDENTITY"
    codesign --force --deep --options runtime --timestamp --sign "$IDENTITY" "$APP"
fi
codesign --verify --deep --strict "$APP"

echo "=== Building the disk image ==="
ln -s /Applications "$STAGE/Applications"
rm -f "$DMG"
hdiutil create -volname "UartX $VERSION" -srcfolder "$STAGE" \
        -fs HFS+ -format UDZO -imagekey zlib-level=9 -ov "$DMG" >/dev/null

echo
echo "Built: $DMG"
