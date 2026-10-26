# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# The PDFium tier: measures pdfkit against the independent reader. `--run`
# scores each file's text of a dataset run against PDFium's with the word-bag
# F1 of words.py and writes pdfium-text.tsv with the medians per set; `--render`
# renders the pages of a redaction's input and output and proves them pixel-
# stable outside the reported rectangles; `--leak` searches PDFium's text and the
# output's decoded bytes for every redacted string. `--self-test` proves the
# tier itself before pdfkit reads anything: PDFium against PDFium must score 1.0
# on every file it opens, a copy with one word in ten dropped must score
# 0.947 +- 0.01, and PDFium must open 95 % of the rows that carry no damage,
# password or unsafe tag. `--check write,select,text` judges the writer: it
# renders every page of a run's identity-write and page-selection outputs against
# their inputs (render.py, exact pixels, annotations off) and compares PDFium's
# text of an identity write with its text of the input, writing
# pdfium-write.json, pdfium-select.json and pdfium-write-text.json into the run;
# `--check redact` renders every page of each text redaction's (and each image
# treatment's) output against its
# input outside the reported rectangles (one pixel of dilation) and counts the
# placeholders PDFium reads against those drawn, into pdfium-redact.json (the
# text redactions) and pdfium-redact-images.json (the image treatments).

from __future__ import annotations

import argparse
import collections
import concurrent.futures
import json
import statistics
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parents[1] / "dataset" / "tools"))

import common  # noqa: E402
import pdfium  # noqa: E402
import render  # noqa: E402
import words  # noqa: E402

AGREE = 0.90
PERTURBED = 0.947
TOLERANCE = 0.01
OPEN_RATE = 0.95
# The tags that excuse PDFium from opening a file: structural damage and a
# password. Structural features that are not damage (object streams, xref
# streams, linearisation, incremental updates) count like any other file.
UNSAFE_TAGS = {"xref_missing", "xref_wrong_offsets", "startxref_wrong", "eof_missing", "junk_before_header",
               "length_wrong", "endstream_missing", "endobj_missing", "two_trailers", "truncated",
               "damaged_flate", "linearised_stale_hints", "user_password"}
# A one-in-ten drop scores 0.947 to 0.952 only from a hundred words up; shorter
# texts round the dropped share and are reported, not judged.
PERTURB_MIN_WORDS = 100


def page_texts(probe: dict) -> list[str]:
    return [t or "" for t in probe.get("text", [])]


def drop_every_tenth(text: str) -> str:
    """The text's words with every tenth one removed."""
    kept = []
    for index, word in enumerate(w for w, n in words.words(text).items() for _ in range(n)):
        if index % 10 != 9:
            kept.append(word)
    return " ".join(kept)


def counts_for_gate(row: dict) -> bool:
    return not set(common.split_tags(row["tags"])) & UNSAFE_TAGS


