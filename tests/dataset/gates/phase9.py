# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# The gate of image treatment: judges a dataset run made with
# `tests/dataset/run.py --checks open,redact_images --runs 2`, then
# `tests/pdfium/images.py`, `tests/pdfium/compare.py --check redact` and
# `tests/pdfium/leak.py --images`, over every manifest row whose open
# expectation is `opens` or `repaired` and whose redact_images expectation is
# not `n/a`. It reads <run>/summary.json, the outcomes, pdfium-images.json,
# pdfium-redact-images.json and pdfium-leak-images.json, measures PDFium's text
# of each output against its text of the input (an image treatment changes no
# text; the input's text of its switched-off optional-content layers, which
# every write removes, is left out), and writes the table of every measure (measure, value, threshold,
# pass or fail) with the files behind each failure into a markdown and a JSON
# file named after this script in the run folder. A treated rectangle that
# keeps an original sample, a removed or treated identity still drawn, or an
# original's encoded bytes anywhere in an output is a survival, and one stops
# the line. With --write-manifest it fills the manifest's expect_redact_images
# column from the run instead: `clean` for a file whose job treated an image,
# `n/a` for one whose job found none or that pdfkit refuses.

from __future__ import annotations

import argparse
import concurrent.futures
import json
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO / "dataset" / "tools"))
sys.path.insert(0, str(REPO / "tests" / "pdfium"))
sys.path.insert(0, str(Path(__file__).resolve().parent))

import common  # noqa: E402
import places  # noqa: E402
from phase7 import Measures, dataset_run, read_json  # noqa: E402

PIXELS = 0.99
REOPEN = 1.0
TEXT = 1.0
DETERMINISM = 1.0


def population(rows: list[dict]) -> list[dict]:
    return [r for r in rows if r["expect_open"] in ("opens", "repaired")
            and r.get("expect_redact_images", "n/a") != "n/a"]


def score(before: str, after: str, off_text) -> float:
    """PDFium's word-bag F1 of the output's text against the input's; when they differ, the
    input's text of its switched-off optional-content layers (off_text(), read only then) is left
    out, because every write removes it."""
    import words
    f1 = words.f1(before, after)
    if f1 == 1.0:
        return f1
    off = off_text()
    return places.f1_without_off(before, after, off) if off else f1


def text_agreement(run_dir: Path, jobs: list[tuple[dict, dict]], workers: int) -> dict:
    """Per file, PDFium's word-bag F1 of the output's text against the input's."""
    import pdfium
    import results_cache

    command = dataset_run.find_pdfkit(None)

    def one(pool, row, part):
        # The input's text comes from the results cache; the output's is new each run.
        before = results_cache.cached_probe(pool, common.file_path(row), ["text"])
        after = pool.probe(run_dir / part["output"], ["text"])
        if not before.get("ok"):
            return None
        if not after.get("ok"):
            return 0.0
        return round(score("\n".join(t or "" for t in before["text"]),
                           "\n".join(t or "" for t in after["text"]),
                           lambda: places.off_text_of(command, common.file_path(row))), 6)

    out = {}
    with pdfium.Pool(workers) as pool, concurrent.futures.ThreadPoolExecutor(workers) as threads:
        futures = {row["id"]: threads.submit(one, pool, row, part) for row, part in jobs}
        for row_id, future in futures.items():
            out[row_id] = future.result()
    return out


