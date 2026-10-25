# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# Patterns for every hidden-text class of docs/DESIGN.md §8: text a viewer does
# not show but an extractor reaches. Each file draws one visible line and one
# hidden line holding a marker; the ladder's text holds both, because pdfkit
# extracts hidden glyphs and flags them rather than dropping them.

from __future__ import annotations

from synth.patterns.kit import TEXTS, Name, Pdf, Specimen, Stream, helvetica, info, ops, page, show

MARKERS = ("HIDDEN MARKER ALPHA 1111", "HIDDEN MARKER BRAVO 2222", "HIDDEN MARKER CHARLIE 3333")


def _visible(i: int) -> bytes:
    return b"BT /F1 12 Tf 72 720 Td " + show(TEXTS[i]) + b" Tj ET"


def _make(tag, count, hidden_ops, note, page_extra=None, catalog=None, ladder_dedupe=False):
    out = []
    for i in range(count):
        pdf = Pdf()
        font = pdf.add(helvetica())
        res = {"Font": {"F1": font}}
        marker = MARKERS[i % len(MARKERS)]
        body, extra_res, extra_cat = hidden_ops(i, marker, pdf)
        res.update(extra_res or {})
        content = ops(_visible(i), *body)
        pdf.document([page(pdf, content, res, **(page_extra or {}))], extra_cat or catalog, info(tag))
        text = TEXTS[i] + "\n" + marker
        ladder = text
        if ladder_dedupe:
            ladder = TEXTS[i] + "\n" + marker
            text = TEXTS[i] + "\n" + marker + "\n" + marker
        out.append(Specimen(pdf.build(), [tag], text, ladder_text=ladder, notes=note))
    return out


def render_invisible(count=3):
    def body(i, marker, pdf):
        mode = 7 if i == 2 else 3
        return [f"BT /F1 12 Tf {mode} Tr 72 600 Td".encode() + b" " + show(marker) + b" Tj ET"], None, None
    return _make("render_invisible", count, body, "a line in render mode 3 or 7")


def white_fill(count=3):
    colours = ("1 g", "1 1 1 rg", "0 0 0 0 k")

    def body(i, marker, pdf):
        return [f"BT {colours[i % 3]} /F1 12 Tf 72 600 Td".encode() + b" " + show(marker) + b" Tj ET"], None, None
    return _make("white_fill", count, body, "a white line on the white page")


def clipped(count=3):
    def body(i, marker, pdf):
        return [b"q 72 700 300 40 re W n", b"BT /F1 12 Tf 72 500 Td " + show(marker) + b" Tj ET", b"Q"], None, None
    return _make("clipped", count, body, "a line outside the clipping rectangle in force")


def off_media(count=3):
    places = ((-400, 600), (72, 900), (700, 300))

    def body(i, marker, pdf):
        x, y = places[i % 3]
        return [f"BT /F1 12 Tf {x} {y} Td".encode() + b" " + show(marker) + b" Tj ET"], None, None
    return _make("off_media", count, body, "a line drawn outside the media box")


def off_crop(count=3):
    def body(i, marker, pdf):
        return [b"BT /F1 12 Tf 72 300 Td " + show(marker) + b" Tj ET"], None, None
    return _make("off_crop", count, body, "a line inside the media box and outside the crop box",
                 page_extra={"CropBox": [0, 500, 612, 792]})


def optional_content_off(count=3):
    out = []
    for i in range(count):
        pdf = Pdf()
        ocg = pdf.add({"Type": Name("OCG"), "Name": f"Layer {i + 1}"})
        cat = {"OCProperties": {"OCGs": [ocg], "D": {"Order": [ocg], "OFF": [ocg]}}}
        res = {"Font": {"F1": pdf.add(helvetica())}, "Properties": {"oc1": ocg}}
        marker = MARKERS[i % 3]
        content = ops(_visible(i), b"/OC /oc1 BDC BT /F1 12 Tf 72 600 Td " + show(marker) + b" Tj ET EMC")
        pdf.document([page(pdf, content, res)], cat, info("optional_content_off"))
        out.append(Specimen(pdf.build(), ["optional_content_off", "ch_optional_content"],
                            TEXTS[i] + "\n" + marker, notes="a line in an optional content group set OFF"))
    return out


def duplicate(count=3):
    def body(i, marker, pdf):
        line = b"BT /F1 12 Tf 72 600 Td " + show(marker) + b" Tj ET"
        if i == 1:
            return [line, b"BT /F1 12 Tf 72.3 600 Td " + show(marker) + b" Tj ET"], None, None
        return [line, line], None, None
    return _make("duplicate", count, body, "the same line drawn twice at one position (fake bold)",
                 ladder_dedupe=True)


def covered(count=3):
    def body(i, marker, pdf):
        line = b"BT /F1 12 Tf 72 600 Td " + show(marker) + b" Tj ET"
        if i == 2:
            image = pdf.add(Stream(bytes([255]) * 80, {
                "Type": Name("XObject"), "Subtype": Name("Image"), "Width": 40, "Height": 2,
                "ColorSpace": Name("DeviceGray"), "BitsPerComponent": 8}))
            return [line, b"q 400 0 0 20 70 595 cm /Im1 Do Q"], {"XObject": {"Im1": image}}, None
        cover = b"q 1 g 70 595 400 20 re f Q" if i == 0 else b"q 0.95 g 70 595 400 20 re f Q"
        return [line, cover], None, None
    return _make("covered", count, body, "a line covered by a filled rectangle or an image drawn later")


PATTERNS = {
    "render_invisible": render_invisible,
    "white_fill": white_fill,
    "clipped": clipped,
    "off_media": off_media,
    "off_crop": off_crop,
    "optional_content_off": optional_content_off,
    "duplicate": duplicate,
    "covered": covered,
}

