# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# Rasterises the caption bitmap font of the image box (docs/DESIGN.md section
# 14) from Liberation Sans Regular (third_party/liberation-fonts/, SIL OFL 1.1,
# whose FAQ permits bitmaps derived from the font) and writes
# src/images/bitmap_font_data.inc: per pixel height (8, 12, 16, 24, 32, 48 and
# 64) and per character of the placeholder alphabet ([, ], _, 0-9, A-Z) a
# bitmap one cell high and one advance wide, its rows packed most significant
# bit first, with the SHA-256 of the canonical lines the unit tier recomputes
# from the compiled table. A caption is drawn in one colour, so the outlines
# are filled without hinting or anti-aliasing: fontTools' pen protocol (MIT,
# pinned in build/fonts/requirements.txt; recalled) walks each glyph's
# quadratic outline, every curve is flattened into straight segments, and a
# pixel is set when a quarter of the samples of a four by four grid inside it
# lie inside the outline by the non-zero winding rule (the TrueType fill rule,
# Apple's TrueType Reference Manual, recalled).
# The cell spans the hhea ascent to the hhea descent, so every glyph of one
# height shares one baseline. The arithmetic is Python's binary floating
# point, the same on every run, so the file depends on the font and the
# fontTools version alone; build/check_generated.py regenerates it and fails
# on a difference.
"""Rasterise the caption font: python build/fonts/bitmap_font.py [--out <folder>]."""

from __future__ import annotations

import argparse
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import cpp_table  # noqa: E402
import subset  # noqa: E402

HEIGHTS = (8, 12, 16, 24, 32, 48, 64)
SOURCE = cpp_table.ROOT / "third_party" / "liberation-fonts" / "LiberationSans-Regular.ttf"
TARGET = Path("src") / "images" / "bitmap_font_data.inc"
# Straight segments per quadratic curve: at 64 pixels a Liberation Sans curve
# spans at most about 40 pixels, so 16 segments keep the chord within a tenth
# of a pixel of the curve.
SEGMENTS = 16
# A pixel is set when at least a quarter of its SUB by SUB samples lie inside
# the outline, so a stem thinner than a pixel at the small heights still
# draws, where a single sample at the centre would miss it.
SUB = 4


def outline(font, name: str) -> list[list[tuple[float, float]]]:
    """The glyph's closed contours as polygons in font units."""
    from fontTools.pens.basePen import BasePen

    class Flattener(BasePen):
        def __init__(self, glyph_set):
            super().__init__(glyph_set)
            self.contours: list[list[tuple[float, float]]] = []
            self.current: list[tuple[float, float]] = []

        def _moveTo(self, pt):
            self.current = [pt]

        def _lineTo(self, pt):
            self.current.append(pt)

        def _qCurveToOne(self, pt1, pt2):
            x0, y0 = self.current[-1]
            for i in range(1, SEGMENTS + 1):
                t = i / SEGMENTS
                u = 1.0 - t
                self.current.append((u * u * x0 + 2 * u * t * pt1[0] + t * t * pt2[0],
                                     u * u * y0 + 2 * u * t * pt1[1] + t * t * pt2[1]))

        def _curveToOne(self, pt1, pt2, pt3):
            x0, y0 = self.current[-1]
            for i in range(1, SEGMENTS + 1):
                t = i / SEGMENTS
                u = 1.0 - t
                self.current.append((u ** 3 * x0 + 3 * u * u * t * pt1[0] + 3 * u * t * t * pt2[0]
                                     + t ** 3 * pt3[0],
                                     u ** 3 * y0 + 3 * u * u * t * pt1[1] + 3 * u * t * t * pt2[1]
                                     + t ** 3 * pt3[1]))

        def _closePath(self):
            if len(self.current) > 2:
                self.contours.append(self.current)
            self.current = []

        _endPath = _closePath

    glyph_set = font.getGlyphSet()
    pen = Flattener(glyph_set)
    glyph_set[name].draw(pen)
    return pen.contours


