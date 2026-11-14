#!/bin/sh
# Regenerates the manual's screenshots (manual/src/images) from the app, so
# they follow the UI. Each shot uses a throwaway settings folder.
#
#   scripts/manual-screenshots.sh [sprite.aseprite] [build-dir]
#
# Defaults: tests/corpus/rgba_features.aseprite, build/release (build/debug if
# there is no release build). The window flashes on screen for each shot.
# Add a line to `shots` to add a picture (needs ImageMagick for crops).
set -e
cd "$(dirname "$0")/.."
file=${1:-tests/corpus/rgba_features.aseprite}
build=${2:-build/release}
[ -d "$build" ] || build=build/debug
if [ -d "$build/Smalto.app" ]; then
    exe="$build/Smalto.app/Contents/MacOS/Smalto"
else
    exe="$build/Smalto"
fi
out=manual/src/images
mkdir -p "$out"

# NAME|app args|crop (WxH+X+Y in the picture's own pixels, optional). The
# shots are made at 2x on a Retina screen, so crops are twice the points.
shots='window-tour|--size 1700x1050 --set preferences/panelStyle=1|
dialog-export|--size 1500x1000 --show export|960x1200+1030+400
dialog-unsaved|--size 1500x950 --show unsaved|1500x640+750+640
dialog-recovery|--size 1500x950 --show recovery|1500x600+750+680
dialog-preferences|--size 1500x950 --show preferences|1340x960+830+330
popup-dither|--size 1500x950 --show popup:dither|720x330+1250+270
popup-symmetry|--size 1500x950 --show popup:symmetry|900x470+1230+260
popup-pen|--size 1500x950 --show popup:pen|780x1010+1380+170'

echo "$shots" | while IFS='|' read -r name args crop; do
    # shellcheck disable=SC2086
    "$exe" --screenshot "$out/$name.png" --theme basic --mode light $args "$file"
    if [ -n "$crop" ]; then
        magick "$out/$name.png" -crop "$crop" +repage "$out/$name.png"
    fi
    echo "wrote $out/$name.png"
done
