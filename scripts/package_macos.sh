#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
if [ "$(uname -s)" != Darwin ]; then
    echo "error: macOS packaging must run on a Mac." >&2
    exit 1
fi
if ! diskutil image create from --help >/dev/null 2>&1 || \
   ! diskutil image attach --help >/dev/null 2>&1; then
    echo "error: macOS packaging requires a system with diskutil image create/attach; upgrade macOS." >&2
    exit 1
fi
QT_ROOT=${QT_ROOT:-}
if [ -z "$QT_ROOT" ]; then
    for candidate in "$HOME"/Qt/*/macos; do
        if [ -x "$candidate/bin/macdeployqt" ]; then
            QT_ROOT=$candidate
        fi
    done
fi
if [ -z "$QT_ROOT" ] && command -v brew >/dev/null 2>&1; then
    QT_ROOT=$(brew --prefix qt 2>/dev/null || true)
fi
if [ ! -x "$QT_ROOT/bin/macdeployqt" ]; then
    echo "error: Qt6 not found; set QT_ROOT to Qt's macos directory." >&2
    exit 1
fi
if [ ! -f "$QT_ROOT/lib/cmake/Qt6SerialPort/Qt6SerialPortConfig.cmake" ]; then
    echo "error: install the SerialPort module for the current Qt." >&2
    exit 1
fi
BUILD_DIR=${1:-"$ROOT/build-macos"}
cmake -S "$ROOT" -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_PREFIX_PATH="$QT_ROOT" \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_BUILD_TYPE=Release \
    -DUPGRADE_TOOL_TESTS=ON
cmake --build "$BUILD_DIR" --parallel
ctest --test-dir "$BUILD_DIR" --output-on-failure
BUILD_DIR=$(CDPATH= cd -- "$BUILD_DIR" && pwd)
VERSION=$(sed -n 's/.*TOOL_VERSION = "\([0-9.]*\)";.*/\1/p' "$ROOT/application/Version.h")
[ -n "$VERSION" ] || exit 1
OUTPUT_DIR="$BUILD_DIR/dist"
mkdir -p "$OUTPUT_DIR"
STAGE=$(mktemp -d "$BUILD_DIR/package.XXXXXX")
MOUNT_DIR="$STAGE/mount"
MOUNTED=0
cleanup() {
    if [ "$MOUNTED" = 1 ]; then
        if ! diskutil eject "$MOUNT_DIR"; then
            if ! diskutil eject force "$MOUNT_DIR"; then
                echo "error: could not unmount ${MOUNT_DIR}; keeping temporary files for manual cleanup." >&2
                return
            fi
        fi
    fi
    rm -rf "$STAGE"
}
trap cleanup EXIT
trap 'exit 1' HUP INT TERM
CONTENTS="$STAGE/contents"
mkdir -p "$CONTENTS"
APP="$CONTENTS/UpgradeTool-GUI.app"
ditto "$BUILD_DIR/upgrade_tool_gui.app" "$APP"
ADB_DIR="$APP/Contents/Helpers/adb"
cmake -DADB_PLATFORM=darwin "-DADB_CACHE_DIR=$BUILD_DIR/adb-cache" \
    "-DADB_DESTINATION=$ADB_DIR" -P "$ROOT/scripts/prepare_adb.cmake"
