# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# Patterns for the layout cases the reading order must survive (docs/DESIGN.md
# §7): words placed without space glyphs, words split by kerning, ligature
# glyphs with and without a ToUnicode, hyphenation across lines and pages,
# columns, rotation, right-to-left text, ruled tables, running headers and
# footers, and /ActualText spans.

from __future__ import annotations

from synth import fonts
from synth.patterns.kit import TEXTS, Name, Pdf, Specimen, helvetica, info, ops, page, show, single
from synth.writer import hexstr, literal

WIDTHS_SCALE = 1 / 1000


def _helv_width(text: str, size: float) -> float:
    return sum(fonts.advance(fonts.glyph_for(c)) for c in text) * size * WIDTHS_SCALE


def no_space_glyphs(count=3):
    """Every word is placed by its own Td; no space character is ever shown."""
    out = []
    for i in range(count):
        text = TEXTS[i]
        pdf = Pdf()
        res = {"Font": {"F1": pdf.add(helvetica("Helvetica"))}}
        body = [b"BT /F1 12 Tf 72 700 Td"]
        x_prev = 72
        x = 72
        for n, word in enumerate(text.split()):
            if n:
                body.append(f"{x - x_prev:.2f} 0 Td".encode("ascii"))
                x_prev = x
            body.append(show(word) + b" Tj")
            x += _helv_width(word, 12) + 3.5
        body.append(b"ET")
        out.append(Specimen(single(ops(*body), res, "no_space_glyphs", pdf=pdf), ["no_space_glyphs"], text,
                            notes="words separated by Td moves only"))
    return out


