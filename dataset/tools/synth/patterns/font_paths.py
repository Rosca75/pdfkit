# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# Patterns for the font decoding paths of the ladder (docs/DESIGN.md §5): a
# wrong ToUnicode, /Differences with uniXXXX, gNN and Cdd names, a symbolic
# TrueType read through its (3,0) cmap, a Type 1 program's built-in encoding, a
# CFF charset, Identity-H and predefined CMaps, vertical writing, Type 3 fonts,
# text drawn as outlines and fonts without an embedded program. Each pattern
# draws a known text so the reading gate knows what the page says.

from __future__ import annotations

from synth import fonts
from synth.patterns.kit import TEXTS, Name, Pdf, Specimen, Stream, ops, single
from synth.writer import hexstr, literal


def _show_simple(text: str, codes: dict, size=14, x=72, y=700, font="F1") -> bytes:
    return ops(f"BT /{font} {size} Tf {x} {y} Td", literal(fonts.encode_simple(text, codes)) + b" Tj", "ET")


def _show_cid(text: str, codes: dict, size=14, x=72, y=700, font="F1") -> bytes:
    return ops(f"BT /{font} {size} Tf {x} {y} Td", hexstr(fonts.encode_cid(text, codes)) + b" Tj", "ET")


def tounicode_wrong(count=5):
    """A ToUnicode that maps every letter to the next one: the drawn text is not the extracted text."""
    out = []
    for i in range(count):
        text = TEXTS[i % len(TEXTS)]
        pdf = Pdf()
        wrong = {}
        for c in set(text):
            if c.isalpha():
                shifted = chr(ord(c) + 1) if c not in "zZ" else ("a" if c == "z" else "A")
                wrong[c.encode("cp1252")[0]] = shifted
        ref, codes = fonts.truetype_simple(pdf, text, base_name=f"WRONG{i}+LiberationSans",
                                           unicode_override=wrong)
        ladder = "".join(wrong.get(c.encode("cp1252")[0], c) for c in text)
        data = single(_show_simple(text, codes), {"Font": {"F1": ref}}, "tounicode_wrong", pdf=pdf)
        out.append(Specimen(data, ["tounicode_wrong"], text, ladder_text=ladder,
                            drop_tags=["tounicode_correct"],
                            notes="ToUnicode shifts every letter by one; the ladder reports the shifted text"))
    return out


def _differences(name_for, tag, count, codes_from=1):
    out = []
    for i in range(count):
        text = TEXTS[(i + codes_from) % len(TEXTS)]
        pdf = Pdf()
        chars = sorted(set(text))
        code_of = {c: 33 + n for n, c in enumerate(chars)}
        diffs = []
        for c in chars:
            diffs += [code_of[c], Name(name_for(c, code_of[c]))]
        enc = {"Type": Name("Encoding"), "Differences": diffs}
        ref, codes = fonts.truetype_simple(pdf, text, base_name=f"DIFF{i}+LiberationSans",
                                           with_tounicode=False, encoding=enc,
                                           code_for=lambda c: code_of[c])
        data = single(_show_simple(text, codes), {"Font": {"F1": ref}}, tag, pdf=pdf)
        out.append(Specimen(data, [tag, "tounicode_missing"], text,
                            notes=f"/Differences with {tag.split('_')[1]} names, no ToUnicode"))
    return out


def differences_uni(count=3):
    return _differences(lambda c, code: f"uni{ord(c):04X}", "differences_uni", count, 0)


def differences_gnn(count=5):
    """gNN names the glyph by its index in the embedded subset."""
    out = []
    for i in range(count):
        text = TEXTS[(i + 2) % len(TEXTS)]
        pdf = Pdf()
        chars = sorted(set(text))
        glyphs = [fonts.glyph_for(c) for c in chars]
        _, gids = fonts.subset_bytes(glyphs)
        code_of = {c: 33 + n for n, c in enumerate(chars)}
        diffs = []
        for c in chars:
            diffs += [code_of[c], Name(f"g{gids[fonts.glyph_for(c)]}")]
        enc = {"Type": Name("Encoding"), "Differences": diffs}
        ref, codes = fonts.truetype_simple(pdf, text, base_name=f"GNN{i}+LiberationSans",
                                           with_tounicode=False, encoding=enc,
                                           code_for=lambda c: code_of[c])
        data = single(_show_simple(text, codes), {"Font": {"F1": ref}}, "differences_gnn", pdf=pdf)
        out.append(Specimen(data, ["differences_gnn", "tounicode_missing"], text,
                            notes="/Differences with gNN names (glyph index), no ToUnicode"))
    return out


def differences_cdd(count=5):
    return _differences(lambda c, code: f"C{ord(c)}", "differences_cdd", count, 3)


def symbolic_truetype(count=2):
    out = []
    for i in range(count):
        text = TEXTS[(i + 1) % len(TEXTS)]
        pdf = Pdf()
        ref, codes = fonts.truetype_simple(pdf, text, base_name=f"SYMB{i}+LiberationSans",
                                           with_tounicode=False, symbolic=True,
                                           code_for=lambda c: ord(c))
        data = single(_show_simple(text, codes), {"Font": {"F1": ref}}, "symbolic_truetype", pdf=pdf)
        out.append(Specimen(data, ["symbolic_truetype", "tounicode_missing"], text,
                            notes="symbolic TrueType, (3,0) cmap at 0xF000 plus the code, no encoding"))
    return out


