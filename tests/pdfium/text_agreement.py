# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# The reading gate: pdfkit's text of every dataset file against PDFium's, with
# the word-bag F1 of CLAUDE.md section 7.5 (words.py). pdfkit's text is the
# `text.text` of a dataset run's outcomes (every page, hidden blocks included,
# because PDFium reads them too, a duplicate copy left out, because PDFium folds
# it); PDFium's is its pages joined. Every file both readers open is scored;
# a file below 0.90 is named with its cause class from text_causes.tsv
# (`pdfkit`, `pdfium` or `legitimate`, with one line of evidence) or as
# `unclassified`. The report goes into the run folder: agreement.tsv (one row
# per file), agreement.json and summary.md (median, share below 0.90, per-tag
# medians, the re-serialisation identity, the determinism of the run, the
# hidden-text check). `--hidden` runs the hidden-text check of the gate:
# on every synthesised file of a hidden class, every planted marker word is
# extracted in a hidden block and flagged with that class. `--write-manifest`
# fills the manifest's expect_text (agree, known_gap:<cause>, n/a) and
# expect_fonts (loads, unreadable:<count>, n/a) from the run.

from __future__ import annotations

import argparse
import collections
import concurrent.futures
import hashlib
import json
import statistics
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(REPO / "dataset" / "tools"))
sys.path.insert(0, str(REPO / "tests" / "dataset"))

import common  # noqa: E402
import pdfium  # noqa: E402
import words  # noqa: E402

AGREE = 0.90
MEDIAN = 0.97
SHARE_BELOW = 0.05
CAUSES = HERE / "text_causes.tsv"
HIDDEN_CLASSES = ("render_invisible", "white_fill", "clipped", "off_media", "off_crop",
                  "optional_content_off", "duplicate", "covered")
MARKER = "HIDDEN MARKER"


def read_causes() -> dict[str, tuple[str, str]]:
    """id -> (cause class, evidence) from text_causes.tsv."""
    causes = {}
    if CAUSES.exists():
        for line in CAUSES.read_text(encoding="utf-8").splitlines()[1:]:
            if not line.strip() or line.startswith("#"):
                continue
            parts = line.split("\t")
            if len(parts) >= 3:
                causes[parts[0]] = (parts[1], parts[2])
    return causes


def pdfium_text(pool, path: Path, cache: Path) -> dict:
    """PDFium's probe of a file, cached by the file's SHA-256."""
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    cached = cache / f"{digest}.json"
    if cached.exists():
        return json.loads(cached.read_text(encoding="utf-8"))
    probe = pool.probe(path, ["text"])
    if probe.get("error") not in ("timeout", "crash"):
        cached.write_text(json.dumps(probe), encoding="utf-8")
    return probe


def score_run(run_dir: Path, rows: list[dict], workers: int) -> list[dict]:
    outcomes = run_dir / "outcomes"
    if not outcomes.exists():
        raise common.ToolError(f"{outcomes} does not exist", "a dataset run's outcomes",
                               "run `python tests/dataset/run.py` first and pass its run id")
    cache = common.cache_dir() / "pdfium-text"
    cache.mkdir(parents=True, exist_ok=True)
    results = []
    with pdfium.Pool(workers) as pool, concurrent.futures.ThreadPoolExecutor(workers) as threads:
        jobs = {}
        for row in rows:
            path = outcomes / f"{row['id']}.json"
            if not path.exists():
                continue
            outcome = json.loads(path.read_text(encoding="utf-8"))
            jobs[row["id"]] = (row, outcome, threads.submit(pdfium_text, pool, common.file_path(row), cache))
        for row_id, (row, outcome, job) in jobs.items():
            text = outcome.get("text") or {}
            probe = job.result()
            entry = {"id": row_id, "set": row["set"], "pages": row["pages"], "tags": row["tags"],
                     "pdfkit": text.get("status"), "pdfium": "ok" if probe.get("ok") else probe.get("error"),
                     "f1": None, "words_pdfkit": 0, "words_pdfium": 0}
            entry["rungs"] = text.get("rungs") or {}
            entry["hidden"] = text.get("hidden_classes") or {}
            entry["warnings"] = text.get("warnings") or []
            entry["reserialisation"] = text.get("reserialisation") or {}
            entry["unreadable"] = sum(p.get("unreadable", 0) for p in text.get("pages", []))
            if text.get("status") == "ok" and probe.get("ok"):
                mine = text.get("text", "")
                theirs = "\n".join(t or "" for t in probe.get("text", []))
                entry["f1"] = round(words.f1(mine, theirs), 6)
                entry["words_pdfkit"] = sum(words.words(mine).values())
                entry["words_pdfium"] = sum(words.words(theirs).values())
            results.append(entry)
    return sorted(results, key=lambda e: e["id"])


