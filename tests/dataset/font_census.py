# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# The font census: the gate of the fonts layer before the content interpreter
# exists. It runs pdfkit_font_census (tests/dataset/font_census.cpp) over every
# manifest file whose open expectation is opens or repaired, each file in its
# own child process with a 30 s limit, reads PDFium's text of the same file
# through tests/pdfium/, and scores each file by the F1 of the two character
# multisets (white space and control characters dropped, the ligatures U+FB00
# to U+FB06 decomposed on both sides), because a character bag needs no
# layout. It writes census.tsv (one row per file) and summary.md (the gate's
# numbers, the per-kind, per-rung and per-tag tables and the worst files)
# into --out. --write-manifest fills the manifest's expect_fonts column from
# the run: loads (every font loads and every code decodes), unreadable:<n>, or
# n/a (no text or not opened); --check compares the run with that column.
"""Run the font census: python tests/dataset/font_census.py --out <folder> [--write-manifest | --check]."""

from __future__ import annotations

import argparse
import collections
import concurrent.futures
import json
import os
import statistics
import subprocess
import sys
import time
import unicodedata
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "dataset" / "tools"))
sys.path.insert(0, str(REPO / "tests" / "pdfium"))

import common  # noqa: E402
import pdfium  # noqa: E402

TIMEOUT = 30.0
MEDIAN_GATE = 0.95
KILL = 0.90
UNREADABLE_GATE = 0.01
RUNGS = ("to_unicode", "glyph_name", "font_program", "actual_text", "unreadable")
LIGATURES = {"ﬀ": "ff", "ﬁ": "fi", "ﬂ": "fl", "ﬃ": "ffi", "ﬄ": "ffl",
             "ﬅ": "ſt", "ﬆ": "st"}
FONT_TAGS = None  # read from the taxonomy at run time


def find_census(explicit: str | None) -> Path | None:
    candidates = [Path(explicit)] if explicit else []
    for preset in ("mingw-release", "mingw-debug"):
        candidates += [REPO / "out" / preset / "pdfkit_font_census.exe",
                       REPO / "out" / preset / "pdfkit_font_census"]
    return next((c for c in candidates if c.is_file()), None)


def characters(text: str) -> collections.Counter:
    """The multiset of characters the gate compares."""
    out: collections.Counter = collections.Counter()
    for char in text or "":
        for piece in LIGATURES.get(char, char):
            if piece.isspace() or unicodedata.category(piece) == "Cc":
                continue
            out[piece] += 1
    return out


def f1(candidate: collections.Counter, reference: collections.Counter) -> float | None:
    """The multiset F1; None when the reference holds nothing."""
    total_a, total_b = sum(candidate.values()), sum(reference.values())
    if total_b == 0:
        return None
    if total_a == 0:
        return 0.0
    shared = sum((candidate & reference).values())
    if shared == 0:
        return 0.0
    precision, recall = shared / total_a, shared / total_b
    return 2 * precision * recall / (precision + recall)


