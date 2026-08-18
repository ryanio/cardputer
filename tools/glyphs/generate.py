#!/usr/bin/env python3
"""Build src/glyphs.h, the bitmap atlas a GlyphBot is drawn from.

A GlyphBot is not a picture. It is four lines of Unicode and two colors, which
is already a display format for a 240x135 panel. The catch is that the
collection draws on 105 non ASCII glyphs, box drawing and geometric shapes, and
the fonts on an ESP32 carry none of them. So they are rendered once here and
shipped as bitmaps.

    tools/glyphs/generate.py           write the atlas
    tools/glyphs/generate.py --check   fail if it is out of date, for CI

The alphabet is read from the collection itself rather than from a list
somebody typed, so a trait added later shows up as a missing glyph here instead
of as a blank square on the device.

Two fonts, both redistributable, which matters because the shapes ship inside
the firmware:

  DejaVu Sans   Bitstream Vera license, covers 99 of the 105
  Unifont       SIL Open Font License 1.1, covers the six DejaVu does not

Both are fetched into tools/glyphs/.fonts on first run and checked by size.

The rendered PNG at media.glyphbots.com is the reference for the layout, not
for the typeface: the site itself ships a four glyph subset of Fira Code and
lets each viewer's OS supply the rest, so there is no one true face. What the
PNG does pin is that the grid is monospace, that every line is centered, and
that a glyph sits centered in its own cell. The device keeps all three. What it
cannot keep is the pitch, because the PNG spends 2.25 cells of height per line
and four lines of that would leave 15 pixels a glyph here.
"""

import argparse
import os
import sys
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
HERE = os.path.dirname(os.path.abspath(__file__))
FONT_DIR = os.path.join(HERE, ".fonts")
OUT = os.path.join(ROOT, "src", "glyphs.h")

FACETS_URL = "https://www.glyphbots.com/api/bots/facets"
USER_AGENT = "cardputer-flint/0.1.0 (glyph atlas)"

# 7 columns by 4 rows is what the collection actually uses, measured across the
# whole alphabet and a spread of bots. 7 * 32 = 224 and 4 * 32 = 128, so a 32
# pixel cell is the largest that fits 240x135 with a margin left over.
CELL = 32

FONTS = [
    ("DejaVuSans.ttf",
     "https://github.com/dejavu-fonts/dejavu-fonts/releases/download/version_2_37/"
     "dejavu-fonts-ttf-2.37.tar.bz2",
     "dejavu-fonts-ttf-2.37/ttf/DejaVuSans.ttf"),
    ("unifont.otf",
     "https://unifoundry.com/pub/unifont/unifont-17.0.05/font-builds/unifont-17.0.05.otf",
     None),
]


def fetch(url):
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(request, timeout=90) as response:
        return response.read()


def font_path(name, url, member):
    """The font, downloaded once into a cache beside this script."""
    os.makedirs(FONT_DIR, exist_ok=True)
    path = os.path.join(FONT_DIR, name)
    if os.path.exists(path) and os.path.getsize(path) > 50000:
        return path

    print("fetching %s" % name)
    blob = fetch(url)
    if member:
        import io
        import tarfile
        with tarfile.open(fileobj=io.BytesIO(blob), mode="r:bz2") as archive:
            blob = archive.extractfile(member).read()
    with open(path, "wb") as handle:
        handle.write(blob)
    return path


def alphabet():
    """Every non ASCII character the collection's traits are built from."""
    import json
    facets = json.loads(fetch(FACETS_URL))
    found = set()
    for trait in facets["traits"]:
        for value in trait["values"]:
            for ch in str(value["value"]):
                if ord(ch) > 127:
                    found.add(ch)
    return sorted(found, key=ord)


