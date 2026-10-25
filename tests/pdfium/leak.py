# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# The second extractor's leak check of a text redaction run (build-8.md
# section 3.3): for every outcome whose redact_text output was written, each
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
#     and attribute names.
# The removed codes pdfkit reports are searched too (binary and
# hexadecimal) and reported as `codes`, not counted: a code means a glyph
# only in its own font, and the same codes in another font's string draw
# other text, which the two searches above judge.
# A counted hit is a leak: build-8.md section 5 stops the line on one. The
# report goes to <run>/pdfium-leak.json.
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
import pdfium  # noqa: E402

PLACEHOLDER = "[REDACTED]"
NAME_CHARS = re.compile(rb"[^\s/\[\]<>(){}%]")
FONT_PROGRAM = re.compile(rb"/(?:FontFile[23]?|ToUnicode)\s+(\d+)\s+\d+\s+R")
TYPE0_ENCODING = re.compile(rb"/Subtype\s*/Type0\b.*?/Encoding\s+(\d+)\s+\d+\s+R", re.S)
# An inline image's data, which holds samples, not text.
INLINE_IMAGE = re.compile(rb"\bBI\b.*?\bID\s.*?\sEI\b", re.S)
XML_NAMES = re.compile(rb"</?[?!]?[A-Za-z_:][-\w:.]*|[A-Za-z_:][-\w:.]*\s*=")
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


def found(blob: bytes, needle: bytes, fold_case: bool) -> bool:
    haystack = blob.lower() if fold_case else blob
    target = needle.lower() if fold_case else needle
    at = haystack.find(target)
    while at >= 0:
        if not in_name(blob, at):
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
            blob = XML_NAMES.sub(b" ", blob)
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


def check_file(pool, row: dict, run_dir: Path, part: dict) -> dict:
    output = run_dir / part["output"]
    strings = [s for s in part.get("strings", []) if s]
    probe = pool.probe(output, ["text"])
    entry = {"opens": bool(probe.get("ok")), "hits": []}
    if probe.get("ok"):
        entry["hits"] += text_hits([t or "" for t in probe.get("text", [])], strings)
    entry["hits"] += byte_hits(output, strings, part.get("codes", []))
    return entry


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run", required=True, help="the dataset run id")
    parser.add_argument("--id", action="append", help="only these ids")
    parser.add_argument("--workers", type=int, default=4)
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
    return 1 if leaking else 0


if __name__ == "__main__":
    common.run_tool(main)
