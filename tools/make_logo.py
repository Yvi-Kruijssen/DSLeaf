#!/usr/bin/env python3
"""Draws the startup logo and writes data/logo.bin for the DS.

    python3 -I tools/make_logo.py data/logo.bin [preview.ppm]

Needs nothing but Python. The picture is drawn directly at 256x192, so every pixel
is deliberate: a chunky pixel art leaf, the name "DSLeaf" in the app's
own font (font8x8, public domain, thickened and enlarged) and the tagline.

The file is 64 colours of 2 bytes each (RGB555, little endian), then 256 x 192
bytes of colour numbers from 0 to 63, row by row, with the background as 0. The
app adds 128 to each number, because the first 64 palette entries belong to the
text themes.
"""
import math
import re
import sys
from pathlib import Path

W, H = 256, 192
U = 3  # one "big pixel" of the leaf and the name is U x U screen pixels

# The shape of the leaf, in cells of the big pixel grid (see draw_leaf).
LEAF = {
    "size": (29, 27),       # the area the leaf is drawn in
    "length": 28.5,         # from the base to the tip, along the midrib
    "width": 8.4,           # the widest half width
    "taper_base": 0.62,     # how round the base is (smaller is rounder)
    "taper_tip": 1.45,      # how long the taper to the tip is (bigger is sharper)
    "bend": 44.0,           # degrees the leaf turns between the base and the tip, clockwise (curling to the right)
    "heading": 77.0,        # the direction the leaf leaves the base, in degrees from the right, counter-clockwise
    "stem": 6.0,            # length of the stem
}

BG       = (24, 56, 48)       # the background
CREAM    = (248, 248, 232)    # the name
SHADOW   = (10, 28, 22)       # the shadow of the name
TAGLINE  = (160, 208, 88)
OUTLINE  = (12, 36, 28)       # the outline of the leaf
LEAF_LT  = (140, 204, 66)     # the lit side of the leaf
LEAF_MID = (92, 172, 62)      # the side in shade
LEAF_DK  = (50, 138, 54)      # its darker edge, and the stem
LEAF_RIB = (28, 96, 46)       # the midrib

def load_font8x8():
    """The 128 ASCII glyphs of font8x8: 8 bytes each, bit 0 is the leftmost pixel."""
    text = (Path(__file__).resolve().parent.parent / "third_party/font8x8/font8x8_basic.h").read_text()
    rows = re.findall(r"\{\s*((?:0x[0-9A-Fa-f]{2}\s*,\s*){7}0x[0-9A-Fa-f]{2})\s*\}", text)
    glyphs = [[int(v, 16) for v in r.split(",")] for r in rows]
    assert len(glyphs) == 128, len(glyphs)
    return glyphs

class Canvas:
    def __init__(self):
        self.px = [[BG] * W for _ in range(H)]

    def dot(self, x, y, colour):
        if 0 <= x < W and 0 <= y < H:
            self.px[y][x] = colour

    def block(self, cx, cy, colour, size=U):
        """A big pixel: cell (cx, cy) of the U grid, offset so the grid is centred."""
        for dy in range(size):
            for dx in range(size):
                self.dot(cx * size + dx, cy * size + dy, colour)

def bold_glyph(rows):
    """Thickens a font8x8 glyph by one pixel to the right, giving 9 columns."""
    out = []
    for r in rows:
        out.append(r | (r << 1))
    return out

def draw_name(c, text, top_cell, left_cell):
    glyphs = load_font8x8()
    x = left_cell
    for ch in text:
        g = bold_glyph(glyphs[ord(ch)])
        for y, bits in enumerate(g):
            for b in range(9):
                if (bits >> b) & 1:
                    c.block(x + b + 1, top_cell + y + 1, SHADOW)   # a dark shadow one cell down and right
        for y, bits in enumerate(g):
            for b in range(9):
                if (bits >> b) & 1:
                    c.block(x + b, top_cell + y, CREAM)
        x += 9 + 1
    return x - left_cell - 1  # width in cells

def draw_tagline(c, text, top, colour):
    glyphs = load_font8x8()
    width = len(text) * 8
    left = (W - width) // 2
    for i, ch in enumerate(text):
        for y, bits in enumerate(glyphs[ord(ch)]):
            for b in range(8):
                if (bits >> b) & 1:
                    c.dot(left + i * 8 + b, top + y, colour)

