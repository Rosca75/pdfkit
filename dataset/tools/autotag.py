# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# Infers taxonomy tags from a PDF's bytes: the file structure (header, startxref,
# cross-reference kind and offsets, revisions, linearisation, object streams),
# the encryption dictionary, the fonts and their encodings, the images and their
# filters and colour spaces, the content streams (render modes, inline images,
# ActualText) and the channels of the channel table, and the generator from the
# producer string. It is a scanner, not a parser: it reads every `n g obj` it
# finds, opens Flate object streams with zlib to see the objects inside them and
# decodes Flate content streams, so a tag it cannot see (an encrypted file's
# content, an LZW-compressed stream) is left to provenance. Tags it never infers
# come from synth/, mutate.py and generate_office.ps1.

from __future__ import annotations

import argparse
import base64
import json
import re
import sys
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import common  # noqa: E402

STREAM_LIMIT = 32 * 1024 * 1024
TOTAL_LIMIT = 256 * 1024 * 1024

OBJ_RE = re.compile(rb"(?<![0-9])(\d{1,10})\s+(\d{1,5})\s+obj\b")
REF_RE = re.compile(rb"(?<![0-9.])(\d{1,10})\s+(\d{1,5})\s+R\b")
NAME_RE = re.compile(rb"/([^\s/\[\]<>(){}%]+)")

# The standard 14 fonts and their common aliases (ISO 32000-1 §9.6.2.2).
CORE14 = {
    "Times-Roman", "Times-Bold", "Times-Italic", "Times-BoldItalic", "Helvetica",
    "Helvetica-Bold", "Helvetica-Oblique", "Helvetica-BoldOblique", "Courier",
    "Courier-Bold", "Courier-Oblique", "Courier-BoldOblique", "Symbol", "ZapfDingbats",
    "Arial", "Arial,Bold", "Arial,Italic", "Arial,BoldItalic", "TimesNewRoman",
    "TimesNewRoman,Bold", "TimesNewRoman,Italic", "TimesNewRoman,BoldItalic",
    "CourierNew", "CourierNew,Bold", "CourierNew,Italic", "CourierNew,BoldItalic",
}

# Inline image abbreviations (ISO 32000-1 §8.9.7, tables 93 and 94).
INLINE_FILTERS = {b"AHx": None, b"A85": None, b"LZW": "img_lzw", b"Fl": "img_flate",
                  b"RL": "img_runlength", b"CCF": "ccitt", b"DCT": "img_dct"}
FILTER_TAGS = {
    "FlateDecode": "img_flate", "DCTDecode": "img_dct", "JPXDecode": "img_jpx",
    "JBIG2Decode": "img_jbig2", "LZWDecode": "img_lzw", "RunLengthDecode": "img_runlength",
}
COLOUR_TAGS = {
    "DeviceGray": "cs_gray", "DeviceRGB": "cs_rgb", "DeviceCMYK": "cs_cmyk",
    "Indexed": "cs_indexed", "ICCBased": "cs_icc", "Separation": "cs_separation",
    "DeviceN": "cs_devicen", "Lab": "cs_lab", "G": "cs_gray", "RGB": "cs_rgb",
    "CMYK": "cs_cmyk", "I": "cs_indexed", "CalRGB": "cs_rgb", "CalGray": "cs_gray",
}

# Producer substrings to generator tags, first match wins.
GENERATORS = (
    ("print to pdf", "gen_print_to_pdf"),
    ("skia/pdf", "gen_chrome"),
    ("chrome", "gen_chrome"),
    ("excel", "gen_excel"),
    ("powerpoint", "gen_powerpoint"),
    ("microsoft® word", "gen_word"),
    ("microsoft word", "gen_word"),
    ("microsoft® office word", "gen_word"),
    ("libreoffice", "gen_libreoffice"),
    ("openoffice", "gen_libreoffice"),
    ("quartz", "gen_quartz"),
    ("mac os x", "gen_quartz"),
    ("macos", "gen_quartz"),
    ("ghostscript", "gen_ghostscript"),
    ("pdftex", "gen_pdftex"),
    ("xetex", "gen_pdftex"),
    ("luatex", "gen_pdftex"),
    ("xdvipdfmx", "gen_pdftex"),
    ("dvipdfm", "gen_pdftex"),
    ("distiller", "gen_distiller"),
    ("acrobat", "gen_acrobat"),
    ("itext", "gen_itext"),
    ("reportlab", "gen_reportlab"),
)

