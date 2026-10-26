# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# Three files whose catalog /Metadata packet does not scan as XML, each with
# an Info dictionary that states the same title: an end tag that closes
# another element, an attribute value whose quote never closes, and a packet
# written in UTF-16 with its byte-order mark (XMP allows the encoding, PDF
# readers rarely meet it). A reader reports each as unreadable and reads the
# Info dictionary; a writer drops the packet with the warning xmp_unreadable.

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from synth.patterns.kit import Name, Pdf, Specimen, Stream, helvetica, ops, page, show  # noqa: E402

TITLE = "Packet that does not scan"
BODY = "This file states its title in the Info dictionary and a broken packet"


def packet(description: str) -> bytes:
    return ("<?xpacket begin=\"﻿\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>\n"
            "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\">\n"
            "<rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">\n"
            "<rdf:Description rdf:about=\"\" xmlns:dc=\"http://purl.org/dc/elements/1.1/\" "
            f"{description}\n"
            "</rdf:RDF>\n</x:xmpmeta>\n<?xpacket end=\"w\"?>").encode("utf-8")


def specimen(xmp: bytes, case: str) -> list[Specimen]:
    pdf = Pdf()
    res = {"Font": {"F1": pdf.add(helvetica())}}
    content = ops(b"BT /F1 12 Tf 72 700 Td " + show(BODY) + b" Tj ET")
    metadata = pdf.add(Stream(xmp, {"Type": Name("Metadata"), "Subtype": Name("XML")}, compress=False))
    pdf.document([page(pdf, content, res)], {"Metadata": metadata},
                 {"Title": TITLE, "Producer": "pdfkit dataset synthesiser"})
    return [Specimen(pdf.build(), ["meta_xmp_unreadable"], BODY, notes=f"XMP packet: {case}")]


def mismatched_end_tag():
    xmp = packet(">\n<dc:title><rdf:Alt><rdf:li xml:lang=\"x-default\">" + TITLE +
                 "</rdf:li></rdf:Alt></dc:creator>\n</rdf:Description>")
    return specimen(xmp, "an end tag that closes another element")


def unclosed_quote():
    xmp = packet("dc:format=\"application/pdf>\n<dc:title><rdf:Alt><rdf:li xml:lang=\"x-default\">" +
                 TITLE + "</rdf:li></rdf:Alt></dc:title>\n</rdf:Description>")
    return specimen(xmp, "an attribute value whose quote never closes")


def utf16():
    text = packet(">\n<dc:title><rdf:Alt><rdf:li xml:lang=\"x-default\">" + TITLE +
                  "</rdf:li></rdf:Alt></dc:title>\n</rdf:Description>").decode("utf-8")
    return specimen(text.encode("utf-16"), "written in UTF-16 with a byte-order mark")


PATTERNS = {
    "meta_xmp_mismatched_end_tag": mismatched_end_tag,
    "meta_xmp_unclosed_quote": unclosed_quote,
    "meta_xmp_utf16": utf16,
}
