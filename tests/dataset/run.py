# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# The dataset tier: runs `pdfkit dataset` over every manifest file, each file in
# its own child process with a 30 s limit, twice, and checks every expectation
# column whose catalogue function is implemented, against the outcome contract
# of tests/dataset/outcome.md. A hang, a crash or two runs that differ are
# failures of their own, because a reader that hangs or answers differently on
# the same bytes cannot be trusted with a redaction. A function that answers
# PDFKIT_E_NOT_IMPLEMENTED is counted as skipped, never as a pass. When no pdfkit
# executable is found the tier skips by name and says how to build one.

from __future__ import annotations

import argparse
import collections
import concurrent.futures
import json
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "dataset" / "tools"))

import common  # noqa: E402

TIMEOUT = 30.0
COLUMNS = {"expect_open": "open", "expect_text": "text", "expect_images": "images",
           "expect_redact": "redact", "expect_metadata": "metadata"}


def find_pdfkit(explicit: str | None) -> list[str] | None:
    """The command that runs pdfkit: --pdfkit, $PDFKIT_EXE, a build under out/, or PATH."""
    candidates = []
    if explicit:
        candidates.append(Path(explicit))
    if os.environ.get("PDFKIT_EXE"):
        candidates.append(Path(os.environ["PDFKIT_EXE"]))
    out = REPO / "out"
    if out.exists():
        for preset in ("mingw-release", "mingw-debug"):
            candidates += sorted((out / preset).rglob("pdfkit.exe")) if (out / preset).exists() else []
            candidates += sorted((out / preset).rglob("pdfkit")) if (out / preset).exists() else []
    found = shutil.which("pdfkit")
    if found:
        candidates.append(Path(found))
    for candidate in candidates:
        if candidate.is_file():
            if candidate.suffix == ".py":
                return [sys.executable, "-I", str(candidate)]
            return [str(candidate)]
    return None


def run_one(command: list[str], row: dict, out_dir: Path) -> dict:
    """Runs the verb on one file; returns the outcome bytes or the failure."""
    path = common.file_path(row)
    args = command + ["dataset", "--file", str(path), "--id", row["id"], "--out", str(out_dir)]
    started = time.monotonic()
    try:
        proc = subprocess.run(args, capture_output=True, timeout=TIMEOUT)
    except subprocess.TimeoutExpired:
        return {"id": row["id"], "result": "hang", "seconds": TIMEOUT}
    seconds = round(time.monotonic() - started, 3)
    target = out_dir / "outcomes" / f"{row['id']}.json"
    if proc.returncode == 0 and target.exists():
        return {"id": row["id"], "result": "written", "bytes": target.read_bytes(), "seconds": seconds}
    stdout = proc.stdout.decode("utf-8", "replace")
    if proc.returncode == 4 and "NOT_IMPLEMENTED" in stdout.upper().replace(" ", "_"):
        return {"id": row["id"], "result": "verb_not_implemented", "seconds": seconds}
    if proc.returncode in (1, 2, 3, 4):
        return {"id": row["id"], "result": "error", "exit": proc.returncode,
                "message": stdout[-300:], "seconds": seconds}
    return {"id": row["id"], "result": "crash", "exit": proc.returncode,
            "message": proc.stderr.decode("utf-8", "replace")[-300:], "seconds": seconds}


def check(column: str, expected: str, outcome: dict) -> tuple[str, str]:
    """('pass' | 'fail' | 'skipped', reason) for one expectation against one outcome."""
    if expected == "n/a":
        return "skipped", "n/a"
    part = outcome.get(COLUMNS[column])
    if part is None:
        return "skipped", "function not run"
    status = part.get("status")
    if status == "not_implemented":
        return "skipped", "not implemented"
    if column == "expect_open":
        if expected.startswith("refused:"):
            code = expected.split(":", 1)[1]
            ok = status == "refused" and part.get("code") == code
            return ("pass" if ok else "fail"), f"expected refusal {code}, got {status} {part.get('code')}"
        repairs = part.get("repairs") or []
        if status != "ok":
            return "fail", f"expected {expected}, got {status} {part.get('code')}"
        if expected == "opens" and repairs:
            return "fail", "opened with repairs: " + "; ".join(repairs)[:120]
        if expected == "repaired" and not repairs:
            return "fail", "opened without any repair"
        return "pass", ""
    if status == "skipped":
        return "fail", "not run because open did not succeed"
    if status != "ok":
        return "fail", f"{status} {part.get('code')} {part.get('message', '')}"[:160]
    if column == "expect_images" and expected == "decoded" and part.get("undecodable"):
        return "fail", f"{len(part['undecodable'])} images undecodable"
    if column == "expect_redact" and part.get("stable") is not True:
        return "fail", "redaction not pixel-stable"
    if column == "expect_metadata" and part.get("agree") is not True:
        return "fail", "metadata disagrees"
    if column == "expect_text" and expected.startswith("known_gap:"):
        return "pass", "known_gap"
    return "pass", ""


