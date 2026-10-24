# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# Font programs and font dictionaries for the synthesised files, cut from
# Liberation Sans 2.1.5 (SIL OFL 1.1, fetched pinned as the `liberation` set of
# sources.tsv) with fontTools: TrueType subsets for simple and CID-keyed fonts,
# a symbolic TrueType with a (3,0) cmap in the 0xF000 range, a CFF program, a
# Type 1 program with its built-in encoding, Type 3 procedures and glyph
# outlines drawn as paths. The formats follow ISO 32000-1 §9.6 to §9.10 and the
# Type 1 eexec and charstring encryption of Adobe's Type 1 Font Format
# (keys 55665 and 4330, recalled; the files are checked by PDFium on generation).

from __future__ import annotations

import io
import sys
import tarfile
from functools import lru_cache
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import common  # noqa: E402
from synth.writer import Name, Stream, ops  # noqa: E402

ARCHIVE = "liberation-fonts-ttf-2.1.5.tar.gz"


def font_path(style: str = "Regular") -> Path:
    """LiberationSans-<style>.ttf, extracted from the pinned archive on first use."""
    folder = common.cache_dir() / "liberation"
    target = folder / f"LiberationSans-{style}.ttf"
    if target.exists():
        return target
    archive = folder / ARCHIVE
    if not archive.exists():
        raise common.ToolError(
            f"{archive} is missing", "the Liberation fonts archive of sources.tsv",
            "run `python dataset/tools/fetch.py --set liberation --no-materialise`")
    with tarfile.open(archive, "r:gz") as tar:
        for member in tar.getmembers():
            if member.name.endswith(f"/LiberationSans-{style}.ttf"):
                target.write_bytes(tar.extractfile(member).read())
    return target


@lru_cache(maxsize=4)
def base_font(style: str = "Regular"):
    from fontTools.ttLib import TTFont

    return TTFont(str(font_path(style)))


def glyph_for(char: str, style: str = "Regular") -> str:
    cmap = base_font(style).getBestCmap()
    name = cmap.get(ord(char))
    if name is None:
        raise common.ToolError(f"Liberation Sans has no glyph for U+{ord(char):04X}",
                               "a character the font covers", "change the pattern's text")
    return name


def advance(glyph: str, style: str = "Regular") -> int:
    font = base_font(style)
    upm = font["head"].unitsPerEm
    return round(font["hmtx"][glyph][0] * 1000 / upm)


def subset_bytes(glyphs: list[str], style: str = "Regular", symbolic_codes: dict | None = None,
                 keep_cmap: bool = True) -> tuple[bytes, dict[str, int]]:
    """A TrueType subset holding `glyphs`; returns the bytes and glyph name to GID."""
    from fontTools import subset
    from fontTools.ttLib import TTFont

    font = TTFont(str(font_path(style)))
    options = subset.Options()
    options.glyph_names = True
    options.notdef_outline = True
    options.name_IDs = [1, 2]
    options.layout_features = []
    options.drop_tables += ["GSUB", "GPOS", "GDEF", "DSIG", "kern", "FFTM"]
    sub = subset.Subsetter(options)
    sub.populate(glyphs=glyphs)
    sub.subset(font)
    if symbolic_codes is not None:
        from fontTools.ttLib.tables._c_m_a_p import cmap_format_4

        table = cmap_format_4(4)
        table.platformID, table.platEncID, table.language = 3, 0, 0
        table.cmap = {0xF000 + code: glyph for code, glyph in symbolic_codes.items()}
        font["cmap"].tables = [table]
    elif not keep_cmap:
        font["cmap"].tables = []
    if "OS/2" in font and symbolic_codes is not None:
        font["OS/2"].usFirstCharIndex = 0xF000
    out = io.BytesIO()
    font.save(out)
    data = out.getvalue()
    reread = TTFont(io.BytesIO(data))
    order = reread.getGlyphOrder()
    return data, {name: order.index(name) for name in order}


def tounicode(mapping: dict[int, str], code_bytes: int = 1) -> bytes:
    """A ToUnicode CMap from code to text (ISO 32000-1 §9.10.3)."""
    lines = [
        "/CIDInit /ProcSet findresource begin", "12 dict begin", "begincmap",
        "/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def",
        "/CMapName /Adobe-Identity-UCS def", "/CMapType 2 def", "1 begincodespacerange",
        "<00> <FF>" if code_bytes == 1 else "<0000> <FFFF>", "endcodespacerange",
    ]
    items = sorted(mapping.items())
    for start in range(0, len(items), 100):
        chunk = items[start:start + 100]
        lines.append(f"{len(chunk)} beginbfchar")
        for code, text in chunk:
            utf16 = text.encode("utf-16-be").hex().upper()
            lines.append(f"<{code:0{code_bytes * 2}X}> <{utf16}>")
        lines.append("endbfchar")
    lines += ["endcmap", "CMapName currentdict /CMap defineresource pop", "end", "end"]
    return "\n".join(lines).encode("ascii")


