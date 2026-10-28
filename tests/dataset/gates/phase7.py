# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# The gate of the writer: judges a dataset run (tests/dataset/run.py with the write
# and select checks, then tests/pdfium/compare.py --check write,select,text)
# against the thresholds of the build plan and exits non-zero when one fails. It
# reads <run>/summary.json, the outcomes and the three pdfium-*.json files, and
# scans the planted-marker files itself: a synthesised input that holds markers
# `PDFKITMARK<RR>X<NN>` is read together with its identity-write output and its
# page-selection output (raw bytes and every stream the scanner decodes). A marker
# of a stripped channel (rows 01 to 24) must occur in no output; a marker of drawn
# content (rows 90 to 93, NN the page) must occur in the identity write and, in a
# selection, exactly for the kept pages. A marker of content in an optional-content
# group the default configuration turns off (row 25) must occur in no output: the
# off-layer specimens of dataset/tools/synth/channels/off_layers.py are read from the
# manifest when registered and are also written here through the pdfkit command line
# (select-pages of their one page, which is the identity write), and every output of a
# file holding optional content must hold no /OCProperties and no /OC name. The result is a markdown table of every
# measure (measure, value, threshold, pass or fail), written into the run folder
# as a markdown and a JSON file named after this script, with every differing
# page listed by file, page and cause.
# A page whose comparison names no cause is `unclassified` until the analyst
# writes its cause into <run>/causes.json, a map from "<id>:<page>" to the text.

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO / "dataset" / "tools"))

import autotag  # noqa: E402
import common  # noqa: E402

sys.path.insert(0, str(REPO / "tests" / "dataset"))

import run as dataset_run  # noqa: E402
from synth.channels import off_layers  # noqa: E402

MARK_RE = re.compile(rb"PDFKITMARK(\d\d)X(\d\d)")
STRIPPED_ROWS = range(1, 25)
OFF_ROWS = (25,)
DRAWN_ROWS = range(90, 94)
# A name /OC or /OCProperties: the name ends at white space, a delimiter or the end.
OC_NAME_RE = re.compile(rb"/OC(?:Properties)?(?=[\s()<>\[\]{}/%]|$)")

# The thresholds of the gate; a share is a fraction of its population.
WRITE_REOPEN = 1.0
WRITE_PDFIUM_OPENS = 1.0
WRITE_PIXELS = 0.99
TEXT_F1 = 0.995
SELECT_COUNT = 1.0
SELECT_PIXELS = 0.99
DETERMINISM = 1.0


# ---------------------------------------------------------------------------
# Markers


def parse_marker(marker: str) -> tuple[int, int]:
    """(row, counter or page) of a marker string `PDFKITMARK<RR>X<NN>`."""
    match = MARK_RE.fullmatch(marker.encode("ascii"))
    if not match:
        raise ValueError(f"{marker} is not a planted marker")
    return int(match.group(1)), int(match.group(2))


def classify(markers) -> dict:
    """The markers split by what they plant: stripped channels, content in a group turned off,
    drawn content, anything else."""
    out = {"stripped": [], "off": [], "drawn": [], "other": []}
    for marker in sorted(markers):
        row, _ = parse_marker(marker)
        key = ("stripped" if row in STRIPPED_ROWS else "off" if row in OFF_ROWS
               else "drawn" if row in DRAWN_ROWS else "other")
        out[key].append(marker)
    return out


def markers_in(blobs) -> set[str]:
    found = set()
    for blob in blobs:
        for match in MARK_RE.finditer(blob):
            found.add(match.group(0).decode("ascii"))
    return found


def blobs_of(path: Path) -> list[bytes]:
    """A file's raw bytes and every stream the scanner decodes."""
    data = path.read_bytes()
    blobs = [data]
    scan = autotag.Scan(data)
    for obj in scan.objects:
        if obj.data is not None:
            decoded = scan.decoded(obj)
            if decoded:
                blobs.append(decoded)
    return blobs


def file_markers(path: Path) -> set[str]:
    """Every marker in a file's raw bytes or in any stream the scanner decodes."""
    return markers_in(blobs_of(path))


def names_optional_content(path: Path) -> bool:
    """Whether a file names /OCProperties or /OC, raw or in a decoded stream."""
    return any(OC_NAME_RE.search(blob) for blob in blobs_of(path))