def self_test(rows: list[dict], workers: int, out_dir: Path) -> int:
    present = [r for r in rows if common.file_path(r).exists()]
    results = {}
    with pdfium.Pool(workers) as pool, concurrent.futures.ThreadPoolExecutor(workers) as threads:
        first = {r["id"]: threads.submit(pool.probe, common.file_path(r), ["text"]) for r in present}
        second = {r["id"]: threads.submit(pool.probe, common.file_path(r), ["text"]) for r in present}
        for row in present:
            results[row["id"]] = (first[row["id"]].result(), second[row["id"]].result())
    lines = ["id\tset\tpdfium\tpages\tself_f1\tperturbed_f1\twords"]
    gate_rows = opened_gate = 0
    self_fail, perturb_fail, perturbed = [], [], []
    not_opened = []
    for row in present:
        a, b = results[row["id"]]
        in_gate = counts_for_gate(row)
        gate_rows += in_gate
        if not a.get("ok"):
            if in_gate:
                not_opened.append(f"{row['id']} ({a.get('error')})")
            lines.append(f"{row['id']}\t{row['set']}\t{a.get('error')}\t\t\t\t")
            continue
        opened_gate += in_gate
        text_a, text_b = "\n".join(page_texts(a)), "\n".join(page_texts(b))
        same = words.f1(text_a, text_b)
        count = sum(words.words(text_a).values())
        drop = words.f1(drop_every_tenth(text_a), text_a) if count >= 10 else None
        if same != 1.0:
            self_fail.append(row["id"])
        if drop is not None:
            perturbed.append(drop)
            if count >= PERTURB_MIN_WORDS and abs(drop - PERTURBED) > TOLERANCE:
                perturb_fail.append(f"{row['id']} ({drop:.3f})")
        lines.append(f"{row['id']}\t{row['set']}\tok\t{a['pages']}\t{same:.4f}\t"
                     f"{'' if drop is None else f'{drop:.4f}'}\t{count}")
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "pdfium-selftest.tsv").write_text("\n".join(lines) + "\n", encoding="utf-8")
    rate = opened_gate / gate_rows if gate_rows else 1.0
    summary = {
        "rows": len(rows), "present": len(present),
        "gate_rows": gate_rows, "gate_opened": opened_gate, "open_rate": round(rate, 4),
        "self_f1_not_one": self_fail,
        "perturbed_files": len(perturbed),
        "perturbed_median": round(statistics.median(perturbed), 4) if perturbed else None,
        "perturbed_out_of_tolerance": perturb_fail,
        "not_opened_in_gate": not_opened,
    }
    (out_dir / "pdfium-selftest.json").write_text(json.dumps(summary, indent=1), encoding="utf-8")
    print(json.dumps({k: v for k, v in summary.items() if k != "not_opened_in_gate"}
                     | {"not_opened_in_gate": len(not_opened)}, indent=1))
    median_ok = bool(perturbed) and abs(statistics.median(perturbed) - PERTURBED) <= TOLERANCE
    ok = (rate >= OPEN_RATE and not self_fail and not perturb_fail and median_ok
          and len(present) == len(rows))
    if len(present) != len(rows):
        print(f"FAIL: {len(rows) - len(present)} manifest files are absent; run `python dataset/tools/fetch.py`",
              file=sys.stderr)
    return 0 if ok else 1


def text_run(run_id: str, rows: list[dict], workers: int) -> int:
    run_dir = common.reports_dir() / run_id
    outcomes = run_dir / "outcomes"
    if not outcomes.exists():
        raise common.ToolError(f"{outcomes} does not exist", "a dataset run's outcomes",
                               "run `python tests/dataset/run.py` first and pass its run id")
    scored = []
    with pdfium.Pool(workers) as pool, concurrent.futures.ThreadPoolExecutor(workers) as threads:
        jobs = {}
        for row in rows:
            path = outcomes / f"{row['id']}.json"
            if not path.exists() or row["expect_text"] == "n/a":
                continue
            outcome = json.loads(path.read_text(encoding="utf-8"))
            text = outcome.get("text") or {}
            if text.get("status") != "ok":
                continue
            jobs[row["id"]] = (row, text, threads.submit(pool.probe, common.file_path(row), ["text"]))
        for row_id, (row, text, job) in jobs.items():
            probe = job.result()
            if not probe.get("ok"):
                scored.append((row, None, 0, f"pdfium:{probe.get('error')}"))
                continue
            unreadable = sum(p.get("unreadable", 0) for p in text.get("pages", []))
            score = words.f1(text.get("text", ""), "\n".join(page_texts(probe)))
            scored.append((row, score, unreadable, row["expect_text"]))
    lines = ["id\tset\tpages\tf1\tunreadable\tnotes"]
    per_set = collections.defaultdict(list)
    for row, score, unreadable, note in scored:
        lines.append(f"{row['id']}\t{row['set']}\t{row['pages']}\t{'' if score is None else f'{score:.4f}'}"
                     f"\t{unreadable}\t{note}")
        if score is not None and row["expect_text"] == "agree":
            per_set[row["set"]].append(score)
    (run_dir / "pdfium-text.tsv").write_text("\n".join(lines) + "\n", encoding="utf-8")
    every = [s for scores in per_set.values() for s in scores]
    summary = {
        "files": len(every),
        "median": round(statistics.median(every), 4) if every else None,
        "per_set_median": {k: round(statistics.median(v), 4) for k, v in sorted(per_set.items())},
        "under_0_90": sorted(r["id"] for r, s, _, _ in scored if s is not None and s < AGREE
                             and r["expect_text"] == "agree"),
    }
    (run_dir / "pdfium-text.json").write_text(json.dumps(summary, indent=1), encoding="utf-8")
    print(json.dumps(summary | {"under_0_90": len(summary["under_0_90"])}, indent=1))
    return 0