DAMAGE_REPAIRED = {"xref_missing", "xref_wrong_offsets", "startxref_wrong", "truncated",
                   "length_wrong", "endstream_missing", "endobj_missing"}


def generator_tag(producer: str) -> str | None:
    text = producer.strip().lower()
    if not text:
        return None
    for needle, tag in GENERATORS:
        if needle in text:
            return tag
    return "gen_other"


class Obj:
    """One object found by the scan: its number, dictionary text and stream."""

    __slots__ = ("num", "gen", "offset", "head", "data", "decoded", "in_objstm")

    def __init__(self, num, gen, offset, head, data, in_objstm=False):
        self.num = num
        self.gen = gen
        self.offset = offset
        self.head = head          # the bytes of the object before `stream` (or all of it)
        self.data = data          # the raw stream bytes, or None
        self.decoded = None       # the decoded stream bytes when a decoder applies
        self.in_objstm = in_objstm

    def has(self, pattern: bytes) -> bool:
        return re.search(pattern, self.head) is not None


def filters_of(head: bytes) -> list[str]:
    match = re.search(rb"/Filter\s*(\[[^\]]*\]|/[^\s/\[\]<>]+)", head)
    if not match:
        return []
    return [n.decode("latin-1") for n in NAME_RE.findall(match.group(1))]


def decode_stream(data: bytes, filters: list[str], budget: list[int]) -> bytes | None:
    """Applies the Flate, ASCII85 and ASCIIHex filters; None for any other filter."""
    out = data
    for name in filters:
        if budget[0] <= 0:
            return None
        if name in ("FlateDecode", "Fl"):
            dec = zlib.decompressobj()
            try:
                out = dec.decompress(out, STREAM_LIMIT)
            except zlib.error:
                return None
        elif name in ("ASCII85Decode", "A85"):
            text = out.strip()
            if text.endswith(b"~>"):
                text = text[:-2]
            try:
                out = base64.a85decode(text, ignorechars=b" \t\r\n\f\x00")
            except ValueError:
                return None
        elif name in ("ASCIIHexDecode", "AHx"):
            text = re.sub(rb"[^0-9A-Fa-f]", b"", out.split(b">")[0])
            if len(text) % 2:
                text += b"0"
            out = bytes.fromhex(text.decode("ascii"))
        else:
            return None
        budget[0] -= len(out)
    return out


def scan_objects(data: bytes) -> list[Obj]:
    """Every `n g obj` of the file in file order, streams cut by /Length or endstream."""
    found = []
    matches = list(OBJ_RE.finditer(data))
    for index, match in enumerate(matches):
        start = match.end()
        limit = matches[index + 1].start() if index + 1 < len(matches) else len(data)
        end_obj = data.find(b"endobj", start, limit)
        body_end = end_obj if end_obj >= 0 else limit
        stream_at = re.search(rb"\bstream(\r\n|\n|\r)", data[start:body_end]) if body_end > start else None
        if stream_at is None:
            stream_at = re.search(rb"\bstream(\r\n|\n|\r)", data[start:limit])
        num, gen = int(match.group(1)), int(match.group(2))
        if stream_at is None:
            found.append(Obj(num, gen, match.start(), data[start:body_end], None))
            continue
        head = data[start:start + stream_at.start()]
        data_start = start + stream_at.end()
        length = None
        lm = re.search(rb"/Length\s+(\d+)(?!\s+\d+\s+R)", head)
        if lm:
            length = int(lm.group(1))
            tail = data[data_start + length:data_start + length + 32].lstrip()
            if not tail.startswith(b"endstream"):
                length = None
        if length is None:
            end = data.find(b"endstream", data_start)
            if end < 0:
                end = limit
            length = end - data_start
            if length > 0 and data[data_start + length - 1:data_start + length] == b"\n":
                length -= 1
            if length > 0 and data[data_start + length - 1:data_start + length] == b"\r":
                length -= 1
        found.append(Obj(num, gen, match.start(), head, data[data_start:data_start + max(length, 0)]))
    return found


