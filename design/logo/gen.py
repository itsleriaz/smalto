# Generates Smalto logo concepts (black-first). Grid-built, no live text.
import os
D = os.path.dirname(os.path.abspath(__file__))

def svg(w, h, body, title):
    return f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {w} {h}" role="img"><title>{title}</title>{body}</svg>\n'

def tiles(cells, x0, y0, u, gap, r=0):
    out = []
    for (cx, cy) in cells:
        s = u - gap
        out.append(f'<rect x="{x0+cx*u+gap/2:g}" y="{y0+cy*u+gap/2:g}" width="{s:g}" height="{s:g}" rx="{r:g}"/>')
    return out

def parse(rows):
    return [(x, y) for y, row in enumerate(rows) for x, c in enumerate(row) if c == '#']

# ---- A: Tessera S — pixel S set as mosaic tiles; last tile just being placed
S_ROWS = [".####", "#....", ".###.", "....#", "####."]
def concept_a(lifted=True):
    u, gap = 44, 6
    cells = parse(S_ROWS)
    w, h = 5*u, 5*u
    x0, y0 = (256-w)/2, (256-h)/2
    body = []
    for c in cells:
        if lifted and c == (4, 0):
            # the tile being set: nudged up-right by half a gap
            body += tiles([c], x0+6, y0-6, u, gap, 3)
        else:
            body += tiles([c], x0, y0, u, gap, 3)
    return '<g fill="#111">' + ''.join(body) + '</g>'

# ---- B: Pixel rosette — a pixel-art circle of tesserae around a centre stone
B_ROWS = ["..##..", ".#..#.", "#....#", "#....#", ".#..#.", "..##.."]
def concept_b():
    u, gap = 40, 7
    x0 = y0 = (256-6*u)/2
    body = tiles(parse(B_ROWS), x0, y0, u, gap, 3)
    c = x0 + 2*u + gap/2  # centre stone spans 2x2 cells
    s = 2*u - gap
    body.append(f'<rect x="{c:g}" y="{c:g}" width="{s:g}" height="{s:g}" rx="4"/>')
    return '<g fill="#111">' + ''.join(body) + '</g>'

# ---- C: Glint — one tessera whose glass highlight is a pixel staircase
def concept_c():
    x0, y0, s, r = 24, 24, 208, 36
    # outer rounded square (clockwise)
    outer = (f'M{x0+r},{y0}H{x0+s-r}A{r},{r} 0 0 1 {x0+s},{y0+r}V{y0+s-r}'
             f'A{r},{r} 0 0 1 {x0+s-r},{y0+s}H{x0+r}A{r},{r} 0 0 1 {x0},{y0+s-r}V{y0+r}A{r},{r} 0 0 1 {x0+r},{y0}Z')
    # stepped glint (3 steps of 24) in the top-left, as a hole
    g, p = 24, 52
    # pixel-art rounded corner: two cells along the top, two down the side, corner left out
    glint = (f'M{p+g},{p}H{p+3*g}V{p+g}H{p+g}Z' f'M{p},{p+g}H{p+g}V{p+3*g}H{p}Z')
    dot = ''
    return f'<path fill="#111" fill-rule="evenodd" d="{outer}{glint}{dot}"/>'

# ---- Wordmark: lowercase 'smalto' built on a pixel grid (solid, no gaps)
GLYPHS = {
 's': ["....", "....", ".###", "#...", ".##.", "...#", "###."],
 'm': [".....", ".....", "####.", "#.#.#", "#.#.#", "#.#.#", "#.#.#"],
 'a': ["....", "....", ".##.", "...#", ".###", "#..#", ".###"],
 'l': ["#", "#", "#", "#", "#", "#", "#"],
 't': ["...", ".#.", "###", ".#.", ".#.", ".#.", "..#"],
 'o': ["....", "....", ".##.", "#..#", "#..#", "#..#", ".##."],
}
def wordmark(x0, y0, u, word="smalto", track=1):
    rects, x = [], x0
    for ch in word:
        g = GLYPHS[ch]
        for (cx, cy) in parse(g):
            rects.append(f'M{x+cx*u:g},{y0+cy*u:g}h{u:g}v{u:g}h{-u:g}Z')
        x += (len(g[0]) + track) * u
    return rects, x - track*u - x0

def lockup(symbol_body, name):
    u = 22
    rects, ww = wordmark(0, 0, u)
    gapx = 64
    w = 256 + gapx + ww
    y0 = (256 - 7*u)/2 + u  # optical: centre the x-height+ascender block a touch low
    body = f'<g>{symbol_body}</g><g fill="#111" transform="translate({256+gapx},{y0:g})"><path d="{"".join(rects)}"/></g>'
    return svg(w, 256, body, name)

for key, fn, nm in [("a", concept_a, "Smalto — Tessera S"),
                    ("b", concept_b, "Smalto — Pixel rosette"),
                    ("c", concept_c, "Smalto — Glint")]:
    b = fn()
    open(f"{D}/concept-{key}-symbol.svg", "w").write(svg(256, 256, b, nm))
    open(f"{D}/concept-{key}-lockup.svg", "w").write(lockup(b, nm + " lockup"))
print("ok")