def render_pair(before: Path, after: Path, rectangles: list, out_dir: Path, pool) -> dict:
    """Pages of `before` and `after` must match outside the rectangles (page, x0, y0, x1, y1 in points)."""
    a = pool.probe(before, ["render"], render={"out": str(out_dir / "before"), "dpi": 72})
    b = pool.probe(after, ["render"], render={"out": str(out_dir / "after"), "dpi": 72})
    if not a.get("ok") or not b.get("ok"):
        return {"stable": False, "reason": f"render failed: {a.get('error')} {b.get('error')}"}
    if len(a["rendered"]) != len(b["rendered"]):
        return {"stable": False, "reason": "page counts differ"}
    changed = []
    for index, (pa, pb) in enumerate(zip(a["rendered"], b["rendered"])):
        ha, da = Path(pa).read_bytes().split(b"\n", 1)
        hb, db = Path(pb).read_bytes().split(b"\n", 1)
        if ha != hb:
            changed.append({"page": index + 1, "reason": "page size differs"})
            continue
        width, height, stride = map(int, ha.split())
        boxes = [r[1:] for r in rectangles if r[0] == index + 1]
        outside = 0
        for y in range(height):
            row_a, row_b = da[y * stride:(y + 1) * stride], db[y * stride:(y + 1) * stride]
            if row_a == row_b:
                continue
            for x in range(width):
                if row_a[4 * x:4 * x + 4] == row_b[4 * x:4 * x + 4]:
                    continue
                px, py = x + 0.5, height - y - 0.5
                # One pixel of anti-aliasing is tolerated around each rectangle.
                if any(x0 - 1 <= px <= x1 + 1 and y0 - 1 <= py <= y1 + 1 for x0, y0, x1, y1 in boxes):
                    continue
                outside += 1
        if outside:
            changed.append({"page": index + 1, "pixels_outside": outside})
    return {"stable": not changed, "changed": changed}


def leak_scan(path: Path, strings: list[str], pool) -> list[dict]:
    """Every redacted string still found in PDFium's text or in the file's decoded bytes."""
    import autotag

    hits = []
    probe = pool.probe(path, ["text"])
    folded = [s.casefold() for s in strings]
    for page, text in enumerate(page_texts(probe), start=1):
        lowered = text.casefold()
        for original, needle in zip(strings, folded):
            if needle and needle in lowered:
                hits.append({"string": original, "surface": "pdfium_text", "page": page})
    data = path.read_bytes()
    scan = autotag.Scan(data)
    blobs = [data] + [scan.decoded(o) or b"" for o in scan.objects if o.data is not None]
    for original in strings:
        for encoding in ("utf-8", "utf-16-be", "latin-1"):
            try:
                needle = original.encode(encoding)
            except UnicodeEncodeError:
                continue
            if any(needle in blob for blob in blobs):
                hits.append({"string": original, "surface": f"bytes:{encoding}"})
                break
    return hits


