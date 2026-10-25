# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# One pattern per row of the channel table (docs/DESIGN.md §15): each file draws
# a visible line and plants a unique marker string in the channel, so the writer's
# stripping can be proven by searching every output for the marker. The three
# specimens of a channel differ in their cross-reference form (table, stream,
# hybrid), because a channel must be found however the file is indexed.

from __future__ import annotations

from synth.patterns.kit import TEXTS, Name, Pdf, Specimen, Stream, helvetica, info, ops, page, show

XREFS = ("table", "stream", "hybrid")


def marker(channel: str, i: int) -> str:
    return f"MARKER-{channel.upper()}-{i + 1:02d}"


def _channel(tag: str, plant, count=3, note=""):
    """`plant(pdf, page_dict, catalog, mark, i, page_ref)` puts the marker in the channel."""
    out = []
    for i in range(count):
        pdf = Pdf()
        font = pdf.add(helvetica())
        mark = marker(tag[3:], i)
        content = ops(b"BT /F1 12 Tf 72 720 Td " + show(TEXTS[i % len(TEXTS)]) + b" Tj ET")
        page_dict = page(pdf, content, {"Font": {"F1": font}})
        pages_ref, page_ref = pdf.reserve(), pdf.reserve()
        catalog: dict = {"Type": Name("Catalog"), "Pages": pages_ref}
        info_extra = plant(pdf, page_dict, catalog, mark, i, page_ref) or {}
        page_dict.update({"Type": Name("Page"), "Parent": pages_ref, "MediaBox": [0, 0, 612, 792]})
        pdf.set(page_ref, page_dict)
        pdf.set(pages_ref, {"Type": Name("Pages"), "Kids": [page_ref], "Count": 1})
        pdf.root = pdf.add(catalog)
        meta = info(tag)
        meta.update(info_extra)
        pdf.info = pdf.add(meta)
        out.append(Specimen(pdf.build(XREFS[i % 3]), [tag], TEXTS[i % len(TEXTS)],
                            notes=f"{note}; marker {mark}; xref {XREFS[i % 3]}"))
    return out


def _xmp(mark: str, history: bool) -> bytes:
    hist = ""
    if history:
        hist = ("<xmpMM:History><rdf:Seq><rdf:li rdf:parseType='Resource'>"
                f"<stEvt:action>saved</stEvt:action><stEvt:softwareAgent>{mark}</stEvt:softwareAgent>"
                "</rdf:li></rdf:Seq></xmpMM:History>")
    return ("<?xpacket begin='﻿' id='W5M0MpCehiHzreSzNTczkc9d'?>"
            "<x:xmpmeta xmlns:x='adobe:ns:meta/'><rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
            "<rdf:Description rdf:about='' xmlns:xmpMM='http://ns.adobe.com/xap/1.0/mm/' "
            "xmlns:stEvt='http://ns.adobe.com/xap/1.0/sType/ResourceEvent#' "
            "xmlns:dc='http://purl.org/dc/elements/1.1/'>"
            f"<dc:description><rdf:Alt><rdf:li xml:lang='x-default'>{mark}</rdf:li></rdf:Alt></dc:description>"
            f"{hist}</rdf:Description></rdf:RDF></x:xmpmeta><?xpacket end='w'?>").encode("utf-8")


def _metadata_stream(pdf, mark, history=False):
    return pdf.add(Stream(_xmp(mark, history), {"Type": Name("Metadata"), "Subtype": Name("XML")}, compress=False))


def ch_thumbnail(count=3):
    def plant(pdf, page_dict, catalog, mark, i, page_ref):
        page_dict["Thumb"] = pdf.add(Stream(bytes(range(48)) * 4, {"Width": 8, "Height": 8,
                                                                  "ColorSpace": Name("DeviceRGB"),
                                                                  "BitsPerComponent": 8}))
        return {"Subject": mark}
    return _channel("ch_thumbnail", plant, count, "a page thumbnail image, marker in /Subject")