def expand_object_streams(objs: list[Obj], budget: list[int]) -> list[Obj]:
    """The objects inside every object stream that decodes."""
    inner = []
    for obj in objs:
        if obj.data is None or not obj.has(rb"/Type\s*/ObjStm"):
            continue
        decoded = decode_stream(obj.data, filters_of(obj.head), budget)
        if decoded is None:
            continue
        n = re.search(rb"/N\s+(\d+)", obj.head)
        first = re.search(rb"/First\s+(\d+)", obj.head)
        if not n or not first:
            continue
        first_at = int(first.group(1))
        numbers = [int(x) for x in re.findall(rb"\d+", decoded[:first_at])]
        pairs = list(zip(numbers[0::2], numbers[1::2]))[: int(n.group(1))]
        for i, (num, off) in enumerate(pairs):
            begin = first_at + off
            end = first_at + pairs[i + 1][1] if i + 1 < len(pairs) else len(decoded)
            inner.append(Obj(num, 0, -1, decoded[begin:end], None, in_objstm=True))
    return inner


class Scan:
    """What one file holds, as far as a byte scan can see."""

    def __init__(self, data: bytes):
        self.data = data
        self.budget = [TOTAL_LIMIT]
        self.top = scan_objects(data)
        self.inner = expand_object_streams(self.top, self.budget)
        self.objects = self.top + self.inner
        # The last definition of a number wins, as in an incremental update.
        self.by_num: dict[int, Obj] = {}
        for obj in self.top:
            self.by_num[obj.num] = obj
        for obj in self.inner:
            self.by_num.setdefault(obj.num, obj)
        self.trailers = [m for m in re.finditer(rb"trailer\s*<<", data)]

    def resolve(self, text: bytes) -> Obj | None:
        match = re.match(rb"\s*(\d+)\s+(\d+)\s+R", text)
        return self.by_num.get(int(match.group(1))) if match else None

    def value(self, head: bytes, key: bytes) -> bytes | None:
        """The text after /key up to the next key or the end (a crude value read)."""
        match = re.search(rb"/" + key + rb"(?![A-Za-z0-9])\s*", head)
        if not match:
            return None
        return head[match.end():match.end() + 400]

    def decoded(self, obj: Obj) -> bytes | None:
        if obj.data is None:
            return None
        if obj.decoded is None:
            filters = filters_of(obj.head)
            obj.decoded = obj.data if not filters else decode_stream(obj.data, filters, self.budget)
        return obj.decoded


# ---------------------------------------------------------------------------
# Structure


def structure_tags(scan: Scan, tags: set, facts: dict) -> None:
    data = scan.data
    header = data.find(b"%PDF-")
    facts["header_offset"] = header
    if header > 0:
        tags.add("junk_before_header")
    eofs = data.count(b"%%EOF")
    linearised = re.search(rb"/Linearized\b", data[:2048]) is not None
    if linearised:
        tags.add("linearised")
    if b"%%EOF" not in data[-1024:]:
        tags.add("eof_missing")
    if eofs - (1 if linearised else 0) > 1:
        tags.add("incremental_update")
    tables = len(re.findall(rb"(?<!start)xref\s+\d+\s+\d+", data))
    if any(o.has(rb"/Type\s*/XRef\b") for o in scan.top):
        tags.add("xref_stream")
    if re.search(rb"/XRefStm\s+\d+", data):
        tags.add("hybrid_xref")
    if any(o.has(rb"/Type\s*/ObjStm\b") for o in scan.top):
        tags.add("object_streams")
    if len(scan.trailers) > max(tables, 1) and tables >= 1:
        tags.add("two_trailers")
    has_xref = tables > 0 or "xref_stream" in tags
    starts = re.findall(rb"startxref\s+(\d+)", data[-4096:] if len(data) > 4096 else data)
    if not has_xref:
        tags.add("xref_missing")
    elif not starts:
        tags.add("startxref_wrong") if b"startxref" in data else tags.add("xref_missing")
    else:
        offset = int(starts[-1]) + max(header, 0)
        at = data[offset:offset + 64].lstrip()
        if not (at.startswith(b"xref") or re.match(rb"\d+\s+\d+\s+obj", at)):
            tags.add("startxref_wrong")
        elif at.startswith(b"xref"):
            if wrong_offsets(data, offset, max(header, 0)):
                tags.add("xref_wrong_offsets")
    if not data.rstrip().endswith(b"%%EOF") and not re.search(rb"endobj\s*$|%%EOF\s*$", data[-64:]):
        if "eof_missing" in tags and b"startxref" not in data[-2048:]:
            tags.add("truncated")


