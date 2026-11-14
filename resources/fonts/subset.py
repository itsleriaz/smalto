#!/usr/bin/env python3
"""Cuts the X11 misc-fixed BDF fonts down to Latin-1 (U+0020..U+007E,
U+00A0..U+00FF) for Smalto's text tool.

Source: https://www.cl.cam.ac.uk/~mgk25/download/ucs-fonts.tar.gz
(Markus Kuhn's Unicode misc-fixed fonts; "Public domain font. Share and enjoy.")

    python3 subset.py <unpacked ucs-fonts folder>   # writes 4x6.bdf, 5x7.bdf, 6x10.bdf here
"""
import os
import sys

FONTS = ["4x6", "5x7", "6x10"]
KEEP = set(range(0x20, 0x7F)) | set(range(0xA0, 0x100))


def subset(text):
    head, chars, out = [], [], []
    lines = text.splitlines()
    i = 0
    while i < len(lines) and not lines[i].startswith("STARTCHAR"):
        if not lines[i].startswith("CHARS "):
            head.append(lines[i])
        i += 1
    while i < len(lines):
        if lines[i].startswith("STARTCHAR"):
            j = i
            while not lines[j].startswith("ENDCHAR"):
                j += 1
            block = lines[i:j + 1]
            code = next(int(l.split()[1]) for l in block if l.startswith("ENCODING"))
            if code in KEEP:
                chars.append(block)
            i = j + 1
        else:
            i += 1
    out = head + ["CHARS %d" % len(chars)]
    for block in chars:
        out += block
    out.append("ENDFONT")
    return "\n".join(out) + "\n"


if __name__ == "__main__":
    src = sys.argv[1]
    here = os.path.dirname(os.path.abspath(__file__))
    for name in FONTS:
        with open(os.path.join(src, name + ".bdf"), encoding="latin-1") as f:
            text = f.read()
        with open(os.path.join(here, name + ".bdf"), "w", encoding="latin-1") as f:
            f.write(subset(text))
