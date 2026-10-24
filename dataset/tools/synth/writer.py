# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# A minimal PDF writer for the synthesised files: Python values to PDF objects,
# streams (Flate or raw), pages with their content and resources, a classic
# cross-reference table, a cross-reference stream or a hybrid of both, and
# incremental updates appended to an earlier revision. It writes no timestamp
# and draws no random number, so a pattern run twice writes the same bytes and
# the dataset's hashes stay valid. The syntax follows ISO 32000-1 §7.3 (objects)
# and §7.5 (file structure).

from __future__ import annotations

import zlib


class Name(str):
    """A PDF name; written as /Name with the delimiters and # escaped."""


class Ref:
    def __init__(self, num: int, gen: int = 0):
        self.num = num
        self.gen = gen


class Raw(bytes):
    """Bytes written as they are (an operator, a hex string already formed)."""


class Stream:
    def __init__(self, data: bytes, entries: dict | None = None, compress: bool = True,
                 filters: list | None = None):
        self.entries = dict(entries or {})
        if compress and filters is None:
            self.data = zlib.compress(data, 9)
            self.entries["Filter"] = Name("FlateDecode")
        else:
            self.data = data
            if filters:
                self.entries["Filter"] = filters[0] if len(filters) == 1 else list(filters)


def hexstr(data: bytes) -> Raw:
    return Raw(b"<" + data.hex().upper().encode("ascii") + b">")


def literal(data: bytes) -> bytes:
    out = bytearray(b"(")
    for byte in data:
        ch = bytes([byte])
        if ch in b"()\\":
            out += b"\\" + ch
        elif ch == b"\r":
            out += b"\\r"
        elif ch == b"\n":
            out += b"\\n"
        else:
            out += ch
    out += b")"
    return bytes(out)


def text_string(text: str) -> bytes:
    """A text string: PDFDocEncoding when ASCII, else UTF-16BE with a BOM."""
    if all(ord(c) < 128 for c in text):
        return literal(text.encode("ascii"))
    return literal(b"\xfe\xff" + text.encode("utf-16-be"))


def number(value: float) -> bytes:
    if isinstance(value, bool):
        return b"true" if value else b"false"
    if isinstance(value, int):
        return str(value).encode("ascii")
    text = f"{value:.4f}".rstrip("0").rstrip(".")
    return (text if text not in ("", "-0") else "0").encode("ascii")


def name_bytes(name: str) -> bytes:
    out = bytearray(b"/")
    for byte in name.encode("utf-8"):
        if byte < 0x21 or byte > 0x7E or bytes([byte]) in b"()<>[]{}/%#":
            out += b"#%02X" % byte
        else:
            out.append(byte)
    return bytes(out)


def serialise(value) -> bytes:
    if value is None:
        return b"null"
    if isinstance(value, Raw):
        return bytes(value)
    if isinstance(value, Name):
        return name_bytes(value)
    if isinstance(value, Ref):
        return b"%d %d R" % (value.num, value.gen)
    if isinstance(value, (bool, int, float)):
        return number(value)
    if isinstance(value, str):
        return text_string(value)
    if isinstance(value, (bytes, bytearray)):
        return literal(bytes(value))
    if isinstance(value, (list, tuple)):
        return b"[" + b" ".join(serialise(v) for v in value) + b"]"
    if isinstance(value, dict):
        parts = [name_bytes(k) + b" " + serialise(v) for k, v in value.items()]
        return b"<< " + b" ".join(parts) + b" >>"
    raise TypeError(f"cannot write {type(value).__name__}")


def object_bytes(num: int, value, gen: int = 0) -> bytes:
    if isinstance(value, Stream):
        entries = dict(value.entries)
        entries["Length"] = len(value.data)
        return (b"%d %d obj\n" % (num, gen) + serialise(entries) + b"\nstream\n" + value.data
                + b"\nendstream\nendobj\n")
    return b"%d %d obj\n" % (num, gen) + serialise(value) + b"\nendobj\n"