def surviving(output_markers) -> list[str]:
    """The markers of stripped channels found in an output: each one is a leak."""
    return classify(output_markers)["stripped"]


def missing_drawn(input_markers, output_markers) -> list[str]:
    """Drawn-content markers of the input that the output lost."""
    return sorted(set(classify(input_markers)["drawn"]) - set(output_markers))


def selection_violations(input_markers, output_markers, kept_pages) -> dict:
    """What a page selection got wrong: markers of stripped channels or removed pages that
    survive (`leaked`) and markers of kept pages that went missing (`lost`)."""
    kept = set(kept_pages)
    leaked = list(surviving(output_markers))
    lost = []
    for marker in classify(input_markers)["drawn"]:
        _, page = parse_marker(marker)
        present = marker in output_markers
        if page in kept and not present:
            lost.append(marker)
        elif page not in kept and present:
            leaked.append(marker)
    return {"leaked": sorted(leaked), "lost": sorted(lost)}


# ---------------------------------------------------------------------------
# Reading the run


def read_json(path: Path):
    return json.loads(path.read_text(encoding="utf-8")) if path.exists() else None


def pages_of(row: dict) -> int:
    """PDFium's page count of a manifest row, 0 when PDFium does not open the file."""
    return int(row["pages"]) if row["pages"].isdigit() else 0


def pdfkit_pages(outcome: dict | None) -> int:
    """pdfkit's page count of a file, 0 when it did not open: the pages a selection draws from.
    PDFium's count can differ (it trusts a root /Count the tree does not hold), and a selection
    is made from the pages pdfkit reads."""
    pages = ((outcome or {}).get("open") or {}).get("pages")
    return pages if isinstance(pages, int) else 0


def reopens(write: dict | None) -> bool:
    """An identity write that opens again in pdfkit without repair with the pages it wrote."""
    if not write or write.get("status") != "ok":
        return False
    reopen = write.get("reopen") or {}
    return reopen.get("status") == "ok" and not reopen.get("repairs") and reopen.get("pages") == write.get("pages")


def valid_selection(select: dict | None, source_pages: int) -> bool:
    """A selection that opens again with as many pages as it names, all of them real and distinct."""
    if not select or select.get("status") != "ok":
        return False
    kept = select.get("pages") or []
    reopen = select.get("reopen") or {}
    return (bool(kept) and len(set(kept)) == len(kept) and all(1 <= p <= source_pages for p in kept)
            and reopen.get("status") == "ok" and not reopen.get("repairs") and reopen.get("pages") == len(kept))


def share(count: int, total: int) -> float | None:
    return count / total if total else None


class Measures:
    """The rows of the gate table."""

    def __init__(self):
        self.rows = []

    def add(self, name: str, value, shown: str, threshold: str, passed: bool, detail=None) -> None:
        self.rows.append({"measure": name, "value": value, "shown": shown, "threshold": threshold,
                          "pass": bool(passed), "detail": detail or {}})

    def share(self, name: str, count: int, total: int, minimum: float, detail=None) -> None:
        value = share(count, total)
        shown = "not measured" if value is None else f"{count} / {total} = {value:.2%}"
        self.add(name, value, shown, f">= {minimum:.1%}" if minimum < 1 else "100%",
                 value is not None and value >= minimum, detail)

    def zero(self, name: str, count: int, detail=None) -> None:
        self.add(name, count, str(count), "0", count == 0, detail)


def differing_pages(entries: dict | None, ids: set, causes: dict) -> list[dict]:
    """Every differing page of the population: file, page, cause (`unclassified` when none)."""
    out = []
    for row_id, entry in sorted((entries or {}).items()):
        if row_id not in ids:
            continue
        for diff in entry.get("differing", []):
            key = f"{row_id}:{diff['page']}"
            out.append({"file": row_id, "page": diff["page"], "pixels": diff.get("pixels"),
                        "cause": causes.get(key) or diff.get("cause") or "unclassified"})
    return out


