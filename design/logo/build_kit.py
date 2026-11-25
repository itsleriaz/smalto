# Builds the Smalto logo kit (direction A, "Tessera S") into design/logo/kit/.
# Everything is constructed from grid cells: no live text, no effects.
import os

D = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(D, "kit")
os.makedirs(OUT, exist_ok=True)

WINE = "#900C3F"      # primary (carried over from the old icon)
CORAL = "#E84A5F"     # accent on light backgrounds: the tile being placed
CORAL_LT = "#FF847C"  # accent on dark/wine backgrounds
CREAM = "#FDF1E4"     # paper / reversed colour
INK = "#1E1418"       # neutral text

S_ROWS = [".####", "#....", ".###.", "....#", "####."]
LIFT = (4, 0)


def cells(rows):
    return [(x, y) for y, r in enumerate(rows) for x, c in enumerate(r) if c == "#"]


def svg(w, h, body, title, bg=None):
    b = f'<rect width="{w:g}" height="{h:g}" fill="{bg}"/>' if bg else ""
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {w:g} {h:g}" role="img">'
            f"<title>{title}</title>{b}{body}</svg>\n")


def rrect(x, y, s, r):
    """Rounded square as one path (clockwise)."""
    return (f"M{x+r:g},{y:g}H{x+s-r:g}A{r:g},{r:g} 0 0 1 {x+s:g},{y+r:g}V{y+s-r:g}"
            f"A{r:g},{r:g} 0 0 1 {x+s-r:g},{y+s:g}H{x+r:g}A{r:g},{r:g} 0 0 1 {x:g},{y+s-r:g}"
            f"V{y+r:g}A{r:g},{r:g} 0 0 1 {x+r:g},{y:g}Z")


def symbol_paths(x0, y0, u, gap, r, lift, fg, accent):
    """The Tessera S: 5x5 grid, 14 tiles; the last tile is nudged up-right (being placed)."""
    s = u - gap
    main, moving = [], ""
    for (cx, cy) in cells(S_ROWS):
        x, y = x0 + cx * u + gap / 2, y0 + cy * u + gap / 2
        if (cx, cy) == LIFT:
            moving = rrect(x + lift, y - lift, s, r)
        else:
            main.append(rrect(x, y, s, r))
    return (f'<path fill="{fg}" d="{"".join(main)}"/>'
            f'<path fill="{accent}" d="{moving}"/>')


# Master geometry on the 256 canvas. Grid unit 44, grout 6, corner radius 3, lift 6.
U, GAP, R, LIFTPX = 44, 6, 3, 6
# The lift makes the bbox lean up-right; shift the grid by half the lift to balance it.
SX = (256 - 5 * U) / 2 - LIFTPX / 2
SY = (256 - 5 * U) / 2 + LIFTPX / 2


def symbol(fg=WINE, accent=CORAL, scale=1.0, dx=0, dy=0, thin=0):
    # thin: shrink every tile by this many units (reversed artwork looks heavier)
    g = (f'<g transform="translate({dx:g},{dy:g}) scale({scale:g})">'
         + symbol_paths(SX, SY, U, GAP + thin, R, LIFTPX, fg, accent) + "</g>")
    return g


# Small-size cut: 32 grid, gapless, no lift, so 16/32 px rasters stay crisp.
def symbol_small(fg=WINE, accent=CORAL):
    u, x0, y0 = 6, 1, 1
    main, moving = [], ""
    for (cx, cy) in cells(S_ROWS):
        d = f"M{x0+cx*u},{y0+cy*u}h{u}v{u}h{-u}Z"
        if (cx, cy) == LIFT:
            moving = d
        else:
            main.append(d)
    return f'<path fill="{fg}" d="{"".join(main)}"/><path fill="{accent}" d="{moving}"/>'


# Wordmark: lowercase on a 7-row pixel grid (5-row x-height), solid, letter gap 1 cell.
GLYPHS = {
    "s": ["....", "....", ".###", "#...", ".##.", "...#", "###."],
    "m": [".....", ".....", "####.", "#.#.#", "#.#.#", "#.#.#", "#.#.#"],
    "a": ["....", "....", ".##.", "...#", ".###", "#..#", ".###"],
    "l": ["#", "#", "#", "#", "#", "#", "#"],
    "t": ["...", ".#.", "###", ".#.", ".#.", ".#.", "..#"],
    "o": ["....", "....", ".##.", "#..#", "#..#", "#..#", ".##."],
}