chmod +x "$ADB_DIR/adb"
lipo -verify_arch arm64 "$ADB_DIR/adb"
ADB_DEPENDENCIES=$(otool -arch arm64 -L "$ADB_DIR/adb")
printf '%s\n' "$ADB_DEPENDENCIES" | tail -n +2 | awk '{print $1}' | while IFS= read -r dependency; do
    case "$dependency" in
        /usr/lib/*|/System/Library/*) ;;
        *) echo "error: unexpected ADB dependency: $dependency" >&2; exit 1 ;;
    esac
done
"$ADB_DIR/adb" version
mkdir -p "$APP/Contents/Resources/licenses"
cp "$ROOT/backends/rockchip/legacy/license.txt" "$APP/Contents/Resources/licenses/upgrade-tool-GPL-2.0.txt"
cp "$ROOT/third_party/libusb/COPYING" "$APP/Contents/Resources/licenses/libusb-LGPL-2.1.txt"
cp "$ROOT/third_party/libefex/LICENSE" "$APP/Contents/Resources/licenses/libefex-MIT.txt"
cp "$ROOT/backends/allwinner/LICENSE.OpenixCLI" "$APP/Contents/Resources/licenses/OpenixCLI-MIT.txt"
cp "$ROOT/third_party/qdl/LICENSE" "$APP/Contents/Resources/licenses/qdl-BSD-3-Clause.txt"
if [ -d "$QT_ROOT/../../Licenses" ]; then
    ditto "$QT_ROOT/../../Licenses" "$APP/Contents/Resources/licenses/Qt"
fi
# Deploy only the platform and SVG plugins used by this application. Qt's
# default deployment also collects SQL drivers with external database dependencies.
set -- "$APP" "-qmldir=$ROOT/gui/qml" -always-overwrite -no-plugins -codesign=-
set -- "$@" "-executable=$ADB_DIR/adb"
for plugin in platforms/libqcocoa.dylib platforms/libqoffscreen.dylib \
              iconengines/libqsvgicon.dylib imageformats/libqsvg.dylib; do
    mkdir -p "$APP/Contents/PlugIns/$(dirname "$plugin")"
    cp "$QT_ROOT/plugins/$plugin" "$APP/Contents/PlugIns/$plugin"
    set -- "$@" "-executable=$APP/Contents/PlugIns/$plugin"
done
cat > "$APP/Contents/Resources/qt.conf" <<'QTCONF'
[Paths]
Plugins = PlugIns
Imports = Resources/qml
QmlImports = Resources/qml
QTCONF
"$QT_ROOT/bin/macdeployqt" "$@"
codesign --force --sign - "$ADB_DIR/adb"
codesign --force --sign - "$APP"
"$ADB_DIR/adb" version
codesign --verify --deep --strict "$APP"
ln -s /Applications "$CONTENTS/Applications"
# Keep a standalone application as well as the disk image.
ditto "$APP" "$OUTPUT_DIR/UpgradeTool-GUI.app"
DMG="$OUTPUT_DIR/UpgradeTool-GUI-$VERSION-macos-arm64.dmg"
mkdir -p "$CONTENTS/.background"
"$BUILD_DIR/upgrade_tool_icon_renderer" --dmg-background "$CONTENTS/.background/background.png"
# Customize a writable image before compressing it, so Finder's layout persists.
WRITABLE_DMG="$STAGE/layout.dmg"
diskutil image create from --format RAW --volumeName "UpgradeTool-GUI" \
    "$CONTENTS" "$WRITABLE_DMG"
mkdir -p "$MOUNT_DIR"
diskutil image attach --mountPoint "$MOUNT_DIR" "$WRITABLE_DMG"
MOUNTED=1
layout_attempt=1
while [ "$layout_attempt" -le 3 ]; do
    echo "Saving Finder DMG layout (attempt $layout_attempt/3)..."
    if ! osascript "$ROOT/scripts/macos/layout_dmg.applescript" "$MOUNT_DIR"; then
        echo "error: could not set the DMG layout at $MOUNT_DIR (attempt $layout_attempt/3); check Finder automation permissions. Final DMG was not generated." >&2
        exit 1
    fi
    layout_wait=0
    while [ ! -s "$MOUNT_DIR/.DS_Store" ] && [ "$layout_wait" -lt 10 ]; do
        sleep 1
        layout_wait=$((layout_wait + 1))
    done
    if [ -s "$MOUNT_DIR/.DS_Store" ]; then
        break
    fi
    layout_attempt=$((layout_attempt + 1))
done
if [ ! -s "$MOUNT_DIR/.DS_Store" ]; then
    echo "error: Finder did not save the DMG layout at $MOUNT_DIR after 3 attempts. Final DMG was not generated." >&2
    exit 1
fi
sync
diskutil eject "$MOUNT_DIR"
MOUNTED=0
diskutil image create from --format UDZO "$WRITABLE_DMG" "$STAGE/final.dmg"
mv -f "$STAGE/final.dmg" "$DMG"
echo "Application: $OUTPUT_DIR/UpgradeTool-GUI.app"
echo "Disk image: $DMG"
