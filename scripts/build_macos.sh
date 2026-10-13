#!/usr/bin/env bash
# ---------------------------------------------------------------------------
#  UartX - one-shot macOS release build
#
#  Produces:
#    build/macos/UartX.app                       the app, runnable in place
#    build/macos/UartX-<ver>-macos.dmg           (with -p dmg) drag-to-install
#                                                image, universal (Intel +
#                                                Apple Silicon), Qt bundled
#
#  Usage, from anywhere:
#    scripts/build_macos.sh              build only
#    scripts/build_macos.sh -p dmg       build and produce the .dmg
#
#  Qt is found from, in order: $QT_DIR, ~/Qt/<newest 6.x>/macos, Homebrew.
#  The .dmg is only universal when that Qt is -- the official Qt installer's
#  (and aqtinstall's) builds are; Homebrew's are not.
#
#  Set UARTX_CODESIGN_IDENTITY="Developer ID Application: ..." to sign with a
#  real certificate instead of ad hoc.
# ---------------------------------------------------------------------------
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$HERE/build/macos"
PACKAGE=""

while getopts ":p:h" opt; do
    case "$opt" in
        p) PACKAGE="$OPTARG" ;;
        h) sed -n '2,21p' "${BASH_SOURCE[0]}"; exit 0 ;;
        *) echo "unknown option: -$OPTARG" >&2; exit 2 ;;
    esac
done

[ "$(uname -s)" = "Darwin" ] || { echo "ERROR: this script builds the macOS app; run it on a Mac." >&2; exit 1; }
command -v cmake >/dev/null 2>&1 || { echo "ERROR: cmake is not installed (brew install cmake)." >&2; exit 1; }
xcrun --find clang++ >/dev/null 2>&1 || { echo "ERROR: no compiler. Run: xcode-select --install" >&2; exit 1; }

# --- find Qt ----------------------------------------------------------------
if [ -z "${QT_DIR:-}" ]; then
    newest=$(ls -d "$HOME"/Qt/6.*/macos 2>/dev/null | sort -V | tail -1 || true)
    if [ -n "$newest" ]; then
        QT_DIR="$newest"
    elif command -v brew >/dev/null 2>&1 && brew --prefix qt >/dev/null 2>&1; then
        QT_DIR="$(brew --prefix qt)"
    fi
fi
if [ -z "${QT_DIR:-}" ] || [ ! -d "$QT_DIR/lib/cmake/Qt6" ]; then
    echo "ERROR: Qt 6 not found. Install it with the Qt online installer, or:" >&2
    echo "    pip3 install aqtinstall && aqt install-qt mac desktop 6.8.3 clang_64 -O ~/Qt" >&2
    echo "or point QT_DIR at a Qt 6 macOS prefix." >&2
    exit 1
fi
echo "Qt: $QT_DIR"

GENERATOR=()
command -v ninja >/dev/null 2>&1 && GENERATOR=(-G Ninja)

echo
echo "=== Configuring (Release) ==="
cmake -S "$HERE" -B "$BUILD_DIR" "${GENERATOR[@]}" \
      -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$QT_DIR"

echo
echo "=== Building ==="
cmake --build "$BUILD_DIR" --parallel "$(sysctl -n hw.ncpu)"

case "$PACKAGE" in
    "")  ;;
    dmg) echo; cmake --build "$BUILD_DIR" --target dmg ;;
    *)   echo "unknown package type: $PACKAGE (use dmg)" >&2; exit 2 ;;
esac

echo
echo "=== Done ==="
echo "App:      $BUILD_DIR/UartX.app   ($(lipo -archs "$BUILD_DIR/UartX.app/Contents/MacOS/UartX"))"
[ -n "$PACKAGE" ] && ls -1 "$BUILD_DIR"/*.dmg 2>/dev/null || true
echo
echo "Run it with:   open \"$BUILD_DIR/UartX.app\""