def judge(run_dir: Path, rows: list[dict], family: dict | None = None) -> dict:
    """The whole gate over one run folder and the population of manifest rows; family is what
    off_layer_family found, none when the specimens were not written."""
    summary = read_json(run_dir / "summary.json") or {}
    outcomes = {}
    for row in rows:
        outcome = read_json(run_dir / "outcomes" / f"{row['id']}.json")
        if outcome is not None:
            outcomes[row["id"]] = outcome
    pixel_write = read_json(run_dir / "pdfium-write.json") or {}
    pixel_select = read_json(run_dir / "pdfium-select.json") or {}
    text_write = read_json(run_dir / "pdfium-write-text.json") or {}
    causes = read_json(run_dir / "causes.json") or {}
    population = [r for r in rows if r["expect_open"] in ("opens", "repaired")]
    ids = {r["id"] for r in population}
    opened = [r for r in population if pages_of(r) > 0]
    measures = Measures()

    reopened = [r["id"] for r in population if reopens((outcomes.get(r["id"]) or {}).get("write"))]
    measures.share("identity write re-opens in pdfkit", len(reopened), len(population), WRITE_REOPEN,
                   {"failing": sorted(ids - set(reopened))[:50]})

    write_entries = pixel_write.get("files", {})
    output_opens = [r["id"] for r in opened if write_entries.get(r["id"], {}).get("output_opens")]
    measures.share("identity write opens in PDFium, of the files PDFium opens as input",
                   len(output_opens), len(opened), WRITE_PDFIUM_OPENS,
                   {"failing": sorted({r["id"] for r in opened} - set(output_opens))[:50]})

    total_pages = sum(pages_of(r) for r in opened)
    identical = sum(write_entries.get(r["id"], {}).get("identical", 0) for r in opened)
    write_diffs = differing_pages(write_entries, ids, causes)
    measures.share("identity write: pages pixel-identical to the input (annotations off)", identical,
                   total_pages, WRITE_PIXELS, {"differing_pages": len(write_diffs)})

    text_entries = text_write.get("files", {})
    perfect = [r["id"] for r in opened if text_entries.get(r["id"], {}).get("f1") == 1.0]
    measures.share("text agreement, output against input (PDFium on both sides): F1 = 1.0",
                   len(perfect), len(opened), TEXT_F1,
                   {"under_one": sorted({r["id"] for r in opened} - set(perfect))[:50]})

    planted = scan_planted(run_dir, rows, outcomes)
    family = family or {"files": 0, "failed": [], "off_surviving": [], "missing_drawn": [], "oc_named": []}
    planted["off_surviving"] += family["off_surviving"]
    planted["missing_drawn"] += family["missing_drawn"]
    planted["failed"] = family["failed"]
    measures.zero("planted markers of stripped channels found in an identity write",
                  len(planted["surviving"]),
                  {"files": planted["files"], "unchecked": planted["unchecked"], "surviving": planted["surviving"][:50]})
    off_files = f"{planted['off_files']} manifest files and {family['files']} off-layer specimens written here"
    off_leaks = len(planted["off_surviving"])
    measures.add("planted markers in optional-content layers turned off found in an identity write",
                 off_leaks, f"{off_leaks} ({off_files})" if not family["failed"] else
                 f"not measured: {family['failed'][0]}", "0", off_leaks == 0 and not family["failed"],
                 {"surviving": planted["off_surviving"][:50], "failed": family["failed"]})
    measures.zero("planted markers of drawn content missing from an identity write",
                  len(planted["missing_drawn"]), {"missing": planted["missing_drawn"][:50]})
    named = optional_content_outputs(run_dir, population, outcomes)
    planted["oc_named"] = named["oc_named"] + family["oc_named"]
    measures.add("identity writes still naming /OCProperties or /OC, of the files holding optional content",
                 len(planted["oc_named"]),
                 f"{len(planted['oc_named'])} of {named['files'] + family['files']}", "0",
                 not planted["oc_named"] and not family["failed"], {"files": planted["oc_named"][:50]})

    multi = [r for r in opened if pdfkit_pages(outcomes.get(r["id"])) >= 2]
    counted = [r["id"] for r in multi if select_count_right(r, outcomes.get(r["id"]), pixel_select)]
    measures.share("page selection, files of two or more pages: output page count right", len(counted),
                   len(multi), SELECT_COUNT, {"failing": sorted({r["id"] for r in multi} - set(counted))[:50]})

    select_entries = pixel_select.get("files", {})
    kept_pages, kept_identical = 0, 0
    for r in multi:
        select = (outcomes.get(r["id"]) or {}).get("select")
        if select and select.get("status") == "ok":
            kept_pages += len(select.get("pages") or [])
            kept_identical += select_entries.get(r["id"], {}).get("identical", 0)
    select_diffs = differing_pages(select_entries, ids, causes)
    measures.share("page selection: kept pages pixel-identical to their source pages", kept_identical,
                   kept_pages, SELECT_PIXELS, {"differing_pages": len(select_diffs)})

    measures.zero("page selection leak test over the planted multi-page files", len(planted["leaks"]),
                  {"files": planted["leak_files"], "violations": planted["leaks"][:50]})

    nondeterministic = summary.get("nondeterministic", [])
    if summary.get("runs") == 2:
        measures.share("two runs of the dataset: output bytes identical",
                       summary.get("files", 0) - len(nondeterministic), summary.get("files", 0), DETERMINISM,
                       {"differing": nondeterministic[:50]})
    else:
        measures.add("two runs of the dataset: output bytes identical", None, "not measured (runs != 2)",
                     "100%", False)
    troubles = len(summary.get("crashes", [])) + len(summary.get("hangs", []))
    measures.zero("crashes, hangs past 30 s, or anything but a status from the ABI", troubles,
                  {"crashes": summary.get("crashes", [])[:20], "hangs": summary.get("hangs", [])[:20]})

    return {"run": run_dir.name, "population": len(population), "outcomes": len(outcomes),
            "rows": measures.rows, "differing_pages": {"write": write_diffs, "select": select_diffs},
            "planted": {k: v for k, v in planted.items()}, "passed": all(m["pass"] for m in measures.rows)}


