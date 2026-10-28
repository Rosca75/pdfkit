# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# The seeded mutator: from clean files of the manifest it writes the structural
# damages, the rewrites and the encryptions the reader must survive, and
# registers each result with the source id, the mutation and the seed in its
# `source` column. A mutated file either opens with its source's text or is
# refused naming the damage (CLAUDE.md §7.2), and each mutation's `expect_open`
# says which. Damages that keep every byte offset (a /Length changed within its
# digits, a keyword blanked with spaces) touch one thing only, so a failure
# names one cause. The same source, mutation and seed always give the same bytes.

from __future__ import annotations

import argparse
import hashlib
import json
import random
import re
import sys
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tests" / "pdfium"))

import autotag  # noqa: E402
import common  # noqa: E402
import pdfcrypt  # noqa: E402

BASE_SEED = 20261024


class NotApplicable(Exception):
    """The source lacks what the mutation needs (a direct /Length, a Flate stream)."""


def seed_for(source_id: str, mutation: str) -> int:
    digest = hashlib.sha256(f"{BASE_SEED}:{source_id}:{mutation}".encode()).digest()
    return int.from_bytes(digest[:4], "big")


# -- helpers over the bytes --------------------------------------------------------


def last_startxref(data: bytes) -> tuple[int, int]:
    """The position of the last `startxref` keyword and the offset it names."""
    at = data.rfind(b"startxref")
    if at < 0:
        raise NotApplicable("no startxref")
    match = re.match(rb"startxref\s+(\d+)", data[at:])
    if not match:
        raise NotApplicable("startxref without a number")
    return at, int(match.group(1))


def stream_spans(data: bytes) -> list[tuple[int, int, int, int]]:
    """(dict start, Length value start, Length value end, data start) of streams with a direct /Length."""
    out = []
    for match in re.finditer(rb"(\d+)\s+(\d+)\s+obj\s*<<", data):
        head_end = data.find(b"stream", match.end())
        if head_end < 0:
            continue
        head = data[match.start():head_end]
        if b"endobj" in head:
            continue
        length = re.search(rb"/Length\s+(\d+)(?!\s+\d+\s+R)", head)
        if not length:
            continue
        nl = re.match(rb"stream(\r\n|\n|\r)", data[head_end:head_end + 8])
        if not nl:
            continue
        out.append((match.start(), match.start() + length.start(1), match.start() + length.end(1),
                    head_end + nl.end()))
    return out


def content_like(data: bytes, span) -> bool:
    head = data[span[0]:span[3]]
    return b"/Subtype" not in head and b"/Type" not in head and b"/Length1" not in head


def same_width(value: int, width: int, rng: random.Random, longer: bool) -> int:
    low, high = (10 ** (width - 1) if width > 1 else 0), 10 ** width - 1
    candidates = [v for v in (value + rng.randint(5, 40), value + 1) if v <= high] if longer else \
        [v for v in (value - rng.randint(5, 40), value - 1) if v >= low]
    if not candidates:
        raise NotApplicable("no same-width length")
    return candidates[0]


# -- the damages --------------------------------------------------------------------


def m_xref_missing(data, rng):
    start = data.rfind(b"\nxref")
    trailer = data.find(b"trailer", start)
    if start < 0 or trailer < 0:
        raise NotApplicable("no classic table")
    return data[:start + 1] + data[trailer:]


def m_xref_wrong_offsets(data, rng):
    _, offset = last_startxref(data)
    if not data[offset:offset + 4] == b"xref":
        raise NotApplicable("startxref does not name a table")
    shift = rng.randint(3, 9)

    def bump(m):
        value = int(m.group(1))
        if m.group(3) != b"n" or value == 0:
            return m.group(0)
        return b"%010d %s %s" % (value + shift, m.group(2), m.group(3))

    end = data.find(b"trailer", offset)
    table = re.sub(rb"(\d{10}) (\d{5}) ([nf])", bump, data[offset:end])
    return data[:offset] + table + data[end:]


