# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# The independent reader's view of the Info dictionary: for every manifest
# file a dataset run opened and whose metadata it read, PDFium's
# FPDF_GetMetaText for each of the eight Info fields (the binding of
# tests/pdfium/pdfium.py; the API name recalled, the binding exercised by
# every run) is compared with pdfkit_metadata's Info value, read through the
# metadata verb of the same build. Both readers hand back text, so the
# comparison is of strings once trailing NUL characters and the byte-order
# marks PDFium leaves in a UTF-16 value are set aside; an absent field is
# the empty string on both sides, which is how PDFium reports one. A
# difference is written with both values, so the report names each one as a
# reader fault or a PDFium difference; a file whose difference is explained
# in tests/pdfium/metadata_causes.tsv is listed with its cause and not
# counted. The report goes to <run>/pdfium-metadata.json.
"""Compare the Info fields with PDFium: python tests/pdfium/metadata.py --run <id> [--pdfkit <exe>]."""

from __future__ import annotations

import argparse
import concurrent.futures
import json
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(REPO / "dataset" / "tools"))

import common  # noqa: E402
import pdfium  # noqa: E402
import results_cache  # noqa: E402


def find_pdfkit(explicit: str | None) -> Path:
    for candidate in ([Path(explicit)] if explicit else []) + [
            REPO / "out" / "mingw-release" / "pdfkit.exe", REPO / "out" / "mingw-debug" / "pdfkit.exe"]:
        if candidate.is_file():
            return candidate
    raise common.ToolError("no pdfkit.exe under out/mingw-release or out/mingw-debug",
                           "a built command-line tool", "cmake --build --preset mingw-release")


def clean(value: str | None) -> str:
    """A value as text to compare: no NUL, no byte-order mark, no surrounding white space."""
    return (value or "").replace("\x00", "").replace("﻿", "").replace("￾", "").strip()


def pdfkit_info(exe: Path, path: Path) -> dict | None:
    result = subprocess.run([str(exe), "metadata", str(path)], capture_output=True, check=False)
    if result.returncode != 0:
        return None
    answer = json.loads(result.stdout.decode("utf-8"))
    return {row["field"]: row["info"] for row in answer["fields"]}


def compare(pool, exe: Path, row: dict) -> dict:
    path = common.file_path(row)
    ours = pdfkit_info(exe, path)
    probe = results_cache.cached_probe(pool, path, ["meta"])
    if ours is None or not probe.get("ok"):
        return {"compared": False, "pdfkit": ours is not None, "pdfium": bool(probe.get("ok"))}
    theirs = probe.get("meta") or {}
    fields, differences = 0, []
    for tag in pdfium.META_TAGS:
        a, b = clean(ours.get(tag)), clean(theirs.get(tag))
        if not a and not b:
            continue
        fields += 1
        if a != b:
            differences.append({"field": tag, "pdfkit": ours.get(tag), "pdfium": theirs.get(tag)})
    return {"compared": True, "fields": fields, "differences": differences}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run", required=True, help="the dataset run id")
    parser.add_argument("--pdfkit", help="the pdfkit executable (default: out/mingw-release)")
    parser.add_argument("--id", action="append", help="only these ids")
    parser.add_argument("--workers", type=int, default=pdfium.default_workers())
    args = parser.parse_args()
    if not pdfium.default_dll().exists():
        print("SKIPPED metadata comparison: PDFium is absent; run `python tests/pdfium/fetch.py`")
        return 0
    exe = find_pdfkit(args.pdfkit)
    run_dir = common.reports_dir() / args.run
    rows = common.read_manifest()
    if args.id:
        rows = [r for r in rows if r["id"] in args.id]
    chosen = []
    for row in rows:
        outcome_path = run_dir / "outcomes" / f"{row['id']}.json"
        if not outcome_path.exists():
            continue
        part = json.loads(outcome_path.read_text(encoding="utf-8")).get("metadata") or {}
        if part.get("status") == "ok" and part.get("info"):
            chosen.append(row)
    results = {}
    with pdfium.Pool(args.workers) as pool, concurrent.futures.ThreadPoolExecutor(args.workers) as threads:
        futures = {row["id"]: threads.submit(compare, pool, exe, row) for row in chosen}
        for row_id, future in futures.items():
            results[row_id] = future.result()
    causes = read_causes()
    compared = {k: v for k, v in results.items() if v["compared"]}
    named = {k: {"cause": causes[k], "differences": v["differences"]} for k, v in compared.items()
             if v["differences"] and k in causes}
    counted = {k: v for k, v in compared.items() if k not in named}
    fields = sum(v["fields"] for v in counted.values())
    differing = {k: v["differences"] for k, v in counted.items() if v["differences"]}
    summary = {"files": len(results), "compared": len(compared),
               "not_compared": sorted(k for k, v in results.items() if not v["compared"]),
               "fields": fields, "agreeing": fields - sum(len(v) for v in differing.values()),
               "differing_files": len(differing), "named_files": sorted(named)}
    (run_dir / "pdfium-metadata.json").write_text(
        json.dumps({"summary": summary, "differences": dict(sorted(differing.items())),
                    "named": dict(sorted(named.items()))}, indent=1),
        encoding="utf-8")
    print(json.dumps(summary, indent=1))
    return 0 if not differing else 1


def read_causes() -> dict[str, str]:
    """The files whose differences are named PDFium differences, from metadata_causes.tsv."""
    path = HERE / "metadata_causes.tsv"
    causes = {}
    for line in path.read_text(encoding="utf-8").splitlines()[1:]:
        cells = line.split("\t")
        if len(cells) >= 3:
            causes[cells[0]] = f"{cells[1]}: {cells[2]}"
    return causes


if __name__ == "__main__":
    common.run_tool(main)