def select_count_right(row: dict, outcome: dict | None, pixel_select: dict) -> bool:
    """The selection opens with the pages it names in pdfkit, and in PDFium when PDFium read it."""
    select = (outcome or {}).get("select")
    if not valid_selection(select, pdfkit_pages(outcome)):
        return False
    entry = pixel_select.get("files", {}).get(row["id"])
    return entry is None or entry.get("pages_right") is True


def scan_planted(run_dir: Path, rows: list[dict], outcomes: dict) -> dict:
    """Reads every synthesised input holding markers with its write and select outputs."""
    result = {"files": 0, "leak_files": 0, "off_files": 0, "unchecked": [], "surviving": [], "off_surviving": [],
              "missing_drawn": [], "leaks": []}
    for row in rows:
        if row["set"] != "synth" or row["expect_open"] not in ("opens", "repaired"):
            continue
        path = common.file_path(row)
        if not path.exists():
            continue
        found = file_markers(path)
        if not found:
            continue
        outcome = outcomes.get(row["id"]) or {}
        result["files"] += 1
        result["off_files"] += 1 if classify(found)["off"] else 0
        write = outcome.get("write") or {}
        output = run_dir / write["output"] if write.get("status") == "ok" and write.get("output") else None
        if output is None or not output.exists():
            result["unchecked"].append(row["id"])
        else:
            written = file_markers(output)
            result["surviving"] += [f"{row['id']}:{m}" for m in surviving(written)]
            result["off_surviving"] += [f"{row['id']}:{m}" for m in classify(written)["off"]]
            result["missing_drawn"] += [f"{row['id']}:{m}" for m in missing_drawn(found, written)]
        # A selection is made only from a file of two pages or more.
        if not classify(found)["drawn"] or pdfkit_pages(outcome) < 2:
            continue
        result["leak_files"] += 1
        select = outcome.get("select") or {}
        chosen = run_dir / select["output"] if select.get("status") == "ok" and select.get("output") else None
        if chosen is None or not chosen.exists():
            result["leaks"].append(f"{row['id']}: no select output to scan")
            continue
        violations = selection_violations(found, file_markers(chosen), select.get("pages") or [])
        result["leaks"] += [f"{row['id']}:{m} leaked" for m in violations["leaked"]]
        result["leaks"] += [f"{row['id']}:{m} lost" for m in violations["lost"]]
    return result