def spans(contours: list[list[tuple[float, float]]], y: float) -> list[tuple[float, float]]:
    """The x intervals of the horizontal line at y inside the contours (non-zero winding)."""
    crossings = []
    for contour in contours:
        count = len(contour)
        for i in range(count):
            x0, y0 = contour[i]
            x1, y1 = contour[(i + 1) % count]
            if y0 <= y < y1 or y1 <= y < y0:
                crossings.append((x0 + (y - y0) * (x1 - x0) / (y1 - y0), 1 if y1 > y0 else -1))
    crossings.sort()
    out = []
    winding = 0
    start = 0.0
    for x, direction in crossings:
        before = winding
        winding += direction
        if before == 0 and winding != 0:
            start = x
        elif before != 0 and winding == 0:
            out.append((start, x))
    return out


def rasterise() -> tuple[list[str], list[tuple[int, str, int, int, bytes]]]:
    """The canonical lines and, per height and character, (height, char, width, stride, rows)."""
    from fontTools.ttLib import TTFont

    font = TTFont(str(SOURCE), recalcTimestamp=False)
    ascent = font["hhea"].ascent
    descent = font["hhea"].descent
    cmap = font.getBestCmap()
    glyphs = []
    lines = []
    for height in HEIGHTS:
        scale = height / (ascent - descent)
        for ch in subset.ALPHABET:
            name = cmap[ord(ch)]
            advance = font["hmtx"][name][0]
            width = max(1, round(advance * scale))
            stride = (width + 7) // 8
            contours = outline(font, name)
            rows = bytearray()
            for row in range(height):
                # Coverage counted on a grid of SUB by SUB samples per pixel,
                # from the cell's top (the ascent) down.
                covered = [0] * width
                for sy in range(SUB):
                    y = ascent - (row + (sy + 0.5) / SUB) / scale
                    for left, right in spans(contours, y):
                        for sample in range(width * SUB):
                            if left < (sample + 0.5) / (SUB * scale) < right:
                                covered[sample // SUB] += 1
                packed = bytearray(stride)
                for column in range(width):
                    if covered[column] * 4 >= SUB * SUB:
                        packed[column // 8] |= 0x80 >> (column % 8)
                rows += packed
            glyphs.append((height, ch, width, stride, bytes(rows)))
            lines.append(f"{height} {ch} {width} {bytes(rows).hex()}")
    assert all(math.isfinite(v) for v in (ascent, descent))
    return lines, glyphs


def generate() -> str:
    lines, glyphs = rasterise()
    data = bytearray()
    entries = []
    for height, ch, width, stride, rows in glyphs:
        entries.append((height, ch, width, stride, len(data)))
        data += rows
    out = [cpp_table.header([
        "The caption font of the image box: the placeholder alphabet rasterised",
        "from Liberation Sans 2.1.5 Regular (third_party/liberation-fonts/, SIL OFL",
        "1.1, read; the licence is third_party/liberation-fonts/LICENSE and",
        "THIRD-PARTY-NOTICES.md) at the pixel heights below, one bit per pixel, rows",
        "packed most significant bit first, without hinting or anti-aliasing.",
        "Included by src/images/treatments/caption_drawer.cpp, which declares the",
        "types.",
    ], "build/fonts/bitmap_font.py"), ""]
    out.append(f"constexpr std::array<std::uint32_t, {len(HEIGHTS)}> bitmap_font_heights{{{{"
               + ", ".join(str(h) for h in HEIGHTS) + "}};")
    out.append("")
    out += subset.byte_lines("bitmap_font_rows", bytes(data))
    out.append("")
    out.append(f"constexpr std::array<BitmapGlyph, {len(entries)}> bitmap_font_glyphs{{{{")
    for height, ch, width, stride, offset in entries:
        out.append(f"    {{{height}, '{ch}', {width}, {stride}, {offset}}},")
    out.append("}};")
    out.append("")
    out.append("// SHA-256 over one line per glyph: height, character, width and the rows in")
    out.append("// lower-case hexadecimal, separated by spaces, each line ended by a line feed.")
    out.append(f"constexpr std::string_view bitmap_font_sha256 = "
               f"{cpp_table.quote(cpp_table.digest(lines))};")
    out.append("")
    return "\n".join(out)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", default=str(cpp_table.ROOT), help="the tree to write into")
    args = parser.parse_args()
    subset.ensure_fonttools()
    cpp_table.write(Path(args.out) / TARGET, generate())
    return 0


if __name__ == "__main__":
    sys.exit(main())