def run_census(census: Path, row: dict) -> dict:
    path = common.file_path(row)
    started = time.monotonic()
    try:
        proc = subprocess.run([str(census), str(path)], capture_output=True, timeout=TIMEOUT)
    except subprocess.TimeoutExpired:
        return {"status": "hang", "seconds": TIMEOUT}
    seconds = round(time.monotonic() - started, 3)
    if proc.returncode != 0:
        return {"status": "crash", "exit": proc.returncode, "seconds": seconds,
                "message": proc.stderr.decode("utf-8", "replace")[-300:]}
    try:
        result = json.loads(proc.stdout.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as err:
        return {"status": "crash", "exit": 0, "seconds": seconds, "message": f"bad output: {err}"}
    result["seconds"] = seconds
    return result


def expectation(result: dict) -> str:
    if result.get("status") != "ok" or not result.get("codes"):
        return "n/a"
    if result.get("unreadable"):
        return f"unreadable:{result['unreadable']}"
    return "loads"


def median(values: list[float]) -> float | None:
    return round(statistics.median(values), 4) if values else None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", required=True, help="the report folder")
    parser.add_argument("--census", help="the pdfkit_font_census executable (default: out/)")
    parser.add_argument("--workers", type=int, default=os.cpu_count() or 4)
    parser.add_argument("--set", action="append", help="only these sets")
    parser.add_argument("--id", action="append", help="only these ids")
    parser.add_argument("--write-manifest", action="store_true",
                        help="write the run's values into the manifest's expect_fonts column")
    parser.add_argument("--check", action="store_true",
                        help="fail when a file's value differs from expect_fonts")
    args = parser.parse_args()

    census = find_census(args.census)
    if census is None:
        print("SKIPPED font census: no pdfkit_font_census executable found. Build it with "
              "`cmake --preset mingw-release && cmake --build --preset mingw-release`, or pass "
              "--census <path>.")
        return 0
    all_rows = common.read_manifest()
    rows = list(all_rows)
    if args.set:
        rows = [r for r in rows if r["set"] in args.set]
    if args.id:
        rows = [r for r in rows if r["id"] in args.id]
    missing = [r["id"] for r in rows if not common.file_path(r).exists()]
    if missing:
        print(f"SKIPPED {len(missing)} rows whose files are absent (first {missing[0]}); "
              "run `python dataset/tools/fetch.py`", file=sys.stderr)
        rows = [r for r in rows if r["id"] not in set(missing)]
    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)
    taxonomy = common.read_taxonomy()
    font_tags = [t["tag"] for t in taxonomy if t["group"] == "fonts"]

    started = time.monotonic()
    results: dict[str, dict] = {}
    texts: dict[str, list] = {}
    with pdfium.Pool(size=max(1, args.workers // 2)) as pool, \
            concurrent.futures.ThreadPoolExecutor(args.workers) as executor:
        census_jobs = {executor.submit(run_census, census, r): r["id"] for r in rows}
        pdfium_jobs = {executor.submit(pool.probe, common.file_path(r), ("text",)): r["id"]
                       for r in rows if r["expect_open"] in ("opens", "repaired")}
        for job in concurrent.futures.as_completed(census_jobs):
            results[census_jobs[job]] = job.result()
        for job in concurrent.futures.as_completed(pdfium_jobs):
            probe = job.result()
            texts[pdfium_jobs[job]] = [t or "" for t in probe.get("text", [])] if probe.get("ok") else None
    seconds = round(time.monotonic() - started, 1)

    table = []
    failures = []
    for row in rows:
        result = results[row["id"]]
        gated = row["expect_open"] in ("opens", "repaired")
        reference = texts.get(row["id"])
        mine = "".join(result.get("pages") or [])
        score = f1(characters(mine), characters("".join(reference))) if reference is not None else None
        fonts = result.get("fonts") or []
        entry = {
            "set": row["set"], "id": row["id"], "gated": gated, "status": result.get("status"),
            "fonts": len(fonts), "kinds": ",".join(sorted({f["kind"] for f in fonts})),
            "codes": result.get("codes", 0), "unreadable": result.get("unreadable", 0),
            "rungs": result.get("rungs", {}), "f1": score, "tags": row["tags"].split(",") if row["tags"] else [],
            "font_list": fonts, "expect": expectation(result), "manifest": row.get("expect_fonts", ""),
            "seconds": result.get("seconds"), "message": result.get("message", ""),
            "code": result.get("code", ""),
        }
        table.append(entry)
        if gated and result.get("status") not in ("ok", "refused"):
            failures.append(entry)

    gated = [e for e in table if e["gated"]]
    scored = [e for e in gated if e["f1"] is not None and e["status"] == "ok"]
    refused = [e for e in gated if e["status"] == "refused"]
    codes = sum(e["codes"] for e in scored)
    unreadable = sum(e["unreadable"] for e in scored)
    by_kind: dict[str, collections.Counter] = collections.defaultdict(collections.Counter)
    by_rung: collections.Counter = collections.Counter()
    for e in scored:
        for font in e["font_list"]:
            by_kind[font["kind"] + "/" + font["program"]]["codes"] += font["codes"]
            by_kind[font["kind"] + "/" + font["program"]]["unreadable"] += font["unreadable"]
            by_kind[font["kind"] + "/" + font["program"]]["fonts"] += 1
        for rung in RUNGS:
            by_rung[rung] += e["rungs"].get(rung, 0)
    tag_rows = []
    for tag in font_tags:
        members = [e for e in scored if tag in e["tags"]]
        tag_codes = sum(e["codes"] for e in members)
        tag_rows.append((tag, len(members), median([e["f1"] for e in members]),
                         round(sum(e["unreadable"] for e in members) / tag_codes, 4) if tag_codes else None))
    med = median([e["f1"] for e in scored])
    share = round(unreadable / codes, 4) if codes else 0.0
    loaded = len(gated) - len(failures)
    summary = {
        "files": len(table), "gated": len(gated), "scored": len(scored), "refused": len(refused),
        "failures": len(failures), "fonts_loaded_share": round(loaded / len(gated), 4) if gated else 1.0,
        "median_f1": med, "unreadable_share": share, "codes": codes, "unreadable": unreadable,
        "seconds": seconds,
        "gate_loaded": not failures,
        "gate_median": med is not None and med >= MEDIAN_GATE,
        "gate_unreadable": share <= UNREADABLE_GATE,
        "kill": med is not None and med < KILL,
    }

    with (out_dir / "census.tsv").open("w", encoding="utf-8", newline="\n") as handle:
        handle.write("set\tid\tstatus\tfonts\tkinds\tcodes\tunreadable\tF1\n")
        for e in table:
            handle.write(f"{e['set']}\t{e['id']}\t{e['status']}\t{e['fonts']}\t{e['kinds']}\t{e['codes']}\t"
                         f"{e['unreadable']}\t{'' if e['f1'] is None else round(e['f1'], 4)}\n")
    (out_dir / "census.json").write_text(json.dumps(
        [{k: v for k, v in e.items() if k != "tags"} for e in table], indent=1), encoding="utf-8")

    lines = ["# Font census", "",
             f"Files run: {summary['files']}; gated (expect_open opens or repaired): {summary['gated']}; "
             f"scored (PDFium extracts text): {summary['scored']}; refused at open: {summary['refused']}.", "",
             "| Gate | Value | Threshold | Result |", "|---|---|---|---|",
             f"| Font dictionaries loaded without an exception | {summary['fonts_loaded_share']:.2%} "
             f"({summary['failures']} failures) | 100% | {'pass' if summary['gate_loaded'] else 'FAIL'} |",
             f"| Median character F1 against PDFium | {med} | >= {MEDIAN_GATE} | "
             f"{'pass' if summary['gate_median'] else 'FAIL'} |",
             f"| Unreadable share of codes | {share:.4%} ({unreadable} of {codes}) | <= {UNREADABLE_GATE:.0%} | "
             f"{'pass' if summary['gate_unreadable'] else 'FAIL'} |", "",
             "## Per font kind and program", "", "| Kind/program | Fonts | Codes | Unreadable | Share |",
             "|---|---|---|---|---|"]
    for kind, counts in sorted(by_kind.items()):
        kind_share = counts["unreadable"] / counts["codes"] if counts["codes"] else 0.0
        lines.append(f"| {kind} | {counts['fonts']} | {counts['codes']} | {counts['unreadable']} | {kind_share:.4%} |")
    lines += ["", "## Per rung", "", "| Rung | Codes | Share |", "|---|---|---|"]
    for rung in RUNGS:
        lines.append(f"| {rung} | {by_rung[rung]} | {by_rung[rung] / codes:.4%} |" if codes else f"| {rung} | 0 | - |")
    lines += ["", "## Per font tag", "", "| Tag | Files | Median F1 | Unreadable share |", "|---|---|---|---|"]
    for tag, count, tag_median, tag_share in tag_rows:
        lines.append(f"| {tag} | {count} | {tag_median} | {tag_share} |")
    worst = sorted(scored, key=lambda e: (e["f1"], e["id"]))[:15]
    lines += ["", "## The worst files", "", "| File | F1 | Codes | Unreadable | Fonts |", "|---|---|---|---|---|"]
    for e in worst:
        fonts = "; ".join(f"{f['name']} {f['kind']}/{f['program']} {f['encoding']} "
                          f"tu={int(f['to_unicode'])} {f['unreadable']}/{f['codes']}" for f in e["font_list"][:4])
        lines.append(f"| {e['id']} | {e['f1']:.4f} | {e['codes']} | {e['unreadable']} | {fonts} |")
    if failures:
        lines += ["", "## Failures", ""]
        lines += [f"- {e['id']}: {e['status']} {e['code']} {e['message']}" for e in failures]
    mismatches = [e for e in table if e["manifest"] and e["manifest"] != e["expect"]]
    if args.check:
        lines += ["", f"## expect_fonts mismatches: {len(mismatches)}", ""]
        lines += [f"- {e['id']}: manifest {e['manifest']}, run {e['expect']}" for e in mismatches[:50]]
    (out_dir / "summary.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    (out_dir / "summary.json").write_text(json.dumps(summary, indent=1), encoding="utf-8")

    if args.write_manifest:
        values = {e["id"]: e["expect"] for e in table}
        for row in all_rows:
            if row["id"] in values:
                row["expect_fonts"] = values[row["id"]]
            elif not row.get("expect_fonts"):
                row["expect_fonts"] = "n/a"
        common.write_manifest(all_rows)
    print(json.dumps(summary, indent=1))
    bad = not (summary["gate_loaded"] and summary["gate_median"] and summary["gate_unreadable"])
    if args.check and mismatches:
        bad = True
    return 1 if bad else 0


if __name__ == "__main__":
    common.run_tool(main)
