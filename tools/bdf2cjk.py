#!/usr/bin/env python3
"""Converts BDF or PCF bitmap fonts with Chinese, Japanese and Korean characters to a DSLeaf CJK font file (.dslc, decision 092).

    python3 -I tools/bdf2cjk.py first.bdf [second.bdf ...] devsd/DSLeaf/Fonts/cjk.dslc [--size 12]
    python3 -I tools/bdf2cjk.py wenquanyi_9pt.pcf unifont.bdf devsd/DSLeaf/Fonts/cjk.dslc
        (a PCF font works too; with several fonts the first one that has a character gives it, so a 12 pixel font
         with the ideographs and kana comes first and Unifont fills in what it lacks, such as Hangul)
    python3 -I tools/bdf2cjk.py Galmuri11.bdf@AC00-D7AF,3130-318F,1100-11FF wenquanyi_9pt.pcf unifont.bdf cjk.dslc
        (font@ranges takes only those characters (hex) from a font: here Korean from a Korean font, the rest from the others)

Only the wide characters are kept (the ranges of source/text.c: CJK ideographs, kana, Hangul, CJK punctuation and symbols,
fullwidth signs). The glyphs go in a cell of --size x --size pixels (default 12, the height of the built-in 6x12 font:
a CJK glyph is drawn two cells wide). A BDF font with larger glyphs (Unifont is 16x16) is scaled down by averaging, which
costs sharpness: a font that is made at 12 pixels (such as a 12 pixel WenQuanYi or Shinonome BDF) is much better.

File format (all numbers little endian):
    "DSLC", version 1, width, height, 0, range count (2 bytes), 6 reserved bytes   (16 bytes)
    for each range: first code point (2 bytes), number of glyphs (2 bytes)
    then the glyphs of the ranges one after another, each `height` rows of (width + 7) // 8 bytes, rows from the top,
    bit 0 of the first byte is the leftmost pixel. A code point in a range that the font does not have is a blank glyph.
Fonts have their own licences: check the licence of the font you convert before giving the result to anyone.
"""
import gzip
import struct
import sys

WIDE = [(0x1100, 0x115F), (0x2E80, 0x303E), (0x3041, 0x33FF), (0x3400, 0x4DBF), (0x4E00, 0x9FFF), (0xA960, 0xA97F),
        (0xAC00, 0xD7A3), (0xF900, 0xFAFF), (0xFE30, 0xFE6F), (0xFFE0, 0xFFE6)]
MAX_GAP = 8   # missing code points between two glyphs that are still kept in one range (as blank glyphs)

def is_wide(cp):
    return any(a <= cp <= b for a, b in WIDE)

def parse_bdf(path):
    """{code point: ((width, height, xoff, yoff), rows)}, rows being lists of 0/1, and the font's ascent."""
    glyphs, enc, bbx, rows, mode, ascent = {}, None, None, [], None, None
    for line in open(path, encoding="latin-1"):
        p = line.split()
        if not p:
            continue
        if p[0] == "FONT_ASCENT":
            ascent = int(p[1])
        elif p[0] == "ENCODING":
            enc = int(p[1])
        elif p[0] == "BBX":
            bbx = tuple(int(x) for x in p[1:5])
        elif p[0] == "BITMAP":
            rows, mode = [], "bitmap"
        elif p[0] == "ENDCHAR":
            mode = None
            if enc is not None and enc >= 0 and is_wide(enc) and bbx:
                glyphs[enc] = (bbx, rows)
        elif mode == "bitmap":
            nbits = len(p[0]) * 4
            bits = int(p[0], 16)
            rows.append([(bits >> (nbits - 1 - x)) & 1 for x in range(bbx[0])])
    return glyphs, ascent

