# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# The second extractor's leak check of a text redaction run: for every outcome whose redact_text output was written, each
# redacted string is searched for
#   - in PDFium's text of every output page, under the default folding of
#     docs/DESIGN.md section 11 (canonical decomposition with the combining
#     marks stripped, lower case, white space collapsed; no compatibility
#     folding, so a full-width letter or a sharp s is another character), with
#     every placeholder cut out first, so a word inside "[REDACTED]" is not a
#     leak; and
#   - in the output's bytes, stream by stream decoded by the dataset tools'
#     own scanner (autotag.py, which shares no code with pdfkit), in Latin-1,
#     UTF-16BE and UTF-8, case folded for ASCII; the font streams are left out
#     by the key or the type that names them (/FontFile, /FontFile2,
#     /FontFile3, /ToUnicode, a CMap, a Type0 /Encoding), as are image samples
#     and ICC profiles, which hold no text; an occurrence inside a PDF name
#     token (a resource name, a marked-content tag) is not text, nor are an
#     inline image's samples, and an XMP packet is read without its element
#     and attribute names and its namespace declarations' URIs.
# With --metadata, the outputs of the metadata rewrite are searched the same
# way for the original values of the rewritten fields, and PDFium's reading
# of their Info fields too; that report goes to
# <run>/pdfium-leak-metadata.json.
# The removed codes pdfkit reports are searched too (binary and
# hexadecimal) and reported as `codes`, not counted: a code means a glyph
# only in its own font, and the same codes in another font's string draw
# other text, which the two searches above judge.
# A counted hit is a leak, and one stops the line. The report goes to
# <run>/pdfium-leak.json.
# With --images it checks an image treatment run instead: for every outcome
# whose redact_images output was written, the encoded bytes of every treated
# and every removed image are taken from the input by PDFium (each image
# object's FPDFImageObj_GetImageDataRaw, aligned with the treatment by the
# SHA-256 pdfkit_redact_images reported) and searched for in the output's raw
# bytes and in every stream of it decoded by autotag.py's scanner. A hit is an
# original that survived, which stops the line; an image PDFium does not
# reach on the input's pages (one inside a pattern) is counted as not
# searched, as is encoded data under 32 bytes, which can occur by chance. The
# report goes to <run>/pdfium-leak-images.json.
"""Search a redaction run's outputs for the redacted strings: python tests/pdfium/leak.py --run <id>."""

from __future__ import annotations

import argparse
import concurrent.futures
import json
import re
import sys
import unicodedata
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parents[1] / "dataset" / "tools"))

import autotag  # noqa: E402
import common  # noqa: E402
import images  # noqa: E402
import pdfium  # noqa: E402

PLACEHOLDER = "[REDACTED]"
NAME_CHARS = re.compile(rb"[^\s/\[\]<>(){}%]")
FONT_PROGRAM = re.compile(rb"/(?:FontFile[23]?|ToUnicode)\s+(\d+)\s+\d+\s+R")
TYPE0_ENCODING = re.compile(rb"/Subtype\s*/Type0\b.*?/Encoding\s+(\d+)\s+\d+\s+R", re.S)
# An inline image's data, which holds samples, not text.
INLINE_IMAGE = re.compile(rb"\bBI\b.*?\bID\s.*?\sEI\b", re.S)
XML_NAMES = re.compile(rb"</?[?!]?[A-Za-z_:][-\w:.]*|[A-Za-z_:][-\w:.]*\s*=")
# A namespace declaration with its URI, the format's vocabulary.
XMLNS = re.compile(rb"xmlns(?::[-\w.]+)?\s*=\s*(['\"]).*?\1", re.S)
ICC = re.compile(rb"/(?:ICCBased|DestOutputProfile)\s+(\d+)\s+\d+\s+R")


def fold(text: str) -> str:
    """The default folding: canonical decomposition, marks stripped, lower case, spaces collapsed."""
    text = unicodedata.normalize("NFD", text)
    text = "".join(c for c in text if unicodedata.category(c) not in ("Mn", "Me"))
    return " ".join(text.lower().split())