def redaction_run(run_id: str, rows: list[dict], workers: int, render: bool, leak: bool) -> int:
    """Reads redact.input, redact.output, redact.rectangles and redact.strings from each outcome."""
    run_dir = common.reports_dir() / run_id
    results = {}
    with pdfium.Pool(workers) as pool:
        for row in rows:
            path = run_dir / "outcomes" / f"{row['id']}.json"
            if not path.exists():
                continue
            redact = json.loads(path.read_text(encoding="utf-8")).get("redact") or {}
            if redact.get("status") != "ok" or not redact.get("output"):
                continue
            entry = {}
            if render:
                entry["render"] = render_pair(common.file_path(row), Path(redact["output"]),
                                              redact.get("rectangles", []), run_dir / "render" / row["id"], pool)
            if leak:
                entry["leaks"] = leak_scan(Path(redact["output"]), redact.get("strings", []), pool)
            results[row["id"]] = entry
    (run_dir / "pdfium-redaction.json").write_text(json.dumps(results, indent=1), encoding="utf-8")
    unstable = [k for k, v in results.items() if v.get("render", {}).get("stable") is False]
    leaking = [k for k, v in results.items() if v.get("leaks")]
    print(json.dumps({"files": len(results), "unstable": len(unstable), "leaking": len(leaking)}, indent=1))
    if not results:
        print("SKIPPED redaction checks: no outcome of this run carries a redaction output", file=sys.stderr)
    return 1 if unstable or leaking else 0


CHECKS = ("write", "select", "text", "redact")


def load_outcomes(run_id: str, rows: list[dict]) -> tuple[Path, list[tuple[dict, dict]]]:
    """The run folder and the (row, outcome) pairs the run wrote, in manifest order."""
    run_dir = common.reports_dir() / run_id
    outcomes = run_dir / "outcomes"
    if not outcomes.exists():
        raise common.ToolError(f"{outcomes} does not exist", "a dataset run's outcomes",
                               "run `python tests/dataset/run.py` first and pass its run id")
    found = []
    for row in rows:
        path = outcomes / f"{row['id']}.json"
        if path.exists():
            found.append((row, json.loads(path.read_text(encoding="utf-8"))))
    return run_dir, found


def pixel_entry(pool, before: Path, after: Path, pairs_for) -> dict:
    """Renders and compares the page pairs of one file.

    `pairs_for(before_pages)` gives the [before_index, after_index] pairs. The entry
    holds input_opens, output_opens, pages (the pairs asked for), output_pages,
    identical and differing: one {page, source_page, pixels, cause} per pair that is
    not identical, where `page` is the output page, `cause` names a mechanical
    failure (a timeout, a crash, a page-count or size difference, a file that does
    not open) and is null for a plain pixel difference, which the analyst classifies."""
    before_pages, after_pages = render.page_counts(pool, before), render.page_counts(pool, after)
    entry = {"input_opens": before_pages is not None, "output_opens": after_pages is not None,
             "pages": 0, "output_pages": after_pages, "identical": 0, "differing": []}
    if before_pages is None:
        return entry
    pairs = pairs_for(before_pages)
    entry["pages"] = len(pairs)
    if after_pages is None:
        entry["differing"] = [{"page": p[1] + 1, "source_page": p[0] + 1, "pixels": None,
                               "cause": "output_open"} for p in pairs]
        return entry
    comparable = [p for p in pairs if p[1] < after_pages]
    entry["differing"] = [{"page": p[1] + 1, "source_page": p[0] + 1, "pixels": None,
                           "cause": "page_count"} for p in pairs if p[1] >= after_pages]
    result = render.compare_pages(pool, before, after, comparable) if comparable else {"pairs": []}
    for pair, answer in zip(comparable, result["pairs"]):
        if answer["identical"]:
            entry["identical"] += 1
            continue
        cause = answer["error"] or ("size_differs" if answer["size_differs"] else None)
        entry["differing"].append({"page": pair[1] + 1, "source_page": pair[0] + 1,
                                   "pixels": answer["differing_pixels"], "cause": cause})
    entry["differing"].sort(key=lambda d: d["page"])
    return entry


