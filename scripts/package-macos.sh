#!/bin/bash
# Builds an unsigned, self-contained Smalto.app and a .dmg (macOS only).
#
#   scripts/package-macos.sh [qt-prefix]
#
# Output: dist/Smalto-<version>-macos-<arch>.dmg (and the app next to it).
# Qt comes from `brew --prefix qt` unless a prefix is given (e.g. an official Qt
# install such as ~/Qt/6.10.3/macos, which is universal and goes down to macOS 12).
# The deployment target follows the Qt build: Homebrew's Qt 6.12 is arm64 only
# and needs macOS 14.4. The app is ad-hoc signed (needed to run on Apple Silicon)
# but not signed with a Developer ID and not notarized: see docs/backlog.md.
set -euo pipefail
cd "$(dirname "$0")/.."

QT=${1:-$(brew --prefix qt)}
BUILD=build/package
DIST=dist
VERSION=$(sed -n 's/^project(Smalto VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
VERSION=${VERSION:-0.0.0}

# The oldest macOS the Qt libraries support.
MINOS=$(vtool -show-build "$QT/lib/QtCore.framework/QtCore" | awk '/minos/ {print $2; exit}')
ARCHS=$(lipo -archs "$QT/lib/QtCore.framework/QtCore" | tr ' ' ';')
echo "Qt: $QT (macOS $MINOS+, $ARCHS)"

cmake -S . -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$QT" \
  -DCMAKE_OSX_DEPLOYMENT_TARGET="$MINOS" \
  -DCMAKE_OSX_ARCHITECTURES="$ARCHS" \
  -DSMALTO_BUILD_TESTS=OFF
cmake --build "$BUILD"

rm -rf "$DIST"; mkdir -p "$DIST"
cp -R "$BUILD/Smalto.app" "$DIST/Smalto.app"
"$QT/bin/macdeployqt" "$DIST/Smalto.app" -qmldir=qml
# Smalto doesn't use SQL; these plugins link to database client libraries that
# aren't on a user's Mac.
rm -rf "$DIST/Smalto.app/Contents/PlugIns/sqldrivers"

# Every binary inside must run on the macOS the package claims. Homebrew builds some
# Qt libraries (Qt Quick) for the newest macOS, which would make the package
# unusable on older systems: a Qt from the official installer avoids that.
NEWEST=$MINOS
while IFS= read -r f; do
  file "$f" | grep -q Mach-O || continue
  v=$(vtool -show-build "$f" 2>/dev/null | awk '/minos/ {print $2; exit}')
  [ -n "$v" ] && [ "$(printf '%s\n%s\n' "$NEWEST" "$v" | sort -V | tail -1)" = "$v" ] && NEWEST=$v
done < <(find "$DIST/Smalto.app" -type f)
if [ "$NEWEST" != "$MINOS" ]; then
  echo "WARNING: some bundled binaries need macOS $NEWEST (Qt itself allows $MINOS)." >&2
  echo "         This package will only run on macOS $NEWEST or later. Use a Qt from the" >&2
  echo "         official installer (scripts/package-macos.sh ~/Qt/<version>/macos)." >&2
  MINOS=$NEWEST
fi

# macdeployqt re-signs ad hoc; make sure the whole bundle is consistent.
codesign --force --deep --sign - "$DIST/Smalto.app"

ARCH_NAME=$(echo "$ARCHS" | tr ';' '-')
DMG="$DIST/Smalto-$VERSION-macos$MINOS+-$ARCH_NAME.dmg"
STAGE=$(mktemp -d); trap 'rm -rf "$STAGE"' EXIT
cp -R "$DIST/Smalto.app" "$STAGE/"
cp LICENSE README.md "$STAGE/"
ln -s /Applications "$STAGE/Applications"
hdiutil create -quiet -volname "Smalto $VERSION" -srcfolder "$STAGE" -ov -format UDZO "$DMG"
echo "Wrote $DMG ($(du -h "$DMG" | cut -f1))"