def wordmark(x0, y0, u, fill):
    d, x = [], x0
    for ch in "smalto":
        g = GLYPHS[ch]
        for cy, row in enumerate(g):          # one rectangle per horizontal run
            cx = 0
            while cx < len(row):
                if row[cx] == "#":
                    n = len(row[cx:]) - len(row[cx:].lstrip("#"))
                    d.append(f"M{x+cx*u:g},{y0+cy*u:g}h{n*u:g}v{u:g}h{-n*u:g}Z")
                    cx += n
                else:
                    cx += 1
        x += (len(g[0]) + 1) * u
    width = x - u - x0
    return f'<path fill="{fill}" d="{"".join(d)}"/>', width


WM_CELLS_W = 26  # 21 glyph cells + 5 letter gaps


def horizontal(fg, accent, word, bg=None, title="Smalto logo"):
    u = 22                       # wordmark cell = half a symbol tile pitch
    gapx = U                     # one tile pitch between symbol and wordmark
    wm_w = WM_CELLS_W * u
    w = 256 + gapx + wm_w
    # baseline of the wordmark sits on the bottom edge of the S's bottom row
    s_bottom = SY + 5 * U - GAP / 2
    y0 = s_bottom - 7 * u
    wm, _ = wordmark(256 + gapx, y0, u, word)
    if not bg:
        return svg(w, 256, symbol(fg, accent) + wm, title)
    p = U  # one tile pitch of padding around the artwork on a colour field
    return svg(w + 2 * p, 256 + 2 * p,
               f'<g transform="translate({p},{p})">' + symbol(fg, accent) + wm + "</g>", title, bg)


def stacked(fg, accent, word, bg=None, title="Smalto logo, stacked"):
    u = 14
    wm_w = WM_CELLS_W * u
    w = max(256, wm_w) + 0
    gapy = U
    h = 256 + gapy + 7 * u
    wm, _ = wordmark((w - wm_w) / 2, 256 + gapy - 15, u, word)
    return svg(w, h, symbol(fg, accent, dx=(w - 256) / 2) + wm, title, bg)


def wordmark_only(fill, title="Smalto wordmark"):
    u = 22
    wm, wdt = wordmark(0, 0, u, fill)
    return svg(wdt, 7 * u, wm, title)


def app_icon(size=128, fill=0.74, title="Smalto app icon"):
    # Full-bleed square, like the previous resources/icon/icon.svg. Mark at ~72 % of the canvas.
    k = size / 256 * fill
    off = (size - 256 * k) / 2
    body = symbol(CREAM, CORAL_LT, scale=k, dx=off, dy=off - size * 0.01, thin=1)
    return svg(size, size, body, title, bg=WINE)


files = {
    "smalto-symbol.svg": svg(256, 256, symbol(), "Smalto symbol"),
    "smalto-symbol-reversed.svg": svg(256, 256, symbol(CREAM, CORAL_LT, thin=1), "Smalto symbol, reversed"),
    "smalto-symbol-small.svg": svg(32, 32, symbol_small(), "Smalto symbol, small-size cut"),
    "smalto-horizontal.svg": horizontal(WINE, CORAL, WINE),
    "smalto-horizontal-reversed.svg": horizontal(CREAM, CORAL_LT, CREAM, title="Smalto logo, reversed"),
    "smalto-horizontal-on-wine.svg": horizontal(CREAM, CORAL_LT, CREAM, bg=WINE, title="Smalto logo on wine"),
    "smalto-stacked.svg": stacked(WINE, CORAL, WINE),
    "smalto-stacked-reversed.svg": stacked(CREAM, CORAL_LT, CREAM, title="Smalto logo, stacked, reversed"),
    "smalto-wordmark.svg": wordmark_only(WINE),
    "smalto-app-icon.svg": app_icon(),
    "smalto-symbol-on-tile.svg": app_icon(256),
    # maskable (PWA/Android): artwork kept inside the 80 % safe circle
    "smalto-app-icon-maskable.svg": app_icon(512, fill=0.56, title="Smalto app icon, maskable"),
}
for name, content in files.items():
    open(os.path.join(OUT, name), "w").write(content)
print("\n".join(files))