def type1_builtin(count=2):
    out = []
    for i in range(count):
        text = TEXTS[(i + 4) % len(TEXTS)]
        pdf = Pdf()
        program, l1, l2, l3, codes = fonts.type1_program(text, f"TONE{i}+LiberationSans")
        descriptor = pdf.add({
            "Type": Name("FontDescriptor"), "FontName": Name(f"TONE{i}+LiberationSans"), "Flags": 4,
            "FontBBox": [-203, -303, 1050, 910], "ItalicAngle": 0, "Ascent": 905, "Descent": -212,
            "CapHeight": 729, "StemV": 80,
            "FontFile": pdf.add(Stream(program, {"Length1": l1, "Length2": l2, "Length3": l3})),
        })
        first, last = min(codes.values()), max(codes.values())
        widths = [0] * (last - first + 1)
        for c, code in codes.items():
            widths[code - first] = fonts.advance(fonts.glyph_for(c))
        ref = pdf.add({"Type": Name("Font"), "Subtype": Name("Type1"),
                       "BaseFont": Name(f"TONE{i}+LiberationSans"), "FirstChar": first,
                       "LastChar": last, "Widths": widths, "FontDescriptor": descriptor})
        data = single(_show_simple(text, codes), {"Font": {"F1": ref}}, "type1_builtin", pdf=pdf)
        out.append(Specimen(data, ["type1_builtin", "tounicode_missing"], text,
                            notes="embedded Type 1 program, no /Encoding: its built-in encoding decides"))
    return out


def cff_charset(count=2):
    out = []
    for i in range(count):
        text = TEXTS[(i + 2) % len(TEXTS)]
        pdf = Pdf()
        program, names = fonts.cff_program(text, f"CFFC{i}+LiberationSans")
        codes = {c: c.encode("cp1252")[0] for c in names}
        descriptor = pdf.add({
            "Type": Name("FontDescriptor"), "FontName": Name(f"CFFC{i}+LiberationSans"), "Flags": 32,
            "FontBBox": [-203, -303, 1050, 910], "ItalicAngle": 0, "Ascent": 905, "Descent": -212,
            "CapHeight": 729, "StemV": 80,
            "FontFile3": pdf.add(Stream(program, {"Subtype": Name("Type1C")})),
        })
        first, last = min(codes.values()), max(codes.values())
        widths = [0] * (last - first + 1)
        for c, code in codes.items():
            widths[code - first] = fonts.advance(fonts.glyph_for(c))
        ref = pdf.add({"Type": Name("Font"), "Subtype": Name("Type1"),
                       "BaseFont": Name(f"CFFC{i}+LiberationSans"), "FirstChar": first,
                       "LastChar": last, "Widths": widths, "FontDescriptor": descriptor,
                       "Encoding": Name("WinAnsiEncoding")})
        data = single(_show_simple(text, codes), {"Font": {"F1": ref}}, "cff_charset", pdf=pdf)
        out.append(Specimen(data, ["cff_charset", "tounicode_missing"], text,
                            notes="embedded CFF (Type1C) program whose charset names the glyphs"))
    return out


def identity_cid(count=2):
    out = []
    for i in range(count):
        text = TEXTS[(i + 3) % len(TEXTS)]
        pdf = Pdf()
        with_map = i % 2 == 0
        ref, codes = fonts.cid_truetype(pdf, text, base_name=f"IDCI{i}+LiberationSans",
                                        with_tounicode=with_map)
        data = single(_show_cid(text, codes), {"Font": {"F1": ref}}, "identity_cid", pdf=pdf)
        tags = ["identity_cid", "tounicode_correct" if with_map else "tounicode_missing"]
        out.append(Specimen(data, tags, text,
                            notes="Identity-H CIDFontType2" + ("" if with_map else " without ToUnicode")))
    return out


CMAPS = ("UniGB-UCS2-H", "UniJIS-UCS2-H", "UniKS-UCS2-H", "UniCNS-UCS2-H", "UniJIS-UTF16-H")
ORDERINGS = {"GB": ("GB1", 2), "JIS": ("Japan1", 4), "KS": ("Korea1", 1), "CNS": ("CNS1", 3)}