def ch_pieceinfo(count=3):
    def plant(pdf, page_dict, catalog, mark, i, page_ref):
        data = {"MarkerApp": {"LastModified": "D:20260101000000Z", "Private": mark}}
        page_dict["PieceInfo"] = data
        catalog["PieceInfo"] = {"MarkerApp": {"LastModified": "D:20260101000000Z", "Private": mark + "-DOC"}}
    return _channel("ch_pieceinfo", plant, count, "/PieceInfo private data on the page and the catalog")


def ch_javascript(count=3):
    def plant(pdf, page_dict, catalog, mark, i, page_ref):
        action = pdf.add({"S": Name("JavaScript"), "JS": f"var marker = '{mark}';"})
        if i == 0:
            catalog["OpenAction"] = action
        else:
            catalog["Names"] = {"JavaScript": {"Names": [mark, action]}}
    return _channel("ch_javascript", plant, count, "a JavaScript action holding the marker (never run)")


def ch_embedded_file(count=3):
    def plant(pdf, page_dict, catalog, mark, i, page_ref):
        ef = pdf.add(Stream(f"Attachment body {mark}\n".encode(), {"Type": Name("EmbeddedFile")}))
        spec = pdf.add({"Type": Name("Filespec"), "F": f"{mark}.txt", "UF": f"{mark}.txt", "EF": {"F": ef}})
        if i == 2:
            page_dict["Annots"] = [pdf.add({"Type": Name("Annot"), "Subtype": Name("FileAttachment"),
                                            "Rect": [500, 700, 520, 720], "FS": spec, "Contents": mark})]
        else:
            catalog["Names"] = {"EmbeddedFiles": {"Names": [mark, spec]}}
    return _channel("ch_embedded_file", plant, count, "an embedded file named and filled with the marker")


def ch_annotation(count=3):
    def plant(pdf, page_dict, catalog, mark, i, page_ref):
        kind = ("Text", "FreeText", "Square")[i % 3]
        annot = {"Type": Name("Annot"), "Subtype": Name(kind), "Rect": [400, 600, 560, 640], "Contents": mark,
                 "T": f"Reviewer {mark}"}
        if kind == "FreeText":
            annot["DA"] = "/Helv 10 Tf 0 g"
        page_dict["Annots"] = [pdf.add(annot)]
    return _channel("ch_annotation", plant, count, "a markup annotation whose contents hold the marker")


def ch_redact_annot(count=3):
    def plant(pdf, page_dict, catalog, mark, i, page_ref):
        page_dict["Annots"] = [pdf.add({
            "Type": Name("Annot"), "Subtype": Name("Redact"), "Rect": [70, 715, 300, 735],
            "QuadPoints": [70, 735, 300, 735, 70, 715, 300, 715], "OverlayText": mark, "Contents": mark})]
    return _channel("ch_redact_annot", plant, count, "a /Redact annotation never applied")


def ch_acroform(count=3):
    def plant(pdf, page_dict, catalog, mark, i, page_ref):
        field = pdf.add({"Type": Name("Annot"), "Subtype": Name("Widget"), "FT": Name("Tx"),
                         "T": f"field{i}", "V": mark, "Rect": [72, 600, 300, 620], "DA": "/Helv 10 Tf 0 g"})
        page_dict["Annots"] = [field]
        catalog["AcroForm"] = {"Fields": [field], "NeedAppearances": True,
                               "DA": "/Helv 10 Tf 0 g"}
    return _channel("ch_acroform", plant, count, "a text field whose value is the marker")


def ch_xfa(count=3):
    def plant(pdf, page_dict, catalog, mark, i, page_ref):
        xdp = pdf.add(Stream(("<xdp:xdp xmlns:xdp='http://ns.adobe.com/xdp/'><xfa:datasets "
                              "xmlns:xfa='http://www.xfa.org/schema/xfa-data/1.0/'><xfa:data><field>"
                              f"{mark}</field></xfa:data></xfa:datasets></xdp:xdp>").encode()))
        catalog["AcroForm"] = {"Fields": [], "XFA": xdp}
    return _channel("ch_xfa", plant, count, "an /XFA stream whose data holds the marker")


def ch_outline(count=3):
    def plant(pdf, page_dict, catalog, mark, i, page_ref):
        outlines = pdf.reserve()
        item = pdf.add({"Title": f"Bookmark {mark}", "Parent": outlines})
        pdf.set(outlines, {"Type": Name("Outlines"), "First": item, "Last": item, "Count": 1})
        catalog["Outlines"] = outlines
    return _channel("ch_outline", plant, count, "a bookmark titled with the marker")