def truetype_simple(pdf, text_chars: str, base_name: str = "AAAAAA+LiberationSans",
                    with_tounicode: bool = True, encoding=None, symbolic: bool = False,
                    code_for=None, unicode_override: dict | None = None) -> tuple[object, dict[str, int]]:
    """A simple TrueType font; returns its reference and the code of each character.

    `code_for` maps a character to its one-byte code (WinAnsi by default);
    `unicode_override` maps a code to the text its ToUnicode entry claims.
    """
    chars = sorted(set(text_chars))
    codes = {c: (code_for(c) if code_for else c.encode("cp1252")[0]) for c in chars}
    glyphs = [glyph_for(c) for c in chars]
    symbolic_codes = {codes[c]: glyph_for(c) for c in chars} if symbolic else None
    data, _ = subset_bytes(glyphs, symbolic_codes=symbolic_codes)
    first, last = min(codes.values()), max(codes.values())
    widths = [0] * (last - first + 1)
    for c in chars:
        widths[codes[c] - first] = advance(glyph_for(c))
    descriptor = pdf.add({
        "Type": Name("FontDescriptor"), "FontName": Name(base_name),
        "Flags": 4 if symbolic else 32, "FontBBox": [-203, -303, 1050, 910], "ItalicAngle": 0,
        "Ascent": 905, "Descent": -212, "CapHeight": 729, "StemV": 80,
        "FontFile2": pdf.add(Stream(data, {"Length1": len(data)})),
    })
    font = {"Type": Name("Font"), "Subtype": Name("TrueType"), "BaseFont": Name(base_name),
            "FirstChar": first, "LastChar": last, "Widths": widths, "FontDescriptor": descriptor}
    if encoding is not None:
        font["Encoding"] = encoding
    elif not symbolic:
        font["Encoding"] = Name("WinAnsiEncoding")
    if with_tounicode:
        mapping = {codes[c]: c for c in chars}
        mapping.update(unicode_override or {})
        font["ToUnicode"] = pdf.add(Stream(tounicode(mapping)))
    return pdf.add(font), codes


def cid_truetype(pdf, text_chars: str, base_name: str = "BAAAAA+LiberationSans",
                 with_tounicode: bool = True, encoding: str = "Identity-H",
                 unicode_override: dict | None = None, extra: dict | None = None):
    """A Type0 font over a CIDFontType2 with CID = GID; returns (ref, char to 2-byte code)."""
    chars = sorted(set(text_chars))
    glyphs = [glyph_for(c) for c in chars]
    data, gids = subset_bytes(glyphs)
    codes = {c: gids[glyph_for(c)] for c in chars}
    w = []
    for c in sorted(chars, key=lambda ch: codes[ch]):
        w += [codes[c], [advance(glyph_for(c))]]
    descriptor = pdf.add({
        "Type": Name("FontDescriptor"), "FontName": Name(base_name), "Flags": 32,
        "FontBBox": [-203, -303, 1050, 910], "ItalicAngle": 0, "Ascent": 905, "Descent": -212,
        "CapHeight": 729, "StemV": 80, "FontFile2": pdf.add(Stream(data, {"Length1": len(data)})),
    })
    cid = {"Type": Name("Font"), "Subtype": Name("CIDFontType2"), "BaseFont": Name(base_name),
           "CIDSystemInfo": {"Registry": b"Adobe", "Ordering": b"Identity", "Supplement": 0},
           "FontDescriptor": descriptor, "W": w, "CIDToGIDMap": Name("Identity"), "DW": 1000}
    cid.update(extra or {})
    font = {"Type": Name("Font"), "Subtype": Name("Type0"), "BaseFont": Name(base_name),
            "Encoding": Name(encoding), "DescendantFonts": [pdf.add(cid)]}
    if with_tounicode:
        mapping = {codes[c]: c for c in chars}
        mapping.update(unicode_override or {})
        font["ToUnicode"] = pdf.add(Stream(tounicode(mapping, 2)))
    return pdf.add(font), codes


