#!/usr/bin/env python3
"""Checks the manual's Markdown (manual/src): every relative link and image
points at a file that exists, every #anchor matches a heading on its page, every
page in SUMMARY.md exists, and every page is listed in SUMMARY.md. Exit status 1
on any problem (CI runs it).

    python3 scripts/check-manual.py
"""
import re
import sys
from pathlib import Path

src = Path(__file__).resolve().parent.parent / "manual/src"
problems = []


def slug(heading: str) -> str:
    """mdBook's heading id: lowercase, spaces to dashes, punctuation dropped."""
    text = re.sub(r"[`*_]|<[^>]+>", "", heading).strip().lower()
    text = re.sub(r"[^\w\- ]", "", text, flags=re.UNICODE)
    return text.replace(" ", "-")


def anchors(path: Path) -> set:
    out, seen = set(), {}
    in_code = False
    for line in path.read_text().splitlines():
        if line.startswith("```"):
            in_code = not in_code
        m = None if in_code else re.match(r"#{1,6}\s+(.*)", line)
        if m:
            s = slug(m.group(1))
            n = seen.get(s, 0)
            out.add(s if n == 0 else f"{s}-{n}")
            seen[s] = n + 1
    return out


pages = sorted(p for p in src.rglob("*.md") if p.name != "SUMMARY.md")
cache = {p: anchors(p) for p in pages}

for page in pages:
    text = page.read_text()
    for m in re.finditer(r"!?\[[^\]]*\]\(([^)\s]+)\)", text):
        target = m.group(1)
        if re.match(r"[a-z]+:", target):  # http:, mailto:
            continue
        path, _, frag = target.partition("#")
        dest = page if path == "" else (page.parent / path).resolve()
        if not dest.exists():
            problems.append(f"{page.relative_to(src)}: missing file {target}")
        elif frag and dest.suffix == ".md" and frag not in cache.get(dest, anchors(dest)):
            problems.append(f"{page.relative_to(src)}: no heading #{frag} in {dest.relative_to(src)}")

summary = (src / "SUMMARY.md").read_text()
listed = set()
for m in re.finditer(r"\]\(([^)]+\.md)\)", summary):
    dest = (src / m.group(1)).resolve()
    listed.add(dest)
    if not dest.exists():
        problems.append(f"SUMMARY.md: missing page {m.group(1)}")
for page in pages:
    if page.resolve() not in listed:
        problems.append(f"{page.relative_to(src)}: not listed in SUMMARY.md")

for p in problems:
    print("manual:", p, file=sys.stderr)
print(f"checked {len(pages)} pages, {len(problems)} problem(s)")
sys.exit(1 if problems else 0)