def ch_named_dest(count=3):
    def plant(pdf, page_dict, catalog, mark, i, page_ref):
        if i == 0:
            catalog["Dests"] = pdf.add({mark: [page_ref, Name("Fit")]})
        else:
            catalog["Names"] = {"Dests": {"Names": [mark, [page_ref, Name("Fit")]]}}
    return _channel("ch_named_dest", plant, count, "a named destination called by the marker")


def ch_struct_tree(count=3):
    def plant(pdf, page_dict, catalog, mark, i, page_ref):
        root = pdf.reserve()
        elem = pdf.add({"Type": Name("StructElem"), "S": Name("Figure"), "P": root, "Alt": mark,
                        "ActualText": mark})
        pdf.set(root, {"Type": Name("StructTreeRoot"), "K": [elem]})
        catalog["StructTreeRoot"] = root
        catalog["MarkInfo"] = {"Marked": True}
    return _channel("ch_struct_tree", plant, count, "a structure element whose /Alt is the marker")


def ch_optional_content(count=3):
    def plant(pdf, page_dict, catalog, mark, i, page_ref):
        ocg = pdf.add({"Type": Name("OCG"), "Name": mark})
        catalog["OCProperties"] = {"OCGs": [ocg], "D": {"Order": [ocg], "ON": [ocg]}}
    return _channel("ch_optional_content", plant, count, "an optional content group named by the marker")


def ch_page_labels(count=3):
    def plant(pdf, page_dict, catalog, mark, i, page_ref):
        catalog["PageLabels"] = {"Nums": [0, {"S": Name("D"), "P": f"{mark}-"}]}
    return _channel("ch_page_labels", plant, count, "a page label prefix holding the marker")


def ch_xmp_history(count=3):
    def plant(pdf, page_dict, catalog, mark, i, page_ref):
        catalog["Metadata"] = _metadata_stream(pdf, mark, history=True)
    return _channel("ch_xmp_history", plant, count, "an XMP packet whose xmpMM:History names the marker")


def ch_object_metadata(count=3):
    def plant(pdf, page_dict, catalog, mark, i, page_ref):
        page_dict["Metadata"] = _metadata_stream(pdf, mark)
    return _channel("ch_object_metadata", plant, count, "an XMP packet on the page object")


def ch_unreferenced(count=3):
    def plant(pdf, page_dict, catalog, mark, i, page_ref):
        pdf.add(Stream(b"BT /F1 12 Tf 72 600 Td (" + mark.encode() + b") Tj ET"))
        pdf.add({"Orphan": mark})
    return _channel("ch_unreferenced", plant, count, "objects no reference reaches, holding the marker")


def ch_threads(count=3):
    def plant(pdf, page_dict, catalog, mark, i, page_ref):
        thread = pdf.reserve()
        bead = pdf.reserve()
        pdf.set(bead, {"Type": Name("Bead"), "T": thread, "N": bead, "V": bead, "P": page_ref,
                       "R": [72, 700, 300, 740]})
        pdf.set(thread, {"Type": Name("Thread"), "F": bead, "I": {"Title": mark}})
        catalog["Threads"] = [thread]
        page_dict["B"] = [bead]
    return _channel("ch_threads", plant, count, "an article thread titled with the marker")


def ch_portfolio(count=3):
    def plant(pdf, page_dict, catalog, mark, i, page_ref):
        ef = pdf.add(Stream(f"Portfolio member {mark}\n".encode(), {"Type": Name("EmbeddedFile")}))
        spec = pdf.add({"Type": Name("Filespec"), "F": f"{mark}.txt", "UF": f"{mark}.txt", "EF": {"F": ef}})
        catalog["Names"] = {"EmbeddedFiles": {"Names": [mark, spec]}}
        catalog["Collection"] = {"Type": Name("Collection"), "View": Name("D")}
    return _channel("ch_portfolio", plant, count, "a portfolio /Collection with a member named by the marker")


PATTERNS = {name: fn for name, fn in globals().items() if name.startswith("ch_") and callable(fn)}
