# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# Reduces the fourteen Core 14 AFM files of third_party/core14/ (Adobe's AFM
# files as Apache PDFBox carries them, under the notice of MustRead.html) to
# the table the fonts layer reads when a file embeds no program and gives no
# /Widths: per font its name, ascender, descender and bounding box, and per
# glyph its name, its code in the font's built-in encoding (the AFM's C field,
# -1 for an unencoded glyph) and its width (WX). Every input file is checked
# against build/fonts/core14.lock by SHA-256 before it is read. The table is
# written to src/fonts/generated/core14_table.cpp with the digest of its
# canonical lines, which the unit tier recomputes from the compiled arrays.
# The AFM syntax read is Adobe's Font Metrics File Format Specification 4.1
# (Technical Note 5004, recalled; the files themselves are the specimen).
"""Generate the Core 14 table: python build/fonts/core14.py [--out <folder>]."""

from __future__ import annotations

import argparse
import hashlib
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import cpp_table  # noqa: E402

FOLDER = cpp_table.ROOT / "third_party" / "core14"
LOCK = Path(__file__).resolve().parent / "core14.lock"
TARGET = Path("src") / "fonts" / "generated" / "core14_table.cpp"
# The fourteen fonts in the order of ISO 32000-1 section 9.6.2.2.
FONTS = ("Times-Roman", "Times-Bold", "Times-Italic", "Times-BoldItalic", "Helvetica",
         "Helvetica-Bold", "Helvetica-Oblique", "Helvetica-BoldOblique", "Courier", "Courier-Bold",
         "Courier-Oblique", "Courier-BoldOblique", "Symbol", "ZapfDingbats")


def read_lock() -> dict[str, str]:
    pins: dict[str, str] = {}
    header_seen = False
    for line in LOCK.read_text(encoding="utf-8").split("\n"):
        if not line or line.startswith("#"):
            continue
        fields = line.split("\t")
        if not header_seen:
            header_seen = True
            if fields != ["file", "url", "sha256"]:
                raise SystemExit(f"{LOCK}: the header is {fields}, expected file, url, sha256")
            continue
        pins[fields[0]] = fields[2]
    return pins


def checked(pins: dict[str, str], name: str) -> bytes:
    data = (FOLDER / name).read_bytes()
    actual = hashlib.sha256(data).hexdigest()
    if pins.get(name) != actual:
        raise SystemExit(f"third_party/core14/{name} hashes to {actual}, the lock pins "
                         f"{pins.get(name)}; restore the file from the URL in build/fonts/core14.lock")
    return data


def number(text: str) -> int:
    value = float(text)
    if value != int(value):
        raise ValueError(f"a fractional AFM value {text}")
    return int(value)


def parse(name: str, data: bytes) -> dict:
    font = {"name": name, "ascender": None, "descender": None, "bbox": None, "glyphs": {}}
    in_metrics = False
    for raw in data.decode("latin-1").split("\n"):
        line = raw.strip()
        if not line:
            continue
        key, _, rest = line.partition(" ")
        if key == "StartCharMetrics":
            in_metrics = True
        elif key == "EndCharMetrics":
            in_metrics = False
        elif in_metrics:
            fields = {}
            for part in line.split(";"):
                words = part.split()
                if words:
                    fields[words[0]] = words[1:]
            glyph = fields["N"][0]
            if glyph in font["glyphs"]:
                raise ValueError(f"{name}: {glyph} is listed twice")
            font["glyphs"][glyph] = (int(fields["C"][0]), number(fields["WX"][0]))
        elif key == "FontName" and rest.strip() != name:
            raise ValueError(f"{name}.afm names the font {rest.strip()}")
        elif key == "Ascender":
            font["ascender"] = number(rest)
        elif key == "Descender":
            font["descender"] = number(rest)
        elif key == "FontBBox":
            font["bbox"] = [number(value) for value in rest.split()]
    # Symbol and ZapfDingbats state no Ascender or Descender; their bounding
    # box stands in, as it does for a font descriptor without them.
    if font["ascender"] is None:
        font["ascender"] = font["bbox"][3]
    if font["descender"] is None:
        font["descender"] = font["bbox"][1]
    return font


def generate() -> str:
    pins = read_lock()
    checked(pins, "MustRead.html")
    fonts = [parse(name, checked(pins, name + ".afm")) for name in FONTS]
    canonical: list[str] = []
    font_rows: list[str] = []
    glyph_rows: list[str] = []
    first = 0
    for font in fonts:
        glyphs = sorted(font["glyphs"].items(), key=lambda item: item[0].encode("ascii"))
        bbox = font["bbox"]
        canonical.append(f"F {font['name']} {font['ascender']} {font['descender']} "
                         + " ".join(str(v) for v in bbox) + f" {len(glyphs)}")
        font_rows.append(f"    {{{cpp_table.quote(font['name'])}, {font['ascender']}, "
                         f"{font['descender']}, {{{bbox[0]}, {bbox[1]}, {bbox[2]}, {bbox[3]}}}, "
                         f"{first}, {len(glyphs)}}},")
        for glyph, (code, width) in glyphs:
            canonical.append(f"{glyph} {code} {width}")
            glyph_rows.append(f"    {{{cpp_table.quote(glyph)}, {code}, {width}}},")
        first += len(glyphs)
    out = [cpp_table.header([
        "The Core 14 font metrics reduced from Adobe's AFM files (third_party/core14/,",
        "as Apache PDFBox carries them; Adobe's notice is MustRead.html there and in",
        "THIRD-PARTY-NOTICES.md): per font the ascender, descender and bounding box,",
        "per glyph its built-in code (-1 when unencoded) and its width in thousandths",
        "of an em. The AFM files are reduced, not modified.",
    ], "build/fonts/core14.py")]
    out.append('#include "fonts/generated/core14_table.hpp"\n')
    out.append("namespace pdfkit::fonts::generated {\n")
    out.append("namespace {\n")
    out.append("// clang-format off")
    out.append("constexpr Core14Table::Font font_entries[] = {")
    out += font_rows
    out.append("};\n")
    out.append("constexpr Core14Table::Glyph glyph_entries[] = {")
    out += glyph_rows
    out.append("};")
    out.append("// clang-format on\n")
    out.append("}  // namespace\n")
    out.append("std::span<const Core14Table::Font> Core14Table::fonts() noexcept {")
    out.append("  return font_entries;")
    out.append("}\n")
    out.append("std::span<const Core14Table::Glyph> Core14Table::glyphs() noexcept {")
    out.append("  return glyph_entries;")
    out.append("}\n")
    out.append("std::string_view Core14Table::content_sha256() noexcept {")
    out.append(f"  return \"{cpp_table.digest(canonical)}\";")
    out.append("}\n")
    out.append("}  // namespace pdfkit::fonts::generated")
    return "\n".join(out) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", default=str(cpp_table.ROOT),
                        help="the tree to write src/fonts/generated/ into (default: the repository)")
    args = parser.parse_args()
    cpp_table.write(Path(args.out) / TARGET, generate())
    return 0


if __name__ == "__main__":
    sys.exit(main())
