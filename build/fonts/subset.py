# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# Cuts the placeholder font (docs/DESIGN.md section 13) from Liberation Sans
# Regular and Bold (third_party/liberation-fonts/, SIL OFL 1.1, whose FAQ
# permits embedding a subset in a document) and writes
# src/writer/placeholder_font_data.inc: per face the TrueType program holding
# .notdef and the 39 glyphs of the placeholder alphabet ([, ], _, 0-9, A-Z),
# its SHA-256, the subset tag derived from that hash, the code-to-glyph map
# with the hmtx widths in thousandths of an em, and the metrics the font
# descriptor states. The run-time writer subsets nothing: it embeds these
# bytes. fontTools (MIT, pinned in build/fonts/requirements.txt) does the
# cutting with hinting dropped and the tables kept to glyf, loca, head, hhea,
# hmtx, maxp, cmap, post (format 3, no glyph names) and name, whose copyright
# and licence entries the OFL requires to stay. The head table's timestamps
# are kept as the release wrote them, so the bytes depend on the inputs and
# the fontTools version alone; build/check_generated.py regenerates the file
# and fails on a difference.
"""Cut the placeholder subsets: python build/fonts/subset.py [--out <folder>]."""

from __future__ import annotations

import argparse
import hashlib
import io
import os
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import cpp_table  # noqa: E402

FONTTOOLS_VERSION = "4.67.0"
ALPHABET = "[]_0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"
FACES = (("regular", "LiberationSans-Regular.ttf", "LiberationSans", 400),
         ("bold", "LiberationSans-Bold.ttf", "LiberationSans-Bold", 700))
SOURCE = cpp_table.ROOT / "third_party" / "liberation-fonts"
TARGET = Path("src") / "writer" / "placeholder_font_data.inc"
KEPT_TABLES = ("glyf", "loca", "head", "hhea", "hmtx", "maxp", "cmap", "post", "name")
# The name records kept: copyright (0), family (1), subfamily (2), unique id
# (3), full name (4), version (5), PostScript name (6), trademark (7), licence
# description (13) and licence URL (14).
NAME_IDS = (0, 1, 2, 3, 4, 5, 6, 7, 13, 14)


