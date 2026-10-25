# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# The page-selection leak fixture of docs/DESIGN.md §16: four pages, each
# owning a marker in every channel a page owns or names. Rows 90 to 93 are drawn
# content that must survive exactly when their page is kept: 90 the page's own
# content stream, 91 a form only the page draws, 92 an image only the page draws,
# 93 an XObject entry of the /Resources dictionary every page inherits from the
# /Pages node, drawn only by the page. Rows 01, 02, 03, 09, 13, 14 and 15 are
# stripped channels whose markers must never survive any output: an incremental
# update that revised the page, an unreferenced object, a thumbnail, an
# annotation and its popup, a structure element whose /Pg is the page, an
# optional-content group only the page uses and a page label range starting at
# the page. The page number is the counter NN of every marker.
#
# One specimen has a classic cross-reference table and every page below one
# /Pages node; the other has a cross-reference stream and a two-level page tree,
# so the inherited resources reach the pages through an intermediate node that
# page selection removes.

from __future__ import annotations

import copy

from synth.channels.kit import (append_stream_update, base_size, form, gray_image, mark, mark_bytes,
                                sample_bytes, text_chunk, verify)
from synth.patterns.kit import Name, Pdf, Specimen, Stream, helvetica, info, ops
from synth.writer import append_update

PAGES = 4
TAGS = ["ch_page_owned", "ch_revisions", "ch_unreferenced", "ch_thumbnail", "ch_annotation",
        "ch_struct_tree", "ch_optional_content", "ch_page_labels"]
STRIPPED_ROWS = {1, 2, 3, 9, 13, 14, 15}


def page_lines(n: int) -> list[str]:
    """The text each page draws, in order: the ordinary line, the markers 90, 91 and 93, a layer line."""
    return [f"Leak fixture page {n} ordinary line", mark(90, n), mark(91, n), mark(93, n),
            f"Layer {n} is shown"]