def judge(run_dir: Path, rows: list[dict], workers: int, with_text: bool) -> dict:
    summary = read_json(run_dir / "summary.json") or {}
    chosen = population(rows)
    ids = {r["id"] for r in chosen}
    gaps = {r["id"]: r["expect_redact_images"] for r in chosen
            if r["expect_redact_images"].startswith("known_gap:")}
    outcomes = {}
    for row in chosen:
        outcome = read_json(run_dir / "outcomes" / f"{row['id']}.json")
        if outcome is not None:
            outcomes[row["id"]] = outcome
    measures = Measures()
    judged = [r for r in chosen if r["id"] not in gaps]
    parts = {r["id"]: (outcomes.get(r["id"]) or {}).get("redact_images") or {} for r in judged}

    images = read_json(run_dir / "pdfium-images.json") or {}
    image_files = {k: v for k, v in (images.get("files") or {}).items() if k in ids}
    survivals = {k: v["survivals"] for k, v in image_files.items() if v["survivals"]}
    rectangles = sum(v["rectangles"] for v in image_files.values())
    measures.zero(f"treated rectangles whose pixels, decoded by PDFium, carry an original sample "
                  f"(of {rectangles} checked)", sum(len(v) for v in survivals.values()), survivals)
    if not images:
        measures.add("PDFium image check run", None, "not run", "run", False,
                     {"fix": "python tests/pdfium/images.py --run " + run_dir.name})
    left_pdfkit = {k: p["survivors"] for k, p in parts.items() if p.get("survivors")}
    left_pdfium = {k: v["left"] for k, v in image_files.items() if v["left"]}
    measures.zero("removed or treated identities still drawn in the output (pdfkit's listing, PDFium's "
                  "image objects)", sum(len(v) for v in left_pdfkit.values())
                  + sum(len(v) for v in left_pdfium.values()), {"pdfkit": left_pdfkit, "pdfium": left_pdfium})

    leak = read_json(run_dir / "pdfium-leak-images.json") or {}
    leak_files = {k: v for k, v in (leak.get("files") or {}).items() if k in ids}
    hits = {k: v["hits"] for k, v in leak_files.items() if v["hits"]}
    searched = sum(v["searched"] for v in leak_files.values())
    measures.zero(f"removed or treated identities whose original encoded bytes occur in the output "
                  f"(of {searched} searched)", sum(len(v) for v in hits.values()),
                  {"hits": hits, "not_reached": {k: v["not_reached"] for k, v in leak_files.items()
                                                 if v["not_reached"]}})
    if not leak:
        measures.add("image leak check run", None, "not run", "run", False,
                     {"fix": "python tests/pdfium/leak.py --images --run " + run_dir.name})

    redact = read_json(run_dir / "pdfium-redact-images.json") or {}
    files = {k: v for k, v in (redact.get("files") or {}).items() if k in ids}
    pages = sum(e["pages"] for e in files.values() if e["input_opens"])
    stable = sum(e["stable"] for e in files.values() if e["input_opens"])
    unstable = [{"file": k, **u} for k, e in sorted(files.items()) for u in e["unstable"]]
    measures.share("pages stable outside the treated draws' rectangles (one-pixel dilation)", stable, pages,
                   PIXELS, {"unstable": unstable})

    good, bad = [], []
    for row in judged:
        part = parts[row["id"]]
        reopen = part.get("reopen") or {}
        opened = (outcomes.get(row["id"]) or {}).get("open") or {}
        if (part.get("status") == "ok" and part.get("output") and reopen.get("status") == "ok"
                and not reopen.get("repairs") and reopen.get("pages") == opened.get("pages")):
            good.append(row["id"])
        else:
            bad.append({"id": row["id"], "status": part.get("status"), "code": part.get("code"),
                        "message": (part.get("message") or "")[:300], "reopen": reopen})
    measures.share("outputs written that re-open without repair with the input's pages (known gaps apart)",
                   len(good), len(judged), REOPEN, {"failed": bad, "known_gaps": gaps})
    if with_text:
        jobs = [(row, parts[row["id"]]) for row in judged if row["id"] in good]
        scores = text_agreement(run_dir, jobs, workers)
        scored = {k: v for k, v in scores.items() if v is not None}
        under = {k: v for k, v in scored.items() if v != 1.0}
        measures.share("outputs whose PDFium text equals the input's (word-bag F1 = 1.0)",
                       len(scored) - len(under), len(scored), TEXT, {"under_one": under})

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
    lines = [f"# Image treatment gate of run {report['run']}", "",
             f"Population: {report['population']} manifest files that open or repair and expect an "
             f"image treatment; {report['outcomes']} outcomes read; {len(report['known_gaps'])} known gaps.", "",
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


def write_manifest(run_dir: Path) -> int:
    """Fills expect_redact_images from the run's outcomes."""
    rows = common.read_manifest()
    changed = 0
    for row in rows:
        outcome = read_json(run_dir / "outcomes" / f"{row['id']}.json")
        if outcome is None:
            continue
        part = outcome.get("redact_images") or {}
        if row["expect_open"] not in ("opens", "repaired") or part.get("status") == "skipped":
            value = "n/a"
        elif part.get("status") == "ok" and not part.get("treatments"):
            value = "n/a"
        elif row["expect_redact_images"].startswith("known_gap:"):
            value = row["expect_redact_images"]
        else:
            value = "clean"
        if value != row["expect_redact_images"]:
            row["expect_redact_images"] = value
            changed += 1
    common.write_manifest(rows)
    print(json.dumps({"changed": changed}))
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="The image treatment gate over a dataset run.")
    parser.add_argument("--run", required=True, help="the run id of the report folder")
    parser.add_argument("--set", action="append", help="only the manifest rows of these sets")
    parser.add_argument("--id", action="append", help="only these manifest ids")
    parser.add_argument("--workers", type=int, default=4)
    parser.add_argument("--no-text", action="store_true", help="leave out PDFium's text comparison")
    parser.add_argument("--write-manifest", action="store_true",
                        help="fill the manifest's expect_redact_images column from the run and stop")
    args = parser.parse_args()
    run_dir = common.reports_dir() / args.run
    if not (run_dir / "summary.json").exists():
        raise common.ToolError(f"{run_dir / 'summary.json'} does not exist", "the summary of a dataset run",
                               "run `python tests/dataset/run.py --checks open,redact_images --runs 2` first")
    if args.write_manifest:
        return write_manifest(run_dir)
    rows = common.read_manifest()
    if args.set:
        rows = [r for r in rows if r["set"] in args.set]
    if args.id:
        rows = [r for r in rows if r["id"] in args.id]
    report = judge(run_dir, rows, args.workers, not args.no_text)
    text = markdown(report)
    (run_dir / "phase9.md").write_text(text, encoding="utf-8")
    (run_dir / "phase9.json").write_text(json.dumps(report, indent=1), encoding="utf-8")
    print(text[:6000])
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    common.run_tool(main)
