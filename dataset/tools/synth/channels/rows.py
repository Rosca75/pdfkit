# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# One pattern per row of the channel table (docs/DESIGN.md §15). Each specimen
# draws one ordinary line of text and plants `PDFKITMARK<RR>X<NN>` markers in
# its channel only, so a writer that strips the channel leaves no marker in any
# output and a writer that keeps it is caught by a byte scan. Rows with more than
# one way to reach the channel plant several counters in one file, or write a
# second specimen. The cross-reference form cycles through table, stream and
# hybrid, because a channel must be found however the file is indexed.
#
# The channel keys follow ISO 32000-1: the catalog (table 28), the page object
# (table 30), annotations (§12.5), interactive forms (§12.7), outlines (§12.3.3),
# named destinations (§12.3.2.3), the structure tree (§14.7), optional content
# (§8.11), page labels (§12.4.2), article threads (§12.4.3), web capture (§14.10),
# collections (§7.11.6), permissions and legal attestation (§12.8.4), the
# encryption dictionary (§7.6.1) and the /URI dictionary (§12.6.4.7); each is
# listed in docs/SOURCES.md.

from __future__ import annotations

import random

import mutate
from synth.channels.kit import (ROWS, Ctx, base_size, build_single, form, gray_image, mark_bytes,
                                metadata_stream, sample_bytes, text_chunk, xmp_packet)
from synth.patterns.kit import Name, Stream, ops
from synth.writer import append_update, hexstr

XREFS = ("table", "stream", "hybrid")


def dest(ctx: Ctx) -> list:
    return [ctx.page_ref, Name("Fit")]


def embedded_file(ctx: Ctx, name: str, body: bytes) -> object:
    """A file specification with its embedded stream."""
    stream = ctx.pdf.add(Stream(body, {"Type": Name("EmbeddedFile"), "Subtype": Name("text/plain"),
                                       "Params": {"Size": len(body)}}))
    return ctx.pdf.add({"Type": Name("Filespec"), "F": name, "UF": name, "EF": {"F": stream}})


# -- 01 to 05 ------------------------------------------------------------------------


def plant_revisions(ctx: Ctx) -> None:
    """Revision 1 draws the marker; revision 2 swaps in a content stream without it."""
    ctx.content.append(text_chunk(ctx.mark(1), y=680))
    new_content = ctx.pdf.reserve()
    line = ctx.visible[0]

    def post(inner: Ctx, data: bytes) -> bytes:
        page = dict(inner.page_dict)
        page["Contents"] = new_content
        objects = {inner.page_ref.num: page, new_content.num: Stream(ops(text_chunk(line)))}
        return append_update(data, objects, inner.pdf.root, base_size(data), info=inner.pdf.info)

    ctx.post = post


def plant_unreferenced(ctx: Ctx) -> None:
    ctx.pdf.add({"Orphan": ctx.mark(1)})
    ctx.pdf.add(Stream(ops(text_chunk(ctx.mark(2), y=600))))


def plant_thumbnail(ctx: Ctx) -> None:
    samples = sample_bytes(mark_bytes(3, 1), 15, 4)
    ctx.mark(1)
    ctx.page["Thumb"] = ctx.pdf.add(gray_image(samples, 15, 4))


def plant_pieceinfo(ctx: Ctx) -> None:
    ctx.page["PieceInfo"] = {"PDFKitSynth": {"LastModified": "D:20260101000000Z", "Private": ctx.mark(1)}}
    ctx.catalog["PieceInfo"] = {"PDFKitSynth": {"LastModified": "D:20260101000000Z", "Private": ctx.mark(2)}}


def plant_last_modified(ctx: Ctx) -> None:
    ctx.page["LastModified"] = ctx.mark(1)
    ctx.catalog["LastModified"] = ctx.mark(2)


# -- 06 to 10 ------------------------------------------------------------------------


def plant_object_metadata(ctx: Ctx) -> None:
    """XMP packets on the page, on an image and on a form, none on the catalog."""
    pdf = ctx.pdf
    ctx.page["Metadata"] = metadata_stream(pdf, xmp_packet("Page packet", description=ctx.mark(1)))
    image = pdf.add(gray_image(bytes([0, 85, 170, 255]), 2, 2, {
        "Metadata": metadata_stream(pdf, xmp_packet("Image packet", description=ctx.mark(2)))}))
    panel = pdf.add(form(b"0.8 g 0 0 40 40 re f", 40, 40, {
        "Metadata": metadata_stream(pdf, xmp_packet("Form packet", description=ctx.mark(3)))}))
    ctx.resources["XObject"] = {"Im1": image, "Fm1": panel}
    ctx.content.append(b"q 50 0 0 50 72 620 cm /Im1 Do Q")
    ctx.content.append(b"q 1 0 0 1 150 620 cm /Fm1 Do Q")


