# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# The redaction traps, each a function returning one specimen that draws the
# marker in one hard way: split by a TJ kerning number, by a font change,
# across a line with a hyphen, across a page's two /Contents pieces, inside a
# form drawn on three pages, inside /ActualText, in render mode 3 over an
# image (a scan's hidden text layer), white on white, doubled for a fake bold,
# overlapped by other text, in a Type 3 font, and in an Identity-H font with
# two-byte codes. The tag of each is redact_<case>.

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from synth import fonts  # noqa: E402
from synth.patterns.kit import Name, Pdf, Specimen, Stream, helvetica, info, ops, page, show  # noqa: E402
from synth.writer import hexstr, literal  # noqa: E402

MARKER = "Zebulon Quarterfield"
FIRST, LAST = MARKER.split()
LEAD = "Statement taken from"
TAIL = "on the first of May"


def one_page(content: bytes, resources: dict, pdf: Pdf | None = None) -> bytes:
    pdf = pdf or Pdf()
    pdf.document([page(pdf, content, resources)], None, info("redaction trap"))
    return pdf.build()


def specimen(data: bytes, case: str, text: str, notes: str, tags=()) -> list[Specimen]:
    return [Specimen(data, [f"redact_{case}", *tags], text, notes=notes)]


def kerning():
    pdf = Pdf()
    res = {"Font": {"F1": pdf.add(helvetica())}}
    content = ops(b"BT /F1 12 Tf 72 700 Td [" + show(f"{LEAD} Zebulon Quar") + b" -35 " + show("terfield")
                  + b" 20 " + show(f" {TAIL}") + b"] TJ ET")
    return specimen(one_page(content, res, pdf), "kerning", f"{LEAD} {MARKER} {TAIL}",
                    "the marker split by TJ kerning numbers", ["kerning_split"])


def font_change():
    pdf = Pdf()
    res = {"Font": {"F1": pdf.add(helvetica()), "F2": pdf.add(helvetica("Helvetica-Bold"))}}
    content = ops(b"BT /F1 12 Tf 72 700 Td " + show(f"{LEAD} Zebu") + b" Tj /F2 12 Tf " + show("lon Quarter")
                  + b" Tj /F1 12 Tf " + show(f"field {TAIL}") + b" Tj ET")
    return specimen(one_page(content, res, pdf), "font_change", f"{LEAD} {MARKER} {TAIL}",
                    "the marker drawn in two fonts by three operators")


def hyphen():
    pdf = Pdf()
    res = {"Font": {"F1": pdf.add(helvetica())}}
    content = ops(b"BT /F1 12 Tf 16 TL 72 700 Td " + show(f"{LEAD} Zebulon Quarter-") + b" Tj T* "
                  + show(f"field {TAIL}") + b" Tj ET")
    return specimen(one_page(content, res, pdf), "hyphen", f"{LEAD} Zebulon Quarter-\nfield {TAIL}",
                    "the marker hyphenated across a line break", ["hyphenation_line"])


def two_contents():
    pdf = Pdf()
    res = {"Font": {"F1": pdf.add(helvetica())}}
    first = pdf.add(Stream(ops(b"BT /F1 12 Tf 72 700 Td " + show(f"{LEAD} Zebulon Quar") + b" Tj")))
    second = pdf.add(Stream(ops(show(f"terfield {TAIL}") + b" Tj ET")))
    pdf.document([{"Resources": res, "Contents": [first, second]}], None, info("redaction trap"))
    return specimen(pdf.build(), "two_contents", f"{LEAD} {MARKER} {TAIL}",
                    "the marker split across the page's two /Contents streams")


def form_three_pages():
    pdf = Pdf()
    font = pdf.add(helvetica())
    form = pdf.add(Stream(ops(b"BT /F1 10 Tf 10 20 Td " + show(f"Prepared for {MARKER}") + b" Tj ET"),
                          {"Type": Name("XObject"), "Subtype": Name("Form"), "BBox": [0, 0, 400, 40],
                           "Resources": {"Font": {"F1": font}}}))
    pages = []
    for number in range(1, 4):
        content = ops(b"BT /F1 12 Tf 72 700 Td " + show(f"Page {number} of the file") + b" Tj ET",
                      b"q 1 0 0 1 72 60 cm /Fm1 Do Q")
        pages.append(page(pdf, content, {"Font": {"F1": font}, "XObject": {"Fm1": form}}))
    pdf.document(pages, None, info("redaction trap"))
    text = "\n".join(f"Page {n} of the file\nPrepared for {MARKER}" for n in range(1, 4))
    return specimen(pdf.build(), "form_three_pages", text,
                    "the marker inside one form drawn on three pages", ["pages_multi"])


