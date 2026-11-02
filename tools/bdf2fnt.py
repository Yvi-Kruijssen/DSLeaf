#!/usr/bin/env python3
"""Converts a fixed-width BDF bitmap font (such as Terminus 8x16) to a DSLeaf .fnt file.

    python3 -I tools/bdf2fnt.py ter-u16n.bdf devsd/DSLeaf/Fonts/Terminus-8x16.fnt

The result has 256 glyphs, one for each Unicode code point U+0000 to U+00FF
(Latin-1), each up to 8 pixels wide and 16 rows high. Rows go from the top, bit 0
of a row byte is the leftmost pixel. An 8x8 or 8x16 font is written as a raw file
of 2048 or 4096 bytes. Any other size gets an 8 byte header ("DSLF", 1, width,
height, 0) in front of the glyph rows (see docs/fonts.md).

Fonts have their own licences. Terminus is under the SIL Open Font License, which asks that a converted copy
carries the copyright notice and the licence and does not use the name "Terminus Font" (see
third_party/terminus/README.md). A converted font put on your own SD card is yours to keep; the 6x12 one is
built into DSLeaf under those conditions (tools/fnt2c.py).
"""
import sys

def convert(bdf_path, out_path):
    width = height = None
    ascent = None
    glyphs = {}
    cur = None
    with open(bdf_path, encoding="latin-1") as f:
        lines = iter(f.read().splitlines())
    for line in lines:
        parts = line.split()
        if not parts:
            continue
        key = parts[0]
        if key == "FONTBOUNDINGBOX":
            width, height = int(parts[1]), int(parts[2])
        elif key == "FONT_ASCENT":
            ascent = int(parts[1])
        elif key == "STARTCHAR":
            cur = {"enc": -1, "bbx": None, "rows": []}
        elif key == "ENCODING" and cur is not None:
            cur["enc"] = int(parts[1])
        elif key == "BBX" and cur is not None:
            cur["bbx"] = tuple(int(x) for x in parts[1:5])
        elif key == "BITMAP" and cur is not None:
            for _ in range(cur["bbx"][1]):
                cur["rows"].append(next(lines).strip())
        elif key == "ENDCHAR" and cur is not None:
            if 0 <= cur["enc"] <= 255:
                glyphs[cur["enc"]] = cur
            cur = None

    if not (1 <= width <= 8 and 1 <= height <= 16):
        sys.exit(f"the font must be at most 8 wide and 16 high, found {width}x{height}")
    if ascent is None:
        ascent = height - height // 4

    out = bytearray()
    for code in range(256):
        cell = [0] * height
        g = glyphs.get(code)
        if g:
            w, h, xoff, yoff = g["bbx"]
            top = ascent - (yoff + h)  # row of the cell where the glyph's top row goes
            for r, hexrow in enumerate(g["rows"]):
                bits = int(hexrow, 16) << (8 * ((7 + 8) // 8) - 4 * len(hexrow)) if False else int(hexrow, 16)
                nbits = 4 * len(hexrow)
                y = top + r
                if not 0 <= y < height:
                    continue
                for x in range(w):
                    if (bits >> (nbits - 1 - x)) & 1:  # BDF puts the leftmost pixel in the top bit
                        px = xoff + x
                        if 0 <= px < width:
                            cell[y] |= 1 << px         # DSLeaf: bit 0 is the leftmost pixel
        out += bytes(cell)
    if not (width == 8 and height in (8, 16)):
        out = b"DSLF" + bytes([1, width, height, 0]) + out
    with open(out_path, "wb") as f:
        f.write(out)
    return width, height, len(glyphs), len(out)

if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    w, h, n, size = convert(sys.argv[1], sys.argv[2])
    print(f"{n} glyphs found in the font, wrote 256 glyphs of {w}x{h}, {size} bytes")