def kerning_split(count=3):
    """Words split into TJ pieces with kerning numbers, and spaces made by large adjustments."""
    out = []
    for i in range(count):
        text = TEXTS[i + 1]
        pdf = Pdf()
        res = {"Font": {"F1": pdf.add(helvetica())}}
        items = []
        for n, word in enumerate(text.split()):
            if n:
                items.append(b"-280")
            cut = max(1, len(word) // 2)
            items.append(show(word[:cut]))
            if len(word) > 1:
                items.append(b"%d" % (12 + 3 * n))
                items.append(show(word[cut:]))
        content = ops("BT /F1 12 Tf 72 700 Td", b"[" + b" ".join(items) + b"] TJ", "ET")
        out.append(Specimen(single(content, res, "kerning_split", pdf=pdf), ["kerning_split", "no_space_glyphs"],
                            text, notes="words split across TJ kerning, spaces as -280 adjustments"))
    return out


LIGATURE_TEXTS = ("The office staff fill the official file", "A flight of affine fixes", "Profit flows to the fifth fund")


def _ligature_runs(text: str):
    """Splits text into characters and fi/fl ligatures."""
    runs, i = [], 0
    while i < len(text):
        if text[i:i + 2] in ("fi", "fl"):
            runs.append(text[i:i + 2])
            i += 2
        else:
            runs.append(text[i])
            i += 1
    return runs


def ligatures(count=3):
    out = []
    for i in range(count):
        text = LIGATURE_TEXTS[i % len(LIGATURE_TEXTS)]
        runs = _ligature_runs(text)
        lig_char = {"fi": "ﬁ", "fl": "ﬂ"}
        drawn = "".join(lig_char.get(r, r) for r in runs)
        pdf = Pdf()
        with_map = i != 1
        chars = sorted(set(drawn))
        code_of = {c: 33 + n for n, c in enumerate(chars)}
        if with_map:
            override = {code_of[l]: s for s, l in lig_char.items() if l in code_of}
            ref, codes = fonts.truetype_simple(pdf, drawn, base_name=f"LIGA{i}+LiberationSans",
                                               code_for=lambda c: code_of[c],
                                               encoding=_name_encoding(chars, code_of),
                                               unicode_override=override)
        else:
            ref, codes = fonts.truetype_simple(pdf, drawn, base_name=f"LIGB{i}+LiberationSans",
                                               with_tounicode=False, code_for=lambda c: code_of[c],
                                               encoding=_name_encoding(chars, code_of))
        content = ops("BT /F1 14 Tf 72 700 Td", literal(fonts.encode_simple(drawn, codes)) + b" Tj", "ET")
        tags = ["ligatures", "tounicode_correct" if with_map else "tounicode_missing", "differences_aglnames"]
        out.append(Specimen(single(content, {"Font": {"F1": ref}}, "ligatures", pdf=pdf), tags, text,
                            notes="fi and fl ligature glyphs" + (" mapped by ToUnicode" if with_map else ", names only")))
    return out


def _name_encoding(chars, code_of):
    from fontTools.agl import UV2AGL

    diffs = []
    for c in chars:
        name = {"ﬁ": "fi", "ﬂ": "fl"}.get(c) or UV2AGL.get(ord(c)) or f"uni{ord(c):04X}"
        diffs += [code_of[c], Name(name)]
    return {"Type": Name("Encoding"), "Differences": diffs}


def hyphenation_line(count=3):
    out = []
    samples = (("The redaction of personal infor-", "mation follows the policy"),
               ("Every docu-", "ment keeps its layout"),
               ("An extrac-", "tor must join the halves"))
    for i in range(count):
        first, second = samples[i % len(samples)]
        pdf = Pdf()
        res = {"Font": {"F1": pdf.add(helvetica())}}
        content = ops("BT /F1 12 Tf 16 TL 72 700 Td", show(first) + b" Tj", b"T* " + show(second) + b" Tj", "ET")
        out.append(Specimen(single(content, res, "hyphenation_line", pdf=pdf), ["hyphenation_line"],
                            first + "\n" + second, notes="a word hyphenated across two lines"))
    return out


def hyphenation_page(count=3):
    out = []
    samples = (("closing the first page with a hyphen-", "ated word that ends on the second page"),
               ("the contract with Example Hol-", "ding is renewed"),
               ("the account of Carl Sam-", "ple is closed"))
    for i in range(count):
        first, second = samples[i % len(samples)]
        pdf = Pdf()
        font = pdf.add(helvetica())
        pages = [page(pdf, ops("BT /F1 12 Tf 72 80 Td", show(first) + b" Tj", "ET"), {"Font": {"F1": font}}),
                 page(pdf, ops("BT /F1 12 Tf 72 740 Td", show(second) + b" Tj", "ET"), {"Font": {"F1": font}})]
        pdf.document(pages, None, info("hyphenation_page"))
        out.append(Specimen(pdf.build(), ["hyphenation_page"], first + "\n" + second,
                            notes="a word hyphenated across the page break"))
    return out


def multi_column(count=3):
    out = []
    left = ["Column one starts here", "with the first part of", "the story about Jane Doe", "and her invoices"]
    right = ["Column two continues", "with the second part", "about Carl Sample", "and his account"]
    for i in range(count):
        pdf = Pdf()
        res = {"Font": {"F1": pdf.add(helvetica())}}
        cols = 2 + (i % 2)
        width = 468 / cols
        parts = []
        texts = [left, right, ["A third column", "for wide pages", "with more text", "at the end"]][:cols]
        for c, col in enumerate(texts):
            parts.append(f"BT /F1 10 Tf 14 TL {72 + c * width:.1f} {700 - 40 * i} Td".encode())
            for n, line in enumerate(col):
                parts.append((b"" if n == 0 else b"T* ") + show(line) + b" Tj")
            parts.append(b"ET")
        text = "\n".join("\n".join(c) for c in texts)
        out.append(Specimen(single(ops(*parts), res, "multi_column", pdf=pdf), ["multi_column"], text,
                            notes=f"{cols} columns drawn column by column"))
    return out


def rotated(count=3):
    out = []
    for i in range(count):
        text = TEXTS[i]
        pdf = Pdf()
        res = {"Font": {"F1": pdf.add(helvetica())}}
        extra = {}
        if i == 0:
            content = ops("BT /F1 14 Tf 0.7071 0.7071 -0.7071 0.7071 150 300 Tm", show(text) + b" Tj", "ET")
            note = "text at 45 degrees"
        elif i == 1:
            content = ops("BT /F1 14 Tf 0 1 -1 0 300 100 Tm", show(text) + b" Tj", "ET")
            note = "text at 90 degrees"
        else:
            content = ops("BT /F1 14 Tf 72 700 Td", show(text) + b" Tj", "ET")
            extra = {"Rotate": 90}
            note = "page /Rotate 90"
        out.append(Specimen(single(content, res, "rotated", page_extra=extra, pdf=pdf), ["rotated"], text,
                            notes=note))
    return out


HEBREW = ("שלום עולם", "חשבון מספר אחד")


def right_to_left(count=2):
    out = []
    for i in range(count):
        logical = HEBREW[i % len(HEBREW)]
        pdf = Pdf()
        ref, codes = fonts.cid_truetype(pdf, logical, base_name=f"HEBR{i}+LiberationSans")
        visual = logical[::-1] if i == 0 else logical
        content = ops("BT /F1 16 Tf 200 700 Td", hexstr(fonts.encode_cid(visual, codes)) + b" Tj", "ET")
        out.append(Specimen(single(content, {"Font": {"F1": ref}}, "right_to_left", pdf=pdf),
                            ["right_to_left", "identity_cid", "tounicode_correct"], logical,
                            notes="Hebrew drawn in " + ("visual order" if i == 0 else "logical order")))
    return out


def table(count=3):
    out = []
    rows = [("Name", "Account", "Amount"), ("Jane Doe", "LU28 0019", "1200"), ("Carl Sample", "LU12 3456", "350"),
            ("Maria Testing", "LU99 0001", "42")]
    for i in range(count):
        pdf = Pdf()
        res = {"Font": {"F1": pdf.add(helvetica())}}
        parts = [b"0.5 w"]
        x0, y0, cw, rh = 72, 700, 150, 20
        n_rows = len(rows) - (i % 2)
        for r in range(n_rows + 1):
            parts.append(f"{x0} {y0 - r * rh} m {x0 + 3 * cw} {y0 - r * rh} l S".encode())
        for c in range(4):
            parts.append(f"{x0 + c * cw} {y0} m {x0 + c * cw} {y0 - n_rows * rh} l S".encode())
        order = range(3) if i < 2 else (2, 0, 1)
        for r in range(n_rows):
            for c in order:
                parts.append(f"BT /F1 10 Tf {x0 + c * cw + 4} {y0 - r * rh - 14} Td".encode()
                             + b" " + show(rows[r][c]) + b" Tj ET")
        text = "\n".join(" ".join(row) for row in rows[:n_rows])
        out.append(Specimen(single(ops(*parts), res, "table", pdf=pdf), ["table"], text,
                            notes="ruled table" + (", cells drawn out of column order" if i >= 2 else "")))
    return out


def header_footer(count=3):
    out = []
    for i in range(count):
        pdf = Pdf()
        font = pdf.add(helvetica())
        pages = []
        texts = []
        for n in range(3):
            body = TEXTS[(i + n) % len(TEXTS)]
            content = ops("BT /F1 9 Tf 72 760 Td", show("Fictional Holding confidential report") + b" Tj", "ET",
                          "BT /F1 12 Tf 72 600 Td", show(body) + b" Tj", "ET",
                          "BT /F1 9 Tf 290 40 Td", show(f"Page {n + 1} of 3") + b" Tj", "ET")
            texts.append(f"Fictional Holding confidential report {body} Page {n + 1} of 3")
            pages.append(page(pdf, content, {"Font": {"F1": font}}))
        pdf.document(pages, None, info("header_footer"))
        out.append(Specimen(pdf.build(), ["header_footer"], "\n".join(texts),
                            notes="a running header and a page-number footer on three pages"))
    return out


def actual_text(count=3):
    out = []
    for i in range(count):
        text = TEXTS[i]
        pdf = Pdf()
        res = {"Font": {"F1": pdf.add(helvetica())}}
        words = text.split()
        head, word, tail = " ".join(words[:2]), words[2], " ".join(words[3:])
        content = ops("BT /F1 12 Tf 72 700 Td", show(head + " ") + b" Tj",
                      b"/Span <</ActualText " + literal(word.encode("ascii")) + b">> BDC "
                      + show(word.upper()) + b" Tj EMC", show(" " + tail) + b" Tj", "ET")
        out.append(Specimen(single(content, res, "actual_text", pdf=pdf), ["actual_text"],
                            f"{head} {word.upper()} {tail}", ladder_text=f"{head} {word} {tail}",
                            notes="an /ActualText span replacing the drawn word"))
    return out


PATTERNS = {
    "no_space_glyphs": no_space_glyphs,
    "kerning_split": kerning_split,
    "ligatures": ligatures,
    "hyphenation_line": hyphenation_line,
    "hyphenation_page": hyphenation_page,
    "multi_column": multi_column,
    "rotated": rotated,
    "right_to_left": right_to_left,
    "table": table,
    "header_footer": header_footer,
    "actual_text": actual_text,
}