def pixel_summary(entries: dict) -> dict:
    """The totals of a pixel check: the files PDFium opens as input, of those the outputs it opens, the pages."""
    opened = [e for e in entries.values() if e["input_opens"]]
    outputs = [e for e in opened if e["output_opens"]]
    pages = sum(e["pages"] for e in opened)
    identical = sum(e["identical"] for e in opened)
    return {"files": len(entries), "input_opens": len(opened), "output_opens": len(outputs),
            "output_open_share": round(len(outputs) / len(opened), 6) if opened else None,
            "pages_compared": pages, "pages_identical": identical,
            "share": round(identical / pages, 6) if pages else None}


def write_pairs(count: int) -> list:
    return [[i, i] for i in range(count)]


def pixel_check(run_id: str, rows: list[dict], workers: int, kind: str) -> int:
    """`write`: every page of an identity write against the input page; `select`: output page i
    against source page select.pages[i-1]. Outputs are named relative to the run folder."""
    run_dir, found = load_outcomes(run_id, rows)
    jobs = []
    for row, outcome in found:
        part = outcome.get(kind) or {}
        if part.get("status") == "ok" and part.get("output"):
            jobs.append((row, part))
    entries = {}
    with render.render_pool(workers) as pool, concurrent.futures.ThreadPoolExecutor(workers) as threads:
        futures = {}
        for row, part in jobs:
            before, after = common.file_path(row), run_dir / part["output"]
            kept = []
            if kind == "write":
                pairs_for = write_pairs
            else:
                kept = [int(p) for p in part.get("pages") or []]

                def pairs_for(count, kept=kept):
                    return [[page - 1, i] for i, page in enumerate(kept)]
            futures[row["id"]] = (kept, threads.submit(pixel_entry, pool, before, after, pairs_for))
        for row_id, (kept, future) in futures.items():
            entry = future.result()
            if kind == "select":
                entry["pages_right"] = entry["output_pages"] == len(kept)
            entries[row_id] = entry
    summary = pixel_summary(entries)
    if kind == "select":
        opened = [e for e in entries.values() if e["input_opens"]]
        summary["page_count_files"] = len(opened)
        summary["page_count_right"] = sum(1 for e in opened if e["pages_right"])
    (run_dir / f"pdfium-{kind}.json").write_text(
        json.dumps({"files": dict(sorted(entries.items())), "summary": summary}, indent=1), encoding="utf-8")
    print(json.dumps({"check": kind} | summary, indent=1))
    if not entries:
        print(f"SKIPPED {kind} check: no outcome of this run carries a {kind} output", file=sys.stderr)
    return 0


def rendered_box(box, crop, rotate: int) -> list:
    """A rectangle of default user space in the space of the rendered page: the crop box's
    lower-left corner at the origin, then turned clockwise by /Rotate (ISO 32000-1 table 30,
    read), as a renderer draws the page."""
    x0, y0, x1, y1 = box
    cx0, cy0, cx1, cy1 = crop
    width, height = cx1 - cx0, cy1 - cy0

    def turn(u: float, v: float) -> tuple[float, float]:
        quarter = int(rotate) % 360
        if quarter == 90:
            return v, width - u
        if quarter == 180:
            return width - u, height - v
        if quarter == 270:
            return height - v, u
        return u, v

    a, b = turn(x0 - cx0, y0 - cy0)
    c, d = turn(x1 - cx0, y1 - cy0)
    return [min(a, c), min(b, d), max(a, c), max(b, d)]


