#!/usr/bin/env python3
"""Writes manual/src/reference/shortcuts.md from the default shortcut table in
app/Shortcuts.cpp, so the manual can't drift from the app.

    python3 scripts/manual-shortcuts.py
"""
import re
from pathlib import Path

root = Path(__file__).resolve().parent.parent
src = (root / "app/Shortcuts.cpp").read_text()

# Keys Qt supplies per platform: written here as the portable defaults.
standard = {
    "QKeySequence::Undo": ["Ctrl+Z"],
    "QKeySequence::Redo": ["Ctrl+Shift+Z"],
    "QKeySequence::Cut": ["Ctrl+X"],
    "QKeySequence::Copy": ["Ctrl+C"],
    "QKeySequence::Paste": ["Ctrl+V"],
    "QKeySequence::SelectAll": ["Ctrl+A"],
}

decl = re.search(r'const QString tools.*?;', src, re.S).group(0)
groups = dict(re.findall(r'(\w+) = tr\("([^"]+)"\)', decl))

rows = {}
order = []
for line in src.splitlines():
    m = re.match(r'\s*\{"(\w+)", (\w+), tr\("([^"]+)"\), (.*)\},\s*$', line)
    if not m:
        continue
    ident, group, name, keys = m.groups()
    ks = []
    for std in re.findall(r'standard\((QKeySequence::\w+)\)', keys):
        ks += standard.get(std, [])
    ks += re.findall(r'"([^"]+)"', keys) if "standard" not in keys else re.findall(r'\+ "([^"]+)"', keys)
    if ident == "nextTab":
        ks = ["Ctrl+Tab"]
    elif ident == "previousTab":
        ks = ["Ctrl+Shift+Tab"]
    label = groups.get(group, group)
    if label not in rows:
        rows[label] = []
        order.append(label)
    rows[label].append((name, ks))

def kbd(key):
    return " + ".join(f"<kbd>{p}</kbd>" for p in key.split("+")) if key not in ("+", "-", "=") else f"<kbd>{key}</kbd>"

out = ["# All shortcuts", "",
       "These are the defaults. Rebind them in **Preferences → Shortcuts** (see [Shortcuts](../customizing/shortcuts.md)). "
       "On a Mac, read <kbd>Ctrl</kbd> as <kbd>Cmd</kbd>. <kbd>Esc</kbd>, <kbd>Enter</kbd>, <kbd>Delete</kbd>, "
       "<kbd>Backspace</kbd>, <kbd>Space</kbd> and the arrows keep their own jobs and can't be rebound.", "",
       "> This page is generated from the app's shortcut table by `scripts/manual-shortcuts.py`. Don't edit it by hand.", ""]
for label in order:
    out += [f"## {label}", "", "| Action | Keys |", "|---|---|"]
    for name, ks in rows[label]:
        out.append(f"| {name} | " + (" or ".join(kbd(k) for k in ks) if ks else "*not bound*") + " |")
    out.append("")
out += ["## Mouse and gestures", "", "| Action | Input |", "|---|---|",
        "| Draw with the secondary color | Right mouse button |",
        "| Pan | <kbd>Space</kbd> + drag, middle-button drag, or a two-finger scroll |",
        "| Zoom | Wheel, pinch, or <kbd>+</kbd> / <kbd>-</kbd> |",
        "| Straight or square shapes | <kbd>Shift</kbd> while dragging a line, rectangle or ellipse (45° / square / circle) |",
        "| Shapes from the center | <kbd>Alt</kbd> while dragging |",
        "| Play, previous and next frame | <kbd>Enter</kbd>, <kbd>←</kbd>, <kbd>→</kbd> |",
        "| Apply a paste or transform | <kbd>Enter</kbd> (<kbd>Esc</kbd> cancels) |", ""]
(root / "manual/src/reference/shortcuts.md").write_text("\n".join(out))
print("wrote", sum(len(v) for v in rows.values()), "actions")