def wrong_offsets(data: bytes, offset: int, base: int) -> bool:
    """True when an in-use entry of the table at `offset` misses its object."""
    pos = offset + data[offset:].find(b"xref") + 4
    checked = 0
    while checked < 4000:
        sub = re.match(rb"\s*(\d+)\s+(\d+)\s*[\r\n]+", data[pos:pos + 64])
        if not sub:
            return False
        first, count = int(sub.group(1)), int(sub.group(2))
        pos += sub.end()
        for i in range(count):
            entry = re.match(rb"\s*(\d{1,10})\s+(\d{1,5})\s+([nf])", data[pos:pos + 40])
            if not entry:
                return True
            pos += entry.end()
            if entry.group(3) != b"n":
                continue
            target = int(entry.group(1)) + base
            obj = re.match(rb"\s*(\d+)\s+\d+\s+obj", data[target:target + 40])
            checked += 1
            if not obj or int(obj.group(1)) != first + i:
                return True


def encryption_tags(scan: Scan, tags: set, facts: dict) -> None:
    trailer_text = b"".join(scan.data[m.start():m.start() + 600] for m in scan.trailers)
    trailer_text += b"".join(o.head for o in scan.top if o.has(rb"/Type\s*/XRef\b"))
    enc = re.search(rb"/Encrypt\s+(\d+\s+\d+\s+R|<<)", trailer_text)
    if not enc:
        return
    head = None
    if enc.group(1).startswith(b"<<"):
        start = trailer_text.find(b"/Encrypt", enc.start())
        head = trailer_text[start:start + 600]
    else:
        obj = scan.resolve(enc.group(1))
        head = obj.head if obj else None
    facts["encrypted"] = True
    if head is None:
        return
    flt = re.search(rb"/Filter\s*/(\w+)", head)
    facts["security_handler"] = flt.group(1).decode() if flt else ""
    r = re.search(rb"/R\s+(\d+)", head)
    revision = int(r.group(1)) if r else 0
    facts["security_revision"] = revision
    cfm = re.search(rb"/CFM\s*/(\w+)", head)
    method = cfm.group(1).decode() if cfm else ""
    if revision == 2:
        tags.add("rc4_40")
    elif revision == 3 or (revision == 4 and method == "V2"):
        tags.add("rc4_128")
    elif revision == 4 and method == "AESV2":
        tags.add("aesv2")
    elif revision == 5:
        tags.add("aesv3_r5")
    elif revision == 6:
        tags.add("aesv3_r6")
    if re.search(rb"/EncryptMetadata\s+false", head):
        tags.add("encrypt_metadata_false")


# ---------------------------------------------------------------------------
# Fonts


def classify_glyph_name(name: str) -> str:
    if re.fullmatch(r"uni[0-9A-Fa-f]{4,}", name) or re.fullmatch(r"u[0-9A-Fa-f]{4,6}", name):
        return "differences_uni"
    if re.fullmatch(r"g\d+|glyph\d+|cid\d+", name):
        return "differences_gnn"
    if re.fullmatch(r"[Cc]\d{2,3}|[Cc][0-9A-Fa-f]{2}", name):
        return "differences_cdd"
    return "differences_aglnames"