def draw_leaf(c, left_cell, top_cell):
    """A chunky pixel art leaf on the same grid of big pixels as the name.

    The leaf is symmetrical around its midrib, and the midrib is bent along a
    circular arc, so the stem (which continues the same arc backwards) always
    meets the middle of the base. The left side of the midrib is lit and the right
    side is in shade."""
    cw, ch = LEAF["size"]
    length, width = LEAF["length"], LEAF["width"]
    a_base, a_tip = LEAF["taper_base"], LEAF["taper_tip"]
    turn = math.radians(LEAF["bend"])
    radius = length / turn                               # of the arc the midrib follows
    heading = math.radians(LEAF["heading"])

    # Work with y pointing up, then flip into screen cells. The arc turns clockwise,
    # so its centre is to the right of the direction of travel.
    centre = (radius * math.sin(heading), -radius * math.cos(heading))   # the base is at (0, 0)
    phi_base = math.atan2(-centre[1], -centre[0])

    t_widest = a_base / (a_base + a_tip)
    f_widest = t_widest ** a_base * (1 - t_widest) ** a_tip

    def half_width(u):
        t = u / length
        if t <= 0.0 or t >= 1.0:
            return 0.0
        return width * (t ** a_base * (1 - t) ** a_tip) / f_widest

    def locate(px, py):
        """The distance along the midrib (u) and across it (v, positive to the left) of a point."""
        dx, dy = px - centre[0], py - centre[1]
        rho = math.hypot(dx, dy)
        phi = math.atan2(dy, dx)
        delta = (phi_base - phi + math.pi) % (2 * math.pi) - math.pi     # clockwise angle from the base
        return radius * delta, rho - radius

    # Find where the leaf lies on the grid, then move it to the middle of the area.
    raw = {}
    reach = int(length + 12)
    for gy in range(-reach, reach):
        for gx in range(-reach, reach):
            u, v = locate(gx + 0.5, gy + 0.5)
            hw = half_width(u)
            if 0.0 < u < length and abs(v) <= hw and (hw >= 0.7 or u < length * 0.5):   # no lone pixel at the tip
                raw[(gx, gy)] = (u, v, hw)
            elif -LEAF["stem"] <= u <= 0.5 and abs(v) <= 0.95:
                raw[(gx, gy)] = (u, v, -1.0)                           # the stem
    xs = [x for x, y in raw]
    ys = [-y for x, y in raw]                                       # screen rows count downwards
    shift_x = (cw - (max(xs) - min(xs) + 1)) // 2 - min(xs)
    shift_y = (ch - (max(ys) - min(ys) + 1)) // 2 - min(ys)

    colour = {}
    for (gx, gy), (u, v, hw) in raw.items():
        cell = (gx + shift_x, -gy + shift_y)
        if hw < 0:
            colour[cell] = LEAF_DK                                   # the stem
        elif abs(v) < 0.75 and 0.7 < u < length - 3:
            colour[cell] = LEAF_RIB
        elif v > 0:
            colour[cell] = LEAF_LT
        else:
            colour[cell] = LEAF_DK if hw - abs(v) < 1.7 else LEAF_MID

    # A one cell outline round everything.
    filled = set(colour)
    for (x, y) in filled:
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            if (x + dx, y + dy) not in filled:
                colour[(x + dx, y + dy)] = OUTLINE
    for (x, y), rgb in colour.items():
        c.block(left_cell + x, top_cell + y, rgb)

def build():
    c = Canvas()

    # The leaf, centred above the name.
    draw_leaf(c, (W // U - LEAF["size"][0]) // 2 + 1, 6)

    # The name: six letters of 9 cells with 1 cell between them.
    name = "DSLeaf"
    name_cells = len(name) * 10 - 1
    draw_name(c, name, 37, (W // U - name_cells) // 2)

    # The tagline.
    draw_tagline(c, "A better way to read on DS", 154, TAGLINE)
    return c

def write(c, path):
    palette = [BG]
    index = {BG: 0}
    out = bytearray()
    for row in c.px:
        for rgb in row:
            if rgb not in index:
                if len(palette) >= 64:
                    sys.exit("more than 64 colours")
                index[rgb] = len(palette)
                palette.append(rgb)
            out.append(index[rgb])
    pal = bytearray()
    for r, g, b in palette + [(0, 0, 0)] * (64 - len(palette)):
        pal += ((r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10)).to_bytes(2, "little")
    Path(path).write_bytes(bytes(pal) + bytes(out))
    return len(palette)

if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    canvas = build()
    n = write(canvas, sys.argv[1])
    print(f"wrote {sys.argv[1]}: {n} colours, {64 * 2 + W * H} bytes")
    if len(sys.argv) > 2:
        data = bytearray(b"P6\n%d %d\n255\n" % (W, H))
        for row in canvas.px:
            for rgb in row:
                data += bytes(rgb)
        Path(sys.argv[2]).write_bytes(bytes(data))