def redact_entry(pool, before: Path, after: Path, part: dict) -> dict:
    """One text redaction: every page against its input outside the reported rectangles, and the
    placeholders PDFium reads on each page against those pdfkit drew there."""
    geometry = {int(row[0]): ([float(v) for v in row[1:5]], int(row[5])) for row in part.get("geometry") or []}
    boxes: dict = {}
    for row in part.get("rectangles") or []:
        page = int(row[0])
        box = [float(v) for v in row[1:5]]
        if page in geometry:
            box = rendered_box(box, *geometry[page])
        boxes.setdefault(page, []).append(box)
    drawn = {int(page): int(count) for page, count in part.get("placeholders") or []}
    before_pages = render.page_counts(pool, before)
    entry = {"input_opens": before_pages is not None, "pages": 0, "stable": 0, "unstable": [],
             "placeholders_drawn": sum(drawn.values()), "placeholders_found": 0, "placeholder_pages": []}
    if before_pages is None:
        return entry
    pairs = [[i, i] for i in range(before_pages)]
    rectangles = [boxes.get(i + 1, []) for i in range(before_pages)]
    result = render.compare_pages(pool, before, after, pairs, rectangles)
    entry["pages"] = len(pairs)
    for pair, answer in zip(pairs, result["pairs"]):
        if answer.get("stable"):
            entry["stable"] += 1
        else:
            entry["unstable"].append({"page": pair[0] + 1, "outside_pixels": answer.get("outside_pixels"),
                                      "cause": answer.get("error") or ("size_differs" if answer.get("size_differs")
                                                                       else None)})
    probe = pool.probe(after, ["text"])
    texts = page_texts(probe) if probe.get("ok") else []
    for page, count in sorted(drawn.items()):
        text = texts[page - 1] if page - 1 < len(texts) else ""
        seen = min(count, text.count("[REDACTED]"))
        entry["placeholders_found"] += seen
        if seen < count:
            entry["placeholder_pages"].append({"page": page, "drawn": count, "found": seen})
    return entry


def redact_check(run_id: str, rows: list[dict], workers: int) -> int:
    """Pixel stability outside the reported rectangles of every text redaction
    (pdfium-redact.json) and every image treatment (pdfium-redact-images.json)
    output of a run, and the placeholder count of the text redactions."""
    redact_member_check(run_id, rows, workers, "redact_text", "pdfium-redact.json")
    redact_member_check(run_id, rows, workers, "redact_images", "pdfium-redact-images.json")
    return 0


def redact_member_check(run_id: str, rows: list[dict], workers: int, member: str, report: str) -> int:
    """The redact check over the outputs of one outcome member."""
    run_dir, found = load_outcomes(run_id, rows)
    jobs = [(row, outcome[member]) for row, outcome in found
            if (outcome.get(member) or {}).get("status") == "ok" and outcome[member].get("output")]
    entries = {}
    with render.render_pool(workers) as pool, concurrent.futures.ThreadPoolExecutor(workers) as threads:
        futures = {row["id"]: threads.submit(redact_entry, pool, common.file_path(row), run_dir / part["output"],
                                             part) for row, part in jobs}
        for row_id, future in futures.items():
            entries[row_id] = future.result()
    opened = [e for e in entries.values() if e["input_opens"]]
    pages = sum(e["pages"] for e in opened)
    stable = sum(e["stable"] for e in opened)
    drawn = sum(e["placeholders_drawn"] for e in opened)
    seen = sum(e["placeholders_found"] for e in opened)
    summary = {"files": len(entries), "input_opens": len(opened), "pages_compared": pages,
               "pages_stable": stable, "stable_share": round(stable / pages, 6) if pages else None,
               "placeholders_drawn": drawn, "placeholders_found": seen,
               "placeholder_share": round(seen / drawn, 6) if drawn else None,
               "unstable_files": sorted(k for k, e in entries.items() if e["unstable"]),
               "placeholder_files": sorted(k for k, e in entries.items() if e["placeholder_pages"])}
    (run_dir / report).write_text(
        json.dumps({"files": dict(sorted(entries.items())), "summary": summary}, indent=1), encoding="utf-8")
    print(json.dumps({"check": "redact", "member": member} | summary
                     | {"unstable_files": len(summary["unstable_files"]),
                        "placeholder_files": len(summary["placeholder_files"])}, indent=1))
    if not entries:
        print(f"SKIPPED redact check: no outcome of this run carries a {member} output", file=sys.stderr)
    return 0


