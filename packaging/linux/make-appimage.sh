#!/usr/bin/env bash
# ---------------------------------------------------------------------------
#  Builds a self-contained UartX AppImage.
#
#  An AppImage bundles Qt and the other runtime libraries into one executable
#  file, so a user can download it from GitHub Releases, mark it executable and
#  run it -- no package manager, no Qt installation, no root.
#
#  Usage:  packaging/linux/make-appimage.sh [build-dir]   (default build/linux)
#
#  linuxdeploy and its Qt plugin are downloaded into build/tools on first use.
# ---------------------------------------------------------------------------
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${1:-$HERE/build/linux}"
APPDIR="$BUILD_DIR/AppDir"
TOOLS="$HERE/build/tools"

ARCH="$(uname -m)"
case "$ARCH" in
    x86_64)  LD_ARCH=x86_64 ;;
    aarch64) LD_ARCH=aarch64 ;;
    *) echo "ERROR: no linuxdeploy build for $ARCH" >&2; exit 1 ;;
esac

if [ ! -x "$BUILD_DIR/UartX" ]; then
    echo "ERROR: $BUILD_DIR/UartX not found - build first." >&2
    exit 1
fi

mkdir -p "$TOOLS"
fetch() {
    local name="$1" url="$2"
    if [ ! -x "$TOOLS/$name" ]; then
        echo "--- downloading $name ---"
        curl -fL --retry 3 -o "$TOOLS/$name" "$url"
        chmod +x "$TOOLS/$name"
    fi
}

BASE="https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous"
QT_BASE="https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous"
fetch "linuxdeploy-$LD_ARCH.AppImage"          "$BASE/linuxdeploy-$LD_ARCH.AppImage"
fetch "linuxdeploy-plugin-qt-$LD_ARCH.AppImage" "$QT_BASE/linuxdeploy-plugin-qt-$LD_ARCH.AppImage"

# Start from a clean install tree so stale files never leak into the image.
rm -rf "$APPDIR"
DESTDIR="$APPDIR" cmake --install "$BUILD_DIR" --prefix /usr

VERSION="$(sed -n 's/^project(UartX VERSION \([0-9.]*\).*/\1/p' "$HERE/CMakeLists.txt")"
export VERSION
export QMAKE="${QMAKE:-$(command -v qmake6 || command -v qmake)}"
# The AppImage runtime needs FUSE; extracting instead works on machines and CI
# runners that have none.
export APPIMAGE_EXTRACT_AND_RUN=1

cd "$BUILD_DIR"
"$TOOLS/linuxdeploy-$LD_ARCH.AppImage" \
    --appdir "$APPDIR" \
    --plugin qt \
    --output appimage

echo
echo "AppImage written to: $(ls -1 "$BUILD_DIR"/UartX*.AppImage 2>/dev/null | head -1)"