def text_hits(page_texts: list[str], strings: list[str]) -> list[dict]:
    hits = []
    for page, text in enumerate(page_texts, start=1):
        folded = fold(re.sub(re.escape(PLACEHOLDER), " ", text or "", flags=re.IGNORECASE))
        for original in strings:
            needle = fold(original)
            if needle and needle in folded:
                hits.append({"string": original, "surface": "pdfium_text", "page": page})
    return hits


def in_name(blob: bytes, at: int) -> bool:
    """Whether the bytes at `at` lie inside a name token (a '/' and regular characters before them)."""
    start = at
    while start > 0 and NAME_CHARS.match(blob, start - 1):
        start -= 1
    return start > 0 and blob[start - 1:start] == b"/"


def in_number(blob: bytes, at: int, length: int) -> bool:
    """Whether a needle of digits at `at` is part of a longer number (a path's 466.1994), which is a
    content stream's operand, not text."""
    before = blob[at - 1:at] if at > 0 else b""
    after = blob[at + length:at + length + 1]
    return bool(re.fullmatch(rb"[0-9.\-]", before or b"x")) or bool(re.fullmatch(rb"[0-9.]", after or b"x"))


def found(blob: bytes, needle: bytes, fold_case: bool) -> bool:
    haystack = blob.lower() if fold_case else blob
    target = needle.lower() if fold_case else needle
    numeric = re.fullmatch(rb"[0-9]+", needle) is not None
    at = haystack.find(target)
    while at >= 0:
        if not in_name(blob, at) and not (numeric and in_number(blob, at, len(target))):
            return True
        at = haystack.find(target, at + 1)
    return False


def byte_hits(path: Path, strings: list[str], codes: list[str]) -> list[dict]:
    data = path.read_bytes()
    scan = autotag.Scan(data)
    skipped = set()
    for obj in scan.objects:
        skipped.update(int(n) for n in FONT_PROGRAM.findall(obj.head))
        skipped.update(int(n) for n in ICC.findall(obj.head))
        skipped.update(int(n) for n in TYPE0_ENCODING.findall(obj.head))
    blobs = []
    for obj in scan.objects:
        if (obj.data is None or obj.num in skipped or obj.has(rb"/Subtype\s*/Image\b")
                or obj.has(rb"/Type\s*/CMap\b")):
            continue
        blob = scan.decoded(obj) or b""
        if obj.has(rb"/Type\s*/Metadata\b"):
            blob = XML_NAMES.sub(b" ", XMLNS.sub(b" ", blob))
        elif b"BI" in blob:
            blob = INLINE_IMAGE.sub(b" ", blob)
        blobs.append((obj.num, blob))
    hits = []
    for original in strings:
        text = " ".join(unicodedata.normalize("NFC", original).split())
        for encoding in ("latin-1", "utf-16-be", "utf-8"):
            try:
                needle = text.encode(encoding)
            except UnicodeEncodeError:
                continue
            where = [num for num, blob in blobs if found(blob.replace(PLACEHOLDER.encode(), b" "),
                                                         needle, encoding != "utf-16-be")]
            if where:
                hits.append({"string": original, "surface": f"bytes:{encoding}", "objects": where[:5]})
                break
    for code in codes:
        binary = bytes.fromhex(code)
        for form in (binary, code.encode(), code.upper().encode()):
            where = [num for num, blob in blobs if form in blob]
            if where:
                hits.append({"string": code, "surface": "codes", "objects": where[:5]})
                break
    return hits


# Encoded data shorter than this is not searched for: a few bytes recur by
# chance in any file.
SHORTEST_IMAGE_DATA = 32