def plant_javascript(ctx: Ctx) -> None:
    pdf = ctx.pdf
    ctx.catalog["OpenAction"] = {"S": Name("JavaScript"), "JS": ctx.mark(1)}
    tree_action = pdf.add({"S": Name("JavaScript"), "JS": ctx.mark(2)})
    ctx.catalog["Names"] = {"JavaScript": {"Names": ["synth-script", tree_action]}}
    script = pdf.add(Stream(f"app.alert('{ctx.mark(3)}');".encode("ascii")))
    ctx.page["AA"] = {"O": pdf.add({"S": Name("JavaScript"), "JS": script})}


def plant_embedded_file(ctx: Ctx) -> None:
    first = ctx.mark(1)
    spec = embedded_file(ctx, f"{first}.txt", f"Attachment body {first}\n".encode("ascii"))
    ctx.catalog["Names"] = {"EmbeddedFiles": {"Names": [f"{first}.txt", spec]}}
    second = ctx.mark(2)
    associated = embedded_file(ctx, f"{second}.txt", f"Associated body {second}\n".encode("ascii"))
    ctx.pdf.objects[associated.num]["AFRelationship"] = Name("Source")
    ctx.catalog["AF"] = [associated]


def plant_annotation(ctx: Ctx) -> None:
    """A FreeText annotation with an appearance stream and a popup, and a Text annotation."""
    pdf = ctx.pdf
    appearance = pdf.add(form(text_chunk(ctx.mark(2), x=4, y=20, size=10), 160, 40,
                              resources={"Font": {"F1": ctx.font}}))
    annot = pdf.reserve()
    popup = pdf.add({"Type": Name("Annot"), "Subtype": Name("Popup"), "Rect": [400, 500, 560, 580],
                     "Parent": annot, "Open": False, "Contents": ctx.mark(3)})
    pdf.set(annot, {"Type": Name("Annot"), "Subtype": Name("FreeText"), "Rect": [400, 600, 560, 640],
                    "Contents": ctx.mark(1), "DA": "/F1 10 Tf 0 g", "F": 4, "AP": {"N": appearance},
                    "Popup": popup})
    note = pdf.add({"Type": Name("Annot"), "Subtype": Name("Text"), "Rect": [500, 700, 520, 720],
                    "Contents": ctx.mark(4), "Name": Name("Comment")})
    ctx.page["Annots"] = [annot, popup, note]


def plant_acroform(ctx: Ctx) -> None:
    field = ctx.pdf.add({"Type": Name("Annot"), "Subtype": Name("Widget"), "FT": Name("Tx"), "T": "field1",
                         "V": ctx.mark(1), "Rect": [72, 600, 300, 620], "DA": "/Helv 10 Tf 0 g",
                         "P": ctx.page_ref, "F": 4})
    ctx.page["Annots"] = [field]
    ctx.catalog["AcroForm"] = {"Fields": [field], "NeedAppearances": True, "DA": "/Helv 10 Tf 0 g",
                               "DR": {"Font": {"Helv": ctx.font}}}


def plant_xfa(ctx: Ctx) -> None:
    packet = ("<xdp:xdp xmlns:xdp='http://ns.adobe.com/xdp/'><xfa:datasets "
              "xmlns:xfa='http://www.xfa.org/schema/xfa-data/1.0/'><xfa:data><field>"
              f"{ctx.mark(2)}</field></xfa:data></xfa:datasets></xdp:xdp>")
    ctx.catalog["AcroForm"] = {"Fields": [], "XFA": ctx.pdf.add(Stream(packet.encode("ascii")))}


# -- 11 to 15 ------------------------------------------------------------------------


def plant_outline(ctx: Ctx) -> None:
    pdf = ctx.pdf
    outlines, first, second = pdf.reserve(), pdf.reserve(), pdf.reserve()
    pdf.set(first, {"Title": ctx.mark(1), "Parent": outlines, "Next": second, "Dest": dest(ctx)})
    pdf.set(second, {"Title": ctx.mark(2), "Parent": outlines, "Prev": first, "Dest": dest(ctx)})
    pdf.set(outlines, {"Type": Name("Outlines"), "First": first, "Last": second, "Count": 2})
    ctx.catalog["Outlines"] = outlines


def plant_named_dest(ctx: Ctx) -> None:
    """A /Names /Dests tree keyed by the marker (a string); the legacy /Dests keys are names."""
    name = ctx.mark(1)
    leaf = ctx.pdf.add({"Limits": [name, name], "Names": [name, dest(ctx)]})
    ctx.catalog["Names"] = {"Dests": {"Kids": [leaf]}}
    ctx.catalog["Dests"] = {"synth": dest(ctx)}


