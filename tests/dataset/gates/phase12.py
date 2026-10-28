# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# The gate of the find function: judges a dataset run made with
# `tests/dataset/run.py --checks open,text,find --runs 2` over every manifest
# row whose find expectation is not `n/a` and that the run reached (an
# outcome written, or a crash or hang recorded), so a run over some ids is
# judged on those ids. It reads <run>/summary.json, the outcomes and the
# timings the verb writes beside them (<run>/outputs/<id>.find-time.json), and
# writes the table of every measure with the files behind each failure into
# a markdown and a JSON file named after this script in the run folder. A
# row the manifest names a known gap is counted apart and named. The
# feedback measure needs the array
# form of a located match in pdfkit_redact_text: when no file could send its
# occurrence back, the measure is reported as not run and fails.

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

AGREEMENT = 1.0
FEEDBACK = 1.0
DETERMINISM = 1.0
TIME_RATIO = 1.5


def population(rows: list[dict]) -> list[dict]:
    return [r for r in rows if r.get("expect_find", "n/a") != "n/a"]


def judge(run_dir: Path, rows: list[dict]) -> dict:
    summary = read_json(run_dir / "summary.json") or {}
    crashes = [c for c in summary.get("crashes", [])]
    hangs = list(summary.get("hangs", []))
    reached = {c["id"] for c in crashes} | set(hangs)
    outcomes = {}
    for row in population(rows):
        outcome = read_json(run_dir / "outcomes" / f"{row['id']}.json")
        if outcome is not None:
            outcomes[row["id"]] = outcome
            reached.add(row["id"])
    chosen = [r for r in population(rows) if r["id"] in reached]
    ids = {r["id"] for r in chosen}
    gaps = {r["id"]: r["expect_find"] for r in chosen if r["expect_find"].startswith("known_gap:")}
    # A file the verb did not finish (a crash or a hang) is counted by the
    # crash and hang measure alone.
    judged = [r["id"] for r in chosen if r["id"] not in gaps and r["id"] in outcomes]
    measures = Measures()

    agree, disagree, failed = [], [], []
    tried = exact = 0
    unsupported, feedback_failures = [], []
    for row_id in judged:
        part = (outcomes.get(row_id) or {}).get("find") or {}
        if part.get("status") != "ok":
            failed.append({"id": row_id, "status": part.get("status"), "code": part.get("code"),
                           "message": (part.get("message") or "")[:300]})
            continue
        if part.get("agree") is True:
            agree.append(row_id)
        else:
            disagree.append({"id": row_id, "disagreements": part.get("disagreements")})
        feedback = part.get("feedback") or {}
        if feedback.get("status") == "unsupported":
            unsupported.append(row_id)
            continue
        tried += feedback.get("tried", 0)
        exact += feedback.get("exact", 0)
        for failure in feedback.get("failures") or []:
            feedback_failures.append({"id": row_id, **failure})
    measures.share("files where find's occurrences equal pdfkit_redact_text's for the job's words "
                   "(same ranges)", len(agree), len(judged), AGREEMENT,
                   {"disagree": disagree, "failed": failed, "known_gaps": gaps})
    if unsupported and tried == 0:
        measures.add("occurrences fed back as located matches that remove exactly one occurrence",
                     None, "not run", "100%", False,
                     {"unsupported": unsupported[:20],
                      "fix": "pdfkit_redact_text lacks the array form of a located match; "
                             "rebase on main once build-11 has merged and run again"})
    else:
        measures.share("occurrences fed back as located matches that remove exactly one occurrence",
                       exact, tried, FEEDBACK,
                       {"failures": feedback_failures, "unsupported": unsupported})

    nondeterministic = [i for i in summary.get("nondeterministic", []) if i in ids]
    runs = summary.get("runs", 1)
    measures.share("two runs give identical outcomes", len(outcomes) - len(nondeterministic)
                   if runs == 2 else 0, len(outcomes), DETERMINISM, {"nondeterministic": nondeterministic})
    crashed = [c for c in crashes if c["id"] in ids]
    hung = [h for h in hangs if h in ids]
    measures.zero("crashes, hangs, anything but a status", len(crashed) + len(hung) + len(failed),
                  {"crashes": crashed, "hangs": hung, "find_failed": failed})

    text_seconds = find_seconds = 0.0
    timed = 0
    for row_id in sorted(ids):
        timing = read_json(run_dir / "outputs" / f"{row_id}.find-time.json")
        if timing:
            text_seconds += timing.get("text", 0.0)
            find_seconds += timing.get("find", 0.0)
            timed += 1
    ratio = find_seconds / text_seconds if text_seconds > 0 else None
    measures.add("time of the find member against the text member, whole run", ratio,
                 "not measured" if ratio is None else
                 f"{find_seconds:.1f} s / {text_seconds:.1f} s = {ratio:.2f} over {timed} files",
                 f"<= {TIME_RATIO}", ratio is not None and ratio <= TIME_RATIO)
    return {"run": run_dir.name, "population": len(chosen), "outcomes": len(outcomes),
            "known_gaps": gaps, "rows": measures.rows, "passed": all(r["pass"] for r in measures.rows)}


def markdown(report: dict) -> str:
    lines = [f"# Find gate of run {report['run']}", "",
             f"Population: {report['population']} manifest files the run reached that expect a find; "
             f"{report['outcomes']} outcomes read; {len(report['known_gaps'])} known gaps.", "",
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
    parser = argparse.ArgumentParser(description="The find gate over a dataset run.")
    parser.add_argument("--run", required=True, help="the run id of the report folder")
    parser.add_argument("--set", action="append", help="only the manifest rows of these sets")
    parser.add_argument("--id", action="append", help="only these manifest ids")
    args = parser.parse_args()
    run_dir = common.reports_dir() / args.run
    if not (run_dir / "summary.json").exists():
        raise common.ToolError(f"{run_dir / 'summary.json'} does not exist", "the summary of a dataset run",
                               "run `python tests/dataset/run.py --checks open,text,find --runs 2` first")
    rows = common.read_manifest()
    if args.set:
        rows = [r for r in rows if r["set"] in args.set]
    if args.id:
        rows = [r for r in rows if r["id"] in args.id]
    report = judge(run_dir, rows)
    text = markdown(report)
    (run_dir / "phase12.md").write_text(text, encoding="utf-8")
    (run_dir / "phase12.json").write_text(json.dumps(report, indent=1), encoding="utf-8")
    print(text[:6000])
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    common.run_tool(main)