class Pdf:
    """Objects numbered from 1, written in number order."""

    def __init__(self, version: str = "1.7"):
        self.version = version
        self.objects: dict[int, object] = {}
        self.next = 1
        self.root: Ref | None = None
        self.info: Ref | None = None
        self.trailer_extra: dict = {}

    def reserve(self) -> Ref:
        ref = Ref(self.next)
        self.next += 1
        return ref

    def add(self, value) -> Ref:
        ref = self.reserve()
        self.objects[ref.num] = value
        return ref

    def set(self, ref: Ref, value) -> Ref:
        self.objects[ref.num] = value
        return ref

    # -- the document skeleton -------------------------------------------------

    def document(self, pages: list[dict], catalog_extra: dict | None = None,
                 info: dict | None = None) -> None:
        """Builds the catalog and the page tree from page dictionaries."""
        pages_ref = self.reserve()
        kids = []
        for page in pages:
            page = dict(page)
            page.setdefault("Type", Name("Page"))
            page["Parent"] = pages_ref
            page.setdefault("MediaBox", [0, 0, 612, 792])
            kids.append(self.add(page))
        self.set(pages_ref, {"Type": Name("Pages"), "Kids": kids, "Count": len(kids)})
        catalog = {"Type": Name("Catalog"), "Pages": pages_ref}
        catalog.update(catalog_extra or {})
        self.root = self.add(catalog)
        self.page_refs = kids
        if info:
            self.info = self.add(info)

    # -- serialisation ---------------------------------------------------------

    def header(self) -> bytes:
        return b"%PDF-" + self.version.encode("ascii") + b"\n%\xe2\xe3\xcf\xd3\n"

    def trailer_dict(self, size: int) -> dict:
        trailer = {"Size": size, "Root": self.root}
        if self.info:
            trailer["Info"] = self.info
        trailer["ID"] = [hexstr(b"pdfkit-synth-id0"), hexstr(b"pdfkit-synth-id0")]
        trailer.update(self.trailer_extra)
        return trailer

    def build(self, xref: str = "table") -> bytes:
        """The whole file with a `table`, `stream` or `hybrid` cross-reference."""
        out = bytearray(self.header())
        offsets: dict[int, int] = {}
        for num in sorted(self.objects):
            offsets[num] = len(out)
            out += object_bytes(num, self.objects[num])
        size = max(offsets) + 1 if offsets else 1
        if xref == "table":
            start = len(out)
            out += table_bytes(offsets, size)
            out += b"trailer\n" + serialise(self.trailer_dict(size)) + b"\n"
        elif xref == "stream":
            start = len(out)
            num = size
            offsets[num] = start
            size += 1
            out += xref_stream_bytes(num, offsets, size, self.trailer_dict(size))
        elif xref == "hybrid":
            num = size
            stm_start = len(out)
            offsets_with_stream = dict(offsets)
            offsets_with_stream[num] = stm_start
            out += xref_stream_bytes(num, offsets_with_stream, num + 1, self.trailer_dict(num + 1))
            start = len(out)
            out += table_bytes(offsets, num + 1, free={num})
            trailer = self.trailer_dict(num + 1)
            trailer["XRefStm"] = stm_start
            out += b"trailer\n" + serialise(trailer) + b"\n"
        else:
            raise ValueError(xref)
        out += b"startxref\n%d\n%%%%EOF\n" % start
        return bytes(out)


def table_bytes(offsets: dict[int, int], size: int, free: set | None = None) -> bytes:
    out = bytearray(b"xref\n0 %d\n" % size)
    out += b"0000000000 65535 f\r\n"
    for num in range(1, size):
        if num in offsets and num not in (free or set()):
            out += b"%010d 00000 n\r\n" % offsets[num]
        else:
            out += b"0000000000 00001 f\r\n"
    return bytes(out)


def xref_stream_bytes(num: int, offsets: dict[int, int], size: int, trailer: dict) -> bytes:
    rows = bytearray()
    for n in range(size):
        if n in offsets:
            rows += bytes([1]) + offsets[n].to_bytes(4, "big") + bytes([0])
        else:
            rows += bytes([0, 0, 0, 0, 0, 0 if n else 255])
    entries = dict(trailer)
    entries.update({"Type": Name("XRef"), "W": [1, 4, 1], "Size": size})
    return object_bytes(num, Stream(bytes(rows), entries))


def append_update(base: bytes, objects: dict[int, object], root: Ref, size: int,
                  prev_xref: int | None = None, info: Ref | None = None) -> bytes:
    """An incremental update: the changed objects, a table section and /Prev."""
    if prev_xref is None:
        tail = base[-64:]
        marker = tail.rfind(b"startxref")
        prev_xref = int(tail[marker + 9:].split()[0])
    out = bytearray(base)
    if not out.endswith(b"\n"):
        out += b"\n"
    offsets = {}
    for num in sorted(objects):
        offsets[num] = len(out)
        out += object_bytes(num, objects[num])
    start = len(out)
    out += b"xref\n0 1\n0000000000 65535 f\r\n"
    for num in sorted(offsets):
        out += b"%d 1\n%010d 00000 n\r\n" % (num, offsets[num])
    trailer = {"Size": max(size, max(offsets) + 1), "Root": root, "Prev": prev_xref,
               "ID": [hexstr(b"pdfkit-synth-id0"), hexstr(b"pdfkit-synth-id1")]}
    if info:
        trailer["Info"] = info
    out += b"trailer\n" + serialise(trailer) + b"\nstartxref\n%d\n%%%%EOF\n" % start
    return bytes(out)


# -- content helpers -----------------------------------------------------------


def ops(*lines) -> bytes:
    """Joins content-stream lines (bytes or str) with LF."""
    return b"\n".join(l.encode("latin-1") if isinstance(l, str) else l for l in lines) + b"\n"


def helvetica(name: str = "Helvetica") -> dict:
    return {"Type": Name("Font"), "Subtype": Name("Type1"), "BaseFont": Name(name),
            "Encoding": Name("WinAnsiEncoding")}


def show(text: str) -> bytes:
    """A literal string of WinAnsi text for Tj."""
    return literal(text.encode("cp1252"))


def text_block(lines: list[str], x: float = 72, y: float = 720, size: float = 12,
               leading: float = 16, font: str = "F1") -> bytes:
    out = [f"BT /{font} {size} Tf {leading} TL {x} {y} Td".encode("ascii")]
    for i, line in enumerate(lines):
        out.append((b"" if i == 0 else b"T* ") + show(line) + b" Tj")
    out.append(b"ET")
    return ops(*out)