def write_text_check(run_id: str, rows: list[dict], workers: int) -> int:
    """PDFium's text of each identity-write output against its text of the input (word-bag F1)."""
    run_dir, found = load_outcomes(run_id, rows)
    jobs = [(row, outcome) for row, outcome in found
            if (outcome.get("write") or {}).get("status") == "ok" and outcome["write"].get("output")]
    entries = {}
    with pdfium.Pool(workers) as pool, concurrent.futures.ThreadPoolExecutor(workers) as threads:
        futures = {}
        for row, outcome in jobs:
            futures[row["id"]] = (threads.submit(pool.probe, common.file_path(row), ["text"]),
                                  threads.submit(pool.probe, run_dir / outcome["write"]["output"], ["text"]))
        for row_id, (before, after) in futures.items():
            a, b = before.result(), after.result()
            entry = {"input_opens": bool(a.get("ok")), "output_opens": bool(b.get("ok")), "f1": None}
            if a.get("ok"):
                entry["f1"] = (round(words.f1("\n".join(page_texts(a)), "\n".join(page_texts(b))), 6)
                               if b.get("ok") else 0.0)
            entries[row_id] = entry
    scored = [e for e in entries.values() if e["input_opens"]]
    perfect = sum(1 for e in scored if e["f1"] == 1.0)
    summary = {"files": len(scored), "f1_one": perfect,
               "share": round(perfect / len(scored), 6) if scored else None,
               "under_one": sorted(k for k, e in entries.items() if e["input_opens"] and e["f1"] != 1.0)}
    (run_dir / "pdfium-write-text.json").write_text(
        json.dumps({"files": dict(sorted(entries.items())), "summary": summary}, indent=1), encoding="utf-8")
    print(json.dumps({"check": "text"} | summary | {"under_one": len(summary["under_one"])}, indent=1))
    implemented = any((outcome.get("text") or {}).get("status") == "ok" for _, outcome in found)
    if not implemented:
        print("SKIPPED pdfkit text half: pdfkit_text is not implemented in this build")
    else:
        print("SKIPPED pdfkit text half: an outcome holds pdfkit's text of the input only; "
              "the output's text needs the dataset verb to report it")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Compare pdfkit with PDFium over the dataset.")
    parser.add_argument("--check", help="the writer checks to run on --run: a comma-separated subset of "
                        + ", ".join(CHECKS))
    parser.add_argument("--run", help="the dataset run id to compare")
    parser.add_argument("--self-test", action="store_true", help="prove the tier with PDFium alone")
    parser.add_argument("--render", action="store_true", help="pixel stability of the run's redactions")
    parser.add_argument("--leak", action="store_true", help="search the run's redaction outputs for the strings")
    parser.add_argument("--set", action="append", help="only these sets")
    parser.add_argument("--workers", type=int, default=8)
    args = parser.parse_args()
    try:
        pdfium.default_dll()
        if not pdfium.default_dll().exists():
            raise FileNotFoundError
    except (FileNotFoundError, common.ToolError):
        print("SKIPPED pdfium tier: PDFium is absent; run `python tests/pdfium/fetch.py`")
        return 0
    rows = common.read_manifest()
    if args.set:
        rows = [r for r in rows if r["set"] in args.set]
    if args.self_test:
        return self_test(rows, args.workers, common.reports_dir() / (args.run or common.run_id()))
    if not args.run:
        raise common.ToolError("no --run and no --self-test", "one of them",
                               "pass --run <run-id> of a dataset run, or --self-test")
    if args.check:
        checks = [c.strip() for c in args.check.split(",") if c.strip()]
        unknown = [c for c in checks if c not in CHECKS]
        if unknown or not checks:
            raise common.ToolError(f"--check names {unknown or 'nothing'}", "a subset of " + ", ".join(CHECKS),
                                   "pass --check write,select,text")
        for check in checks:
            if check == "text":
                write_text_check(args.run, rows, args.workers)
            elif check == "redact":
                redact_check(args.run, rows, args.workers)
            else:
                pixel_check(args.run, rows, args.workers, check)
        return 0
    if args.render or args.leak:
        return redaction_run(args.run, rows, args.workers, args.render, args.leak)
    return text_run(args.run, rows, args.workers)


if __name__ == "__main__":
    common.run_tool(main)
