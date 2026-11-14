#!/bin/bash
# Builds a self-contained Smalto AppImage (Linux x86_64 only). EXPERIMENTAL: never run
# on a real Linux desktop yet, see docs/known-issues.md.
#
#   scripts/package-linux.sh <qt-prefix>
#
# <qt-prefix> is an official Qt install, e.g. .qt/6.10.3/gcc_64 (aqtinstall:
# `aqt install-qt linux desktop 6.10.3 linux_gcc_64 -O .qt -m qtshadertools`).
# Build on the oldest distro you want to support: an AppImage only runs on systems
# at least as new as the glibc it was built against (CI uses Ubuntu 22.04).
#
# Needs: cmake, ninja, ImageMagick (convert), curl, and Qt's system libraries.
# Output: dist/Smalto-<version>-linux-x86_64.AppImage
set -euo pipefail
cd "$(dirname "$0")/.."

QT=${1:?usage: scripts/package-linux.sh <qt-prefix>}
QT=$(cd "$QT" && pwd)
BUILD=build/package-linux
DIST=dist
TOOLS=build/linuxdeploy
VERSION=$(sed -n 's/^project(Smalto VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
VERSION=${VERSION:-0.0.0}

cmake -S . -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$QT" -DSMALTO_BUILD_TESTS=OFF
cmake --build "$BUILD"

# linuxdeploy and its Qt plugin (AppImages themselves: run without FUSE).
mkdir -p "$TOOLS"
fetch() { [ -x "$TOOLS/$1" ] || { curl -fsSL -o "$TOOLS/$1" "$2" && chmod +x "$TOOLS/$1"; }; }
fetch linuxdeploy https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
fetch linuxdeploy-plugin-qt https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage
export APPIMAGE_EXTRACT_AND_RUN=1

rm -rf "$DIST" "$BUILD/AppDir"; mkdir -p "$DIST" "$BUILD/AppDir/usr/bin"
cp "$BUILD/smalto" "$BUILD/AppDir/usr/bin/smalto"
ICON="$BUILD/smalto.png"
convert resources/icon/icon-1024.png -resize 512x512 "$ICON"

# Smalto doesn't use SQL; these Qt plugins link to database client libraries that
# aren't installed and would stop linuxdeploy.
rm -rf "$QT/plugins/sqldrivers"

export QMAKE="$QT/bin/qmake"
export QML_SOURCES_PATHS="$PWD/qml"
export LD_LIBRARY_PATH="$QT/lib:${LD_LIBRARY_PATH:-}"
export EXTRA_PLATFORM_PLUGINS="libqwayland-generic.so;libqwayland-egl.so"
export NO_STRIP=1
export LDAI_OUTPUT="$DIST/Smalto-$VERSION-linux-x86_64.AppImage"
export LINUXDEPLOY_OUTPUT_VERSION="$VERSION"
PATH="$PWD/$TOOLS:$PATH" "$TOOLS/linuxdeploy" \
  --appdir "$BUILD/AppDir" \
  --executable "$BUILD/AppDir/usr/bin/smalto" \
  --desktop-file resources/linux/smalto.desktop \
  --icon-file "$ICON" --icon-filename smalto \
  --plugin qt \
  --output appimage
# The tool names the file itself; make it match.
mv -f Smalto*.AppImage "$LDAI_OUTPUT" 2>/dev/null || true
ls -l "$DIST"