def plant_struct_tree(ctx: Ctx) -> None:
    pdf = ctx.pdf
    root, element = pdf.reserve(), pdf.reserve()
    pdf.set(element, {"Type": Name("StructElem"), "S": Name("P"), "P": root, "Pg": ctx.page_ref, "K": 0,
                      "Alt": ctx.mark(1), "ActualText": ctx.mark(2)})
    pdf.set(root, {"Type": Name("StructTreeRoot"), "K": [element], "ParentTree": {"Nums": [0, [element]]}})
    ctx.catalog["StructTreeRoot"] = root
    ctx.catalog["MarkInfo"] = {"Marked": True}
    ctx.page["StructParents"] = 0
    ctx.content = [b"/P << /MCID 0 >> BDC"] + ctx.content + [b"EMC"]


def plant_optional_content(ctx: Ctx) -> None:
    group = ctx.pdf.add({"Type": Name("OCG"), "Name": ctx.mark(1),
                         "Usage": {"CreatorInfo": {"Creator": ctx.mark(3), "Subtype": Name("Artwork")}}})
    ctx.catalog["OCProperties"] = {
        "OCGs": [group],
        "D": {"ON": [group], "Order": [group]},
        "Configs": [{"Name": ctx.mark(2), "ON": [group]}],
    }
    ctx.resources["Properties"] = {"oc1": group}
    layer = "Layer text is shown"
    ctx.content.append(b"/OC /oc1 BDC " + text_chunk(layer, y=690) + b" EMC")
    ctx.visible.append(layer)


def plant_page_labels(ctx: Ctx) -> None:
    ctx.catalog["PageLabels"] = {"Nums": [0, {"S": Name("D"), "P": ctx.mark(1)}]}


# -- 16 to 20 ------------------------------------------------------------------------


def plant_xmp_history(ctx: Ctx) -> None:
    packet = xmp_packet("Specimen document", agent=ctx.mark(1))
    ctx.catalog["Metadata"] = metadata_stream(ctx.pdf, packet)


def plant_threads(ctx: Ctx) -> None:
    pdf = ctx.pdf
    thread, bead = pdf.reserve(), pdf.reserve()
    pdf.set(bead, {"Type": Name("Bead"), "T": thread, "N": bead, "V": bead, "P": ctx.page_ref,
                   "R": [72, 700, 300, 740]})
    pdf.set(thread, {"Type": Name("Thread"), "F": bead,
                     "I": {"Title": ctx.mark(1), "Subject": ctx.mark(2)}})
    ctx.catalog["Threads"] = [thread]
    ctx.page["B"] = [bead]


def plant_web_capture(ctx: Ctx) -> None:
    ctx.catalog["SpiderInfo"] = {"V": "1.0", "C": [{"URL": ctx.mark(1), "L": 1, "F": 0}]}
    ctx.page["ID"] = ctx.mark(2)


def plant_portfolio(ctx: Ctx) -> None:
    name = f"{ctx.mark(1)}.txt"
    spec = embedded_file(ctx, name, b"Portfolio member body\n")
    ctx.catalog["Names"] = {"EmbeddedFiles": {"Names": [name, spec]}}
    ctx.catalog["Collection"] = {"Type": Name("Collection"), "View": Name("D"), "D": name,
                                 "Schema": {"Type": Name("CollectionSchema"),
                                            "FileName": {"Subtype": Name("F"), "N": "Name", "O": 1}}}


def plant_perms_legal(ctx: Ctx) -> None:
    signature = ctx.pdf.add({
        "Type": Name("Sig"), "Filter": Name("Adobe.PPKLite"), "SubFilter": Name("adbe.pkcs7.detached"),
        "ByteRange": [0, 0, 0, 0], "Contents": hexstr(bytes(16)), "Reason": ctx.mark(1),
        "Reference": [{"Type": Name("SigRef"), "TransformMethod": Name("DocMDP"),
                       "TransformParams": {"Type": Name("TransformParams"), "P": 2, "V": Name("1.2")}}]})
    ctx.catalog["Perms"] = {"DocMDP": signature}
    ctx.catalog["Legal"] = {"Attestation": ctx.mark(2)}


# -- 21 to 23 ------------------------------------------------------------------------