def actual_text():
    pdf = Pdf()
    res = {"Font": {"F1": pdf.add(helvetica())}}
    content = ops(b"BT /F1 12 Tf 72 700 Td " + show(f"{LEAD} ") + b" Tj",
                  b"/Span << /ActualText " + literal(MARKER.encode("latin-1")) + b" >> BDC "
                  + show("Z. Q.") + b" Tj EMC " + show(f" {TAIL}") + b" Tj ET")
    return specimen(one_page(content, res, pdf), "actual_text", f"{LEAD} {MARKER} {TAIL}",
                    "the marker given as the /ActualText of abbreviated glyphs", ["actual_text"])


def invisible_over_image():
    pdf = Pdf()
    width, height = 64, 16
    samples = bytes((x * 4) % 256 for _ in range(height) for x in range(width))
    image = pdf.add(Stream(samples, {
        "Type": Name("XObject"), "Subtype": Name("Image"), "Width": width, "Height": height,
        "ColorSpace": Name("DeviceGray"), "BitsPerComponent": 8}))
    res = {"Font": {"F1": pdf.add(helvetica())}, "XObject": {"Im1": image}}
    content = ops(b"q 400 0 0 100 72 650 cm /Im1 Do Q",
                  b"BT 3 Tr /F1 12 Tf 80 690 Td " + show(f"{LEAD} {MARKER} {TAIL}") + b" Tj ET")
    return specimen(one_page(content, res, pdf), "invisible_over_image", f"{LEAD} {MARKER} {TAIL}",
                    "an invisible text layer over a scanned picture", ["render_invisible", "img_flate"])


def white_on_white():
    pdf = Pdf()
    res = {"Font": {"F1": pdf.add(helvetica())}}
    content = ops(b"BT /F1 12 Tf 72 720 Td " + show("Nothing to read here") + b" Tj ET",
                  b"1 g BT /F1 12 Tf 72 700 Td " + show(f"{LEAD} {MARKER}") + b" Tj ET")
    return specimen(one_page(content, res, pdf), "white_on_white",
                    f"Nothing to read here\n{LEAD} {MARKER}", "the marker in white on the white page",
                    ["white_fill"])


def fake_bold():
    pdf = Pdf()
    res = {"Font": {"F1": pdf.add(helvetica())}}
    line = show(f"{LEAD} {MARKER} {TAIL}")
    content = ops(b"BT /F1 12 Tf 72 700 Td " + line + b" Tj ET",
                  b"BT /F1 12 Tf 72.4 700 Td " + line + b" Tj ET")
    return specimen(one_page(content, res, pdf), "fake_bold", f"{LEAD} {MARKER} {TAIL}",
                    "the line drawn twice 0.4 pt apart for a fake bold", ["duplicate"])


def overlapped():
    pdf = Pdf()
    res = {"Font": {"F1": pdf.add(helvetica())}}
    content = ops(b"BT /F1 12 Tf 72 700 Td " + show(MARKER) + b" Tj ET",
                  b"BT /F1 12 Tf 72 700 Td " + show("Approved by the board") + b" Tj ET")
    return specimen(one_page(content, res, pdf), "overlapped", f"{MARKER}\nApproved by the board",
                    "the marker with other text drawn over it", ["overlapped_text"])


def type3():
    pdf = Pdf()
    text = f"{LEAD} {MARKER}"
    ref, codes = fonts.type3_font(pdf, text, True)
    content = ops(b"BT /F1 14 Tf 72 700 Td " + literal(fonts.encode_simple(text, codes)) + b" Tj ET")
    return specimen(one_page(content, {"Font": {"F1": ref}}, pdf), "type3", text,
                    "the marker in a Type 3 font with a ToUnicode", ["type3", "tounicode_correct"])


def identity_h():
    pdf = Pdf()
    text = f"{LEAD} {MARKER} {TAIL}"
    ref, codes = fonts.cid_truetype(pdf, text)
    content = ops(b"BT /F1 12 Tf 72 700 Td " + hexstr(fonts.encode_cid(text, codes)) + b" Tj ET")
    return specimen(one_page(content, {"Font": {"F1": ref}}, pdf), "identity_h", text,
                    "the marker in two-byte Identity-H codes", ["identity_cid", "tounicode_correct"])


PATTERNS = {
    "redact_kerning": kerning,
    "redact_font_change": font_change,
    "redact_hyphen": hyphen,
    "redact_two_contents": two_contents,
    "redact_form_three_pages": form_three_pages,
    "redact_actual_text": actual_text,
    "redact_invisible_over_image": invisible_over_image,
    "redact_white_on_white": white_on_white,
    "redact_fake_bold": fake_bold,
    "redact_overlapped": overlapped,
    "redact_type3": type3,
    "redact_identity_h": identity_h,
}
