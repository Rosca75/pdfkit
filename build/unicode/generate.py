# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# Compiles the Unicode Character Database files a search's folding needs
# (docs/DESIGN.md section 11) into src/platform/unicode/tables.inc, each a
# sorted table the platform layer reads by binary search:
#   - the simple case folding: the status C and S lines of CaseFolding.txt
#     (the full foldings F and the Turkic T are left out, as the simple
#     folding of the Unicode Standard section 3.13 leaves them);
#   - the canonical decompositions of UnicodeData.txt field 5 (the entries
#     without a <tag>), applied recursively so each maps to its full
#     decomposition; the Hangul syllables, whose decomposition is
#     arithmetic and holds no combining mark, are left whole;
#   - the combining marks a diacritic folding removes: General_Category Mn
#     and Me (field 2), as ranges;
#   - the white space a whitespace folding collapses: General_Category Zs, Zl
#     and Zp, and the controls U+0009 to U+000D and U+0085, which the
#     White_Space property of PropList.txt adds to them (recalled).
# Every input file is pinned by its SHA-256 in build/unicode/unicode.lock;
# build/check_generated.py regenerates the output and fails on a difference.
"""Generate the folding tables: python build/unicode/generate.py [--out <folder>]."""

from __future__ import annotations

import argparse
import hashlib
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "build" / "fonts"))

import cpp_table  # noqa: E402

DATA = ROOT / "third_party" / "unicode"
LOCK = Path(__file__).resolve().parent / "unicode.lock"
TARGET = Path("src") / "platform" / "unicode" / "tables.inc"
CONTROL_SPACES = [0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x85]


def read_pinned(name: str) -> str:
    """A data file's text after its hash is checked against the lock."""
    pins = {}
    for line in LOCK.read_text(encoding="utf-8").split("\n"):
        if line and not line.startswith("#") and not line.startswith("file\t"):
            file, _, digest = line.split("\t")
            pins[file] = digest
    data = (DATA / name).read_bytes()
    if hashlib.sha256(data).hexdigest() != pins.get(name):
        raise SystemExit(f"third_party/unicode/{name} does not match build/unicode/unicode.lock; "
                         "restore the pinned file")
    return data.decode("utf-8")


def case_folding() -> list[tuple[int, int]]:
    pairs = []
    for line in read_pinned("CaseFolding.txt").split("\n"):
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        code, status, mapping = (field.strip() for field in line.split(";")[:3])
        if status in ("C", "S"):
            pairs.append((int(code, 16), int(mapping, 16)))
    return sorted(pairs)


def unicode_data() -> tuple[dict[int, list[int]], list[tuple[int, str]]]:
    """The canonical decompositions and every assigned code point's category."""
    decompositions: dict[int, list[int]] = {}
    categories: list[tuple[int, str]] = []
    pending_first = None
    for line in read_pinned("UnicodeData.txt").split("\n"):
        if not line:
            continue
        fields = line.split(";")
        code = int(fields[0], 16)
        name, category, mapping = fields[1], fields[2], fields[5]
        if name.endswith(", First>"):
            pending_first = code
            continue
        if name.endswith(", Last>") and pending_first is not None:
            categories += [(c, category) for c in range(pending_first, code + 1)]
            pending_first = None
            continue
        categories.append((code, category))
        if mapping and not mapping.startswith("<"):
            decompositions[code] = [int(part, 16) for part in mapping.split()]
    return decompositions, categories


def full(code: int, decompositions: dict[int, list[int]]) -> list[int]:
    if code not in decompositions:
        return [code]
    out: list[int] = []
    for part in decompositions[code]:
        out += full(part, decompositions)
    return out


def ranges(codes: list[int]) -> list[tuple[int, int]]:
    out: list[tuple[int, int]] = []
    for code in sorted(codes):
        if out and out[-1][1] + 1 == code:
            out[-1] = (out[-1][0], code)
        else:
            out.append((code, code))
    return out


def generate() -> str:
    folds = case_folding()
    decompositions, categories = unicode_data()
    marks = ranges([c for c, cat in categories if cat in ("Mn", "Me")])
    spaces = ranges([c for c, cat in categories if cat in ("Zs", "Zl", "Zp")] + CONTROL_SPACES)
    out = [cpp_table.header([
        "The Unicode 18.0.0 tables of the search folding (third_party/unicode/,",
        "https://www.unicode.org/Public/18.0.0/ucd/, Unicode License v3, read): the simple",
        "case folding, the full canonical decompositions, the combining marks and the",
        "white space, each sorted for a binary search. Included by",
        "src/platform/unicode/folding.cpp, which declares the types.",
    ], "build/unicode/generate.py"), ""]
    out.append(f"constexpr std::array<CaseFold, {len(folds)}> case_folds{{{{")
    out += [f"    {{0x{a:04X}, 0x{b:04X}}}," for a, b in folds]
    out += ["}};", ""]
    data: list[int] = []
    entries = []
    for code in sorted(decompositions):
        sequence = full(code, decompositions)
        entries.append((code, len(data), len(sequence)))
        data += sequence
    out.append(f"constexpr std::array<char32_t, {len(data)}> decomposition_data{{{{")
    for start in range(0, len(data), 8):
        out.append("    " + ", ".join(f"0x{c:04X}" for c in data[start:start + 8]) + ",")
    out += ["}};", ""]
    out.append(f"constexpr std::array<Decomposition, {len(entries)}> decompositions{{{{")
    out += [f"    {{0x{c:04X}, {o}, {n}}}," for c, o, n in entries]
    out += ["}};", ""]
    for name, table in (("marks", marks), ("spaces", spaces)):
        out.append(f"constexpr std::array<CodeRange, {len(table)}> {name}{{{{")
        out += [f"    {{0x{a:04X}, 0x{b:04X}}}," for a, b in table]
        out += ["}};", ""]
    return "\n".join(out)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", default=str(ROOT), help="the tree to write into")
    args = parser.parse_args()
    cpp_table.write(Path(args.out) / TARGET, generate())
    return 0


if __name__ == "__main__":
    sys.exit(main())
