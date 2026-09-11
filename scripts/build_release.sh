#!/usr/bin/env bash
# ---------------------------------------------------------------------------
#  UartX - one-shot Linux release build
#
#  Produces:
#    build/linux/UartX                                  the binary
#    build/linux/uartx_<ver>_<arch>.deb                 (with -p deb)
#    build/linux/uartx-<ver>-Linux.tar.gz               (with -p tgz)
#
#  Usage, from anywhere:
#    scripts/build_release.sh              build only
#    scripts/build_release.sh -p deb       build and produce a .deb
#    scripts/build_release.sh -p tgz       build and produce a .tar.gz
#    scripts/build_release.sh -p appimage  build and produce a portable AppImage
# ---------------------------------------------------------------------------
set -euo pipefail

# The script lives in scripts/; every path below is relative to the repository
# root, so step up one level.
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# Windows and Linux get separate trees. A CMake build directory is bound to
# the source path and toolchain that created it, so sharing one across a
# Windows checkout mounted in WSL fails with "does not match the source ...
# used to generate cache".
BUILD_DIR="$HERE/build/linux"
PACKAGE=""

while getopts ":p:h" opt; do
    case "$opt" in
        p) PACKAGE="$OPTARG" ;;
        h) sed -n '2,16p' "${BASH_SOURCE[0]}"; exit 0 ;;
        *) echo "unknown option: -$OPTARG" >&2; exit 2 ;;
    esac
done

need() {
    command -v "$1" >/dev/null 2>&1 || {
        echo "ERROR: '$1' is not installed." >&2
        echo "       See the Linux build section of README.md for the package list." >&2
        exit 1
    }
}
need cmake
need g++

GENERATOR=()
command -v ninja >/dev/null 2>&1 && GENERATOR=(-G Ninja)

# An existing cache is only reusable if it was made by this script, for this
# source tree, against the system Qt. Anything else is discarded: a cache is
# permanently bound to the paths it was configured with, and silently reusing
# a foreign one produces a package that fails at run time on other machines.
CACHE="$BUILD_DIR/CMakeCache.txt"
if [ -f "$CACHE" ]; then
    stale=""
    grep -q "^UartX_SOURCE_DIR:STATIC=$HERE\$" "$CACHE" \
        || stale="it was created for a different source directory"
    # This script never passes -DCMAKE_PREFIX_PATH, so a pinned one came from
    # somewhere else -- usually a hand-installed Qt, which would link the
    # binary against a Qt the target machines do not have.
    grep -qE "^CMAKE_PREFIX_PATH:[A-Za-z]+=." "$CACHE" \
        && stale="it pins a Qt outside the system packages"
    if [ -n "$stale" ]; then
        echo "Discarding the existing build directory: $stale."
        rm -rf "$BUILD_DIR"
    fi
fi

echo
echo "=== Configuring (Release) ==="
cmake -S "$HERE" -B "$BUILD_DIR" "${GENERATOR[@]}" -DCMAKE_BUILD_TYPE=Release

# Say which Qt was chosen. A package is only portable to machines that have at
# least this version, so building against the distribution's Qt is what makes
# the .deb installable on other systems.
QT_USED=$(sed -n 's/^Qt6_DIR:PATH=//p' "$BUILD_DIR/CMakeCache.txt")
QT_VERSION=$(sed -n 's/^Qt6Core_VERSION:[A-Za-z]*=//p' "$BUILD_DIR/CMakeCache.txt")
echo "Qt: ${QT_USED:-unknown} ${QT_VERSION:+($QT_VERSION)}"
case "$QT_USED" in
    /usr/*) ;;
    *)  echo
        echo "WARNING: this is not the distribution's Qt."
        echo "         The binary will demand this exact Qt version at run time and"
        echo "         will fail elsewhere with \"version \`Qt_X.Y' not found\"."
        echo "         For a package other machines can install, use the distro Qt:"
        echo "             sudo apt install qt6-base-dev"
        echo "             rm -rf \"$BUILD_DIR\" && ./build_release.sh -p deb"
        echo ;;
esac

echo
echo "=== Building ==="
cmake --build "$BUILD_DIR" --parallel "$(nproc)"

case "$PACKAGE" in
    "")   ;;
    deb)  echo; echo "=== Packaging (.deb) ==="
          ( cd "$BUILD_DIR" && cpack -G DEB ) ;;
    rpm)  echo; echo "=== Packaging (.rpm) ==="
          ( cd "$BUILD_DIR" && cpack -G RPM ) ;;
    tgz)  echo; echo "=== Packaging (.tar.gz) ==="
          ( cd "$BUILD_DIR" && cpack -G TGZ ) ;;
    appimage)
          echo; echo "=== Packaging (AppImage) ==="
          "$HERE/packaging/linux/make-appimage.sh" "$BUILD_DIR" ;;
    *)    echo "unknown package type: $PACKAGE (use deb, rpm, tgz or appimage)" >&2
          exit 2 ;;
esac

echo
echo "=== Done ==="
echo "Binary:   $BUILD_DIR/UartX"
[ -n "$PACKAGE" ] && ls -1 "$BUILD_DIR"/*.deb "$BUILD_DIR"/*.rpm "$BUILD_DIR"/*.tar.gz \
                            "$BUILD_DIR"/*.AppImage 2>/dev/null || true
echo
echo "Run it with:   $BUILD_DIR/UartX"