def m_startxref_wrong(data, rng):
    at, offset = last_startxref(data)
    wrong = rng.randint(max(16, offset // 4), max(17, offset - 8))
    match = re.match(rb"startxref\s+\d+", data[at:])
    return data[:at] + b"startxref\n%d" % wrong + data[at + match.end():]


def m_eof_missing(data, rng):
    at = data.rfind(b"%%EOF")
    if at < 0:
        raise NotApplicable("no %%EOF")
    return data[:at].rstrip(b"\r\n") + b"\n"


def m_junk_short(data, rng):
    return bytes(rng.randrange(256) for _ in range(rng.randint(40, 600))) + data


def m_junk_long(data, rng):
    return bytes(rng.randrange(256) for _ in range(rng.randint(1100, 3000))) + data


def _length(data, rng, longer):
    spans = [s for s in stream_spans(data) if content_like(data, s)] or stream_spans(data)
    if not spans:
        raise NotApplicable("no stream with a direct /Length")
    span = spans[rng.randrange(len(spans))]
    value = int(data[span[1]:span[2]])
    new = same_width(value, span[2] - span[1], rng, longer)
    text = str(new).encode().rjust(span[2] - span[1], b"0")
    return data[:span[1]] + text + data[span[2]:]


def m_length_short(data, rng):
    return _length(data, rng, False)


def m_length_long(data, rng):
    return _length(data, rng, True)


def m_endstream_missing(data, rng):
    spots = [m.start() for m in re.finditer(rb"endstream", data)]
    if not spots:
        raise NotApplicable("no stream")
    at = spots[rng.randrange(len(spots))]
    return data[:at] + b" " * 9 + data[at + 9:]


def m_endobj_missing(data, rng):
    spots = [m.start() for m in re.finditer(rb"(?<!end)endobj", data)]
    if len(spots) < 2:
        raise NotApplicable("too few objects")
    at = spots[rng.randrange(len(spots) - 1)]
    return data[:at] + b" " * 6 + data[at + 6:]


def m_two_trailers(data, rng):
    at, _ = last_startxref(data)
    trailer = data.rfind(b"trailer", 0, at)
    if trailer < 0:
        raise NotApplicable("no trailer")
    body = data[trailer:at]
    return data[:at] + body + data[at:]


def m_truncated(data, rng):
    return data[:int(len(data) * rng.uniform(0.55, 0.8))]


def m_damaged_flate(data, rng):
    candidates = []
    for span in stream_spans(data):
        head = data[span[0]:span[3]]
        if b"/FlateDecode" in head and content_like(data, span):
            length = int(data[span[1]:span[2]])
            if length > 40:
                candidates.append((span[3], length))
    if not candidates:
        raise NotApplicable("no Flate content stream with a direct /Length")
    start, length = candidates[rng.randrange(len(candidates))]
    out = bytearray(data)
    begin = start + length // 2
    for i in range(begin, min(begin + 12, start + length - 4)):
        out[i] ^= 0x5A
    return bytes(out)


def _first_page_content(scan: autotag.Scan):
    for obj in scan.top:
        if re.search(rb"/Type\s*/Page\b", obj.head):
            ref = re.search(rb"/Contents\s+(\d+)\s+(\d+)\s+R", obj.head)
            if not ref:
                raise NotApplicable("the first page's /Contents is not one reference")
            content = scan.by_num.get(int(ref.group(1)))
            if content is None or content.data is None or content.in_objstm:
                raise NotApplicable("the content stream is not a top-level stream")
            return content
    raise NotApplicable("no page")


def m_incremental_update(data, rng):
    """Appends a revision whose content stream reverses the first shown string."""
    scan = autotag.Scan(data)
    content = _first_page_content(scan)
    decoded = scan.decoded(content)
    if decoded is None:
        raise NotApplicable("the content stream does not decode")
    match = re.search(rb"\(((?:\\.|[^\\)]){4,})\)\s*Tj", decoded)
    if not match:
        raise NotApplicable("no literal Tj string of four bytes or more")
    reversed_string = match.group(1)[::-1].replace(b"\\", b"")
    reversed_string = re.sub(rb"([()])", rb"\\\1", reversed_string)
    revised = decoded[:match.start(1)] + reversed_string + decoded[match.end(1):]
    _, prev = last_startxref(data)
    root = re.findall(rb"/Root\s+(\d+\s+\d+\s+R)", data)
    if not root:
        raise NotApplicable("no /Root")
    size = max(o.num for o in scan.objects) + 1
    body = zlib.compress(revised, 9)
    out = bytearray(data if data.endswith(b"\n") else data + b"\n")
    at = len(out)
    out += b"%d 0 obj\n<< /Length %d /Filter /FlateDecode >>\nstream\n" % (content.num, len(body)) + body
    out += b"\nendstream\nendobj\n"
    xref_at = len(out)
    out += b"xref\n0 1\n0000000000 65535 f\r\n%d 1\n%010d 00000 n\r\n" % (content.num, at)
    ids = re.findall(rb"/ID\s*\[[^\]]*\]", data)
    out += b"trailer\n<< /Size %d /Root %s /Prev %d %s>>\nstartxref\n%d\n%%%%EOF\n" % (
        size, root[-1], prev, (ids[-1] + b" ") if ids else b"", xref_at)
    return bytes(out)


def m_linearised_stale_hints(data, rng):
    if not re.search(rb"/Linearized\b", data[:2048]):
        raise NotApplicable("not linearised")
    return m_incremental_update(data, rng)


def m_linearised_wrong_first_page(data, rng):
    head = data[:2048]
    match = re.search(rb"/O\s+(\d+)", head)
    if not re.search(rb"/Linearized\b", head) or not match:
        raise NotApplicable("not linearised")
    value = int(match.group(1))
    width = len(match.group(1))
    new = value + 1 if len(str(value + 1)) == width else value - 1
    return data[:match.start(1)] + str(new).encode() + data[match.end(1):]


# -- rewrites from the scanned objects ----------------------------------------------


def latest_objects(scan: autotag.Scan) -> dict[int, autotag.Obj]:
    out = {}
    for obj in scan.top:
        if re.search(rb"/Type\s*/(XRef|ObjStm)\b", obj.head) or re.search(rb"/Linearized\b", obj.head):
            continue
        out[obj.num] = obj
    for obj in scan.inner:
        out.setdefault(obj.num, obj)
    return out


def trailer_refs(data: bytes) -> dict[str, bytes]:
    out = {}
    for key in (b"Root", b"Info"):
        found = re.findall(rb"/" + key + rb"\s+(\d+\s+\d+\s+R)", data)
        if found:
            out[key.decode()] = found[-1]
    ids = re.findall(rb"/ID\s*\[\s*(<[0-9A-Fa-f\s]*>)", data)
    out["ID0"] = ids[-1] if ids else b"<" + hashlib.md5(data).hexdigest().encode() + b">"
    return out


def body_of(obj: autotag.Obj) -> bytes:
    return obj.head.strip()


def stream_object(num: int, head: bytes, data: bytes) -> bytes:
    head = re.sub(rb"/Length\s+\d+(\s+\d+\s+R)?", b"", head.strip())
    if not head.endswith(b">>"):
        raise NotApplicable("a stream dictionary that does not end with >>")
    head = head[:-2] + b" /Length %d>>" % len(data)
    return b"%d 0 obj\n" % num + head + b"\nstream\n" + data + b"\nendstream\nendobj\n"


def write_classic(objects: dict[int, bytes], trailer: bytes) -> bytes:
    out = bytearray(b"%PDF-1.7\n%\xe2\xe3\xcf\xd3\n")
    offsets = {}
    for num in sorted(objects):
        offsets[num] = len(out)
        out += objects[num]
    size = max(offsets) + 1
    start = len(out)
    out += b"xref\n0 %d\n0000000000 65535 f\r\n" % size
    for num in range(1, size):
        out += (b"%010d 00000 n\r\n" % offsets[num]) if num in offsets else b"0000000000 00001 f\r\n"
    out += b"trailer\n<< /Size %d " % size + trailer + b" >>\nstartxref\n%d\n%%%%EOF\n" % start
    return bytes(out)


def plain_rewrite(data: bytes) -> tuple[dict[int, tuple[bytes, bytes | None]], dict]:
    scan = autotag.Scan(data)
    if any(re.search(rb"/Encrypt\b", m) for m in [data[t.start():t.start() + 400] for t in scan.trailers]):
        raise NotApplicable("already encrypted")
    objs = {}
    for num, obj in latest_objects(scan).items():
        objs[num] = (body_of(obj), obj.data)
    return objs, trailer_refs(data)


def m_object_streams(data, rng):
    objs, refs = plain_rewrite(data)
    if "Root" not in refs:
        raise NotApplicable("no /Root")
    packed = {n: b for n, (b, d) in objs.items() if d is None}
    streams = {n: (b, d) for n, (b, d) in objs.items() if d is not None}
    order = sorted(packed)
    offsets_text = bytearray()
    body = bytearray()
    for num in order:
        offsets_text += b"%d %d " % (num, len(body))
        body += packed[num] + b"\n"
    first = len(offsets_text)
    objstm_num = max(objs) + 1
    xref_num = objstm_num + 1
    payload = zlib.compress(bytes(offsets_text) + bytes(body), 9)
    out = bytearray(b"%PDF-1.7\n%\xe2\xe3\xcf\xd3\n")
    entries = {}
    for num in sorted(streams):
        entries[num] = (1, len(out), 0)
        head, stream = streams[num]
        out += stream_object(num, head, stream)
    entries[objstm_num] = (1, len(out), 0)
    out += (b"%d 0 obj\n<< /Type /ObjStm /N %d /First %d /Filter /FlateDecode /Length %d >>\nstream\n"
            % (objstm_num, len(order), first, len(payload)) + payload + b"\nendstream\nendobj\n")
    for index, num in enumerate(order):
        entries[num] = (2, objstm_num, index)
    entries[xref_num] = (1, len(out), 0)
    size = xref_num + 1
    rows = bytearray()
    for n in range(size):
        kind, f2, f3 = entries.get(n, (0, 0, 0 if n else 255))
        rows += bytes([kind]) + f2.to_bytes(4, "big") + bytes([f3 & 0xFF])
    rows = zlib.compress(bytes(rows), 9)
    info = b" /Info " + refs["Info"] if "Info" in refs else b""
    out += (b"%d 0 obj\n<< /Type /XRef /Size %d /W [1 4 1] /Root %s%s /ID [%s %s] /Filter /FlateDecode "
            b"/Length %d >>\nstream\n" % (xref_num, size, refs["Root"], info, refs["ID0"], refs["ID0"], len(rows))
            + rows + b"\nendstream\nendobj\n")
    out += b"startxref\n%d\n%%%%EOF\n" % entries[xref_num][1]
    return bytes(out)


# -- encryption ---------------------------------------------------------------------


def encrypt_strings(text: bytes, encrypt) -> bytes:
    """Replaces every literal and hex string of an object's text by its encrypted hex form."""
    out = bytearray()
    i = 0
    n = len(text)
    while i < n:
        ch = text[i:i + 1]
        if ch == b"%":
            end = text.find(b"\n", i)
            end = n if end < 0 else end
            out += text[i:end]
            i = end
            continue
        if ch == b"(":
            depth, j, raw = 1, i + 1, bytearray()
            while j < n and depth:
                c = text[j:j + 1]
                if c == b"\\":
                    nxt = text[j + 1:j + 2]
                    esc = {b"n": b"\n", b"r": b"\r", b"t": b"\t", b"b": b"\b", b"f": b"\f",
                           b"(": b"(", b")": b")", b"\\": b"\\"}
                    if nxt in esc:
                        raw += esc[nxt]
                        j += 2
                    elif nxt.isdigit():
                        m = re.match(rb"[0-7]{1,3}", text[j + 1:j + 4])
                        raw.append(int(m.group(0), 8) & 0xFF)
                        j += 1 + len(m.group(0))
                    elif nxt in (b"\r", b"\n"):
                        j += 2 if text[j + 1:j + 3] == b"\r\n" else 2
                    else:
                        raw += nxt
                        j += 2
                    continue
                if c == b"(":
                    depth += 1
                elif c == b")":
                    depth -= 1
                    if depth == 0:
                        break
                raw += c
                j += 1
            out += b"<" + encrypt(bytes(raw)).hex().upper().encode() + b">"
            i = j + 1
            continue
        if ch == b"<" and text[i + 1:i + 2] != b"<":
            end = text.find(b">", i)
            digits = re.sub(rb"[^0-9A-Fa-f]", b"", text[i + 1:end])
            if len(digits) % 2:
                digits += b"0"
            out += b"<" + encrypt(bytes.fromhex(digits.decode())).hex().upper().encode() + b">"
            i = end + 1
            continue
        if ch == b"<":
            out += b"<<"
            i += 2
            continue
        out += ch
        i += 1
    return bytes(out)


def encrypt_file(data: bytes, rng: random.Random, revision: int, user: bytes = b"",
                 encrypt_metadata: bool = True, encrypt_extra: bytes = b"") -> bytes:
    """Encrypts a clean file; `encrypt_extra` is dictionary text appended to /Encrypt as it stands
    (the synthesiser plants a vendor key there, whose strings the format leaves unencrypted)."""
    objs, refs = plain_rewrite(data)
    if "Root" not in refs:
        raise NotApplicable("no /Root")
    doc_id = bytes.fromhex(re.sub(rb"[^0-9A-Fa-f]", b"", refs["ID0"]).decode() or "00")
    owner = b"pdfkit-owner"
    p = -3904
    aes = revision >= 4
    if revision <= 4:
        length = {2: 5, 3: 16, 4: 16}[revision]
        o_entry = pdfcrypt.owner_entry_r234(owner, user, revision, length)
        file_key = pdfcrypt.file_key_r234(user, o_entry, p, doc_id, revision, length, encrypt_metadata)
        u_entry = pdfcrypt.user_entry_r234(file_key, doc_id, revision)
        if revision == 2:
            enc_dict = b"/Filter /Standard /V 1 /R 2 /Length 40"
        elif revision == 3:
            enc_dict = b"/Filter /Standard /V 2 /R 3 /Length 128"
        else:
            enc_dict = (b"/Filter /Standard /V 4 /R 4 /Length 128 /CF << /StdCF << /CFM /AESV2 /AuthEvent "
                        b"/DocOpen /Length 16 >> >> /StmF /StdCF /StrF /StdCF")
        enc_dict += b" /O <%s> /U <%s> /P %d" % (o_entry.hex().encode(), u_entry.hex().encode(), p)
    else:
        file_key = bytes(rng.randrange(256) for _ in range(32))
        salts = bytes(rng.randrange(256) for _ in range(32))
        entries = pdfcrypt.entries_r56(user, owner, file_key, revision, p, encrypt_metadata, salts)
        enc_dict = (b"/Filter /Standard /V 5 /R %d /Length 256 /CF << /StdCF << /CFM /AESV3 /AuthEvent /DocOpen "
                    b"/Length 32 >> >> /StmF /StdCF /StrF /StdCF" % revision)
        for key in ("O", "U", "OE", "UE", "Perms"):
            enc_dict += b" /%s <%s>" % (key.encode(), entries[key].hex().encode())
        enc_dict += b" /P %d" % p
    if not encrypt_metadata:
        enc_dict += b" /EncryptMetadata false"
    enc_dict += encrypt_extra

    def cipher_for(num):
        if revision >= 5:
            key = file_key
        else:
            key = pdfcrypt.object_key(file_key, num, 0, aes)

        def run(raw: bytes) -> bytes:
            if not aes:
                return pdfcrypt.rc4(key, raw)
            iv = bytes(rng.randrange(256) for _ in range(16))
            return iv + pdfcrypt.aes_cbc(key, iv, raw)
        return run

    out_objects = {}
    for num, (head, stream) in objs.items():
        cipher = cipher_for(num)
        is_xmp = stream is not None and re.search(rb"/Type\s*/Metadata\b", head)
        new_head = encrypt_strings(head, cipher)
        if stream is None:
            out_objects[num] = b"%d 0 obj\n" % num + new_head + b"\nendobj\n"
        else:
            payload = stream if (is_xmp and not encrypt_metadata) else cipher(stream)
            out_objects[num] = stream_object(num, new_head, payload)
    enc_num = max(objs) + 1
    out_objects[enc_num] = b"%d 0 obj\n<< " % enc_num + enc_dict + b" >>\nendobj\n"
    info = b" /Info " + refs["Info"] if "Info" in refs else b""
    trailer = b"/Root %s%s /Encrypt %d 0 R /ID [<%s> <%s>]" % (
        refs["Root"], info, enc_num, doc_id.hex().encode(), doc_id.hex().encode())
    return write_classic(out_objects, trailer)


def encryption(revision, encrypt_metadata=True, user=b""):
    def mutate(data, rng):
        return encrypt_file(data, rng, revision, user, encrypt_metadata)
    return mutate


# -- the table of mutations --------------------------------------------------------

# name: (function, tags, expect_open, needs)
MUTATIONS = {
    "xref_missing": (m_xref_missing, ["xref_missing"], "repaired", "table"),
    "xref_wrong_offsets": (m_xref_wrong_offsets, ["xref_wrong_offsets"], "repaired", "table"),
    "startxref_wrong": (m_startxref_wrong, ["startxref_wrong"], "repaired", ""),
    "eof_missing": (m_eof_missing, ["eof_missing"], "opens", ""),
    "junk_before_header_short": (m_junk_short, ["junk_before_header"], "opens", ""),
    "junk_before_header_long": (m_junk_long, ["junk_before_header"], "repaired", ""),
    "length_wrong_short": (m_length_short, ["length_wrong"], "repaired", ""),
    "length_wrong_long": (m_length_long, ["length_wrong"], "repaired", ""),
    "endstream_missing": (m_endstream_missing, ["endstream_missing"], "repaired", ""),
    "endobj_missing": (m_endobj_missing, ["endobj_missing"], "repaired", ""),
    "two_trailers": (m_two_trailers, ["two_trailers"], "opens", "table"),
    "truncated": (m_truncated, ["truncated"], "repaired", ""),
    "damaged_flate": (m_damaged_flate, ["damaged_flate"], "opens", ""),
    "incremental_update": (m_incremental_update, ["incremental_update"], "opens", ""),
    "linearised_stale_hints": (m_linearised_stale_hints, ["linearised_stale_hints", "linearised",
                                                          "incremental_update"], "opens", "linearised"),
    "linearised_wrong_first_page": (m_linearised_wrong_first_page, ["linearised_stale_hints", "linearised"],
                                    "repaired", "linearised"),
    "object_streams": (m_object_streams, ["object_streams", "xref_stream"], "opens", "plain"),
    "rc4_40": (encryption(2), ["rc4_40"], "opens", "plain"),
    "rc4_128": (encryption(3), ["rc4_128"], "opens", "plain"),
    "aesv2": (encryption(4), ["aesv2"], "opens", "plain"),
    "aesv3_r5": (encryption(5), ["aesv3_r5"], "opens", "plain"),
    "aesv3_r6": (encryption(6), ["aesv3_r6"], "opens", "plain"),
    "encrypt_metadata_false": (encryption(4, encrypt_metadata=False), ["aesv2", "encrypt_metadata_false"],
                               "opens", "plain"),
    "user_password_r2": (encryption(2, user=b"secret"), ["rc4_40", "user_password"],
                         "refused:password_required", "plain"),
    "user_password_r3": (encryption(3, user=b"secret"), ["rc4_128", "user_password"],
                         "refused:password_required", "plain"),
    "user_password_r4": (encryption(4, user=b"secret"), ["aesv2", "user_password"],
                         "refused:password_required", "plain"),
    "user_password_r5": (encryption(5, user=b"secret"), ["aesv3_r5", "user_password"],
                         "refused:password_required", "plain"),
    "user_password_r6": (encryption(6, user=b"secret"), ["aesv3_r6", "user_password"],
                         "refused:password_required", "plain"),
}

# How many sources each mutation is applied to.
PER_MUTATION = {"rc4_40": 3, "rc4_128": 3, "aesv2": 3, "aesv3_r5": 2, "aesv3_r6": 3,
                "encrypt_metadata_false": 3, "user_password_r2": 1, "user_password_r3": 1,
                "user_password_r4": 1, "user_password_r5": 1, "user_password_r6": 1,
                "junk_before_header_short": 2, "junk_before_header_long": 2, "xref_wrong_offsets": 2,
                "startxref_wrong": 3, "xref_missing": 3, "eof_missing": 3, "two_trailers": 3,
                "truncated": 3, "incremental_update": 3, "object_streams": 4,
                "linearised_stale_hints": 3, "linearised_wrong_first_page": 2,
                "length_wrong_short": 3, "length_wrong_long": 2}
DEFAULT_PER_MUTATION = 5

BAD_FOR_SOURCE = {"incremental_update", "xref_stream", "object_streams", "hybrid_xref", "rc4_40", "rc4_128",
                  "aesv2", "aesv3_r5", "aesv3_r6", "user_password", "two_trailers", "junk_before_header",
                  "eof_missing", "truncated", "xref_missing", "startxref_wrong", "xref_wrong_offsets",
                  "length_wrong", "endstream_missing", "endobj_missing", "damaged_flate"}


def sources(rows: list[dict], need: str) -> list[dict]:
    """Clean source rows for a mutation, one per generator first, in a stable order."""
    pool = []
    for row in rows:
        if row["set"] in ("mutate", "synth") or row["expect_open"] != "opens" or row["expect_text"] != "agree":
            continue
        if not row["pages"] or int(row["bytes"]) > 1_500_000:
            continue
        tags = set(common.split_tags(row["tags"]))
        linear = "linearised" in tags
        if need == "linearised":
            if not linear or tags & (BAD_FOR_SOURCE - {"incremental_update"}):
                continue
        elif tags & BAD_FOR_SOURCE or linear:
            continue
        pool.append(row)
    pool.sort(key=lambda r: hashlib.sha256((r["id"] + need).encode()).hexdigest())
    by_gen, rest = [], []
    seen = set()
    for row in pool:
        gen = next((t for t in common.split_tags(row["tags"]) if t.startswith("gen_")), "none")
        (rest if gen in seen else by_gen).append(row)
        seen.add(gen)
    return by_gen + rest


def restore(rows: list[dict]) -> int:
    """Rewrites every absent mutated file and checks it against the manifest's hash."""
    by_id = {r["id"]: r for r in rows}
    restored, problems = 0, []
    for row in rows:
        target = common.file_path(row)
        if row["set"] != "mutate" or target.exists():
            continue
        _, source_id, name, seed = row["source"].split(":")
        source = common.file_path(by_id[source_id])
        if not source.exists():
            problems.append(f"{row['id']}: its source {source_id} is absent; fetch it first")
            continue
        data = MUTATIONS[name][0](source.read_bytes(), random.Random(int(seed)))
        if common.sha256_bytes(data) != row["sha256"]:
            problems.append(f"{row['id']}: the mutation does not reproduce the registered bytes")
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        restored += 1
    print(json.dumps({"restored": restored, "problems": problems}, indent=1))
    return 1 if problems else 0


def main() -> int:
    import add
    import pdfium

    parser = argparse.ArgumentParser(description="Write and register the seeded mutations.")
    parser.add_argument("--mutation", action="append", help="only these mutations")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--restore", action="store_true",
                        help="write back absent mutated files from their source, mutation and seed")
    args = parser.parse_args()

    rows = common.read_manifest()
    if args.restore:
        return restore(rows)
    chosen = args.mutation or list(MUTATIONS)
    staging = common.cache_dir() / "mutate-staging"
    planned = []
    skipped = {}
    for name in chosen:
        fn, tags, expect_open, need = MUTATIONS[name]
        wanted = PER_MUTATION.get(name, DEFAULT_PER_MUTATION)
        made = 0
        for row in sources(rows, need):
            if made >= wanted:
                break
            data = common.file_path(row).read_bytes()
            if need == "table":
                _, offset = last_startxref(data)
                if data[offset:offset + 4] != b"xref":
                    continue
            seed = seed_for(row["id"], name)
            try:
                result = fn(data, random.Random(seed))
            except NotApplicable as err:
                skipped.setdefault(name, []).append(f"{row['id']}: {err}")
                continue
            path = staging / name / f"{row['id']}.pdf"
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(result)
            planned.append((path, row, name, seed, tags, expect_open))
            made += 1
    report = {"planned": len(planned), "skipped": {k: len(v) for k, v in skipped.items()}}
    if args.dry_run:
        print(json.dumps(report, indent=1))
        return 0
    with pdfium.Pool(8) as pool:
        paths = [p[0] for p in planned]
        analysis = add.analyse(paths, pool)
        per_file = {}
        for path, row, name, seed, tags, expect_open in planned:
            probe = analysis[str(path)].get("pdfium", {})
            refused = expect_open.startswith("refused:")
            overrides = {"expect_open": expect_open,
                         "expect_text": "agree" if probe.get("ok") and not refused and probe.get("text_chars") else "n/a",
                         "expect_images": "n/a" if refused else row["expect_images"]}
            drop = set()
            if refused:
                drop = {t for t in analysis[str(path)]["tags"] if not t.startswith(("rc4", "aes", "user_", "encrypt"))}
            per_file[path] = {"tags": tags, "drop_tags": sorted(drop), "overrides": overrides,
                              "source": f"mutate:{row['id']}:{name}:{seed}", "licence": row["licence"],
                              "notes": f"{name} of {row['id']}"}
        added = add.register(paths, "mutate", "inherited", lambda p: "", pool, rows=rows,
                             analysis=analysis, per_file=per_file)
        common.write_manifest(rows)
    report["added"] = len(added)
    print(json.dumps(report, indent=1))
    return 0


if __name__ == "__main__":
    common.run_tool(main)