def _predefined(cmap: str, text: str, title: str, vertical=False) -> bytes:
    pdf = Pdf()
    key = next(k for k in ORDERINGS if f"Uni{k}-" in cmap)
    ordering, supplement = ORDERINGS[key]
    descriptor = pdf.add({"Type": Name("FontDescriptor"), "FontName": Name("MSungStd-Light"),
                          "Flags": 6, "FontBBox": [-160, -259, 1015, 888], "ItalicAngle": 0,
                          "Ascent": 880, "Descent": -120, "CapHeight": 880, "StemV": 93})
    cid = pdf.add({"Type": Name("Font"), "Subtype": Name("CIDFontType0"),
                   "BaseFont": Name("MSungStd-Light"),
                   "CIDSystemInfo": {"Registry": b"Adobe", "Ordering": ordering.encode(), "Supplement": supplement},
                   "FontDescriptor": descriptor, "DW": 1000})
    ref = pdf.add({"Type": Name("Font"), "Subtype": Name("Type0"),
                   "BaseFont": Name(f"MSungStd-Light-{cmap}"), "Encoding": Name(cmap),
                   "DescendantFonts": [cid]})
    content = ops("BT /F1 14 Tf 72 700 Td", hexstr(text.encode("utf-16-be")) + b" Tj", "ET")
    if vertical:
        content = ops("BT /F1 14 Tf 300 740 Td", hexstr(text.encode("utf-16-be")) + b" Tj", "ET")
    return single(content, {"Font": {"F1": ref}}, title, pdf=pdf)


def predefined_cmap(count=2):
    out = []
    for i in range(count):
        cmap = CMAPS[i % len(CMAPS)]
        text = TEXTS[i % len(TEXTS)]
        data = _predefined(cmap, text, "predefined_cmap")
        out.append(Specimen(data, ["predefined_cmap", "font_not_embedded"], text,
                            notes=f"predefined CMap {cmap}, no embedded program"))
    return out


def vertical(count=2):
    out = []
    text = "Vertical writing sample"
    data = _predefined("UniJIS-UCS2-V", text, "vertical", vertical=True)
    out.append(Specimen(data, ["vertical", "predefined_cmap", "font_not_embedded"], text,
                        notes="vertical predefined CMap UniJIS-UCS2-V"))
    if count > 1:
        pdf = Pdf()
        ref, codes = fonts.cid_truetype(pdf, text, base_name="VERT1+LiberationSans", encoding="Identity-V")
        content = ops("BT /F1 14 Tf 300 740 Td", hexstr(fonts.encode_cid(text, codes)) + b" Tj", "ET")
        out.append(Specimen(single(content, {"Font": {"F1": ref}}, "vertical", pdf=pdf),
                            ["vertical", "identity_cid", "tounicode_correct"], text,
                            notes="Identity-V CIDFontType2"))
    return out


def type3(count=2):
    out = []
    for i in range(count):
        text = TEXTS[(i + 2) % len(TEXTS)]
        pdf = Pdf()
        with_map = i % 2 == 0
        ref, codes = fonts.type3_font(pdf, text, with_map)
        data = single(_show_simple(text, codes, size=14), {"Font": {"F1": ref}}, "type3", pdf=pdf)
        out.append(Specimen(data, ["type3", "tounicode_correct" if with_map else "tounicode_missing",
                                   "differences_aglnames"], text,
                            notes="Type 3 font drawing glyph outlines" + ("" if with_map else ", no ToUnicode")))
    return out


def text_as_outlines(count=5):
    out = []
    for i in range(count):
        text = TEXTS[i % len(TEXTS)]
        content = ops("0 0 0 rg") + fonts.outline_ops(text, 72, 700 - 20 * i, 16)
        data = single(content, {}, "text_as_outlines")
        out.append(Specimen(data, ["text_as_outlines"], "", notes=f"outlines of: {text}"))
    return out


def font_not_embedded(count=2):
    names = ("Verdana", "Tahoma", "Georgia", "Calibri", "Garamond")
    out = []
    for i in range(count):
        text = TEXTS[i % len(TEXTS)]
        pdf = Pdf()
        descriptor = pdf.add({"Type": Name("FontDescriptor"), "FontName": Name(names[i]), "Flags": 32,
                              "FontBBox": [-200, -300, 1100, 950], "ItalicAngle": 0, "Ascent": 900,
                              "Descent": -210, "CapHeight": 720, "StemV": 80})
        widths = [fonts.advance(fonts.glyph_for(chr(c))) if chr(c).isprintable() else 0 for c in range(32, 127)]
        ref = pdf.add({"Type": Name("Font"), "Subtype": Name("TrueType"), "BaseFont": Name(names[i]),
                       "FirstChar": 32, "LastChar": 126, "Widths": widths,
                       "FontDescriptor": descriptor, "Encoding": Name("WinAnsiEncoding")})
        content = ops("BT /F1 14 Tf 72 700 Td", literal(text.encode("cp1252")) + b" Tj", "ET")
        out.append(Specimen(single(content, {"Font": {"F1": ref}}, "font_not_embedded", pdf=pdf),
                            ["font_not_embedded", "tounicode_missing"], text,
                            notes=f"TrueType {names[i]} named but not embedded"))
    return out


PATTERNS = {
    "tounicode_wrong": tounicode_wrong,
    "differences_uni": differences_uni,
    "differences_gnn": differences_gnn,
    "differences_cdd": differences_cdd,
    "symbolic_truetype": symbolic_truetype,
    "type1_builtin": type1_builtin,
    "cff_charset": cff_charset,
    "identity_cid": identity_cid,
    "predefined_cmap": predefined_cmap,
    "vertical": vertical,
    "type3": type3,
    "text_as_outlines": text_as_outlines,
    "font_not_embedded": font_not_embedded,
}
