# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# The off-layer family: one-page files whose default optional-content
# configuration (ISO 32000-1 section 8.11, read: /BaseState, then /ON and
# /OFF) turns a group off, with `PDFKITMARK25X<NN>` planted only in content no
# viewer shows and `PDFKITMARK90X<NN>` only in content it shows. Every write
# removes the off content (docs/DESIGN.md section 15), so a marker of row 25
# in an output is a leak and a marker of row 90 missing from it is a loss.
# The variants: a BDC sequence (one of them inside a text object whose shown
# text follows it), an off form, an off image XObject with an inline image in
# an off sequence, nested groups (on inside off, off inside on), and a
# /BaseState /OFF configuration. The groups' names carry no marker: the row
# 14 specimen of synth/channels/rows.py plants those.

from __future__ import annotations

import common
from synth.channels.kit import find_markers, mark, readable_blobs, text_chunk
from synth.patterns.kit import Name, Pdf, Specimen, Stream, helvetica, info, ops

OFF_ROW = 25
SHOWN_ROW = 90
TAGS = ["ch_optional_content", "optional_content_off"]
SHOWN_LINE = "This page shows one layer and hides another"


def build(variant: str) -> Specimen:
    """The specimen of one variant, checked to hold every marker it plants."""
    pdf = Pdf()
    font = pdf.add(helvetica())
    on = pdf.add({"Type": Name("OCG"), "Name": "Shown layer"})
    off = pdf.add({"Type": Name("OCG"), "Name": "Hidden layer"})
    resources = {"Font": {"F1": font}, "Properties": {"on": on, "off": off}}
    defaults = {"OFF": [off], "Order": [on, off]}
    hidden = [mark(OFF_ROW, n) for n in (1, 2, 3)]
    shown = [mark(SHOWN_ROW, 1)]
    if variant == "bdc":
        shown.append(mark(SHOWN_ROW, 2))
        body = [b"/OC /on BDC " + text_chunk(shown[0], y=700) + b" EMC",
                b"/OC /off BDC q 1 0 0 RG " + text_chunk(hidden[0], y=680) +
                b" 0 0 m 100 100 l S 10 10 50 50 re f Q EMC",
                f"BT /F1 12 Tf 72 660 Td /OC /off BDC ({hidden[1]}) Tj [({hidden[2]}) -250 (x)] TJ EMC "
                f"({shown[1]}) Tj ET".encode("ascii")]
    elif variant == "form":
        hidden = hidden[:1]
        form = pdf.add(Stream(text_chunk(hidden[0], y=680),
                              {"Type": Name("XObject"), "Subtype": Name("Form"), "BBox": [0, 0, 612, 792],
                               "OC": off, "Resources": {"Font": {"F1": font}}}))
        shown_form = pdf.add(Stream(text_chunk(shown[0], y=700),
                                    {"Type": Name("XObject"), "Subtype": Name("Form"), "BBox": [0, 0, 612, 792],
                                     "OC": on, "Resources": {"Font": {"F1": font}}}))
        resources["XObject"] = {"Fo": form, "Fn": shown_form}
        body = [b"q /Fo Do Q q /Fn Do Q"]
    elif variant == "image":
        hidden = hidden[:2]
        samples = hidden[0].encode("ascii")
        image = pdf.add(Stream(samples, {"Type": Name("XObject"), "Subtype": Name("Image"),
                                         "Width": len(samples), "Height": 1,
                                         "ColorSpace": Name("DeviceGray"), "BitsPerComponent": 8,
                                         "OC": off}, compress=False))
        resources["XObject"] = {"Io": image}
        body = [b"q 100 0 0 10 72 600 cm /Io Do Q",
                b"/OC /off BDC q 100 0 0 10 72 580 cm BI /W 15 /H 1 /CS /G /BPC 8 ID " +
                hidden[1].encode("ascii") + b" EI Q EMC",
                b"/OC /on BDC " + text_chunk(shown[0], y=700) + b" EMC"]
    elif variant == "nested":
        hidden = hidden[:2]
        body = [b"/OC /off BDC /OC /on BDC " + text_chunk(hidden[0], y=680) + b" EMC EMC",
                b"/OC /on BDC /Span << /MCID 0 >> BDC /OC /off BDC " + text_chunk(hidden[1], y=660) +
                b" EMC " + text_chunk(shown[0], y=700) + b" EMC EMC"]
    elif variant == "base_off":
        hidden = hidden[:1]
        defaults = {"BaseState": Name("OFF"), "ON": [on]}
        body = [b"/OC /on BDC " + text_chunk(shown[0], y=700) + b" EMC",
                b"/OC /off BDC " + text_chunk(hidden[0], y=680) + b" EMC"]
    else:
        raise common.ToolError(f"unknown off-layer variant {variant}", "bdc, form, image, nested or base_off",
                               "name one of the variants of synth/channels/off_layers.py")
    content = ops(*body, text_chunk(SHOWN_LINE, y=740))
    page = {"Resources": resources, "Contents": pdf.add(Stream(content))}
    catalog = {"OCProperties": {"OCGs": [on, off], "D": defaults}, "PageMode": Name("UseOC")}
    pdf.document([page], catalog, info("ch_optional_content"))
    data = pdf.build("stream" if variant in ("form", "nested") else "table")
    found = find_markers(readable_blobs(data))
    missing = sorted(set(hidden + shown) - found)
    if missing:
        raise common.ToolError(f"markers {missing} are planted but no scan finds them",
                               "every planted marker in the raw bytes or a decoded stream",
                               "plant the marker as a literal string or as stream data")
    visible = [SHOWN_LINE] + shown
    notes = f"off layer, {variant}; hidden markers {', '.join(hidden)}; shown {', '.join(shown)}"
    return Specimen(data, list(TAGS), "\n".join(visible), notes=notes)


VARIANTS = ("bdc", "form", "image", "nested", "base_off")

PATTERNS = {"mark_off_layers": lambda: [build(v) for v in VARIANTS]}