def off_layer_family(run_dir: Path, pdfkit: list[str] | None) -> dict:
    """Writes every off-layer specimen into <run>/off-layers/ and its identity write beside it
    through the command line, and scans the output: off markers that survive, shown markers lost,
    and whether it still names optional content."""
    result = {"files": 0, "failed": [], "off_surviving": [], "missing_drawn": [], "oc_named": []}
    if pdfkit is None:
        result["failed"].append("no pdfkit executable: build one or pass --pdfkit")
        return result
    folder = run_dir / "off-layers"
    folder.mkdir(parents=True, exist_ok=True)
    for index, specimen in enumerate(off_layers.PATTERNS["mark_off_layers"]()):
        name = f"mark_off_layers-{index + 1:02d}"
        source = folder / f"{name}.pdf"
        output = folder / f"{name}.out.pdf"
        source.write_bytes(specimen.data)
        output.unlink(missing_ok=True)
        done = subprocess.run([*pdfkit, "select-pages", str(source), "--pages", "1", "--out", str(output)],
                              capture_output=True, timeout=120, check=False)
        if done.returncode != 0 or not output.exists():
            result["failed"].append(f"{name}: pdfkit exited {done.returncode}")
            continue
        result["files"] += 1
        found = file_markers(source)
        written = file_markers(output)
        result["off_surviving"] += [f"{name}:{m}" for m in classify(written)["off"]]
        result["missing_drawn"] += [f"{name}:{m}" for m in missing_drawn(found, written)]
        if names_optional_content(output):
            result["oc_named"].append(name)
    return result


def optional_content_outputs(run_dir: Path, rows: list[dict], outcomes: dict) -> dict:
    """The identity writes of the files tagged ch_optional_content that still name /OCProperties
    or /OC."""
    result = {"files": 0, "oc_named": []}
    for row in rows:
        if "ch_optional_content" not in row.get("tags", "").split(","):
            continue
        write = (outcomes.get(row["id"]) or {}).get("write") or {}
        output = run_dir / write["output"] if write.get("status") == "ok" and write.get("output") else None
        if output is None or not output.exists():
            continue
        result["files"] += 1
        if names_optional_content(output):
            result["oc_named"].append(row["id"])
    return result


def markdown(report: dict) -> str:
    lines = [f"# Gate of run {report['run']}", "",
             f"Population: {report['population']} manifest files that open or repair; "
             f"{report['outcomes']} outcomes read.", "",
             "| Measure | Value | Threshold | Result |", "|---|---|---|---|"]
    for row in report["rows"]:
        lines.append(f"| {row['measure']} | {row['shown']} | {row['threshold']} | {'pass' if row['pass'] else 'FAIL'} |")
    lines += ["", f"Gate: {'PASSED' if report['passed'] else 'FAILED'}"]
    for kind in ("write", "select"):
        diffs = report["differing_pages"][kind]
        if diffs:
            lines += ["", f"## Differing pages of the {kind} check ({len(diffs)})", "",
                      "| File | Page | Pixels | Cause |", "|---|---|---|---|"]
            lines += [f"| {d['file']} | {d['page']} | {'' if d['pixels'] is None else d['pixels']} | {d['cause']} |"
                      for d in diffs]
    planted = report["planted"]
    for key, title in (("surviving", "Markers of stripped channels found in an output"),
                       ("off_surviving", "Markers of optional-content layers turned off found in an output"),
                       ("oc_named", "Outputs still naming /OCProperties or /OC"),
                       ("missing_drawn", "Markers of drawn content missing from an identity write"),
                       ("leaks", "Page selection violations"), ("unchecked", "Planted files without a write output")):
        if planted[key]:
            lines += ["", f"## {title}", ""] + [f"- {item}" for item in planted[key]]
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description="The writer gate over a dataset run.")
    parser.add_argument("--run", required=True, help="the run id of the report folder")
    parser.add_argument("--set", action="append", help="only the manifest rows of these sets")
    parser.add_argument("--id", action="append", help="only these manifest ids")
    parser.add_argument("--pdfkit", help="the pdfkit executable that writes the off-layer specimens "
                        "(default: as tests/dataset/run.py finds it)")
    args = parser.parse_args()
    run_dir = common.reports_dir() / args.run
    if not (run_dir / "summary.json").exists():
        raise common.ToolError(f"{run_dir / 'summary.json'} does not exist", "the summary of a dataset run",
                               "run `python tests/dataset/run.py --checks open,write,select --runs 2` first")
    rows = common.read_manifest()
    if args.set:
        rows = [r for r in rows if r["set"] in args.set]
    if args.id:
        rows = [r for r in rows if r["id"] in args.id]
    report = judge(run_dir, rows, off_layer_family(run_dir, dataset_run.find_pdfkit(args.pdfkit)))
    text = markdown(report)
    (run_dir / "phase7.md").write_text(text, encoding="utf-8")
    (run_dir / "phase7.json").write_text(json.dumps(report, indent=1), encoding="utf-8")
    print(text)
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    common.run_tool(main)
