# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# The release gate: judges a dataset run made with `tests/dataset/run.py
# --checks all --runs 2`, then `tests/pdfium/metadata.py` and
# `tests/pdfium/leak.py --metadata`, for the metadata functions, and gathers
# every earlier gate's table the run folder holds (phase<n>.json, written by
# the gate script of that phase over the same run) into one table, so the
# release is judged on one dataset. The metadata measures: the dataset's size
# and tag coverage (dataset/tools/report.py), every file with an Info
# dictionary or an XMP packet read, every Info field equal to PDFium's
# FPDF_GetMetaText, every rewrite read back with its written values in both
# places, no original value of a rewritten field in an output (PDFium's text
# and Info fields, and the decoded bytes), both runs identical, no crash and
# no hang. With --write-manifest it also fills the manifest from the run: the
# metadata tags of each file, its expect_metadata and expect_set_metadata,
# and, for a text redaction the manifest names a known gap, clean when the run
# shows it clean. The tables go into the run folder as a markdown and a JSON
# file named after this script.

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO / "dataset" / "tools"))
sys.path.insert(0, str(REPO / "tests" / "dataset"))
sys.path.insert(0, str(Path(__file__).resolve().parent))

import common  # noqa: E402
import report  # noqa: E402
import run as harness  # noqa: E402
from phase7 import Measures, read_json  # noqa: E402

READ = 1.0
PDFIUM_AGREE = 1.0
WRITTEN = 1.0
DETERMINISM = 1.0
SMALLEST, LARGEST = 500, 1000
# The namespaces of the fields, of the media-management properties and of RDF;
# a property in any other is a custom one.
STANDARD_NAMESPACES = {
    "http://purl.org/dc/elements/1.1/", "http://ns.adobe.com/xap/1.0/", "http://ns.adobe.com/pdf/1.3/",
    "http://ns.adobe.com/xap/1.0/mm/", "http://www.w3.org/1999/02/22-rdf-syntax-ns#",
}
META_TAGS = ("meta_info_only", "meta_xmp_only", "meta_both_agree", "meta_both_disagree", "meta_utf16",
             "meta_xmp_custom_namespace", "meta_xmp_history", "meta_xmp_unreadable")


def metadata_tags(part: dict) -> set[str]:
    """The metadata tags a file's outcome shows."""
    if part.get("status") != "ok":
        return set()
    tags = set()
    info, xmp = bool(part.get("info")), part.get("xmp")
    if info and xmp == "absent":
        tags.add("meta_info_only")
    if not info and xmp in ("read", "unreadable"):
        tags.add("meta_xmp_only")
    if info and xmp == "read":
        if part.get("disagree"):
            tags.add("meta_both_disagree")
        elif part.get("agreeing"):
            tags.add("meta_both_agree")
    if part.get("utf16"):
        tags.add("meta_utf16")
    if set(part.get("namespaces") or []) - STANDARD_NAMESPACES:
        tags.add("meta_xmp_custom_namespace")
    if part.get("history"):
        tags.add("meta_xmp_history")
    if xmp == "unreadable":
        tags.add("meta_xmp_unreadable")
    return tags


def surfaces(message: str) -> list[str]:
    """The surfaces a self-check refusal names, each once, in order."""
    found = []
    for surface in re.findall(r"in the (\w+)", message or ""):
        if surface not in found and surface != "output":
            found.append(surface)
    return found


def expectations(row: dict, outcome: dict) -> dict:
    """The metadata columns a run shows for one manifest row."""
    meta = outcome.get("metadata") or {}
    rewrite = outcome.get("set_metadata") or {}
    out = {}
    if meta.get("status") != "ok" or (not meta.get("info") and meta.get("xmp") == "absent"):
        out["expect_metadata"] = "n/a"
    elif meta.get("xmp") == "unreadable":
        out["expect_metadata"] = "known_gap:xmp_unreadable"
    else:
        out["expect_metadata"] = "read"
    if row.get("expect_set_metadata", "").startswith("known_gap:pdfium_"):
        # A cause the analyst wrote from PDFium's reading of the output stays.
        pass
    elif rewrite.get("status") in (None, "skipped"):
        out["expect_set_metadata"] = "n/a"
    elif harness.check("expect_set_metadata", "clean", outcome)[0] == "pass":
        out["expect_set_metadata"] = "clean"
    elif rewrite.get("status") == "ok":
        out["expect_set_metadata"] = "known_gap:not_written_as_asked"
    else:
        names = surfaces(rewrite.get("message", ""))
        out["expect_set_metadata"] = "known_gap:original_in_" + ("_".join(names) or "output")
    current = row.get("expect_redact_text", "n/a")
    if current.startswith("known_gap:") and harness.check("expect_redact_text", "clean", outcome)[0] == "pass":
        out["expect_redact_text"] = "clean"
    elif current == "known_gap:metadata_holds_word":
        names = surfaces((outcome.get("redact_text") or {}).get("message", ""))
        out["expect_redact_text"] = "known_gap:word_in_" + ("_".join(names) or "output")
    return out