def image_leaks(images_pool, row: dict, run_dir: Path, part: dict) -> dict:
    """The original encoded bytes of every treated or removed image found in the output."""
    wanted = {t["encoded"]["sha256"]: t["identity"] for t in part.get("treatments") or [] if t.get("encoded")}
    work = run_dir / "pdfium-leak-images" / row["id"]
    answer = images_pool.ask({"path": str(common.file_path(row)), "raw": sorted(wanted), "out": str(work)})
    entry = {"input_opens": bool(answer.get("ok")), "searched": 0, "short": 0, "not_reached": [],
             "hits": []}
    if not answer.get("ok"):
        return entry
    needles = {}
    for digest, identity in wanted.items():
        record = answer["images"].get(digest)
        if record is None or not record.get("raw"):
            entry["not_reached"].append(identity)
            continue
        blob = Path(record["raw"]).read_bytes()
        if len(blob) < SHORTEST_IMAGE_DATA:
            entry["short"] += 1
            continue
        needles[identity] = blob
    if not needles:
        return entry
    data = (run_dir / part["output"]).read_bytes()
    scan = autotag.Scan(data)
    blobs = [(0, data)] + [(obj.num, scan.decoded(obj) or b"") for obj in scan.objects if obj.data is not None]
    for identity, blob in sorted(needles.items()):
        entry["searched"] += 1
        where = [num for num, haystack in blobs if blob in haystack]
        if where:
            entry["hits"].append({"identity": identity, "bytes": len(blob), "objects": where[:5]})
    return entry


def images_main(args, rows: list[dict], run_dir: Path) -> int:
    jobs = []
    for row in rows:
        path = run_dir / "outcomes" / f"{row['id']}.json"
        if not path.exists():
            continue
        part = json.loads(path.read_text(encoding="utf-8")).get("redact_images") or {}
        if part.get("status") == "ok" and part.get("output"):
            jobs.append((row, part))
    results = {}
    with images.pool(args.workers) as images_pool, \
            concurrent.futures.ThreadPoolExecutor(args.workers) as threads:
        futures = {row["id"]: threads.submit(image_leaks, images_pool, row, run_dir, part) for row, part in jobs}
        for row_id, future in futures.items():
            results[row_id] = future.result()
    leaking = {k: v["hits"] for k, v in results.items() if v["hits"]}
    summary = {"files": len(results), "searched": sum(v["searched"] for v in results.values()),
               "leaking_files": len(leaking), "hits": sum(len(v) for v in leaking.values()),
               "short": sum(v["short"] for v in results.values()),
               "not_reached": sum(len(v["not_reached"]) for v in results.values()),
               "unopened": sorted(k for k, v in results.items() if not v["input_opens"])}
    (run_dir / "pdfium-leak-images.json").write_text(
        json.dumps({"summary": summary, "files": dict(sorted(results.items()))}, indent=1), encoding="utf-8")
    print(json.dumps(summary | {"leaking": sorted(leaking)[:50]}, indent=1))
    if not results:
        print("SKIPPED image leak check: no outcome of this run carries a redact_images output", file=sys.stderr)
    return 1 if leaking else 0


def check_file(pool, row: dict, run_dir: Path, part: dict) -> dict:
    output = run_dir / part["output"]
    strings = [s for s in part.get("strings", []) if s]
    probe = pool.probe(output, ["text"])
    entry = {"opens": bool(probe.get("ok")), "hits": []}
    if probe.get("ok"):
        entry["hits"] += text_hits([t or "" for t in probe.get("text", [])], strings)
    entry["hits"] += byte_hits(output, strings, part.get("codes", []))
    return entry


def check_metadata(pool, row: dict, run_dir: Path, part: dict) -> dict:
    """The original values of a metadata rewrite in its output: PDFium's text, its
    Info fields (FPDF_GetMetaText) and the bytes, as check_file searches them."""
    output = run_dir / part["output"]
    strings = [s for s in part.get("originals", []) if s]
    probe = pool.probe(output, ["text", "meta"])
    entry = {"opens": bool(probe.get("ok")), "hits": []}
    if probe.get("ok"):
        entry["hits"] += text_hits([t or "" for t in probe.get("text", [])], strings)
        for tag, value in (probe.get("meta") or {}).items():
            folded = fold(value or "")
            entry["hits"] += [{"string": s, "surface": "pdfium_meta", "field": tag}
                              for s in strings if fold(s) and fold(s) in folded]
    entry["hits"] += byte_hits(output, strings, [])
    return entry