def font_tags(scan: Scan, tags: set, facts: dict) -> None:
    fonts = 0
    for obj in scan.objects:
        head = obj.head
        if not re.search(rb"/Type\s*/Font\b", head) and not re.search(
                rb"/Subtype\s*/(Type1|TrueType|Type0|Type3|MMType1)\b", head):
            continue
        sub = re.search(rb"/Subtype\s*/(\w+)", head)
        if not sub:
            continue
        subtype = sub.group(1).decode()
        if subtype in ("CIDFontType0", "CIDFontType2"):
            continue
        fonts += 1
        base = re.search(rb"/BaseFont\s*/([^\s/\[\]<>(){}%]+)", head)
        base_name = base.group(1).decode("latin-1") if base else ""
        if subtype == "Type3":
            tags.add("type3")
        if re.search(rb"/ToUnicode\b", head):
            tags.add("tounicode_correct")
        elif subtype != "Type0":
            tags.add("tounicode_missing")
        diff_text = None
        enc_val = scan.value(head, b"Encoding")
        enc_obj = scan.resolve(enc_val) if enc_val else None
        if enc_obj is not None:
            enc_head = enc_obj.head
        else:
            enc_head = head
        diff = re.search(rb"/Differences\s*\[([^\]]*)\]", enc_head)
        if diff:
            diff_text = diff.group(1)
            for name in NAME_RE.findall(diff_text):
                tags.add(classify_glyph_name(name.decode("latin-1")))
        if subtype == "Type0":
            enc = re.search(rb"/Encoding\s*/([^\s/\[\]<>]+)", head)
            if enc:
                name = enc.group(1).decode("latin-1")
                if name.startswith("Identity"):
                    tags.add("identity_cid")
                else:
                    tags.add("predefined_cmap")
                if name.endswith("-V"):
                    tags.add("vertical")
            descendants = scan.value(head, b"DescendantFonts") or b""
            inner = re.search(rb"\[\s*(\d+\s+\d+\s+R)", descendants)
            cid = scan.resolve(inner.group(1)) if inner else None
            desc_holder = cid.head if cid else head
        else:
            desc_holder = head
        if subtype == "Type3":
            continue
        desc_val = scan.value(desc_holder, b"FontDescriptor")
        desc = scan.resolve(desc_val) if desc_val else None
        if desc is None:
            if base_name.split("+")[-1] not in CORE14:
                tags.add("font_not_embedded")
            continue
        dh = desc.head
        flags = re.search(rb"/Flags\s+(\d+)", dh)
        symbolic = bool(flags and int(flags.group(1)) & 4)
        has1 = re.search(rb"/FontFile\s+\d", dh) is not None
        has2 = re.search(rb"/FontFile2\s+\d", dh) is not None
        ff3 = re.search(rb"/FontFile3\s+(\d+\s+\d+\s+R)", dh)
        if not (has1 or has2 or ff3):
            if base_name.split("+")[-1] not in CORE14:
                tags.add("font_not_embedded")
        if has2 and subtype == "TrueType" and symbolic:
            tags.add("symbolic_truetype")
        if has1 and subtype == "Type1" and enc_val is None:
            tags.add("type1_builtin")
        if ff3:
            program = scan.resolve(ff3.group(1))
            if program is not None and re.search(rb"/Subtype\s*/(Type1C|CIDFontType0C)", program.head):
                tags.add("cff_charset")
    facts["fonts"] = fonts


# ---------------------------------------------------------------------------
# Images