def hidden_check(rows: list[dict], command: list[str]) -> dict:
    """Every marker word of each hidden-class specimen, extracted and flagged with its class."""
    checked, failures = 0, []
    for row in rows:
        tags = set(common.split_tags(row["tags"]))
        classes = [c for c in HIDDEN_CLASSES if c in tags]
        if row["set"] != "synth" or not classes:
            continue
        proc = subprocess.run(command + ["text", str(common.file_path(row))], capture_output=True)
        if proc.returncode != 0:
            failures.append({"id": row["id"], "reason": f"exit {proc.returncode}"})
            continue
        result = json.loads(proc.stdout)
        visible_words, hidden_words = [], []
        for page in result["pages"]:
            for block in page["blocks"]:
                for line in block["lines"]:
                    visible_words += [(w["text"], set()) for w in line["words"]]
            for block in page["hidden_blocks"]:
                for line in block["lines"]:
                    hidden_words += [(w["text"], set(w["hidden"])) for w in line["words"]]
        planted = MARKER in " ".join(w for w, _ in visible_words + hidden_words)
        if not planted:
            continue
        checked += 1
        for cls in classes:
            flagged = " ".join(w for w, c in hidden_words if cls in c)
            missing = [m for m in ("HIDDEN", "MARKER") if m not in flagged.split()]
            if missing:
                failures.append({"id": row["id"], "class": cls, "reason": "marker words not flagged: "
                                 + ", ".join(missing)})
    return {"files": checked, "failures": failures}


def summarise(results: list[dict], run_dir: Path, causes: dict, hidden: dict | None) -> dict:
    scored = [r for r in results if r["f1"] is not None]
    below = [r for r in scored if r["f1"] < AGREE]
    per_tag = collections.defaultdict(list)
    for r in scored:
        for tag in common.split_tags(r["tags"]):
            per_tag[tag].append(r["f1"])
    reser = collections.Counter()
    mismatched = []
    for r in results:
        for key in ("streams", "identical", "damaged", "failed"):
            reser[key] += r["reserialisation"].get(key, 0)
        if r["reserialisation"].get("failed"):
            mismatched.append({"id": r["id"], "mismatches": r["reserialisation"].get("mismatches", [])[:3]})
    determinism = None
    run_summary = run_dir / "summary.json"
    if run_summary.exists():
        data = json.loads(run_summary.read_text(encoding="utf-8"))
        determinism = {"runs": data.get("runs"), "nondeterministic": data.get("nondeterministic", []),
                       "hangs": data.get("hangs", []), "crashes": data.get("crashes", [])}
    undamaged = reser["streams"] - reser["damaged"]
    summary = {
        "files_scored": len(scored),
        "median": round(statistics.median([r["f1"] for r in scored]), 4) if scored else None,
        "below_0_90": len(below),
        "share_below_0_90": round(len(below) / len(scored), 4) if scored else None,
        "below": [{"id": r["id"], "f1": r["f1"], "cause": causes.get(r["id"], ("unclassified", ""))[0],
                   "evidence": causes.get(r["id"], ("", ""))[1]} for r in sorted(below, key=lambda r: r["f1"])],
        "per_tag_median": {t: round(statistics.median(v), 4) for t, v in sorted(per_tag.items())},
        "reserialisation": dict(reser) | {"undamaged_identical_share":
                                          round((undamaged - reser["failed"]) / undamaged, 6) if undamaged else None},
        "reserialisation_failures": mismatched[:50],
        "determinism": determinism,
        "hidden": hidden,
        "pdfium_failed": sorted(r["id"] for r in results if r["pdfium"] != "ok" and r["pdfkit"] == "ok"),
        "pdfkit_failed": sorted(r["id"] for r in results if r["pdfkit"] not in ("ok", "skipped")),
    }
    passed = (summary["median"] is not None and summary["median"] >= MEDIAN
              and summary["share_below_0_90"] <= SHARE_BELOW
              and reser["failed"] == 0
              and (determinism is None or not determinism["nondeterministic"])
              and (hidden is None or not hidden["failures"]))
    summary["gate"] = "passed" if passed else "failed"
    return summary


