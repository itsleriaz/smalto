#!/bin/sh
# Builds the manual (manual/src, Markdown) into manual/book (plain HTML) with
# mdBook, after regenerating the shortcut table from the app's own defaults.
# The packaging scripts bundle manual/book when it exists.
#
#   scripts/build-manual.sh
#
# Needs mdBook 0.5+ (`brew install mdbook`, or `cargo install mdbook`). Without
# it this prints a note and exits 0, so a package can still be made (without the manual).
set -e
cd "$(dirname "$0")/.."
if ! command -v mdbook >/dev/null 2>&1; then
    echo "mdbook not found: the manual isn't built (brew install mdbook)." >&2
    exit 0
fi
python3 scripts/manual-shortcuts.py
python3 scripts/check-manual.py
mdbook build manual