def colour_space_tags(scan: Scan, text: bytes, tags: set, depth: int = 0) -> None:
    if depth > 3:
        return
    text = text.strip()
    if re.match(rb"\d+\s+\d+\s+R", text):
        obj = scan.resolve(text)
        if obj is not None and obj.data is None:
            colour_space_tags(scan, obj.head, tags, depth + 1)
        return
    if text.startswith(b"["):
        names = NAME_RE.findall(text[:200])
        if names:
            family = names[0].decode("latin-1")
            if family in COLOUR_TAGS:
                tags.add(COLOUR_TAGS[family])
            if family == "ICCBased":
                ref = re.search(rb"/ICCBased\s+(\d+\s+\d+\s+R)", text)
                icc = scan.resolve(ref.group(1)) if ref else None
                if icc is not None:
                    n = re.search(rb"/N\s+(\d)", icc.head)
                    if n and n.group(1) == b"4":
                        tags.add("_cmyk_icc")
            if family == "Indexed":
                base = re.search(rb"/Indexed\s*(/\w+|\[[^\]]*\]|\d+\s+\d+\s+R)", text)
                if base:
                    inner: set = set()
                    colour_space_tags(scan, base.group(1), inner, depth + 1)
                    tags.update(t for t in inner if t == "cs_icc")
        return
    names = NAME_RE.findall(text[:60])
    if names:
        name = names[0].decode("latin-1")
        if name in COLOUR_TAGS:
            tags.add(COLOUR_TAGS[name])


def image_tags(scan: Scan, tags: set, facts: dict) -> None:
    count = 0
    image_nums = set()
    for obj in scan.objects:
        head = obj.head
        if not re.search(rb"/Subtype\s*/Image\b", head):
            continue
        count += 1
        image_nums.add(obj.num)
        filters = filters_of(head)
        for name in filters:
            if name in FILTER_TAGS:
                tags.add(FILTER_TAGS[name])
            if name == "CCITTFaxDecode":
                k = re.search(rb"/K\s+(-?\d+)", head)
                tags.add("img_ccitt_g4" if k and int(k.group(1)) < 0 else "img_ccitt_g3")
            if name == "JBIG2Decode" and re.search(rb"/JBIG2Globals\b", head):
                tags.add("img_jbig2_globals")
        cs_tags: set = set()
        cs = scan.value(head, b"ColorSpace")
        if cs is not None:
            colour_space_tags(scan, cs, cs_tags)
        if "DCTDecode" in filters and ("cs_cmyk" in cs_tags or "_cmyk_icc" in cs_tags):
            tags.add("img_dct_cmyk")
        tags.update(t for t in cs_tags if not t.startswith("_"))
        bpc = re.search(rb"/BitsPerComponent\s+(\d+)", head)
        if bpc and bpc.group(1) == b"1":
            tags.add("bpc_1")
        if bpc and bpc.group(1) == b"16":
            tags.add("bpc_16")
        if re.search(rb"/ImageMask\s+true", head):
            tags.add("stencil")
            tags.add("bpc_1")
        if re.search(rb"/SMask\s+\d+\s+\d+\s+R", head):
            tags.add("smask")
        if re.search(rb"/Mask\s*\[", head):
            tags.add("colour_key")
    facts["images"] = count
    # A form XObject whose resources name an image.
    for obj in scan.objects:
        if not re.search(rb"/Subtype\s*/Form\b", obj.head):
            continue
        res = obj.head
        res_ref = re.search(rb"/Resources\s+(\d+\s+\d+\s+R)", res)
        if res_ref:
            target = scan.resolve(res_ref.group(1))
            res = target.head if target else b""
        xo = re.search(rb"/XObject\s*(<<.*?>>|\d+\s+\d+\s+R)", res, re.S)
        if not xo:
            continue
        body = xo.group(1)
        if not body.startswith(b"<<"):
            target = scan.resolve(body)
            body = target.head if target else b""
        for ref in REF_RE.finditer(body):
            if int(ref.group(1)) in image_nums:
                tags.add("image_in_form")


# ---------------------------------------------------------------------------
# Content streams


def is_content_candidate(obj: Obj) -> bool:
    head = obj.head
    if re.search(rb"/Subtype\s*/(Image|Type1C|CIDFontType0C|OpenType|XML)\b", head):
        return False
    if re.search(rb"/Type\s*/(ObjStm|XRef|Metadata|EmbeddedFile)\b", head):
        return False
    if re.search(rb"/Length1|/Length2|/Length3|/N\s+[134]\b", head):
        return False
    return True


