# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# What the planted-marker patterns share: the marker format, the context a
# channel plants into, the one-call builder of a single-page specimen, the
# incremental update writer for a cross-reference stream, the XMP packet and
# the check that every marker a specimen plants can be found by a byte scan of
# the raw file or of a decompressed stream. A marker is `PDFKITMARK<RR>X<NN>`:
# RR is the row of the channel table (01 to 24), 25 for content in an
# optional-content group turned off (synth/channels/off_layers.py), or the drawn
# content (90 to 93), NN a counter, or the page number for a page-owned marker. Markers are
# always literal strings or stream data and never hex strings or names, so one
# byte scan finds them wherever a reader leaves them.

from __future__ import annotations

import re
from dataclasses import dataclass, field
from typing import Callable

import autotag
import common
from synth.patterns.kit import TEXTS, Name, Pdf, Specimen, Stream, helvetica, info, ops, show
from synth.writer import Ref, hexstr, object_bytes

PREFIX = "PDFKITMARK"
MARK_RE = re.compile(rb"PDFKITMARK(\d\d)X(\d\d)")

# The rows of the channel table: row number to taxonomy tag. The numbers are the
# RR of the markers; 90 to 93 are the drawn content of the leak fixture.
ROWS = {
    1: "ch_revisions", 2: "ch_unreferenced", 3: "ch_thumbnail", 4: "ch_pieceinfo",
    5: "ch_last_modified", 6: "ch_object_metadata", 7: "ch_javascript", 8: "ch_embedded_file",
    9: "ch_annotation", 10: "ch_acroform", 11: "ch_outline", 12: "ch_named_dest",
    13: "ch_struct_tree", 14: "ch_optional_content", 15: "ch_page_labels", 16: "ch_xmp_history",
    17: "ch_threads", 18: "ch_web_capture", 19: "ch_portfolio", 20: "ch_perms_legal",
    21: "ch_encrypt", 22: "ch_uri_base", 23: "ch_search_index", 24: "ch_xobject_private",
}
DRAWN_ROWS = (90, 91, 92, 93)


def mark(row: int, n: int = 1) -> str:
    """The marker of channel row `row` with counter (or page number) `n`."""
    return f"{PREFIX}{row:02d}X{n:02d}"


def mark_bytes(row: int, n: int = 1) -> bytes:
    return mark(row, n).encode("ascii")


def find_markers(blobs) -> set[str]:
    """Every marker occurring in the given byte strings."""
    found = set()
    for blob in blobs:
        for match in MARK_RE.finditer(blob):
            found.add(match.group(0).decode("ascii"))
    return found


def readable_blobs(data: bytes) -> list[bytes]:
    """The raw file and every stream the scanner can decode, the places a scan looks."""
    scan = autotag.Scan(data)
    blobs = [data]
    for obj in scan.objects:
        if obj.data is not None:
            decoded = scan.decoded(obj)
            if decoded:
                blobs.append(decoded)
    return blobs


def verify(data: bytes, planted: list[str], allowed: set[int] | None = None) -> None:
    """Fails when a planted marker is not findable or a marker of another row occurs."""
    found = find_markers(readable_blobs(data))
    missing = sorted(set(planted) - found)
    if missing:
        raise common.ToolError(f"markers {missing} are planted but no scan finds them",
                               "every planted marker in the raw bytes or a decoded stream",
                               "plant the marker as a literal string or as stream data")
    if allowed is not None:
        foreign = sorted(m for m in found if int(m[len(PREFIX):len(PREFIX) + 2]) not in allowed)
        if foreign:
            raise common.ToolError(f"markers {foreign} occur outside their row",
                                   f"only markers of rows {sorted(allowed)}",
                                   "plant each marker in its own channel only")


def visible_text(row: int) -> str:
    """The ordinary line a specimen draws; it never holds a marker."""
    return TEXTS[row % len(TEXTS)]


def text_chunk(text: str, x: float = 72, y: float = 720, size: float = 12, font: str = "F1") -> bytes:
    return f"BT /{font} {size} Tf {x} {y} Td ".encode("ascii") + show(text) + b" Tj ET"


def gray_image(data: bytes, width: int, height: int, entries: dict | None = None,
               compress: bool = False) -> Stream:
    """A DeviceGray 8-bit image whose samples are `data` (width times height bytes)."""
    if len(data) != width * height:
        raise ValueError("sample count differs from width times height")
    base = {"Type": Name("XObject"), "Subtype": Name("Image"), "Width": width, "Height": height,
            "ColorSpace": Name("DeviceGray"), "BitsPerComponent": 8}
    base.update(entries or {})
    return Stream(data, base, compress=compress)


def sample_bytes(marker: bytes, width: int, height: int) -> bytes:
    """`width * height` sample bytes that begin with the marker and continue in a gradient."""
    total = width * height
    return (marker + bytes((17 * i) % 251 for i in range(total)))[:total]


def form(content: bytes, width: float = 200, height: float = 40, entries: dict | None = None,
         resources: dict | None = None, compress: bool = True) -> Stream:
    base = {"Type": Name("XObject"), "Subtype": Name("Form"), "BBox": [0, 0, width, height]}
    if resources:
        base["Resources"] = resources
    base.update(entries or {})
    return Stream(content, base, compress=compress)