def build(xref: str, two_levels: bool) -> Specimen:
    pdf = Pdf()
    font = pdf.add(helvetica())
    revised = [pdf.reserve() for _ in range(PAGES)]
    root_pages = pdf.reserve()
    mids = [pdf.reserve() for _ in range(2)] if two_levels else []
    page_refs = [pdf.reserve() for _ in range(PAGES)]
    resources = {"Font": {"F1": font}, "XObject": {}, "Properties": {}}
    structure_root = pdf.reserve()
    elements, groups, labels = [], [], []
    old_pages: dict[int, dict] = {}
    planted: list[str] = []
    for n in range(1, PAGES + 1):
        # 91: a form only page n draws.
        resources["XObject"][f"Fm{n}"] = pdf.add(form(
            text_chunk(mark(91, n), x=4, y=12, size=10), 300, 30, resources={"Font": {"F1": font}}))
        # 92: an image only page n draws; its samples begin with the marker.
        resources["XObject"][f"Im{n}"] = pdf.add(gray_image(
            sample_bytes(mark_bytes(92, n), 15, 2), 15, 2, compress=True))
        # 93: an XObject entry of the inherited dictionary, drawn only by page n.
        resources["XObject"][f"Xo{n}"] = pdf.add(form(
            text_chunk(mark(93, n), x=4, y=12, size=10), 300, 30, resources={"Font": {"F1": font}}))
        # 14: a group only page n uses.
        group = pdf.add({"Type": Name("OCG"), "Name": mark(14, n)})
        groups.append(group)
        resources["Properties"][f"oc{n}"] = group
        # 03: a thumbnail whose samples begin with the marker.
        thumb = pdf.add(gray_image(sample_bytes(mark_bytes(3, n), 15, 4), 15, 4))
        # 09: an annotation and its popup.
        annot = pdf.reserve()
        popup = pdf.add({"Type": Name("Annot"), "Subtype": Name("Popup"), "Rect": [400, 500, 560, 580],
                         "Parent": annot, "Open": False, "Contents": mark(9, n)})
        pdf.set(annot, {"Type": Name("Annot"), "Subtype": Name("Text"), "Rect": [500, 700, 520, 720],
                        "Contents": mark(9, n), "Name": Name("Comment"), "Popup": popup})
        # 02: an object no reference reaches.
        pdf.add({"Orphan": mark(2, n)})
        # 13: a structure element whose /Pg is page n.
        element = pdf.add({"Type": Name("StructElem"), "S": Name("P"), "P": structure_root,
                           "Pg": page_refs[n - 1], "K": 0, "ActualText": mark(13, n)})
        elements.append(element)
        # 15: a page label range starting at page n.
        labels += [n - 1, {"S": Name("D"), "P": mark(15, n)}]
        # 01: the content of the earlier revision, which drew the marker.
        old_content = pdf.add(Stream(ops(text_chunk(page_lines(n)[0]), text_chunk(mark(1, n), y=680))))
        content = [b"/P << /MCID 0 >> BDC", text_chunk(page_lines(n)[0], y=740),
                   text_chunk(mark(90, n), y=700), b"EMC",
                   b"/OC /oc%d BDC " % n + text_chunk(page_lines(n)[4], y=660) + b" EMC",
                   b"q 1 0 0 1 72 600 cm /Fm%d Do Q" % n,
                   b"q 120 0 0 8 72 560 cm /Im%d Do Q" % n,
                   b"q 1 0 0 1 72 500 cm /Xo%d Do Q" % n]
        pdf.set(revised[n - 1], Stream(ops(*content)))
        parent = (mids[(n - 1) // 2] if two_levels else root_pages)
        page = {"Type": Name("Page"), "Parent": parent, "MediaBox": [0, 0, 612, 792],
                "Contents": revised[n - 1], "Thumb": thumb, "Annots": [annot, popup],
                "StructParents": n - 1}
        pdf.set(page_refs[n - 1], page)
        old = dict(page)
        old["Contents"] = old_content
        old_pages[page_refs[n - 1].num] = old
    pdf.set(structure_root, {"Type": Name("StructTreeRoot"), "K": elements,
                             "ParentTree": {"Nums": [x for n, e in enumerate(elements) for x in (n, [e])]}})
    if two_levels:
        for index, mid in enumerate(mids):
            pdf.set(mid, {"Type": Name("Pages"), "Parent": root_pages, "Kids": page_refs[2 * index:2 * index + 2],
                          "Count": 2})
        pdf.set(root_pages, {"Type": Name("Pages"), "Kids": mids, "Count": PAGES, "Resources": resources})
    else:
        pdf.set(root_pages, {"Type": Name("Pages"), "Kids": page_refs, "Count": PAGES, "Resources": resources})
    pdf.root = pdf.add({"Type": Name("Catalog"), "Pages": root_pages, "StructTreeRoot": structure_root,
                        "MarkInfo": {"Marked": True}, "PageLabels": {"Nums": labels},
                        "OCProperties": {"OCGs": groups, "D": {"ON": groups, "Order": groups}}})
    pdf.info = pdf.add(info("ch_page_owned"))
    base = copy.copy(pdf)
    base.objects = {num: value for num, value in pdf.objects.items() if num not in {r.num for r in revised}}
    base.objects.update(old_pages)
    data = base.build(xref)
    update = {r.num: pdf.objects[r.num] for r in revised}
    update.update({num: pdf.objects[num] for num in old_pages})
    if xref == "stream":
        data = append_stream_update(data, update, pdf.root, pdf.info)
    else:
        data = append_update(data, update, pdf.root, base_size(data), info=pdf.info)
    for n in range(1, PAGES + 1):
        planted += [mark(row, n) for row in (1, 2, 3, 9, 13, 14, 15, 90, 91, 92, 93)]
    verify(data, planted, STRIPPED_ROWS | {90, 91, 92, 93})
    text = "\n".join("\n".join(page_lines(n)) for n in range(1, PAGES + 1))
    notes = (f"four pages, markers 01 02 03 09 13 14 15 and 90 to 93 per page; xref {xref}; "
             + ("a two-level page tree" if two_levels else "one /Pages node"))
    return Specimen(data, list(TAGS), text, notes=notes)


def ch_page_owned() -> list:
    return [build("table", False), build("stream", True)]


PATTERNS = {"mark_ch_page_owned": ch_page_owned}