def parse_pcf(path):
    """The same from a PCF font (X11 bitmap font), plain or gzipped."""
    raw = open(path, "rb").read()
    if raw[:2] == b"\x1f\x8b":
        raw = gzip.decompress(raw)
    if raw[:4] != b"\x01fcp":
        sys.exit("not a PCF font")
    tables = {}
    for i in range(struct.unpack_from("<I", raw, 4)[0]):
        t, fmt, size, off = struct.unpack_from("<IIII", raw, 8 + 16 * i)
        tables[t] = (fmt, off)
    def endian(fmt):
        return ">" if fmt & 4 else "<"
    # metrics
    fmt, off = tables[4]
    e = endian(fmt)
    metrics = []
    if fmt & 0x100:      # compressed metrics: 5 bytes each
        n = struct.unpack_from(e + "H", raw, off + 4)[0]
        for i in range(n):
            l, r, w, a, d = (x - 128 for x in raw[off + 6 + 5 * i: off + 11 + 5 * i])
            metrics.append((l, r, w, a, d))
    else:
        n = struct.unpack_from(e + "I", raw, off + 4)[0]
        for i in range(n):
            metrics.append(struct.unpack_from(e + "5h", raw, off + 8 + 12 * i)[:5])
    # bitmaps
    fmt, off = tables[8]
    e = endian(fmt)
    count = struct.unpack_from(e + "I", raw, off + 4)[0]
    offsets = struct.unpack_from(e + "%dI" % count, raw, off + 8)
    base = off + 8 + 4 * count + 16
    pad = 1 << (fmt & 3)
    unit = 1 << ((fmt >> 4) & 3)
    msb_first = bool(fmt & 8)
    # encodings
    fmt2, off2 = tables[32]
    e2 = endian(fmt2)
    min2, max2, min1, max1, default = struct.unpack_from(e2 + "5h", raw, off2 + 4)
    idx = struct.unpack_from(e2 + "%dH" % ((max2 - min2 + 1) * (max1 - min1 + 1)), raw, off2 + 14)
    ascent = None
    for t in (256, 2):
        if t in tables:
            fa, oa = tables[t]
            ascent = struct.unpack_from(endian(fa) + "i", raw, oa + 4 + 8)[0]
            break
    glyphs = {}
    for k, gi in enumerate(idx):
        if gi == 0xFFFF:
            continue
        cp = ((k // (max2 - min2 + 1) + min1) << 8) | (k % (max2 - min2 + 1) + min2)
        if not is_wide(cp):
            continue
        l, r, w, a, d = metrics[gi]
        gw, gh = r - l, a + d
        rowbytes = ((gw + pad * 8 - 1) // (pad * 8)) * pad
        rows = []
        for y in range(gh):
            chunk = bytearray(raw[base + offsets[gi] + y * rowbytes: base + offsets[gi] + (y + 1) * rowbytes])
            if unit > 1 and e == "<":        # bytes of a scan unit are in the byte order of the table
                for u in range(0, len(chunk), unit):
                    chunk[u:u + unit] = chunk[u:u + unit][::-1]
            bits = []
            for byte in chunk:
                for b in range(8):
                    bits.append((byte >> (7 - b)) & 1 if msb_first else (byte >> b) & 1)
            rows.append(bits[:gw])
        glyphs[cp] = ((gw, gh, l, -d), rows)
    return glyphs, ascent

def parse(path):
    return parse_pcf(path) if path.lower().endswith((".pcf", ".pcf.gz")) else parse_bdf(path)

def to_cell(bbx, rows, src, ascent):
    """The glyph as a src x src list of lists of 0/1 (src is the cell of the font: 16 for Unifont, 12 for WenQuanYi)."""
    w, h, xoff, yoff = bbx
    cell = [[0] * src for _ in range(src)]
    top = (ascent if ascent is not None else src - 2) - yoff - h      # rows from the top of the cell to the glyph
    if top < 0 or top + h > src:                                       # a font whose ascent is not that of its glyphs (Galmuri):
        top = max(0, (src - h) // 2)                                    # centre the glyph in the cell instead of cutting it off
    for y, bits in enumerate(rows):
        for x in range(min(w, len(bits))):
            if bits[x]:
                cx, cy = xoff + x, top + y
                if 0 <= cx < src and 0 <= cy < src:
                    cell[cy][cx] = 1
    return cell

def scale(cell, src, dst):
    if src == dst:
        return cell
    out = [[0] * dst for _ in range(dst)]
    for y in range(dst):
        for x in range(dst):
            x0, x1 = x * src / dst, (x + 1) * src / dst
            y0, y1 = y * src / dst, (y + 1) * src / dst
            total = ink = 0.0
            for sy in range(int(y0), min(src, int(y1) + 1)):
                for sx in range(int(x0), min(src, int(x1) + 1)):
                    w = (min(x1, sx + 1) - max(x0, sx)) * (min(y1, sy + 1) - max(y0, sy))
                    if w > 0:
                        total += w
                        ink += w * cell[sy][sx]
            out[y][x] = 1 if total and ink / total >= 0.5 else 0
    return out

def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    size = 12
    if "--size" in sys.argv:
        size = int(sys.argv[sys.argv.index("--size") + 1])
        args = [a for a in args if a != str(size)]
    if len(args) < 2 or not 8 <= size <= 16:
        sys.exit(__doc__)
    cells = {}   # code point -> the glyph in a size x size cell (rows of 0/1)
    for spec in args[:-1]:               # several fonts: the first that has a character gives it
        path, _, only = spec.partition("@")   # "font.bdf@AC00-D7AF,3130-318F": only the characters in these ranges
        limits = [tuple(int(x, 16) for x in r.split("-")) for r in only.split(",")] if only else None
        glyphs, ascent = parse(path)
        if limits:
            glyphs = {c: g for c, g in glyphs.items() if any(a <= c <= b for a, b in limits)}
        src = max(max(g[0][0], g[0][1]) for g in glyphs.values())   # the cell of the font: its widest glyph
        src = 16 if src > 12 else (12 if src > 8 else src)
        new = 0
        for c, (bbx, rows) in glyphs.items():
            if c not in cells:
                cells[c] = scale(to_cell(bbx, rows, src, ascent), src, size)
                new += 1
        print(spec.split("/")[-1], ":", len(glyphs), "glyphs,", new, "used", "(scaled from %d)" % src if src != size else "")
    codes = sorted(cells)
    ranges = []
    for c in codes:
        if ranges and c - (ranges[-1][0] + ranges[-1][1]) < MAX_GAP:
            ranges[-1][1] = c - ranges[-1][0] + 1
        else:
            ranges.append([c, 1])
    bpr = (size + 7) // 8
    data = bytearray()
    for first, count in ranges:
        for c in range(first, first + count):
            if c not in cells:
                data += bytes(size * bpr)
                continue
            cell = cells[c]
            for y in range(size):
                v = sum(cell[y][x] << x for x in range(size))
                data += v.to_bytes(bpr, "little")
    head = b"DSLC" + bytes([1, size, size, 0]) + struct.pack("<H", len(ranges)) + bytes(6)
    table = b"".join(struct.pack("<HH", f, n) for f, n in ranges)
    open(args[-1], "wb").write(head + table + bytes(data))
    print(len(codes), "glyphs in", len(ranges), "ranges,", len(head) + len(table) + len(data), "bytes")

main()
