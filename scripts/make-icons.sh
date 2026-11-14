#!/bin/bash
# Rebuilds the app icon files from resources/icon/icon.svg (macOS only: needs Homebrew
# Qt with QtSvg, clang++ and iconutil). Output: icon-1024.png, Smalto.icns, Smalto.ico.
# Edit icon.svg (square viewBox), run this, rebuild the app.
set -euo pipefail
cd "$(dirname "$0")/.."
SRC=resources/icon/icon.svg
OUT=resources/icon
QT=$(brew --prefix qt)
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT

clang++ -std=c++17 -fPIC -O1 scripts/render-svg.cpp -o "$T/render" \
  -F"$QT/lib" -framework QtCore -framework QtGui -framework QtSvg -Wl,-rpath,"$QT/lib"
mkdir "$T/Smalto.iconset" "$T/ico"

for p in "16 icon_16x16" "32 icon_16x16@2x" "32 icon_32x32" "64 icon_32x32@2x" \
         "128 icon_128x128" "256 icon_128x128@2x" "256 icon_256x256" \
         "512 icon_256x256@2x" "512 icon_512x512" "1024 icon_512x512@2x"; do
  set -- $p
  "$T/render" "$SRC" "$T/Smalto.iconset/$2.png" "$1"
done
cp "$T/Smalto.iconset/icon_512x512@2x.png" "$OUT/icon-1024.png"
iconutil -c icns "$T/Smalto.iconset" -o "$OUT/Smalto.icns"

for n in 16 32 48 64 128 256; do "$T/render" "$SRC" "$T/ico/$n.png" "$n"; done
python3 -I - "$T/ico" "$OUT/Smalto.ico" <<'PY'
import struct, sys
d, dst = sys.argv[1:]
sizes = [16, 32, 48, 64, 128, 256]
png = [open(f"{d}/{n}.png", "rb").read() for n in sizes]
out = struct.pack("<HHH", 0, 1, len(sizes)); off = 6 + 16 * len(sizes); body = b""
for n, p in zip(sizes, png):
    out += struct.pack("<BBBBHHII", n % 256, n % 256, 0, 0, 1, 32, len(p), off + len(body)); body += p
open(dst, "wb").write(out + body)
PY
echo "Icons rebuilt in $OUT"
