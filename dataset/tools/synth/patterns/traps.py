# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# The redaction traps of the public record: text drawn over other text, a box
# painted over text that is still extractable (the Manafort filing), an
# incremental update that hides a name an earlier revision still holds, and a
# name removed from the page's content but kept in an object the document still
# reaches (deniable removal). Each plants an invented name.

from __future__ import annotations

from synth.patterns.kit import TEXTS, Name, Pdf, Specimen, Stream, helvetica, info, ops, page, show
from synth.writer import append_update

NAMES = ("Jane Doe", "Carl Sample", "Maria Testing")


def overlapped_text(count=3):
    out = []
    for i in range(count):
        pdf = Pdf()
        res = {"Font": {"F1": pdf.add(helvetica())}}
        name = NAMES[i % 3]
        cover = "X" * len(name) if i != 2 else "Approved by the board"
        content = ops(b"BT /F1 12 Tf 72 720 Td " + show(TEXTS[i]) + b" Tj ET",
                      b"BT /F1 12 Tf 72 600 Td " + show(name) + b" Tj ET",
                      b"BT /F1 12 Tf 72 600 Td " + show(cover) + b" Tj ET")
        pdf.document([page(pdf, content, res)], None, info("overlapped_text"))
        out.append(Specimen(pdf.build(), ["overlapped_text"], "\n".join([TEXTS[i], name, cover]),
                            notes="a name with another string drawn over it at the same position"))
    return out


def box_over_live_text(count=3):
    out = []
    for i in range(count):
        pdf = Pdf()
        res = {"Font": {"F1": pdf.add(helvetica())}}
        name = NAMES[i % 3]
        box = ("0 g 70 596 120 16 re f", "0 0 0 rg 70 596 120 16 re f", "0 0 0 1 k 70 596 120 16 re f")[i % 3]
        content = ops(b"BT /F1 12 Tf 72 720 Td " + show(TEXTS[i]) + b" Tj ET",
                      b"BT /F1 12 Tf 72 600 Td " + show(name) + b" Tj ET", box)
        pdf.document([page(pdf, content, res)], None, info("box_over_live_text"))
        out.append(Specimen(pdf.build(), ["box_over_live_text", "covered"], TEXTS[i] + "\n" + name,
                            notes="a black box painted over a name that is still text"))
    return out


def incremental_redaction(count=3):
    out = []
    for i in range(count):
        pdf = Pdf()
        res = {"Font": {"F1": pdf.add(helvetica())}}
        name = NAMES[i % 3]
        original = ops(b"BT /F1 12 Tf 72 720 Td " + show(f"Account holder {name}") + b" Tj ET")
        content_ref = pdf.add(Stream(original))
        pdf.document([{"Resources": res, "Contents": content_ref}], None, info("incremental_redaction"))
        base = pdf.build()
        revised = ops(b"BT /F1 12 Tf 72 720 Td " + show("Account holder [REDACTED]") + b" Tj ET")
        if i == 2:
            revised += ops(b"0 g 70 715 200 20 re f")
        data = append_update(base, {content_ref.num: Stream(revised)}, pdf.root, max(pdf.objects) + 1,
                             info=pdf.info)
        out.append(Specimen(data, ["incremental_redaction", "incremental_update"],
                            "Account holder [REDACTED]",
                            notes=f"the earlier revision still draws {name}"))
    return out


def deniable_removal(count=2):
    out = []
    for i in range(count):
        pdf = Pdf()
        font = pdf.add(helvetica())
        name = NAMES[i % 3]
        original = ops(b"BT /F1 12 Tf 72 720 Td " + show(f"Witness statement by {name}") + b" Tj ET")
        shown = ops(b"BT /F1 12 Tf 72 720 Td " + show("Witness statement by [REDACTED]") + b" Tj ET")
        kept = pdf.add(Stream(original))
        res = {"Font": {"F1": font}}
        extra = {}
        if i == 0:
            extra["PieceInfo"] = {"EditorApp": {"LastModified": "D:20260101000000Z", "Private": kept}}
        else:
            res["XObject"] = {"Old": pdf.add(Stream(original, {"Type": Name("XObject"), "Subtype": Name("Form"),
                                                               "BBox": [0, 0, 612, 792],
                                                               "Resources": {"Font": {"F1": font}}}))}
        pdf.document([page(pdf, shown, res, **extra)], None, info("deniable_removal"))
        where = "/PieceInfo" if i == 0 else "an unused form XObject in the page resources"
        out.append(Specimen(pdf.build(), ["deniable_removal"], "Witness statement by [REDACTED]",
                            notes=f"the original content naming {name} is kept in {where}"))
    return out


PATTERNS = {
    "overlapped_text": overlapped_text,
    "box_over_live_text": box_over_live_text,
    "incremental_redaction": incremental_redaction,
    "deniable_removal": deniable_removal,
}