def xmp_packet(title: str, agent: str | None = None, description: str | None = None) -> bytes:
    """An XMP packet with a plain dc:title; `agent` adds an xmpMM:History event naming it."""
    history = ""
    if agent is not None:
        history = ("<xmpMM:History><rdf:Seq><rdf:li rdf:parseType='Resource'>"
                   f"<stEvt:action>saved</stEvt:action><stEvt:softwareAgent>{agent}</stEvt:softwareAgent>"
                   "</rdf:li></rdf:Seq></xmpMM:History>")
    describe = ""
    if description is not None:
        describe = (f"<dc:description><rdf:Alt><rdf:li xml:lang='x-default'>{description}</rdf:li>"
                    "</rdf:Alt></dc:description>")
    return ("<?xpacket begin='﻿' id='W5M0MpCehiHzreSzNTczkc9d'?>"
            "<x:xmpmeta xmlns:x='adobe:ns:meta/'><rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
            "<rdf:Description rdf:about='' xmlns:xmpMM='http://ns.adobe.com/xap/1.0/mm/' "
            "xmlns:stEvt='http://ns.adobe.com/xap/1.0/sType/ResourceEvent#' "
            "xmlns:dc='http://purl.org/dc/elements/1.1/'>"
            f"<dc:title><rdf:Alt><rdf:li xml:lang='x-default'>{title}</rdf:li></rdf:Alt></dc:title>"
            f"{describe}{history}</rdf:Description></rdf:RDF></x:xmpmeta><?xpacket end='w'?>").encode("utf-8")


def metadata_stream(pdf: Pdf, packet: bytes) -> Ref:
    """An XMP /Metadata stream, left uncompressed so tools that scan for it find it."""
    return pdf.add(Stream(packet, {"Type": Name("Metadata"), "Subtype": Name("XML")}, compress=False))


# -- incremental updates -----------------------------------------------------------


def base_size(base: bytes) -> int:
    """The /Size of the last trailer or cross-reference stream dictionary of `base`."""
    return int(re.findall(rb"/Size (\d+)", base)[-1])


def previous_xref(base: bytes) -> int:
    tail = base[-64:]
    return int(tail[tail.rfind(b"startxref") + 9:].split()[0])


def append_stream_update(base: bytes, objects: dict[int, object], root: Ref,
                         info_ref: Ref | None = None) -> bytes:
    """An incremental update closed by a cross-reference stream whose /Prev is the base's section."""
    out = bytearray(base)
    if not out.endswith(b"\n"):
        out += b"\n"
    offsets = {}
    for num in sorted(objects):
        offsets[num] = len(out)
        out += object_bytes(num, objects[num])
    xref_num = max(base_size(base), max(offsets) + 1)
    start = len(out)
    offsets[xref_num] = start
    numbers = sorted(offsets)
    rows = b"".join(bytes([1]) + offsets[n].to_bytes(4, "big") + bytes([0]) for n in numbers)
    index: list[int] = []
    run_start, run_len = numbers[0], 1
    for n in numbers[1:]:
        if n == run_start + run_len:
            run_len += 1
        else:
            index += [run_start, run_len]
            run_start, run_len = n, 1
    index += [run_start, run_len]
    entries = {"Type": Name("XRef"), "Size": xref_num + 1, "W": [1, 4, 1], "Index": index, "Root": root,
               "Prev": previous_xref(base),
               "ID": [hexstr(b"pdfkit-synth-id0"), hexstr(b"pdfkit-synth-id1")]}
    if info_ref is not None:
        entries["Info"] = info_ref
    out += object_bytes(xref_num, Stream(rows, entries))
    out += b"startxref\n%d\n%%%%EOF\n" % start
    return bytes(out)


# -- the single-page specimen ------------------------------------------------------


@dataclass
class Ctx:
    """What a channel plants into: the document, the one page and the catalog being built."""

    pdf: Pdf
    row: int
    font: Ref
    pages_ref: Ref
    page_ref: Ref
    catalog: dict
    page: dict = field(default_factory=dict)
    resources: dict = field(default_factory=dict)
    content: list = field(default_factory=list)
    visible: list = field(default_factory=list)
    markers: list = field(default_factory=list)
    post: Callable | None = None
    page_dict: dict | None = None

    def mark(self, n: int = 1) -> str:
        """The marker of this row with counter `n`, recorded as planted."""
        text = mark(self.row, n)
        if text not in self.markers:
            self.markers.append(text)
        return text


def build_single(row: int, plant: Callable[[Ctx], None], tags: list[str], xref: str = "table",
                 notes: str = "") -> Specimen:
    """A one-page file with a visible line, the channel planted by `plant(ctx)`."""
    pdf = Pdf()
    font = pdf.add(helvetica())
    ctx = Ctx(pdf=pdf, row=row, font=font, pages_ref=pdf.reserve(), page_ref=pdf.reserve(),
              catalog={}, resources={"Font": {"F1": font}})
    ctx.catalog.update({"Type": Name("Catalog"), "Pages": ctx.pages_ref})
    line = visible_text(row)
    ctx.content.append(text_chunk(line))
    ctx.visible.append(line)
    plant(ctx)
    content_ref = pdf.add(Stream(ops(*ctx.content)))
    ctx.page_dict = {"Type": Name("Page"), "Parent": ctx.pages_ref, "MediaBox": [0, 0, 612, 792],
                     "Resources": ctx.resources, "Contents": content_ref}
    ctx.page_dict.update(ctx.page)
    pdf.set(ctx.page_ref, ctx.page_dict)
    pdf.set(ctx.pages_ref, {"Type": Name("Pages"), "Kids": [ctx.page_ref], "Count": 1})
    pdf.root = pdf.add(ctx.catalog)
    pdf.info = pdf.add(info(tags[0]))
    data = pdf.build(xref)
    if ctx.post is not None:
        data = ctx.post(ctx, data)
    verify(data, ctx.markers, {row})
    note = "; ".join(n for n in [notes, "markers " + ", ".join(ctx.markers), f"xref {xref}"] if n)
    return Specimen(data, tags, "\n".join(ctx.visible), notes=note)
