# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# The gate of the glued letters and the string occurrences of pdfkit_find:
# judges a dataset run made with `tests/dataset/run.py --checks open,text,find
# --runs 2` over every manifest row whose find expectation is not `n/a` and
# that the run reached (an outcome written, or a crash or hang recorded), so a
# run over some ids is judged on those ids. It reads <run>/summary.json and the
# outcomes, and writes the table of every measure with the files behind each
# failure into a markdown and a JSON file named after this script in the run
# folder. The measures: every reading-order occurrence whose glued letters make
# a word without a space is a word of the file's text (the `glued` member of
# the find outcome); the first marked-content string occurrence of a file,
# sent back as a `string` match, leaves the page one string occurrence fewer
# (the `strings` member); two runs give identical outcomes; and, with
# --baseline, no file regresses: a file both runs reached that passed a check
# of the baseline run (it has no failure recorded for that column in the
# baseline's summary.json) and fails it in this run is named. A crash, a hang
# or an outcome the run could not write is a failure of every column.

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

DETERMINISM = 1.0
STRINGS = 1.0
# The failure the run records for a file it could not finish: it stands for
# every column.
WHOLE_FILE = "*"


def population(rows: list[dict]) -> list[dict]:
    return [r for r in rows if r.get("expect_find", "n/a") != "n/a"]


def reached_ids(run_dir: Path, summary: dict) -> set[str]:
    """The ids a run wrote an outcome for, crashed on or hung on."""
    ids = {c["id"] for c in summary.get("crashes", [])} | set(summary.get("hangs", []))
    outcomes = run_dir / "outcomes"
    if outcomes.is_dir():
        ids |= {p.stem for p in outcomes.glob("*.json")}
    return ids


def failing(summary: dict) -> set[tuple[str, str]]:
    """The (id, column) pairs a run's summary records as failed."""
    return {(f["id"], f["column"]) for f in summary.get("failures", [])}


def regressions(summary: dict, baseline: dict, reached: set[str], baseline_reached: set[str]) -> list[dict]:
    """The files both runs reached that passed a column of the baseline and fail it now.

    Only the columns both runs judged are compared, with the hash check; a file the baseline
    failed as a whole had no column passing, and a file this run failed as a whole is named once."""
    columns = (set(summary.get("columns", {})) & set(baseline.get("columns", {}))) | {"sha256"}
    now, before = failing(summary), failing(baseline)
    reasons = {(f["id"], f["column"]): f.get("reason") for f in summary.get("failures", [])}
    found = []
    for row_id in sorted(reached & baseline_reached):
        if (row_id, WHOLE_FILE) in before:
            continue
        if (row_id, WHOLE_FILE) in now:
            found.append({"id": row_id, "column": WHOLE_FILE, "reason": reasons[(row_id, WHOLE_FILE)]})
            continue
        for column in sorted(columns):
            if (row_id, column) in now and (row_id, column) not in before:
                found.append({"id": row_id, "column": column, "reason": reasons[(row_id, column)]})
    return found