def content_tags(scan: Scan, tags: set, facts: dict) -> None:
    contents = 0
    for obj in scan.top:
        if obj.data is None or not is_content_candidate(obj):
            continue
        decoded = scan.decoded(obj)
        if not decoded or not re.search(rb"\b(BT|Do|re|Tj|TJ)\b", decoded[:65536] + decoded[-4096:]):
            continue
        contents += 1
        if re.search(rb"(?<![0-9.])[37]\s+Tr\b", decoded):
            tags.add("render_invisible")
        if b"/ActualText" in decoded:
            tags.add("actual_text")
        for bi in re.finditer(rb"\bBI\b(.{0,400}?)\bID\b", decoded, re.S):
            tags.add("inline_image")
            body = bi.group(1)
            for key, value in re.findall(rb"/(F|Filter)\s*(/\w+|\[[^\]]*\])", body):
                for name in NAME_RE.findall(value):
                    tag = INLINE_FILTERS.get(name) or FILTER_TAGS.get(name.decode("latin-1"))
                    if tag == "ccitt":
                        k = re.search(rb"/K\s+(-?\d+)", body)
                        tag = "img_ccitt_g4" if k and int(k.group(1)) < 0 else "img_ccitt_g3"
                    if tag:
                        tags.add(tag)
            cs = re.search(rb"/(?:CS|ColorSpace)\s*(/\w+|\[[^\]]*\])", body)
            if cs:
                colour_space_tags(scan, cs.group(1), tags)
            bpc = re.search(rb"/(?:BPC|BitsPerComponent)\s+(\d+)", body)
            if bpc and bpc.group(1) == b"1":
                tags.add("bpc_1")
            if re.search(rb"/(?:IM|ImageMask)\s+true", body):
                tags.add("stencil")
    for obj in scan.objects:
        if obj.data is None and b"/ActualText" in obj.head:
            tags.add("actual_text")
    facts["content_streams"] = contents
    tags.discard("_cmyk_icc")


# ---------------------------------------------------------------------------
# Channels


CHANNEL_PATTERNS = (
    (rb"/Thumb\s+\d+\s+\d+\s+R", "ch_thumbnail"),
    (rb"/PieceInfo\b", "ch_pieceinfo"),
    (rb"/JavaScript\b|/JS\b", "ch_javascript"),
    (rb"/EmbeddedFiles\b|/AF\s*[\[\d]|/Subtype\s*/FileAttachment\b|/Type\s*/EmbeddedFile\b", "ch_embedded_file"),
    (rb"/Annots\b", "ch_annotation"),
    (rb"/Subtype\s*/Redact\b", "ch_redact_annot"),
    (rb"/AcroForm\b", "ch_acroform"),
    (rb"/XFA\b", "ch_xfa"),
    (rb"/Outlines\s+\d+\s+\d+\s+R", "ch_outline"),
    (rb"/Dests\b", "ch_named_dest"),
    (rb"/StructTreeRoot\b", "ch_struct_tree"),
    (rb"/OCProperties\b", "ch_optional_content"),
    (rb"/PageLabels\b", "ch_page_labels"),
    (rb"/Threads\b", "ch_threads"),
    (rb"/Collection\b", "ch_portfolio"),
)


