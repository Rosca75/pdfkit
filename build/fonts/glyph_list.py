# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# Compiles the Adobe Glyph List (third_party/agl/glyphlist.txt, 4,281 names)
# and the ITC Zapf Dingbats glyph list (third_party/agl/zapfdingbats.txt, 201
# names), both as published at https://github.com/adobe-type-tools/agl-aglfn
# under BSD-3-Clause, into src/fonts/generated/glyph_list_table.cpp: two arrays
# sorted by glyph name in byte order, so the fonts layer looks a name up by
# binary search, and the digest of each table's canonical lines
# ("name;HHHH HHHH"), which the unit tier recomputes from the compiled arrays.
"""Generate the glyph-list table: python build/fonts/glyph_list.py [--out <folder>]."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import cpp_table  # noqa: E402

AGL = cpp_table.ROOT / "third_party" / "agl" / "glyphlist.txt"
ZAPF = cpp_table.ROOT / "third_party" / "agl" / "zapfdingbats.txt"
TARGET = Path("src") / "fonts" / "generated" / "glyph_list_table.cpp"


def read_list(path: Path) -> list[tuple[str, list[int]]]:
    """The (name, code points) of a glyph list, sorted by the name's bytes."""
    entries: dict[str, list[int]] = {}
    for line in path.read_text(encoding="ascii").split("\n"):
        if not line or line.startswith("#"):
            continue
        name, values = line.split(";")
        if name in entries:
            raise ValueError(f"{path.name}: {name} is listed twice")
        entries[name] = [int(value, 16) for value in values.split()]
    return sorted(entries.items(), key=lambda item: item[0].encode("ascii"))


def canonical(entries: list[tuple[str, list[int]]]) -> list[str]:
    return [name + ";" + " ".join(f"{value:04X}" for value in values) for name, values in entries]


def array(name: str, entries: list[tuple[str, list[int]]]) -> list[str]:
    lines = [f"constexpr GlyphListTable::Entry {name}[] = {{"]
    for glyph, values in entries:
        text = "".join(f"\\U{value:08X}" for value in values)
        lines.append(f"    {{{cpp_table.quote(glyph)}, U\"{text}\"}},")
    lines.append("};")
    return lines


def generate() -> str:
    agl = read_list(AGL)
    zapf = read_list(ZAPF)
    out = [cpp_table.header([
        "The Adobe Glyph List and the ITC Zapf Dingbats glyph list as sorted arrays",
        "(https://github.com/adobe-type-tools/agl-aglfn, BSD-3-Clause, read; the",
        "licence is third_party/agl/LICENSE.md and THIRD-PARTY-NOTICES.md).",
    ], "build/fonts/glyph_list.py")]
    out.append('#include "fonts/generated/glyph_list_table.hpp"\n')
    out.append("namespace pdfkit::fonts::generated {\n")
    out.append("namespace {\n")
    out.append("// clang-format off")
    out += array("agl_entries", agl)
    out.append("")
    out += array("zapf_dingbats_entries", zapf)
    out.append("// clang-format on\n")
    out.append("}  // namespace\n")
    out.append("std::span<const GlyphListTable::Entry> GlyphListTable::adobe_glyph_list() noexcept {")
    out.append("  return agl_entries;")
    out.append("}\n")
    out.append("std::span<const GlyphListTable::Entry> GlyphListTable::zapf_dingbats() noexcept {")
    out.append("  return zapf_dingbats_entries;")
    out.append("}\n")
    out.append("std::string_view GlyphListTable::adobe_glyph_list_sha256() noexcept {")
    out.append(f"  return \"{cpp_table.digest(canonical(agl))}\";")
    out.append("}\n")
    out.append("std::string_view GlyphListTable::zapf_dingbats_sha256() noexcept {")
    out.append(f"  return \"{cpp_table.digest(canonical(zapf))}\";")
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