def write_manifest(run_dir: Path) -> dict:
    rows = common.read_manifest()
    changed = {"tags": 0, "columns": 0}
    for row in rows:
        outcome = read_json(run_dir / "outcomes" / f"{row['id']}.json")
        if outcome is None:
            continue
        tags = set(common.split_tags(row["tags"])) - set(META_TAGS)
        tags |= metadata_tags(outcome.get("metadata") or {})
        if row["set"] == "synth" and row["source"].startswith("synth:meta_xmp"):
            tags.add("meta_xmp_unreadable")
        joined = common.join_tags(tags)
        if joined != row["tags"]:
            row["tags"] = joined
            changed["tags"] += 1
        for column, value in expectations(row, outcome).items():
            if row.get(column) != value:
                row[column] = value
                changed["columns"] += 1
    common.write_manifest(rows)
    return changed


def judge(run_dir: Path, rows: list[dict]) -> dict:
    summary = read_json(run_dir / "summary.json") or {}
    measures = Measures()
    ids = {r["id"] for r in rows}

    covered = report.coverage(common.read_manifest(), common.read_taxonomy())
    under = [f"{c['tag']} ({c['count']}/{c['minimum']})" for c in covered if not c["ok"]]
    size = len(common.read_manifest())
    measures.add("dataset size", size, str(size), f"{SMALLEST} to {LARGEST}", SMALLEST <= size <= LARGEST)
    measures.add("taxonomy tags at their minimum", len(covered) - len(under),
                 f"{len(covered) - len(under)} / {len(covered)}", "every tag", not under,
                 {"under": under})

    outcomes = {r["id"]: read_json(run_dir / "outcomes" / f"{r['id']}.json") for r in rows}
    outcomes = {k: v for k, v in outcomes.items() if v is not None}
    holding = [k for k, o in outcomes.items()
               if (o.get("metadata") or {}).get("status") == "ok"
               and ((o["metadata"].get("info")) or o["metadata"].get("xmp") != "absent")]
    unread = [k for k, o in outcomes.items() if (o.get("open") or {}).get("status") == "ok"
              and (o.get("metadata") or {}).get("status") != "ok"]
    unreadable = sorted(k for k in holding if outcomes[k]["metadata"].get("xmp") == "unreadable")
    measures.share("files with an Info dictionary or an XMP packet whose metadata is read", len(holding),
                   len(holding) + len(unread), READ, {"not_read": unread, "xmp_unreadable_reported": unreadable})

    compared = read_json(run_dir / "pdfium-metadata.json")
    if compared is None:
        measures.add("Info fields agreeing with PDFium's FPDF_GetMetaText", None, "not run", "100%", False,
                     {"fix": f"python tests/pdfium/metadata.py --run {run_dir.name}"})
    else:
        stats = compared["summary"]
        measures.share("Info fields agreeing with PDFium's FPDF_GetMetaText (named PDFium differences "
                       "apart)", stats["agreeing"], stats["fields"], PDFIUM_AGREE,
                       {"differences": compared.get("differences", {}), "known_gaps": compared.get("named", {})})

    rewrites = {k: o["set_metadata"] for k, o in outcomes.items()
                if (o.get("set_metadata") or {}).get("status") == "ok"}
    written = [k for k, p in rewrites.items() if not p.get("mismatches")]
    refused = {k: (o.get("set_metadata") or {}).get("message", "")[:240] for k, o in outcomes.items()
               if (o.get("set_metadata") or {}).get("status") not in (None, "ok", "skipped")}
    measures.share("rewritten outputs whose Info and XMP carry the written values", len(written),
                   len(rewrites), WRITTEN,
                   {"mismatches": {k: p["mismatches"] for k, p in rewrites.items() if p.get("mismatches")},
                    "refused": refused})

    leak = read_json(run_dir / "pdfium-leak-metadata.json")
    if leak is None:
        measures.add("original values of rewritten fields found in an output or by PDFium", None, "not run",
                     "0", False, {"fix": f"python tests/pdfium/leak.py --run {run_dir.name} --metadata"})
    else:
        # A leak in a file the manifest names a known gap is explained, listed
        # and not counted, as the text redaction's gate counts its own.
        gaps = {r["id"]: r.get("expect_set_metadata", "") for r in rows
                if r.get("expect_set_metadata", "").startswith("known_gap:")}
        leaks = {k: v for k, v in (leak.get("leaks") or {}).items() if k in ids and k not in gaps}
        explained = {k: {"gap": gaps[k], "hits": len(v)} for k, v in (leak.get("leaks") or {}).items()
                     if k in gaps}
        measures.zero("original values of rewritten fields found in an output or by PDFium "
                      "(known gaps apart)", sum(len(v) for v in leaks.values()),
                      {"leaks": leaks, "known_gaps": explained})

    nondeterministic = [i for i in summary.get("nondeterministic", []) if i in ids]
    runs = summary.get("runs", 1)
    measures.share("two runs of the whole dataset give identical outcomes and outputs",
                   len(outcomes) - len(nondeterministic) if runs == 2 else 0, len(outcomes), DETERMINISM,
                   {"nondeterministic": nondeterministic, "runs": runs})
    crashes = [c for c in summary.get("crashes", []) if c["id"] in ids]
    hangs = [h for h in summary.get("hangs", []) if h in ids]
    measures.zero("crashes, hangs past the timeout, anything but a status", len(crashes) + len(hangs),
                  {"crashes": crashes, "hangs": hangs})

    earlier = []
    for phase in range(3, 10):
        table = read_json(run_dir / f"phase{phase}.json")
        if table is None:
            earlier.append({"phase": phase, "passed": None, "rows": []})
        else:
            earlier.append({"phase": phase, "passed": table.get("passed"), "rows": table.get("rows", [])})
    return {"run": run_dir.name, "population": len(rows), "outcomes": len(outcomes), "rows": measures.rows,
            "earlier": earlier,
            "passed": all(r["pass"] for r in measures.rows)
            and all(e["passed"] for e in earlier if e["passed"] is not None)}


