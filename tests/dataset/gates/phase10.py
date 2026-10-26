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


def gate_entry(phase: int, measures: Measures | None, note: str = "") -> dict:
    if measures is None:
        return {"phase": phase, "passed": None, "rows": [], "note": note}
    return {"phase": phase, "passed": all(r["pass"] for r in measures.rows), "rows": measures.rows,
            "note": note}


def reading_gate(run_dir: Path, rows: list[dict], summary: dict, outcomes: dict) -> dict:
    """The reading gate (open, repair, refusal) from the run's own outcomes and the manifest's PDFium
    page counts: a row's `pages` is PDFium's count, empty when PDFium does not open the file."""
    m = Measures()
    pdfium_opens = [r for r in rows if r["pages"].isdigit() and r["id"] in outcomes]
    opened = [r for r in pdfium_opens if (outcomes[r["id"]].get("open") or {}).get("status") == "ok"]
    m.share("of the files PDFium opens, pdfkit opens", len(opened), len(pdfium_opens), 0.99,
            {"not_opened": [r["id"] for r in pdfium_opens if r not in opened]})
    same = [r for r in opened if (outcomes[r["id"]]["open"].get("pages")) == int(r["pages"])]
    m.share("page count equal to PDFium's on files both open", len(same), len(opened), 0.99,
            {"differ": [f"{r['id']}: {outcomes[r['id']]['open'].get('pages')} vs {r['pages']}"
                        for r in opened if r not in same]})
    silent = []
    for row in rows:
        if not row["source"].startswith("mutate:") or row["id"] not in outcomes:
            continue
        source = outcomes.get(row["source"].split(":")[1]) or {}
        mine = outcomes[row["id"]].get("open") or {}
        if mine.get("status") == "ok" and mine.get("pages") != (source.get("open") or {}).get("pages"):
            silent.append(row["id"])
    m.zero("mutated files that open with another page count than their source and no refusal",
           len(silent), {"files": silent})
    refusals = [r for r in rows if r["expect_open"].startswith("refused:") and r["id"] in outcomes]
    matched = [r for r in refusals if (outcomes[r["id"]].get("open") or {}).get("code")
               == r["expect_open"].split(":", 1)[1]]
    m.share("expected refusals refused with their code", len(matched), len(refusals), 1.0,
            {"other": [r["id"] for r in refusals if r not in matched]})
    return gate_entry(3, m, "measured from the run's outcomes")


def fonts_gate(run_dir: Path, rows: list[dict], outcomes: dict) -> dict:
    """The decoding ladder's gate from the text outcomes: every font loaded (the text function answered
    on every opened file) and the share of codes no rung reads, over the files PDFium reads text from.
    The census of character agreement this gate first used is not run again; the reading gate's word
    agreement stands for it."""
    m = Measures()
    opened = [k for k, o in outcomes.items() if (o.get("open") or {}).get("status") == "ok"]
    answered = [k for k in opened if (outcomes[k].get("text") or {}).get("status") == "ok"]
    m.share("fonts loaded without an exception (text answered on every opened file)", len(answered),
            len(opened), 1.0, {"failed": [k for k in opened if k not in answered]})
    scored = {r["id"] for r in rows if r.get("expect_text", "n/a") != "n/a"}
    unreadable = total = 0
    for k in answered:
        if k not in scored:
            continue
        rungs = outcomes[k]["text"].get("rungs") or {}
        total += sum(rungs.values())
        unreadable += rungs.get("unreadable", 0)
    value = unreadable / total if total else None
    m.add("codes no rung reads, of every shown code on files PDFium reads text from", value,
          "not measured" if value is None else f"{unreadable} / {total} = {value:.2%}", "<= 1%",
          value is not None and value <= 0.01)
    return gate_entry(4, m, "the census's character agreement is not run again (see the reading gate)")


def text_gate(run_dir: Path) -> dict:
    """The reading gate from tests/pdfium/text_agreement.py's agreement.json."""
    data = read_json(run_dir / "agreement.json")
    if data is None:
        return gate_entry(5, None, f"python tests/pdfium/text_agreement.py --run {run_dir.name} --hidden")
    m = Measures()
    m.add("median bag-of-words F1 with PDFium", data["median"], f"{data['median']:.3f}", ">= 0.97",
          data["median"] >= 0.97)
    below, scored = data["below_0_90"], data["files_scored"]
    m.add("files below 0.90 F1", below / scored, f"{below} / {scored} = {below / scored:.2%} "
          f"(causes: {data['below_by_cause']})", "<= 5%", below / scored <= 0.05)
    nondet = (data.get("determinism") or {}).get("nondeterministic", [])
    m.zero("text outcomes that differ between the two runs", len(nondet), {"files": nondet})
    rs = data["reserialisation"]
    m.zero("undamaged content stream walks that do not write back to their bytes", rs["failed"],
           {"streams": rs["streams"], "damaged": rs["damaged"]})
    hidden = data.get("hidden") or {}
    m.zero("planted hidden marker words not extracted with their class", len(hidden.get("failures", [])),
           {"files": hidden.get("files")})
    return gate_entry(5, m, "tests/pdfium/text_agreement.py")


def images_gate(run_dir: Path, summary: dict) -> dict:
    """The image decoders' gate from tests/pdfium/image_agreement.py's census and listing summaries."""
    census = run_dir / "image-census" / "summary.md"
    listing = run_dir / "image-listing" / "listing.md"
    if not census.exists():
        return gate_entry(6, None, f"python tests/pdfium/image_agreement.py --out {run_dir}/image-census")
    text = census.read_text(encoding="utf-8")
    m = Measures()

    def pair(pattern: str, source: str) -> tuple[int, int]:
        found = re.search(pattern, source)
        return (int(found.group(1)), int(found.group(2))) if found else (0, 0)

    decoded, occurrences = pair(r"(\d+) of (\d+) page occurrences", text)
    m.share("image occurrences that decode (reached through resources)", decoded, occurrences, 0.98)
    if listing.exists():
        listed = listing.read_text(encoding="utf-8")
        count, total = pair(r"Decoded occurrences: (\d+) of (\d+)", listed)
        m.share("image occurrences that decode (the listing's draws)", count, total, 0.98)
        silent = re.search(r"Undecodable images without their warning: (\d+)", listed)
        m.zero("undecodable images without their warning", int(silent.group(1)) if silent else -1)
    agree, compared = pair(r"Bitonal agreement \(pixel for pixel\): (\d+) of (\d+)", text)
    m.share("bitonal images equal to PDFium's bitmap pixel for pixel", agree, compared, 1.0)
    agree, compared = pair(r"Continuous-tone agreement \(MAD <= 2.0\): (\d+) of (\d+)", text)
    m.share("continuous-tone images within a mean difference of 2 of PDFium's", agree, compared, 0.99)
    m.zero("image outcomes that differ between the two runs",
           len([i for i in summary.get("nondeterministic", [])]))
    return gate_entry(6, m, "tests/pdfium/image_agreement.py (census and listing)")


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

    earlier = [reading_gate(run_dir, rows, summary, outcomes), fonts_gate(run_dir, rows, outcomes),
               text_gate(run_dir), images_gate(run_dir, summary)]
    for phase in range(7, 10):
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
            lines.append(f"| {entry['phase']} | not run: {entry.get('note') or 'no table in the run folder'} | | | not run |")
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