def plant_encrypt(revision: int, counter: int):
    """RC4-128 (revision 3) or AESV2 (revision 4) with the empty user password; the marker
    is a vendor key of the /Encrypt dictionary, whose strings the format leaves unencrypted."""
    def plant(ctx: Ctx) -> None:
        marker = ctx.mark(counter)

        def post(inner: Ctx, data: bytes) -> bytes:
            extra = b" /PDFKitVendor (" + marker.encode("ascii") + b")"
            return mutate.encrypt_file(data, random.Random(21000 + revision), revision, encrypt_extra=extra)

        ctx.post = post
    return plant


def plant_uri_base(ctx: Ctx) -> None:
    ctx.catalog["URI"] = {"Base": ctx.mark(1)}


def plant_search_index(ctx: Ctx) -> None:
    index = ctx.pdf.add(Stream(f"index of words: {ctx.mark(1)}\n".encode("ascii"),
                               {"Type": Name("SearchIndex"), "Name": ctx.mark(2)}))
    ctx.catalog["SearchIndex"] = index
    ctx.catalog["PageMode"] = Name("UseSearchIndex")


def plant_xobject_private(ctx: Ctx) -> None:
    shape = ctx.pdf.add(Stream(b"0 0 1 1 re f", {
        "Type": Name("XObject"), "Subtype": Name("Form"), "BBox": [0, 0, 1, 1],
        "PTEX.FileName": ctx.mark(1)}))
    picture = ctx.pdf.add(Stream(bytes([128] * 16), {
        "Type": Name("XObject"), "Subtype": Name("Image"), "Width": 4, "Height": 4,
        "ColorSpace": Name("DeviceGray"), "BitsPerComponent": 8, "PrivateNote": ctx.mark(2)}))
    ctx.resources.setdefault("XObject", {}).update({"Fx": shape, "Ix": picture})
    ctx.content.append(b"q 20 0 0 20 300 300 cm /Fx Do Q q 20 0 0 20 340 300 cm /Ix Do Q")


# row -> list of (plant, extra tags, cross-reference form, note)
VARIANTS = {
    1: [(plant_revisions, (), "table", "an incremental update replaces the page content")],
    2: [(plant_unreferenced, (), None, "a dictionary and a stream no reference reaches")],
    3: [(plant_thumbnail, (), None, "a page thumbnail whose samples begin with the marker")],
    4: [(plant_pieceinfo, (), None, "page and catalog /PieceInfo private data")],
    5: [(plant_last_modified, (), None, "page and catalog /LastModified")],
    6: [(plant_object_metadata, (), None, "XMP packets on a page, an image and a form")],
    7: [(plant_javascript, (), None, "a JavaScript action in /OpenAction, the name tree and a page /AA")],
    8: [(plant_embedded_file, (), None, "an embedded file and an associated file")],
    9: [(plant_annotation, (), None, "a FreeText annotation with appearance and popup, and a Text annotation")],
    10: [(plant_acroform, (), None, "a text field whose value is the marker"),
         (plant_xfa, ("ch_xfa",), None, "an /XFA stream whose data holds the marker")],
    11: [(plant_outline, (), None, "two bookmarks titled with markers")],
    12: [(plant_named_dest, (), None, "a /Names /Dests tree keyed by the marker")],
    13: [(plant_struct_tree, (), None, "a structure element with /Alt and /ActualText")],
    14: [(plant_optional_content, (), None, "a group named by the marker, drawn through BDC /OC")],
    15: [(plant_page_labels, (), None, "a page label prefix")],
    16: [(plant_xmp_history, (), None, "an XMP packet whose xmpMM:History names the marker")],
    17: [(plant_threads, (), None, "an article thread titled with the marker")],
    18: [(plant_web_capture, (), None, "a /SpiderInfo command URL and a page /ID")],
    19: [(plant_portfolio, (), None, "a /Collection with a member named by the marker")],
    20: [(plant_perms_legal, (), None, "a /DocMDP signature reason and a /Legal attestation")],
    21: [(plant_encrypt(3, 1), ("rc4_128",), "table", "RC4-128, empty user password, vendor key in /Encrypt"),
         (plant_encrypt(4, 2), ("aesv2",), "table", "AESV2, empty user password, vendor key in /Encrypt")],
    22: [(plant_uri_base, (), None, "a catalog /URI dictionary with /Base")],
    23: [(plant_search_index, (), None, "a catalog /SearchIndex stream and a non-standard /PageMode")],
    24: [(plant_xobject_private, (), None, "private keys on a form and an image XObject")],
}


def pattern(row: int):
    tag = ROWS[row]

    def make() -> list:
        return [build_single(row, plant, [tag, *extra], xref or XREFS[row % 3], note)
                for plant, extra, xref, note in VARIANTS[row]]
    return make


PATTERNS = {f"mark_{ROWS[row]}": pattern(row) for row in VARIANTS}