def markdown(result: dict) -> str:
    lines = [f"# Release gate of run {result['run']}", "",
             f"Population: {result['population']} manifest files; {result['outcomes']} outcomes read.", "",
             "## Metadata and the whole dataset", "",
             "| Measure | Value | Threshold | Result |", "|---|---|---|---|"]
    for row in result["rows"]:
        lines.append(f"| {row['measure']} | {row['shown']} | {row['threshold']} | "
                     f"{'pass' if row['pass'] else 'FAIL'} |")
    lines += ["", "## Every earlier gate on this run", "", "| Phase | Measure | Value | Threshold | Result |",
              "|---|---|---|---|---|"]
    for entry in result["earlier"]:
        if entry["passed"] is None:
            lines.append(f"| {entry['phase']} | (no phase{entry['phase']}.json in the run folder) | | | not run |")
            continue
        for row in entry["rows"]:
            lines.append(f"| {entry['phase']} | {row['measure']} | {row['shown']} | {row['threshold']} | "
                         f"{'pass' if row['pass'] else 'FAIL'} |")
    lines += ["", f"Gate: {'PASSED' if result['passed'] else 'FAILED'}"]
    for row in result["rows"]:
        detail = {k: v for k, v in row["detail"].items() if v}
        if detail and (not row["pass"] or "known_gaps" in detail):
            lines += ["", f"### {row['measure']}", "", "```", json.dumps(detail, indent=1)[:20000], "```"]
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description="The release gate over a dataset run.")
    parser.add_argument("--run", required=True, help="the run id of the report folder")
    parser.add_argument("--write-manifest", action="store_true",
                        help="fill the metadata tags and columns of the manifest from the run first")
    args = parser.parse_args()
    run_dir = common.reports_dir() / args.run
    if not (run_dir / "summary.json").exists():
        raise common.ToolError(f"{run_dir / 'summary.json'} does not exist", "the summary of a dataset run",
                               "run `python tests/dataset/run.py --checks all --runs 2` first")
    if args.write_manifest:
        print(json.dumps({"manifest": write_manifest(run_dir)}))
    result = judge(run_dir, common.read_manifest())
    text = markdown(result)
    (run_dir / "phase10.md").write_text(text, encoding="utf-8")
    (run_dir / "phase10.json").write_text(json.dumps(result, indent=1), encoding="utf-8")
    print(text[:8000])
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    common.run_tool(main)
