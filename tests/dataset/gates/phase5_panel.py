# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# The reading gate judged by the reference panel: pdfkit's text of every
# dataset file a run read (the outcome's text.text, every page with its hidden
# blocks, as tests/pdfium/text_agreement.py reads it) against the text of each
# text member of the panel (PDFium, Poppler, MuPDF, pdf_oxide, pdf-extract),
# read from the answers cache. A file counts against pdfkit only when its word
# F1 is below 0.90 against a strict majority of at least three voters
# (tests/panel/judge.py text_verdict); a file with fewer voters or a tie is
# undecided, listed and counted neither way. The gate: the median over the
# decided files of pdfkit's median F1 across voters is at least 0.97, and at
# most 5 % of the decided files count against pdfkit, the thresholds of the
# reading gate. pdfkit's other readings (its visible text, its text with every
# duplicate copy) come from `pdfkit text` for the files that hold hidden or
# duplicate text, and the gate refuses when that binary's text of such a file
# is not the run's (its SHA-256 is reported), because a reading from another
# build is not the run's reading. Poppler, MuPDF and pdf_oxide print the text
# annotations and form fields draw, which pdfkit leaves out as a channel
# (CLAUDE.md section 2): on a file where QPDF finds an annotation that draws
# text (a widget, a FreeText, or another whose appearance shows a string;
# never a link), those three are judged on their text of a copy QPDF writes
# without the annotations and the form (tests/panel/evidence.py), asked and
# cached like any answer. The literal rule, pdfkit's full text against each
# member's own text with words.py alone, is reported beside it.
#
# It writes a JSON and a markdown report named after this script into the run
# folder: the two
# numbers, every file counting against pdfkit with its group from
# tests/panel/text_causes.tsv (else `unclassified`) and its cause from
# tests/pdfium/text_causes.tsv, every undecided file with why, the
# member-pair agreement, the median per tag, how each file of
# tests/pdfium/text_causes.tsv moved, and the share undecided against the
# 10 % beyond which the panel is too thin to judge. A missing answer is an
# abstention, reported; --collect asks the member instead.
"""The reading gate under the panel: python tests/dataset/gates/phase5_panel.py --run <run-id> [--smoke]."""

from __future__ import annotations

import argparse
import collections
import concurrent.futures
import json
import statistics
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[3]
for folder in (REPO / "tests" / "panel", REPO / "tests" / "pdfium", REPO / "tests" / "dataset",
               REPO / "dataset" / "tools"):
    sys.path.insert(0, str(folder))

import common  # noqa: E402
import evidence  # noqa: E402
import judge  # noqa: E402

MEDIAN = 0.97
SHARE_AGAINST = 0.05
UNDECIDED_LIMIT = 0.10
PANEL_CAUSES = REPO / "tests" / "panel" / "text_causes.tsv"
PDFIUM_CAUSES = REPO / "tests" / "pdfium" / "text_causes.tsv"
SMOKE = REPO / "tests" / "panel" / "smoke.tsv"
GROUPS = ("cjk_cmap", "cjk_width", "cjk", "type3_code_names", "subset_unreadable", "layout", "other", "legitimate")
# Members whose text holds annotation and form-field appearances: synth-0011's form-field
# marker is in Poppler's, MuPDF's and pdf_oxide's text and in neither PDFium's nor pdf-extract's.
DRAWS_ANNOTATIONS = ("mupdf", "oxide", "poppler")


def read_causes(path: Path) -> dict[str, tuple[str, str]]:
    """id -> (group or cause, evidence) of a causes table."""
    out = {}
    if path.exists():
        for line in path.read_text(encoding="utf-8").splitlines()[1:]:
            parts = line.split("\t")
            if len(parts) >= 3 and line.strip() and not line.startswith("#"):
                out[parts[0]] = (parts[1], parts[2])
    return out


def smoke_ids() -> set[str]:
    return {line.split("\t")[0] for line in SMOKE.read_text(encoding="utf-8").splitlines()[1:] if line.strip()}