def venv_python() -> Path | None:
    """The interpreter of the shared virtual environment in the main checkout, if any."""
    try:
        common = subprocess.run(["git", "rev-parse", "--git-common-dir"], cwd=cpp_table.ROOT,
                                capture_output=True, text=True, check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return None
    main = (cpp_table.ROOT / common).resolve().parent
    for candidate in (main / "build" / "cache" / "fonts-venv" / "Scripts" / "python.exe",
                      main / "build" / "cache" / "fonts-venv" / "bin" / "python"):
        if candidate.is_file():
            return candidate
    return None


def ensure_fonttools() -> None:
    """Re-runs the tool under the pinned fontTools when this interpreter lacks it."""
    try:
        import fontTools
        if fontTools.version == FONTTOOLS_VERSION:
            return
        found = fontTools.version
    except ImportError:
        found = "none"
    python = venv_python()
    if python is not None and os.environ.get("PDFKIT_FONTS_VENV") != "1":
        env = dict(os.environ, PDFKIT_FONTS_VENV="1")
        sys.exit(subprocess.run([str(python), str(Path(__file__).resolve())] + sys.argv[1:],
                                env=env, check=False).returncode)
    sys.exit(f"build/fonts/subset.py needs fontTools {FONTTOOLS_VERSION} (found {found}); run "
             "`python -m venv <main checkout>/build/cache/fonts-venv` and "
             "`<that venv's python> -m pip install --require-hashes -r build/fonts/requirements.txt`")


def scaled(value: int, units: int) -> int:
    """value font units in thousandths of an em, rounded half away from zero."""
    sign = -1 if value < 0 else 1
    return sign * ((abs(value) * 2000 + units) // (2 * units))


def cut(path: Path) -> tuple[bytes, dict]:
    """The subset program of one face and what the descriptor and widths need."""
    from fontTools import subset
    from fontTools.ttLib import TTFont

    source = TTFont(str(path), recalcTimestamp=False)
    units = source["head"].unitsPerEm
    cap_height = source["OS/2"].sCapHeight
    weight = source["OS/2"].usWeightClass
    options = subset.Options()
    options.layout_features = []
    options.hinting = False
    options.notdef_outline = True
    options.notdef_glyph = True
    options.glyph_names = False
    options.name_IDs = list(NAME_IDS)
    options.name_languages = [0x0409]
    options.name_legacy = False
    options.recalc_timestamp = False
    options.drop_tables = sorted(set(source.keys()) - set(KEPT_TABLES) - {"GlyphOrder"})
    subsetter = subset.Subsetter(options)
    subsetter.populate(unicodes=[ord(ch) for ch in ALPHABET])
    subsetter.subset(source)
    for tag in list(source.keys()):
        if tag not in KEPT_TABLES and tag != "GlyphOrder":
            del source[tag]
    source["post"].formatType = 3.0
    buffer = io.BytesIO()
    source.save(buffer)
    program = buffer.getvalue()

    font = TTFont(io.BytesIO(program))
    order = font.getGlyphOrder()
    cmap = font.getBestCmap()
    glyphs = []
    for ch in sorted(ALPHABET):
        name = cmap[ord(ch)]
        glyphs.append((ch, order.index(name), scaled(font["hmtx"][name][0], units)))
    head = font["head"]
    info = {
        "glyphs": glyphs,
        "notdef_width": scaled(font["hmtx"][order[0]][0], units),
        "ascent": scaled(font["hhea"].ascent, units),
        "descent": scaled(font["hhea"].descent, units),
        "cap_height": scaled(cap_height, units),
        "bbox": (scaled(head.xMin, units), scaled(head.yMin, units), scaled(head.xMax, units),
                 scaled(head.yMax, units)),
        # A stem width from the weight class, the estimate PDF writers state
        # when the program does not say (recalled); viewers of an embedded
        # TrueType program do not read it.
        "stem_v": 50 + (weight * weight) // 4225,
        "weight": weight,
        "glyph_count": len(order),
    }
    return program, info


def subset_tag(digest: str) -> str:
    """Six capital letters from the program's hash (ISO 32000-1 section 9.6.4, read)."""
    return "".join(chr(ord("A") + int(digest[i * 2:i * 2 + 2], 16) % 26) for i in range(6))


def byte_lines(name: str, data: bytes) -> list[str]:
    lines = [f"constexpr std::array<std::uint8_t, {len(data)}> {name}{{{{"]
    for start in range(0, len(data), 16):
        chunk = data[start:start + 16]
        lines.append("    " + ", ".join(f"0x{b:02X}" for b in chunk) + ",")
    lines.append("}};")
    return lines


def generate() -> str:
    out = [cpp_table.header([
        "The placeholder font's two subsets, cut from Liberation Sans 2.1.5 Regular",
        "and Bold (third_party/liberation-fonts/, SIL OFL 1.1, read; the licence is",
        "third_party/liberation-fonts/LICENSE and THIRD-PARTY-NOTICES.md): each face's",
        "TrueType program, its SHA-256 and subset tag, its glyphs (character, glyph",
        "id, advance in thousandths of an em) and the metrics of its descriptor.",
        "Included by src/writer/placeholder_font.cpp, which declares the types.",
    ], "build/fonts/subset.py"), ""]
    faces = []
    for key, file, base, _ in FACES:
        program, info = cut(SOURCE / file)
        digest = hashlib.sha256(program).hexdigest()
        out += byte_lines(f"{key}_program", program)
        out.append("")
        out.append(f"constexpr std::array<SubsetGlyph, {len(info['glyphs'])}> {key}_glyphs{{{{")
        for ch, gid, width in info["glyphs"]:
            out.append(f"    {{'{ch}', {gid}, {width}}},")
        out.append("}};")
        out.append("")
        faces.append((key, base, digest, info))
    for key, base, digest, info in faces:
        bbox = ", ".join(str(v) for v in info["bbox"])
        out += [
            f"constexpr SubsetFace {key}_face{{",
            f"    {key}_program,",
            f"    {key}_glyphs,",
            f"    {cpp_table.quote(digest)},",
            f"    {cpp_table.quote(subset_tag(digest) + '+' + base)},",
            f"    {info['notdef_width']},",
            f"    {info['ascent']},",
            f"    {info['descent']},",
            f"    {info['cap_height']},",
            f"    {{{bbox}}},",
            f"    {info['stem_v']},",
            f"    {info['weight']},",
            f"    {info['glyph_count']},",
            "};",
            "",
        ]
    return "\n".join(out)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", default=str(cpp_table.ROOT), help="the tree to write into")
    args = parser.parse_args()
    ensure_fonttools()
    cpp_table.write(Path(args.out) / TARGET, generate())
    return 0


if __name__ == "__main__":
    sys.exit(main())