def write_report(results: list[dict], summary: dict, run_dir: Path, causes: dict) -> None:
    lines = ["set\tid\tpages\tf1\twords_pdfkit\twords_pdfium\tunreadable\trungs\thidden\twarnings\tcause"]
    for r in results:
        rungs = ",".join(f"{k}:{v}" for k, v in r["rungs"].items() if v)
        hidden = ",".join(f"{k}:{v}" for k, v in r["hidden"].items() if v)
        cause = causes.get(r["id"], ("", ""))[0] if r["f1"] is not None and r["f1"] < AGREE else ""
        f1 = "" if r["f1"] is None else f"{r['f1']:.4f}"
        lines.append(f"{r['set']}\t{r['id']}\t{r['pages']}\t{f1}\t{r['words_pdfkit']}\t{r['words_pdfium']}"
                     f"\t{r['unreadable']}\t{rungs}\t{hidden}\t{','.join(r['warnings'])}\t{cause}")
    (run_dir / "agreement.tsv").write_text("\n".join(lines) + "\n", encoding="utf-8")
    (run_dir / "agreement.json").write_text(json.dumps(summary, indent=1), encoding="utf-8")
    md = [f"# Reading gate, run {run_dir.name}", "",
          f"- files scored (both readers open): {summary['files_scored']}",
          f"- median F1: {summary['median']} (gate {MEDIAN})",
          f"- below 0.90: {summary['below_0_90']} ({summary['share_below_0_90']}; gate {SHARE_BELOW})",
          f"- re-serialisation: {summary['reserialisation']}",
          f"- determinism: {summary['determinism']}",
          f"- hidden text: {summary['hidden']}",
          f"- gate: **{summary['gate']}**", "", "## Files below 0.90", "",
          "| id | F1 | cause | evidence |", "|---|---|---|---|"]
    md += [f"| {b['id']} | {b['f1']:.4f} | {b['cause']} | {b['evidence']} |" for b in summary["below"]]
    md += ["", "## Median per tag", "", "| tag | median |", "|---|---|"]
    md += [f"| {t} | {m} |" for t, m in summary["per_tag_median"].items()]
    (run_dir / "summary-text.md").write_text("\n".join(md) + "\n", encoding="utf-8")


def write_manifest(rows: list[dict], results: list[dict], causes: dict) -> int:
    by_id = {r["id"]: r for r in results}
    changed = 0
    for row in rows:
        r = by_id.get(row["id"])
        if r is None:
            continue
        if r["pdfkit"] != "ok" or r["f1"] is None:
            text = "n/a"
        elif r["f1"] >= AGREE:
            text = "agree"
        else:
            text = "known_gap:" + causes.get(row["id"], ("unclassified", ""))[0]
        codes = sum(r["rungs"].values())
        if r["pdfkit"] != "ok" or codes == 0:
            fonts = "n/a"
        else:
            fonts = "loads" if r["unreadable"] == 0 else f"unreadable:{r['unreadable']}"
        if row["expect_text"] != text or row["expect_fonts"] != fonts:
            changed += 1
        row["expect_text"], row["expect_fonts"] = text, fonts
    common.write_manifest(rows)
    return changed


def main() -> int:
    parser = argparse.ArgumentParser(description="The reading gate: pdfkit's text against PDFium's.")
    parser.add_argument("--run", required=True, help="the dataset run id whose outcomes are scored")
    parser.add_argument("--hidden", action="store_true", help="also run the hidden-text check")
    parser.add_argument("--pdfkit", help="the pdfkit executable for --hidden")
    parser.add_argument("--write-manifest", action="store_true", help="fill expect_text and expect_fonts")
    parser.add_argument("--set", action="append", help="only these sets")
    parser.add_argument("--workers", type=int, default=8)
    args = parser.parse_args()
    if not pdfium.default_dll().exists():
        print("SKIPPED reading gate: PDFium is absent; run `python tests/pdfium/fetch.py`")
        return 0
    rows = common.read_manifest()
    selected = [r for r in rows if not args.set or r["set"] in args.set]
    run_dir = common.reports_dir() / args.run
    causes = read_causes()
    results = score_run(run_dir, selected, args.workers)
    hidden = None
    if args.hidden:
        import run as dataset_run  # noqa: PLC0415

        command = dataset_run.find_pdfkit(args.pdfkit)
        if command is None:
            raise common.ToolError("no pdfkit executable", "a built pdfkit", "build it or pass --pdfkit")
        hidden = hidden_check(selected, command)
    summary = summarise(results, run_dir, causes, hidden)
    write_report(results, summary, run_dir, causes)
    if args.write_manifest:
        print(f"manifest rows changed: {write_manifest(rows, results, causes)}")
    print(json.dumps({k: summary[k] for k in ("files_scored", "median", "below_0_90", "share_below_0_90",
                                             "reserialisation", "gate")}, indent=1))
    return 0 if summary["gate"] == "passed" else 1


if __name__ == "__main__":
    common.run_tool(main)