def metadata_leaks(run_dir: Path, rows: list[dict], workers: int) -> int:
    """The leak check of every set_metadata output; writes pdfium-leak-metadata.json."""
    jobs = []
    for row in rows:
        path = run_dir / "outcomes" / f"{row['id']}.json"
        if not path.exists():
            continue
        part = json.loads(path.read_text(encoding="utf-8")).get("set_metadata") or {}
        if part.get("status") == "ok" and part.get("output"):
            jobs.append((row, part))
    results = {}
    with pdfium.Pool(workers) as pool, concurrent.futures.ThreadPoolExecutor(workers) as threads:
        futures = {row["id"]: threads.submit(check_metadata, pool, row, run_dir, part) for row, part in jobs}
        for row_id, future in futures.items():
            results[row_id] = future.result()
    leaking = {k: v["hits"] for k, v in results.items() if v["hits"]}
    summary = {"files": len(results), "leaking_files": len(leaking),
               "pdfium_text_hits": sum(1 for v in leaking.values() for h in v if h["surface"] == "pdfium_text"),
               "pdfium_meta_hits": sum(1 for v in leaking.values() for h in v if h["surface"] == "pdfium_meta"),
               "byte_hits": sum(1 for v in leaking.values() for h in v if h["surface"].startswith("bytes")),
               "unopened": sorted(k for k, v in results.items() if not v["opens"])}
    (run_dir / "pdfium-leak-metadata.json").write_text(
        json.dumps({"summary": summary, "leaks": dict(sorted(leaking.items()))}, indent=1), encoding="utf-8")
    print(json.dumps({"metadata": summary | {"leaking": sorted(leaking)[:50]}}, indent=1))
    return 1 if leaking else 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run", required=True, help="the dataset run id")
    parser.add_argument("--id", action="append", help="only these ids")
    parser.add_argument("--workers", type=int, default=4)
    parser.add_argument("--metadata", action="store_true",
                        help="also search every metadata rewrite's output for the original values")
    parser.add_argument("--images", action="store_true",
                        help="search the image treatment outputs for the treated and removed images' bytes")
    args = parser.parse_args()
    try:
        if not pdfium.default_dll().exists():
            raise FileNotFoundError
    except (FileNotFoundError, common.ToolError):
        print("SKIPPED leak check: PDFium is absent; run `python tests/pdfium/fetch.py`")
        return 0
    run_dir = common.reports_dir() / args.run
    rows = common.read_manifest()
    if args.id:
        rows = [r for r in rows if r["id"] in args.id]
    if args.images:
        return images_main(args, rows, run_dir)
    jobs = []
    for row in rows:
        path = run_dir / "outcomes" / f"{row['id']}.json"
        if not path.exists():
            continue
        part = json.loads(path.read_text(encoding="utf-8")).get("redact_text") or {}
        if part.get("status") == "ok" and part.get("output"):
            jobs.append((row, part))
    results = {}
    with pdfium.Pool(args.workers) as pool, concurrent.futures.ThreadPoolExecutor(args.workers) as threads:
        futures = {row["id"]: threads.submit(check_file, pool, row, run_dir, part) for row, part in jobs}
        for row_id, future in futures.items():
            results[row_id] = future.result()
    codes = {k: [h for h in v["hits"] if h["surface"] == "codes"] for k, v in results.items()}
    codes = {k: v for k, v in codes.items() if v}
    leaking = {k: [h for h in v["hits"] if h["surface"] != "codes"] for k, v in results.items()}
    leaking = {k: v for k, v in leaking.items() if v}
    text_words = sum(1 for v in leaking.values() for h in v if h["surface"] == "pdfium_text")
    byte_words = sum(1 for v in leaking.values() for h in v if h["surface"].startswith("bytes"))
    summary = {"files": len(results), "leaking_files": len(leaking), "pdfium_text_hits": text_words,
               "byte_hits": byte_words, "code_matches": sum(len(v) for v in codes.values()),
               "unopened": sorted(k for k, v in results.items() if not v["opens"])}
    (run_dir / "pdfium-leak.json").write_text(
        json.dumps({"summary": summary, "leaks": dict(sorted(leaking.items())),
                    "codes": dict(sorted(codes.items()))}, indent=1), encoding="utf-8")
    print(json.dumps(summary | {"leaking": sorted(leaking)[:50]}, indent=1))
    if not results:
        print("SKIPPED leak check: no outcome of this run carries a redact_text output", file=sys.stderr)
    metadata = metadata_leaks(run_dir, rows, args.workers) if args.metadata else 0
    return 1 if leaking or metadata else 0


if __name__ == "__main__":
    common.run_tool(main)