def encode_cid(text: str, codes: dict[str, int]) -> bytes:
    return b"".join(codes[c].to_bytes(2, "big") for c in text)


def encode_simple(text: str, codes: dict[str, int]) -> bytes:
    return bytes(codes[c] for c in text)


# -- CFF and Type 1 programs -----------------------------------------------------


def _charstrings(glyphs: list[str], pen_factory):
    font = base_font()
    glyph_set = font.getGlyphSet()
    scale = 1000 / font["head"].unitsPerEm
    from fontTools.pens.transformPen import TransformPen

    out = {}
    for name in glyphs:
        width = advance(name)
        pen = pen_factory(width, glyph_set)
        glyph_set[name].draw(TransformPen(pen, (scale, 0, 0, scale, 0, 0)))
        out[name] = pen
    return out


def cff_program(chars: str, font_name: str = "CAAAAA+LiberationSans") -> tuple[bytes, dict[str, str]]:
    """A bare CFF (Type1C) program; returns its bytes and character to glyph name."""
    from fontTools.fontBuilder import FontBuilder
    from fontTools.pens.t2CharStringPen import T2CharStringPen

    names = {c: glyph_for(c) for c in sorted(set(chars))}
    glyphs = [".notdef"] + sorted(set(names.values()))
    pens = _charstrings(glyphs, lambda w, gs: T2CharStringPen(w, gs))
    charstrings = {n: p.getCharString() for n, p in pens.items()}
    fb = FontBuilder(1000, isTTF=False)
    fb.setupGlyphOrder(glyphs)
    fb.setupCharacterMap({ord(c): n for c, n in names.items()})
    fb.setupCFF(font_name, {"FullName": font_name}, charstrings, {})
    fb.setupHorizontalMetrics({n: (advance(n) if n != ".notdef" else 500, 0) for n in glyphs})
    fb.setupHorizontalHeader(ascent=905, descent=-212)
    fb.setupNameTable({"familyName": "LiberationSans", "styleName": "Regular"})
    fb.setupOS2()
    fb.setupPost()
    out = io.BytesIO()
    fb.font["CFF "].cff.compile(out, fb.font)
    return out.getvalue(), names


def _eexec(data: bytes, key: int) -> bytes:
    out = bytearray()
    r = key
    for byte in data:
        cipher = byte ^ (r >> 8)
        r = ((cipher + r) * 52845 + 22719) & 0xFFFF
        out.append(cipher)
    return bytes(out)


def type1_program(chars: str, font_name: str = "DAAAAA+LiberationSans") -> tuple[bytes, int, int, int, dict[str, int]]:
    """A Type 1 program whose built-in encoding places each character at its WinAnsi code.

    Returns the program, /Length1, /Length2, /Length3 and character to code.
    """
    from fontTools.pens.t1CharStringPen import T1CharStringPen

    names = {c: glyph_for(c) for c in sorted(set(chars))}
    codes = {c: c.encode("cp1252")[0] for c in names}
    glyphs = [".notdef"] + sorted(set(names.values()))
    pens = _charstrings(glyphs, lambda w, gs: T1CharStringPen(w, gs))
    clear = [
        "%!PS-AdobeFont-1.0: " + font_name, "11 dict begin",
        "/FontInfo 2 dict dup begin /FullName (" + font_name + ") readonly def end readonly def",
        "/FontName /" + font_name + " def", "/PaintType 0 def", "/FontType 1 def",
        "/FontMatrix [0.001 0 0 0.001 0 0] readonly def", "/FontBBox {-203 -303 1050 910} readonly def",
        "/Encoding 256 array", "0 1 255 {1 index exch /.notdef put} for",
    ]
    for c, name in sorted(names.items(), key=lambda kv: codes[kv[0]]):
        clear.append(f"dup {codes[c]} /{name} put")
    clear += ["readonly def", "currentdict end", "currentfile eexec", ""]
    clear_bytes = "\n".join(clear).encode("ascii")
    private = bytearray(b"dup /Private 8 dict dup begin\n/RD{string currentfile exch readstring pop}executeonly def\n"
                        b"/ND{noaccess def}executeonly def\n/NP{noaccess put}executeonly def\n"
                        b"/lenIV 4 def\n/BlueValues [] def\n/MinFeature {16 16} def\n/password 5839 def\n"
                        b"/Subrs 0 array\n")
    private += b"2 index /CharStrings %d dict dup begin\n" % len(glyphs)
    for name in glyphs:
        program = pens[name].getCharString()
        program.compile()
        encrypted = _eexec(b"\0\0\0\0" + program.bytecode, 4330)
        private += b"/%s %d RD " % (name.encode("ascii"), len(encrypted)) + encrypted + b" ND\n"
    private += b"end\nend\nreadonly put\nnoaccess put\ndup /FontName get exch definefont pop\nmark currentfile closefile\n"
    encrypted_part = _eexec(b"\0\0\0\0" + bytes(private), 55665)
    trailer = ("0" * 64 + "\n") * 8 + "cleartomark\n"
    data = clear_bytes + encrypted_part + trailer.encode("ascii")
    return data, len(clear_bytes), len(encrypted_part), len(trailer), codes


