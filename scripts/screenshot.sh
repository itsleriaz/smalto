#!/bin/sh
# Renders the app's window into PNGs, one per theme and mode, without touching
# your settings or recovery copies.
#
#   scripts/screenshot.sh [file.aseprite] [out-dir] [build-dir]
#
# Defaults: tests/corpus/rgba_features.aseprite, ./screenshots, build/release.
# The window flashes on screen for a moment per shot (the canvas needs the GPU;
# with QT_QPA_PLATFORM=offscreen the chrome renders but the canvas stays empty).
set -e
cd "$(dirname "$0")/.."
file=${1:-tests/corpus/rgba_features.aseprite}
out=${2:-screenshots}
build=${3:-build/release}
if [ -d "$build/Smalto.app" ]; then
    exe="$build/Smalto.app/Contents/MacOS/Smalto"
else
    exe="$build/Smalto"
fi
mkdir -p "$out"
for theme in basic everforest tokyonight catppuccin gruvbox; do
    for mode in dark light; do
        "$exe" --screenshot "$out/$theme-$mode.png" --theme "$theme" --mode "$mode" "$file"
    done
done
echo "Wrote $out/*.png"