def channel_tags(scan: Scan, tags: set, facts: dict) -> None:
    heads = [o.head for o in scan.objects]
    trailer_bytes = [scan.data[m.start():m.start() + 600] for m in scan.trailers]
    for pattern, tag in CHANNEL_PATTERNS:
        regex = re.compile(pattern)
        if any(regex.search(h) for h in heads):
            tags.add(tag)
    if any(re.search(rb"/Type\s*/Annot\b", h) and re.search(rb"/Subtype\s*/Link\b", h) is None for h in heads):
        tags.add("ch_annotation")
    for obj in scan.objects:
        if b"xmpMM:History" in obj.head:
            tags.add("ch_xmp_history")
        if obj.data is not None and re.search(rb"/Type\s*/Metadata\b", obj.head):
            text = scan.decoded(obj) or b""
            if b"xmpMM:History" in text or b"<xmpMM:History" in text:
                tags.add("ch_xmp_history")
        if re.search(rb"/Metadata\s+\d+\s+\d+\s+R", obj.head) and not re.search(rb"/Type\s*/Catalog\b", obj.head):
            if re.search(rb"/Type\s*/(Page|FontDescriptor)\b|/Subtype\s*/(Image|Form)\b", obj.head):
                tags.add("ch_object_metadata")
    if re.search(rb"/Names\s*<<[^>]*/Dests\b", b" ".join(heads)):
        tags.add("ch_named_dest")
    reach = reachable(scan, trailer_bytes)
    unreferenced = [n for n, o in scan.by_num.items()
                    if n not in reach and n != 0 and not re.search(
                        rb"/Type\s*/(ObjStm|XRef)\b|/Linearized\b", o.head)]
    facts["unreferenced"] = len(unreferenced)
    if unreferenced:
        tags.add("ch_unreferenced")


def reachable(scan: Scan, trailer_bytes: list[bytes]) -> set[int]:
    roots: list[int] = []
    for text in trailer_bytes:
        roots += [int(m.group(1)) for m in REF_RE.finditer(text)]
    for obj in scan.top:
        if re.search(rb"/Type\s*/XRef\b", obj.head):
            roots += [int(m.group(1)) for m in REF_RE.finditer(obj.head)]
    seen: set[int] = set()
    stack = list(roots)
    while stack:
        num = stack.pop()
        if num in seen:
            continue
        seen.add(num)
        obj = scan.by_num.get(num)
        if obj is None:
            continue
        for match in REF_RE.finditer(obj.head):
            ref = int(match.group(1))
            if ref not in seen:
                stack.append(ref)
    # An object inside an object stream is reached through its container.
    return seen


# ---------------------------------------------------------------------------
# Producer


def producer_from_bytes(scan: Scan) -> str:
    for obj in reversed(scan.objects):
        match = re.search(rb"/Producer\s*\(((?:\\.|[^\\)])*)\)", obj.head)
        if match:
            raw = match.group(1)
            if raw.startswith(b"\xfe\xff"):
                return raw[2:].decode("utf-16-be", "replace")
            return raw.decode("latin-1")
    for obj in scan.objects:
        if obj.data is not None and re.search(rb"/Type\s*/Metadata\b", obj.head):
            text = scan.decoded(obj) or b""
            match = re.search(rb"<pdf:Producer>([^<]*)</pdf:Producer>|pdf:Producer=\"([^\"]*)\"", text)
            if match:
                return (match.group(1) or match.group(2)).decode("utf-8", "replace")
    return ""


RTL_RE = re.compile(r"[֐-ࣿיִ-﷿ﹰ-﻿]")


def autotag(data: bytes, producer: str | None = None, text: str | None = None) -> tuple[list[str], dict]:
    """Tags and facts of one file; `producer` and `text` come from PDFium when known."""
    scan = Scan(data)
    tags: set = set()
    facts: dict = {}
    structure_tags(scan, tags, facts)
    encryption_tags(scan, tags, facts)
    font_tags(scan, tags, facts)
    image_tags(scan, tags, facts)
    content_tags(scan, tags, facts)
    channel_tags(scan, tags, facts)
    found_producer = producer if producer else producer_from_bytes(scan)
    facts["producer"] = found_producer
    gen = generator_tag(found_producer)
    if gen:
        tags.add(gen)
    if text and RTL_RE.search(text):
        tags.add("right_to_left")
    facts["has_page"] = any(re.search(rb"/Type\s*/Page\b", o.head) for o in scan.objects)
    return sorted(tags), facts


def main() -> int:
    parser = argparse.ArgumentParser(description="Infer taxonomy tags from PDF bytes.")
    parser.add_argument("files", nargs="+")
    args = parser.parse_args()
    for name in args.files:
        tags, facts = autotag(Path(name).read_bytes())
        print(json.dumps({"file": name, "tags": tags, "facts": facts}, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    common.run_tool(main)