def summarise(rows: list[dict], first: dict, second: dict, run_dir: Path, seconds: float) -> dict:
    columns = {c: collections.Counter() for c in COLUMNS}
    per_set: dict = collections.defaultdict(lambda: collections.Counter())
    failures, crashes, hangs, nondeterministic, verb_skipped = [], [], [], [], []
    for row in rows:
        a, b = first[row["id"]], second.get(row["id"])
        if a["result"] == "hang" or (b and b["result"] == "hang"):
            hangs.append(row["id"])
        if a["result"] in ("crash", "error") or (b and b["result"] in ("crash", "error")):
            crashes.append({"id": row["id"], "exit": a.get("exit"), "message": a.get("message", "")[:160]})
        if a["result"] == "verb_not_implemented":
            verb_skipped.append(row["id"])
            for column in COLUMNS:
                columns[column]["skipped"] += 1
            per_set[row["set"]]["skipped"] += 1
            continue
        if a["result"] != "written":
            for column in COLUMNS:
                columns[column]["fail"] += 1
            per_set[row["set"]]["fail"] += 1
            failures.append({"id": row["id"], "column": "*", "reason": a["result"]})
            continue
        if b and b["result"] == "written" and b["bytes"] != a["bytes"]:
            nondeterministic.append(row["id"])
        outcome = json.loads(a["bytes"])
        if outcome.get("sha256") != row["sha256"]:
            failures.append({"id": row["id"], "column": "sha256", "reason": "the outcome names other bytes"})
        for column in COLUMNS:
            result, reason = check(column, row[column], outcome)
            columns[column][result] += 1
            per_set[row["set"]][result] += 1
            if result == "fail":
                failures.append({"id": row["id"], "column": column, "reason": reason})
    return {
        "run": run_dir.name, "files": len(rows), "seconds": round(seconds, 1),
        "columns": {c: dict(v) for c, v in columns.items()},
        "per_set": {s: dict(v) for s, v in sorted(per_set.items())},
        "failures": failures, "crashes": crashes, "hangs": hangs,
        "nondeterministic": nondeterministic, "verb_not_implemented": len(verb_skipped),
    }


def write_markdown(summary: dict, path: Path) -> None:
    lines = [f"# Dataset run {summary['run']}", "",
             f"{summary['files']} files in {summary['seconds']} s; {len(summary['hangs'])} hangs, "
             f"{len(summary['crashes'])} crashes or errors, {len(summary['nondeterministic'])} non-deterministic.",
             "", "| Column | pass | fail | skipped |", "|---|---|---|---|"]
    for column, counts in summary["columns"].items():
        lines.append(f"| {column} | {counts.get('pass', 0)} | {counts.get('fail', 0)} | {counts.get('skipped', 0)} |")
    lines += ["", "| Set | pass | fail | skipped |", "|---|---|---|---|"]
    for name, counts in summary["per_set"].items():
        lines.append(f"| {name} | {counts.get('pass', 0)} | {counts.get('fail', 0)} | {counts.get('skipped', 0)} |")
    if summary["failures"]:
        lines += ["", "## Failures", ""]
        lines += [f"- {f['id']} {f['column']}: {f['reason']}" for f in summary["failures"][:500]]
    for key in ("hangs", "nondeterministic"):
        if summary[key]:
            lines += ["", f"## {key.capitalize()}", "", ", ".join(summary[key])]
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description="Run pdfkit dataset over the manifest and check expectations.")
    parser.add_argument("--pdfkit", help="the pdfkit executable (default: $PDFKIT_EXE, out/, PATH)")
    parser.add_argument("--set", action="append", help="only these sets")
    parser.add_argument("--id", action="append", help="only these ids")
    parser.add_argument("--workers", type=int, default=os.cpu_count() or 4)
    parser.add_argument("--once", action="store_true", help="skip the second, determinism run")
    parser.add_argument("--run-id", help="the report folder name (default: a fresh run id)")
    args = parser.parse_args()

    command = find_pdfkit(args.pdfkit)
    if command is None:
        print("SKIPPED dataset tier: no pdfkit executable found. Build it with "
              "`cmake --preset mingw-release && cmake --build --preset mingw-release`, "
              "or pass --pdfkit <path> or set PDFKIT_EXE.")
        return 0
    rows = common.read_manifest()
    if args.set:
        rows = [r for r in rows if r["set"] in args.set]
    if args.id:
        rows = [r for r in rows if r["id"] in args.id]
    missing = [r["id"] for r in rows if not common.file_path(r).exists()]
    if missing:
        print(f"SKIPPED {len(missing)} rows whose files are absent (first {missing[0]}); "
              "run `python dataset/tools/fetch.py`", file=sys.stderr)
        rows = [r for r in rows if r["id"] not in set(missing)]
    run_dir = common.reports_dir() / (args.run_id or common.run_id())
    first_dir, second_dir = run_dir, run_dir / "repeat"
    for folder in (first_dir / "outcomes", second_dir / "outcomes"):
        folder.mkdir(parents=True, exist_ok=True)
    started = time.monotonic()
    first, second = {}, {}
    with concurrent.futures.ThreadPoolExecutor(args.workers) as pool:
        jobs = {pool.submit(run_one, command, r, first_dir): r["id"] for r in rows}
        for job in concurrent.futures.as_completed(jobs):
            first[jobs[job]] = job.result()
        if not args.once:
            jobs = {pool.submit(run_one, command, r, second_dir): r["id"] for r in rows}
            for job in concurrent.futures.as_completed(jobs):
                second[jobs[job]] = job.result()
    seconds = time.monotonic() - started
    summary = summarise(rows, first, second, run_dir, seconds)
    summary["missing_files"] = missing
    if not args.once:
        shutil.rmtree(second_dir, ignore_errors=True)
    (run_dir / "summary.json").write_text(json.dumps(summary, indent=1), encoding="utf-8")
    write_markdown(summary, run_dir / "summary.md")
    print(json.dumps({k: summary[k] for k in ("run", "files", "seconds", "columns", "verb_not_implemented")}
                     | {"hangs": len(summary["hangs"]), "crashes": len(summary["crashes"]),
                        "nondeterministic": len(summary["nondeterministic"]),
                        "failures": len(summary["failures"])}, indent=1))
    bad = summary["hangs"] or summary["crashes"] or summary["nondeterministic"] or summary["failures"]
    return 1 if bad else 0


if __name__ == "__main__":
    common.run_tool(main)