# -- outlines and Type 3 ---------------------------------------------------------


class _PathPen:
    """Collects glyph outlines as PDF path operators."""

    def __init__(self):
        self.parts: list[str] = []

    @staticmethod
    def _f(v: float) -> str:
        return f"{v:.2f}".rstrip("0").rstrip(".")

    def moveTo(self, p):
        self.parts.append(f"{self._f(p[0])} {self._f(p[1])} m")

    def lineTo(self, p):
        self.parts.append(f"{self._f(p[0])} {self._f(p[1])} l")

    def curveTo(self, *points):
        (x1, y1), (x2, y2), (x3, y3) = points[-3:]
        self.parts.append(" ".join(self._f(v) for v in (x1, y1, x2, y2, x3, y3)) + " c")

    def qCurveTo(self, *points):
        from fontTools.pens.basePen import decomposeQuadraticSegment

        current = self._current
        for (cx, cy), (ex, ey) in decomposeQuadraticSegment(points):
            x0, y0 = current
            c1 = (x0 + 2 / 3 * (cx - x0), y0 + 2 / 3 * (cy - y0))
            c2 = (ex + 2 / 3 * (cx - ex), ey + 2 / 3 * (cy - ey))
            self.curveTo(c1, c2, (ex, ey))
            current = (ex, ey)

    def closePath(self):
        self.parts.append("h")

    def endPath(self):
        pass


def outline_ops(text: str, x: float, y: float, size: float) -> bytes:
    """`text` as filled glyph outlines, without any font."""
    from fontTools.pens.recordingPen import RecordingPen
    from fontTools.pens.transformPen import TransformPen

    font = base_font()
    glyph_set = font.getGlyphSet()
    scale = size / font["head"].unitsPerEm
    pen = _PathPen()
    cursor = x
    for char in text:
        name = glyph_for(char)
        record = RecordingPen()
        glyph_set[name].draw(TransformPen(record, (scale, 0, 0, scale, cursor, y)))
        current = (0.0, 0.0)
        for op, args in record.value:
            if op == "moveTo":
                current = args[0]
                pen.moveTo(args[0])
            elif op == "lineTo":
                current = args[0]
                pen.lineTo(args[0])
            elif op == "curveTo":
                pen.curveTo(*args)
                current = args[-1]
            elif op == "qCurveTo":
                pen._current = current
                pen.qCurveTo(*args)
                current = args[-1]
            elif op == "closePath":
                pen.closePath()
        cursor += font["hmtx"][name][0] * scale
    return ops(*pen.parts, "f")


def type3_font(pdf, text_chars: str, with_tounicode: bool):
    """A Type 3 font whose procedures draw each glyph's outline; returns (ref, codes)."""
    chars = sorted(set(text_chars))
    codes = {c: c.encode("cp1252")[0] for c in chars}
    procs = {}
    widths_by_code = {}
    for c in chars:
        name = glyph_for(c)
        width = advance(name)
        body = f"{width} 0 d0\n".encode("ascii") + outline_ops(c, 0, 0, 1000)
        procs[name] = pdf.add(Stream(body))
        widths_by_code[codes[c]] = width
    first, last = min(codes.values()), max(codes.values())
    differences = []
    for c in sorted(chars, key=lambda ch: codes[ch]):
        differences += [codes[c], Name(glyph_for(c))]
    font = {"Type": Name("Font"), "Subtype": Name("Type3"), "FontBBox": [-203, -303, 1050, 910],
            "FontMatrix": [0.001, 0, 0, 0.001, 0, 0], "CharProcs": procs,
            "Encoding": {"Type": Name("Encoding"), "Differences": differences},
            "FirstChar": first, "LastChar": last,
            "Widths": [widths_by_code.get(code, 0) for code in range(first, last + 1)],
            "Resources": {}}
    if with_tounicode:
        font["ToUnicode"] = pdf.add(Stream(tounicode({codes[c]: c for c in chars})))
    return pdf.add(font), codes
