# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# The gate of text redaction: judges a dataset run made
# with `tests/dataset/run.py --checks open,redact_text --runs 2`, then
# `tests/pdfium/compare.py --check redact` and `tests/pdfium/leak.py`, over
# every manifest row whose open expectation is `opens` or `repaired` and whose
# redact_text expectation is not `n/a`. It reads <run>/summary.json, the
# outcomes, pdfium-redact.json and pdfium-leak.json, and writes the table of
# every measure (measure, value, threshold, pass or fail) with the files behind
# each failure into a markdown and a JSON file named after this script in the
# run folder. A row the manifest names a known gap is counted apart and named.
# A survivor is a leak, and one stops the line.

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO / "dataset" / "tools"))
sys.path.insert(0, str(Path(__file__).resolve().parent))

import common  # noqa: E402
from phase7 import Measures, read_json  # noqa: E402

SELF_CHECK = 1.0
PIXELS = 0.99
PLACEHOLDERS = 0.99
DETERMINISM = 1.0


def population(rows: list[dict]) -> list[dict]:
    return [r for r in rows if r["expect_open"] in ("opens", "repaired")
            and r.get("expect_redact_text", "n/a") != "n/a"]


def judge(run_dir: Path, rows: list[dict]) -> dict:
    summary = read_json(run_dir / "summary.json") or {}
    chosen = population(rows)
    ids = {r["id"] for r in chosen}
    gaps = {r["id"]: r["expect_redact_text"] for r in chosen if r["expect_redact_text"].startswith("known_gap:")}
    outcomes = {}
    for row in chosen:
        outcome = read_json(run_dir / "outcomes" / f"{row['id']}.json")
        if outcome is not None:
            outcomes[row["id"]] = outcome
    measures = Measures()

    judged = [r["id"] for r in chosen if r["id"] not in gaps]
    passed, refused = [], []
    for row_id in judged:
        part = (outcomes.get(row_id) or {}).get("redact_text") or {}
        if part.get("status") == "ok" and part.get("self_check") == "passed":
            passed.append(row_id)
        else:
            refused.append({"id": row_id, "status": part.get("status"), "code": part.get("code"),
                            "message": (part.get("message") or "")[:300]})
    measures.share("outputs that pass the self-check (known gaps apart)", len(passed), len(judged), SELF_CHECK,
                   {"refused": refused, "known_gaps": gaps})

    leak = read_json(run_dir / "pdfium-leak.json") or {}
    leaks = {k: v for k, v in (leak.get("leaks") or {}).items() if k in ids}
    text_hits = {k: [h for h in v if h["surface"] == "pdfium_text"] for k, v in leaks.items()}
    byte_hits = {k: [h for h in v if h["surface"].startswith("bytes")] for k, v in leaks.items()}
    measures.zero("redacted words found by PDFium in the text of an output",
                  sum(len(v) for v in text_hits.values()), {k: v for k, v in text_hits.items() if v})
    measures.zero("redacted words found in the decoded bytes of an output outside font programs",
                  sum(len(v) for v in byte_hits.values()), {k: v for k, v in byte_hits.items() if v})
    if not leak:
        measures.add("leak check run", None, "not run", "run", False,
                     {"fix": "python tests/pdfium/leak.py --run " + run_dir.name})

    redact = read_json(run_dir / "pdfium-redact.json") or {}
    files = {k: v for k, v in (redact.get("files") or {}).items() if k in ids}
    pages = sum(e["pages"] for e in files.values() if e["input_opens"])
    stable = sum(e["stable"] for e in files.values() if e["input_opens"])
    unstable = [{"file": k, **u} for k, e in sorted(files.items()) for u in e["unstable"]]
    measures.share("pages stable outside the reported rectangles (one-pixel dilation)", stable, pages, PIXELS,
                   {"unstable": unstable})
    drawn = sum(e["placeholders_drawn"] for e in files.values() if e["input_opens"])
    found = sum(e["placeholders_found"] for e in files.values() if e["input_opens"])
    short = [{"file": k, **p} for k, e in sorted(files.items()) for p in e["placeholder_pages"]]
    measures.share("placeholders found in PDFium's text of the output, of those drawn", found, drawn,
                   PLACEHOLDERS, {"short": short})

    nondeterministic = [i for i in summary.get("nondeterministic", []) if i in ids]
    runs = summary.get("runs", 1)
    measures.share("two runs give identical outcomes and outputs", len(outcomes) - len(nondeterministic)
                   if runs == 2 else 0, len(outcomes), DETERMINISM, {"nondeterministic": nondeterministic})
    crashes = [c for c in summary.get("crashes", []) if c["id"] in ids]
    hangs = [h for h in summary.get("hangs", []) if h in ids]
    measures.zero("crashes, hangs past 30 s, anything but a status", len(crashes) + len(hangs),
                  {"crashes": crashes, "hangs": hangs})
    return {"run": run_dir.name, "population": len(chosen), "outcomes": len(outcomes),
            "known_gaps": gaps, "rows": measures.rows, "passed": all(r["pass"] for r in measures.rows)}


def markdown(report: dict) -> str:
    lines = [f"# Text redaction gate of run {report['run']}", "",
             f"Population: {report['population']} manifest files that open or repair and expect a "
             f"redaction; {report['outcomes']} outcomes read; {len(report['known_gaps'])} known gaps.", "",
             "| Measure | Value | Threshold | Result |", "|---|---|---|---|"]
    for row in report["rows"]:
        lines.append(f"| {row['measure']} | {row['shown']} | {row['threshold']} | {'pass' if row['pass'] else 'FAIL'} |")
    lines += ["", f"Gate: {'PASSED' if report['passed'] else 'FAILED'}"]
    for row in report["rows"]:
        detail = {k: v for k, v in row["detail"].items() if v}
        if detail and not row["pass"]:
            lines += ["", f"## {row['measure']}", "", "```", json.dumps(detail, indent=1)[:20000], "```"]
    if report["known_gaps"]:
        lines += ["", "## Known gaps", ""] + [f"- {k}: {v}" for k, v in sorted(report["known_gaps"].items())]
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description="The text redaction gate over a dataset run.")
    parser.add_argument("--run", required=True, help="the run id of the report folder")
    parser.add_argument("--set", action="append", help="only the manifest rows of these sets")
    parser.add_argument("--id", action="append", help="only these manifest ids")
    args = parser.parse_args()
    run_dir = common.reports_dir() / args.run
    if not (run_dir / "summary.json").exists():
        raise common.ToolError(f"{run_dir / 'summary.json'} does not exist", "the summary of a dataset run",
                               "run `python tests/dataset/run.py --checks open,redact_text --runs 2` first")
    rows = common.read_manifest()
    if args.set:
        rows = [r for r in rows if r["set"] in args.set]
    if args.id:
        rows = [r for r in rows if r["id"] in args.id]
    report = judge(run_dir, rows)
    text = markdown(report)
    (run_dir / "phase8.md").write_text(text, encoding="utf-8")
    (run_dir / "phase8.json").write_text(json.dumps(report, indent=1), encoding="utf-8")
    print(text[:6000])
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    common.run_tool(main)