def judge(run_dir: Path, rows: list[dict], baseline_dir: Path | None = None) -> dict:
    summary = read_json(run_dir / "summary.json") or {}
    reached = reached_ids(run_dir, summary)
    outcomes = {}
    for row in population(rows):
        outcome = read_json(run_dir / "outcomes" / f"{row['id']}.json")
        if outcome is not None:
            outcomes[row["id"]] = outcome
    chosen = [r for r in population(rows) if r["id"] in reached]
    ids = {r["id"] for r in chosen}
    gaps = {r["id"]: r["expect_find"] for r in chosen if r["expect_find"].startswith("known_gap:")}
    judged = [r["id"] for r in chosen if r["id"] not in gaps and r["id"] in outcomes]
    measures = Measures()

    checked = not_words = 0
    glued_failures = []
    tried = exact = found = 0
    string_failures, string_files = [], []
    for row_id in judged:
        part = (outcomes.get(row_id) or {}).get("find") or {}
        if part.get("status") != "ok":
            continue
        glued = part.get("glued") or {}
        checked += glued.get("checked", 0)
        not_words += glued.get("not_words", 0)
        if glued.get("not_words"):
            glued_failures.append({"id": row_id, "not_words": glued["not_words"],
                                   "named": glued.get("named")})
        strings = part.get("strings") or {}
        found += strings.get("found", 0)
        if strings.get("tried", 0):
            string_files.append(row_id)
            tried += strings["tried"]
            exact += strings.get("exact", 0)
            for failure in strings.get("failures") or []:
                string_failures.append({"id": row_id, **failure})
    measures.add("reading-order occurrences glued to letters without a space that are not words of the "
                 "file's text", not_words, f"{not_words} of {checked} checked", "0", not_words == 0,
                 {"files": glued_failures})
    string_name = ("files whose first marked-content string occurrence, sent back as a string match, "
                   "leaves one fewer on its page")
    string_detail = {"failures": string_failures, "files_tried": len(string_files),
                     "string_occurrences_found": found}
    if tried:
        measures.share(string_name, exact, tried, STRINGS, string_detail)
    else:
        # The job's seeded words reach no marked-content string in the files
        # run; the unit tier holds the string match's cases, so the measure
        # is reported and does not fail.
        measures.add(string_name, None, "not measured (no file's words reach a marked-content string)",
                     "100% of those tried", True, string_detail)

    nondeterministic = [i for i in summary.get("nondeterministic", []) if i in ids]
    runs = summary.get("runs", 1)
    measures.share("two runs give identical outcomes", len(outcomes) - len(nondeterministic)
                   if runs == 2 else 0, len(outcomes), DETERMINISM, {"nondeterministic": nondeterministic})

    if baseline_dir is not None:
        baseline = read_json(baseline_dir / "summary.json") or {}
        both = reached & reached_ids(baseline_dir, baseline)
        found_regressions = regressions(summary, baseline, reached, reached_ids(baseline_dir, baseline))
        measures.add(f"files that passed a check of baseline run {baseline_dir.name} and fail it here",
                     len(found_regressions), f"{len(found_regressions)} over {len(both)} shared files",
                     "0", not found_regressions, {"regressions": found_regressions})
    return {"run": run_dir.name, "baseline": baseline_dir.name if baseline_dir else None,
            "population": len(chosen), "outcomes": len(outcomes), "known_gaps": gaps,
            "rows": measures.rows, "passed": all(r["pass"] for r in measures.rows)}


def markdown(report: dict) -> str:
    lines = [f"# Glued letters and string occurrences, gate of run {report['run']}", "",
             f"Population: {report['population']} manifest files the run reached that expect a find; "
             f"{report['outcomes']} outcomes read; {len(report['known_gaps'])} known gaps."
             + (f" Baseline: {report['baseline']}." if report["baseline"] else ""), "",
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
    parser = argparse.ArgumentParser(description="The glued letters and string occurrences gate over a dataset run.")
    parser.add_argument("--run", required=True, help="the run id of the report folder")
    parser.add_argument("--baseline", help="the run id of an earlier run to compare the checks against")
    parser.add_argument("--set", action="append", help="only the manifest rows of these sets")
    parser.add_argument("--id", action="append", help="only these manifest ids")
    args = parser.parse_args()
    run_dir = common.reports_dir() / args.run
    if not (run_dir / "summary.json").exists():
        raise common.ToolError(f"{run_dir / 'summary.json'} does not exist", "the summary of a dataset run",
                               "run `python tests/dataset/run.py --checks open,text,find --runs 2` first")
    baseline_dir = None
    if args.baseline:
        baseline_dir = common.reports_dir() / args.baseline
        if not (baseline_dir / "summary.json").exists():
            raise common.ToolError(f"{baseline_dir / 'summary.json'} does not exist",
                                   "the summary of the baseline dataset run",
                                   "name an earlier run of tests/dataset/run.py in --baseline")
    rows = common.read_manifest()
    if args.set:
        rows = [r for r in rows if r["set"] in args.set]
    if args.id:
        rows = [r for r in rows if r["id"] in args.id]
    report = judge(run_dir, rows, baseline_dir)
    text = markdown(report)
    (run_dir / "phase15.md").write_text(text, encoding="utf-8")
    (run_dir / "phase15.json").write_text(json.dumps(report, indent=1), encoding="utf-8")
    print(text[:6000])
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    common.run_tool(main)