def render(chars):
    """One 1bpp cell per glyph, all at a common baseline.

    Size is chosen so the tallest and widest glyph in the set just fits the
    cell, and every glyph is drawn at that one size: normalising each glyph to
    fill its own cell would make a middle dot the size of a square, which is
    not what the collection looks like. Vertical placement stays on the shared
    baseline for the same reason, and only the horizontal is centered, because
    the device grid is monospace and the source fonts are not.
    """
    from PIL import Image, ImageDraw, ImageFont

    faces = [font_path(*f) for f in FONTS]

    def face_for(ch):
        from fontTools.ttLib import TTFont
        for path in faces:
            font = TTFont(path, fontNumber=0)
            covered = set()
            for table in font["cmap"].tables:
                covered |= set(table.cmap.keys())
            if ord(ch) in covered:
                return path
        raise SystemExit("no bundled font has %r (U+%04X)" % (ch, ord(ch)))

    owner = {}
    for ch in chars:
        owner[ch] = face_for(ch)

    # The largest size at which every glyph still fits the cell.
    def extents(size):
        loaded = {p: ImageFont.truetype(p, size) for p in set(owner.values())}
        left = top = 10**6
        right = bottom = -(10**6)
        for ch in chars:
            box = loaded[owner[ch]].getbbox(ch)
            if box is None:
                continue
            left = min(left, box[0])
            top = min(top, box[1])
            right = max(right, box[2])
            bottom = max(bottom, box[3])
        return left, top, right, bottom, loaded

    size = CELL
    while size > 4:
        left, top, right, bottom, loaded = extents(size)
        if right - left <= CELL and bottom - top <= CELL:
            break
        size -= 1
    print("rendering %d glyphs at %dpx into %dx%d cells" % (len(chars), size, CELL, CELL))

    cells = []
    for ch in chars:
        image = Image.new("L", (CELL, CELL), 0)
        draw = ImageDraw.Draw(image)
        font = loaded[owner[ch]]
        box = font.getbbox(ch) or (0, 0, 0, 0)
        # Vertical from the shared baseline, horizontal centered in the cell.
        x = (CELL - (box[2] - box[0])) // 2 - box[0]
        y = -top + (CELL - (bottom - top)) // 2
        draw.text((x, y), ch, font=font, fill=255)

        rows = []
        pixels = image.load()
        for row in range(CELL):
            bits = 0
            for column in range(CELL):
                if pixels[column, row] > 127:
                    bits |= 1 << (CELL - 1 - column)
            rows.append(bits)
        cells.append(rows)
    return cells, size, owner


def emit(chars, cells, size, owner):
    faces = sorted({os.path.basename(p) for p in owner.values()})
    lines = []
    lines.append("#pragma once\n")
    lines.append("\n")
    lines.append("// Generated by tools/glyphs/generate.py. Do not edit by hand.\n")
    lines.append("//\n")
    lines.append("// The %d non ASCII glyphs the GlyphBots collection draws on, read from its\n"
                 % len(chars))
    lines.append("// own facets endpoint and rendered at %dpx into %dx%d cells.\n"
                 % (size, CELL, CELL))
    lines.append("//\n")
    lines.append("// DejaVu Sans, Bitstream Vera license:\n")
    lines.append("//   Copyright (c) 2003 by Bitstream, Inc. DejaVu changes are in public domain.\n")
    lines.append("// Unifont, SIL Open Font License 1.1:\n")
    lines.append("//   Copyright (c) 1998-2025 Roman Czyborra, Paul Hardy and others.\n")
    lines.append("// Faces used: %s\n" % ", ".join(faces))
    lines.append("//\n")
    lines.append("// One bit per pixel, row major, MSB first, %d bytes a row.\n" % (CELL // 8))
    lines.append("\n")
    lines.append("#include <cstdint>\n")
    lines.append("\n")
    lines.append("namespace glyphs {\n")
    lines.append("\n")
    lines.append("constexpr int CELL = %d;\n" % CELL)
    lines.append("constexpr int COUNT = %d;\n" % len(chars))
    lines.append("\n")
    lines.append("// Sorted, so a lookup is a binary search.\n")
    lines.append("constexpr uint16_t CODEPOINTS[COUNT] = {\n")
    for i in range(0, len(chars), 8):
        row = ", ".join("0x%04X" % ord(c) for c in chars[i:i + 8])
        lines.append("    %s,\n" % row)
    lines.append("};\n")
    lines.append("\n")
    lines.append("constexpr uint8_t BITMAPS[COUNT][CELL * %d] = {\n" % (CELL // 8))
    for ch, rows in zip(chars, cells):
        body = []
        for bits in rows:
            for shift in range(CELL - 8, -1, -8):
                body.append("0x%02x" % ((bits >> shift) & 0xFF))
        lines.append("    {%s},  // U+%04X\n" % (", ".join(body), ord(ch)))
    lines.append("};\n")
    lines.append("\n")
    lines.append("}  // namespace glyphs\n")
    return "".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--check", action="store_true",
                        help="exit non zero when the atlas does not match the collection")
    args = parser.parse_args()

    chars = alphabet()
    cells, size, owner = render(chars)
    text = emit(chars, cells, size, owner)

    current = ""
    if os.path.exists(OUT):
        with open(OUT) as handle:
            current = handle.read()

    if args.check:
        if current != text:
            print("src/glyphs.h is stale. Run tools/glyphs/generate.py.")
            return 1
        print("src/glyphs.h matches the %d glyphs the collection uses" % len(chars))
        return 0

    with open(OUT, "w") as handle:
        handle.write(text)
    print("wrote %s, %d glyphs, %.1fKB"
          % (os.path.relpath(OUT, ROOT), len(chars), len(chars) * CELL * (CELL // 8) / 1024.0))
    return 0


if __name__ == "__main__":
    sys.exit(main())