def timeout_of(row: dict) -> float:
    pages = int(row["pages"]) if str(row["pages"]).isdigit() else 1
    return 30.0 * max(1, -(-pages // 200))


def literal_rule(full: str, answers: dict) -> dict:
    """pdfkit's full text against each member's own text with words.py alone."""
    scores = {m: round(judge.words.f1(full, judge.member_text(a)), 6)
              for m, a in sorted(answers.items()) if judge.abstention(a) is None}
    below = [m for m, v in scores.items() if v < judge.AGREE]
    if len(scores) < judge.TEXT_QUORUM or 2 * len(below) == len(scores):
        verdict = "undecided"
    else:
        verdict = "counts_against" if 2 * len(below) > len(scores) else "agrees"
    return {"verdict": verdict, "scores": scores,
            "median": round(statistics.median(scores.values()), 6) if scores else None}


def judge_file(row: dict, outcome: dict, panel: evidence.Panel, command: list[str] | None) -> dict:
    text = outcome.get("text") or {}
    path = common.file_path(row)
    timeout = timeout_of(row)
    own = {name: panel.answer(name, path, "text", timeout=timeout) for name in evidence.TEXT_MEMBERS}
    for name in panel.absent:
        if name in own:
            own[name] = {"ok": False, "error": "absent"}
    full = text.get("text", "")
    readings, note, mismatch = [], None, False
    if evidence.hidden_count(text) or (text.get("hidden_classes") or {}).get("duplicate"):
        if command is None:
            note = "no pdfkit executable: the visible and every-copy readings are left out"
        else:
            other = evidence.other_readings(command, path)
            if other is None:
                note = "pdfkit text failed: the visible and every-copy readings are left out"
            elif other["full"] != full:
                mismatch = True
            else:
                readings = other["readings"]
    answers = dict(own)
    survey = panel.answer("qpdf", path, "annotations", timeout=timeout, **evidence.ANNOTATIONS_REQUEST)
    read_copy = []
    if survey and survey.get("ok") and survey.get("draws_text"):
        copy, why = panel.stripped_copy(path, timeout=4 * timeout)
        for name in DRAWS_ANNOTATIONS:
            if name not in own:
                continue
            answers[name] = panel.answer(name, copy, "text", timeout=timeout) if copy else \
                {"ok": False, "error": f"no copy without annotations: {why}"}
            read_copy.append(name)
    verdict = judge.text_verdict(full, answers, readings=readings)
    return {"id": row["id"], "set": row["set"], "tags": row["tags"], **verdict,
            "readings": len(readings), "read_copy": read_copy, "note": note, "mismatch": mismatch,
            "annotations": None if not survey else {k: survey.get(k) for k in ("ok", "error", "draws_text",
                                                                               "text", "subtypes")},
            "literal": literal_rule(full, own), "answers": answers}


def summarise(results: list[dict], panel_causes: dict, pdfium_causes: dict) -> dict:
    decided = [r for r in results if r["verdict"] != "undecided"]
    against = [r for r in decided if r["verdict"] == "counts_against"]
    undecided = [r for r in results if r["verdict"] == "undecided"]
    literal_decided = [r for r in results if r["literal"]["verdict"] != "undecided"]
    literal_against = [r for r in literal_decided if r["literal"]["verdict"] == "counts_against"]
    per_tag = collections.defaultdict(list)
    for r in decided:
        for tag in common.split_tags(r["tags"]):
            per_tag[tag].append(r["median"])
    moved = []
    by_id = {r["id"]: r for r in results}
    for file_id, (cause, _) in sorted(pdfium_causes.items()):
        r = by_id.get(file_id)
        moved.append({"id": file_id, "pdfium_cause": cause,
                      "panel": r["verdict"] if r else "not judged",
                      "group": panel_causes.get(file_id, ("", ""))[0]})
    groups = collections.Counter(panel_causes.get(r["id"], ("unclassified", ""))[0] for r in against)
    summary = {
        "files_judged": len(results),
        "decided": len(decided),
        "undecided": len(undecided),
        "share_undecided": round(len(undecided) / len(results), 4) if results else None,
        "median": round(statistics.median(r["median"] for r in decided), 4) if decided else None,
        "counts_against": len(against),
        "share_against": round(len(against) / len(decided), 4) if decided else None,
        "against_by_group": dict(sorted(groups.items())),
        "literal": {"decided": len(literal_decided), "counts_against": len(literal_against),
                    "share_against": round(len(literal_against) / len(literal_decided), 4) if literal_decided else None,
                    "median": round(statistics.median(r["literal"]["median"] for r in literal_decided), 4)
                    if literal_decided else None},
        "annotations": {"files_drawing_text": sum(1 for r in results if r["read_copy"]),
                        "files_surveyed": sum(1 for r in results if r["annotations"] and r["annotations"]["ok"]),
                        "files_not_surveyed": sorted(r["id"] for r in results
                                                     if not (r["annotations"] and r["annotations"]["ok"]))},
        "against": [{"id": r["id"], "median": r["median"], "scores": r["scores"], "below": r["below"],
                     "group": panel_causes.get(r["id"], ("unclassified", ""))[0],
                     "pdfium_cause": pdfium_causes.get(r["id"], ("", ""))[0],
                     "evidence": panel_causes.get(r["id"], pdfium_causes.get(r["id"], ("", "")))[1]}
                    for r in sorted(against, key=lambda r: (r["median"], r["id"]))],
        "undecided_files": [{"id": r["id"], "why": r["why"], "voters": r["voters"], "abstained": r["abstained"]}
                            for r in sorted(undecided, key=lambda r: r["id"])],
        "per_tag_median": {t: round(statistics.median(v), 4) for t, v in sorted(per_tag.items())},
        "pdfium_causes_moved": moved,
        "notes": sorted({r["note"] for r in results if r["note"]}),
    }
    summary["kill_criterion"] = ("fired" if summary["share_undecided"] is not None
                                 and summary["share_undecided"] > UNDECIDED_LIMIT else "not fired")
    passed = (summary["median"] is not None and summary["median"] >= MEDIAN
              and summary["share_against"] is not None and summary["share_against"] <= SHARE_AGAINST)
    summary["gate"] = "passed" if passed else "failed"
    return summary


def write_report(run_dir: Path, summary: dict, agreement: dict, absent: dict) -> None:
    (run_dir / "phase5_panel.json").write_text(json.dumps(summary, indent=1, ensure_ascii=False) + "\n",
                                               encoding="utf-8")
    s = summary
    md = [f"# Reading gate under the panel, run {run_dir.name}", "",
          "| measure | value | threshold | result |", "|---|---|---|---|",
          f"| median over decided files of pdfkit's median F1 | {s['median']} | {MEDIAN} | "
          f"{'pass' if s['median'] is not None and s['median'] >= MEDIAN else 'fail'} |",
          f"| decided files counting against pdfkit | {s['counts_against']} of {s['decided']} "
          f"({s['share_against']}) | {SHARE_AGAINST} | "
          f"{'pass' if s['share_against'] is not None and s['share_against'] <= SHARE_AGAINST else 'fail'} |",
          f"| undecided files | {s['undecided']} of {s['files_judged']} ({s['share_undecided']}) | "
          f"{UNDECIDED_LIMIT} | kill criterion {s['kill_criterion']} |", "",
          f"Gate: **{s['gate']}**. Literal rule (full text against each member's own text, words.py's "
          f"metric alone): {s['literal']['counts_against']} of {s['literal']['decided']} "
          f"({s['literal']['share_against']}), median {s['literal']['median']}.",
          f"Files whose annotations draw text, where Poppler, MuPDF and pdf_oxide read QPDF's copy "
          f"without them: {s['annotations']['files_drawing_text']} "
          f"(of {s['annotations']['files_surveyed']} QPDF surveyed).",
          f"Against, by group of tests/panel/text_causes.tsv: {s['against_by_group']}.", ""]
    if absent:
        md += ["Absent members: " + "; ".join(absent.values()), ""]
    md += [f"Note: {n}" for n in s["notes"]]
    md += ["## Files counting against pdfkit", "", "| id | median | below | group | PDFium cause | evidence |",
           "|---|---|---|---|---|---|"]
    md += [f"| {a['id']} | {a['median']} | {', '.join(a['below'])} | {a['group']} | {a['pdfium_cause']} | "
           f"{a['evidence']} |" for a in s["against"]]
    md += ["", "## Undecided files", "", "| id | why | voters | abstained |", "|---|---|---|---|"]
    md += [f"| {u['id']} | {u['why']} | {', '.join(u['voters'])} | "
           f"{', '.join(f'{m}: {w}' for m, w in sorted(u['abstained'].items()))} |" for u in s["undecided_files"]]
    md += ["", "## Member agreement (median F1 between two members over the files both read)", "",
           "| members | files | median |", "|---|---|---|"]
    md += [f"| {a} and {b} | {v['files']} | {v['median']} |" for (a, b), v in agreement.items()]
    md += ["", "## The files of tests/pdfium/text_causes.tsv", "", "| id | PDFium cause | panel | group |",
           "|---|---|---|---|"]
    md += [f"| {m['id']} | {m['pdfium_cause']} | {m['panel']} | {m['group']} |" for m in s["pdfium_causes_moved"]]
    md += ["", "## Median per tag", "", "| tag | median |", "|---|---|"]
    md += [f"| {t} | {m} |" for t, m in s["per_tag_median"].items()]
    (run_dir / "phase5_panel.md").write_text("\n".join(md) + "\n", encoding="utf-8")


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description="The reading gate judged by the reference panel.")
    parser.add_argument("--run", required=True, help="the dataset run id whose outcomes are judged")
    parser.add_argument("--smoke", action="store_true", help="only the files of tests/panel/smoke.tsv")
    parser.add_argument("--set", action="append", help="only these sets")
    parser.add_argument("--id", action="append", help="only these ids")
    parser.add_argument("--collect", action="store_true", help="ask a member for an answer the cache lacks")
    parser.add_argument("--pdfkit", help="the pdfkit executable for the other readings (default: out/)")
    parser.add_argument("--workers", type=int, default=2)
    args = parser.parse_args(argv)

    run_dir = common.reports_dir() / args.run
    if not (run_dir / "outcomes").exists():
        raise common.ToolError(f"{run_dir / 'outcomes'} does not exist", "a dataset run's outcomes",
                               "run `python tests/dataset/run.py --checks open,text` and pass its run id")
    import run as dataset_run  # noqa: PLC0415

    command = dataset_run.find_pdfkit(args.pdfkit)
    panel = evidence.Panel(evidence.TEXT_MEMBERS + ("qpdf",), collect_missing=args.collect)
    if not any(name in panel.members for name in evidence.TEXT_MEMBERS):
        print("SKIPPED reading gate under the panel: no text member is present; "
              + "; ".join(panel.absent[name] for name in evidence.TEXT_MEMBERS if name in panel.absent))
        return 0
    rows = common.read_manifest()
    if args.smoke:
        chosen = smoke_ids()
        rows = [r for r in rows if r["id"] in chosen]
    if args.set:
        rows = [r for r in rows if r["set"] in args.set]
    if args.id:
        rows = [r for r in rows if r["id"] in args.id]
    jobs = []
    for row in rows:
        outcome_path = run_dir / "outcomes" / f"{row['id']}.json"
        if not outcome_path.exists():
            continue
        outcome = json.loads(outcome_path.read_text(encoding="utf-8"))
        if (outcome.get("text") or {}).get("status") == "ok":
            jobs.append((row, outcome))
    with concurrent.futures.ThreadPoolExecutor(max(1, args.workers)) as pool:
        results = list(pool.map(lambda job: judge_file(job[0], job[1], panel, command), jobs))
    mismatched = sorted(r["id"] for r in results if r["mismatch"])
    if mismatched:
        raise common.ToolError(
            f"the pdfkit executable {command[0] if command else ''} reads {len(mismatched)} files otherwise than "
            f"run {args.run} did: {', '.join(mismatched[:10])}",
            "the binary that made the run, whose full text of a file equals the run's outcome",
            "build the run's commit, or run the dataset tier again with this build, and pass --pdfkit")
    agreement = judge.member_agreement({r["id"]: r.pop("answers") for r in results})
    summary = summarise(results, read_causes(PANEL_CAUSES), read_causes(PDFIUM_CAUSES))
    summary["absent"] = panel.absent
    summary["pdfkit_binary"] = evidence.binary_digest(command) if command else None
    summary["member_agreement"] = {f"{a} {b}": v for (a, b), v in agreement.items()}
    summary["files"] = [{k: r[k] for k in ("id", "verdict", "median", "scores", "reading", "below", "abstained",
                                           "read_copy", "annotations", "literal")} for r in results]
    write_report(run_dir, summary, agreement, panel.absent)
    print(json.dumps({k: summary[k] for k in ("files_judged", "decided", "undecided", "share_undecided", "median",
                                             "counts_against", "share_against", "against_by_group", "literal",
                                             "kill_criterion", "gate")}, indent=1))
    return 0 if summary["gate"] == "passed" else 1


if __name__ == "__main__":
    common.run_tool(main)
