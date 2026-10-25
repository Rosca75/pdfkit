# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# Registers files in the dataset: copies each under its id into
# dataset/files/<set>/, hashes it, asks PDFium (in a child process) for the page
# count, the producer and the text, runs autotag.py, and writes the manifest row
# with its expectations. The expectations default to opens, agree, decoded, n/a,
# n/a, and follow from what the scan and PDFium found otherwise: a password is
# refused, a damaged structure is repaired, a file without text or images
# expects n/a. The write expectation is identical for a file pdfkit reads, and
# the select expectation is ok for such a file of two or more pages. A hash
# already in the manifest is refused, naming the existing id, because one file
# under two ids would count twice in every gate.

from __future__ import annotations

import argparse
import concurrent.futures
import json
import os
import shutil
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tests" / "pdfium"))

import autotag  # noqa: E402
import common  # noqa: E402

ANALYSIS_VERSION = "1"
TEXT_SAMPLE = 200_000


def analyse_one(path: str) -> dict:
    """The autotag result of one file (run in a worker process)."""
    data = Path(path).read_bytes()
    try:
        tags, facts = autotag.autotag(data)
    except Exception as err:  # a scanner fault on a hostile file is recorded, not fatal
        tags, facts = [], {"autotag_error": repr(err)[:200]}
    return {"sha256": common.sha256_bytes(data), "bytes": len(data), "tags": tags, "facts": facts}


def analyse(paths: list[Path], pool, workers: int = 8, cache: Path | None = None) -> dict[str, dict]:
    """PDFium and autotag results per path, cached by SHA-256 in `cache`."""
    stored: dict = {}
    if cache and cache.exists():
        stored = json.loads(cache.read_text(encoding="utf-8"))
        if stored.get("version") != ANALYSIS_VERSION:
            stored = {}
    entries = stored.setdefault("entries", {})
    stored["version"] = ANALYSIS_VERSION
    results: dict[str, dict] = {}
    pending = []
    for path in paths:
        key = str(path)
        entry = entries.get(key)
        if entry and entry.get("bytes") == path.stat().st_size and entry.get("mtime") == path.stat().st_mtime:
            results[key] = entry
        else:
            pending.append(path)
    if pending:
        with concurrent.futures.ProcessPoolExecutor(workers) as procs, \
                concurrent.futures.ThreadPoolExecutor(workers) as threads:
            scans = {p: procs.submit(analyse_one, str(p)) for p in pending}
            probes = {p: threads.submit(pool.probe, p, ["info", "text"]) for p in pending}
            for path in pending:
                entry = scans[path].result()
                probe = probes[path].result()
                text = probe.pop("text", None) or []
                joined = "\n".join(t for t in text if t)
                probe["text_chars"] = sum(len(t.strip()) for t in text if t)
                probe["rtl"] = bool(autotag.RTL_RE.search(joined[:TEXT_SAMPLE]))
                entry["pdfium"] = probe
                entry["mtime"] = path.stat().st_mtime
                if probe.get("ok") and probe.get("producer"):
                    entry["facts"]["producer"] = probe["producer"]
                    tags = set(entry["tags"])
                    tags = {t for t in tags if not t.startswith("gen_")}
                    gen = autotag.generator_tag(probe["producer"])
                    if gen:
                        tags.add(gen)
                    entry["tags"] = sorted(tags)
                if probe["rtl"]:
                    entry["tags"] = sorted(set(entry["tags"]) | {"right_to_left"})
                if probe.get("error") == "password":
                    entry["tags"] = sorted(set(entry["tags"]) | {"user_password"})
                entries[str(path)] = entry
                results[str(path)] = entry
        if cache:
            cache.parent.mkdir(parents=True, exist_ok=True)
            tmp = cache.with_suffix(".tmp")
            tmp.write_text(json.dumps(stored), encoding="utf-8")
            os.replace(tmp, cache)
    return results


def expectations(entry: dict) -> tuple[dict, list[str]]:
    """The expectation columns and notes that follow from one analysis."""
    tags = set(entry["tags"])
    facts = entry["facts"]
    probe = entry.get("pdfium", {})
    notes = []
    # expect_fonts stays n/a until tests/pdfium/text_agreement.py --write-manifest
    # measures the new file.
    expect = {"expect_open": "opens", "expect_text": "agree", "expect_images": "decoded",
              "expect_redact": "n/a", "expect_metadata": "n/a", "expect_fonts": "n/a"}
    handler = facts.get("security_handler")
    if probe.get("error") == "password" or "user_password" in tags:
        expect["expect_open"] = "refused:password_required"
    elif probe.get("error") == "security" or (handler and handler != "Standard"):
        expect["expect_open"] = "refused:unsupported_security_handler"
    elif tags & autotag.DAMAGE_REPAIRED:
        expect["expect_open"] = "repaired"
    elif not probe.get("ok"):
        if facts.get("header_offset", -1) < 0 and not facts.get("has_page"):
            expect["expect_open"] = "refused:not_pdf"
        elif not facts.get("has_page"):
            expect["expect_open"] = "refused:no_page"
        else:
            expect["expect_open"] = "repaired"
    if not probe.get("ok"):
        notes.append(f"pdfium:{probe.get('error', 'failed')}")
    refused = expect["expect_open"].startswith("refused:")
    if refused or not probe.get("ok") or probe.get("text_chars", 0) == 0:
        expect["expect_text"] = "n/a"
    if refused or (facts.get("images", 0) == 0 and "inline_image" not in tags):
        expect["expect_images"] = "n/a"
    if facts.get("autotag_error"):
        notes.append("autotag:" + facts["autotag_error"][:80])
    return expect, notes


def register(paths: list[Path], set_name: str, licence: str, source_for, pool,
             extra_tags=(), drop_tags=(), overrides: dict | None = None, notes: str = "",
             rows: list[dict] | None = None, analysis: dict | None = None,
             per_file: dict | None = None) -> list[dict]:
    """Registers files and returns the new rows; writes the manifest when `rows` is None.

    `per_file` maps a path to a dict that may hold `tags`, `drop_tags`, `overrides`,
    `notes`, `source` and `licence` for that file alone.
    """
    own = rows is None
    rows = common.read_manifest() if own else rows
    known = {r["sha256"]: r["id"] for r in rows}
    results = analysis or analyse(paths, pool)
    added = []
    for path in paths:
        entry = results[str(path)]
        extra = (per_file or {}).get(path, {})
        if entry["sha256"] in known:
            print(f"skipped {path}: the same bytes are registered as {known[entry['sha256']]}",
                  file=sys.stderr)
            continue
        tags = set(entry["tags"]) | set(extra_tags) | set(extra.get("tags", ()))
        tags -= set(drop_tags) | set(extra.get("drop_tags", ()))
        expect, auto_notes = expectations({**entry, "tags": sorted(tags)})
        expect.update(overrides or {})
        expect.update(extra.get("overrides", {}))
        row_id = common.next_id(rows, set_name)
        probe = entry.get("pdfium", {})
        # The writer's expectations follow from the open expectation and the page count.
        expect.setdefault("expect_write", common.default_expect_write(expect["expect_open"]))
        expect.setdefault("expect_select", common.default_expect_select(
            expect["expect_open"], str(probe["pages"]) if probe.get("ok") else ""))
        row = {
            "id": row_id, "set": set_name,
            "source": extra.get("source") or source_for(path),
            "licence": extra.get("licence") or licence,
            "sha256": entry["sha256"], "bytes": str(entry["bytes"]),
            "pages": str(probe["pages"]) if probe.get("ok") else "",
            "producer": (probe.get("producer") if probe.get("ok") else "") or entry["facts"].get("producer", ""),
            "tags": common.join_tags(tags),
            **expect,
            "notes": "; ".join(n for n in [notes, extra.get("notes", ""), *auto_notes] if n),
        }
        for column in common.EXPECTATIONS:
            if not common.check_expectation(column, row[column]):
                raise common.ToolError(
                    f"{path}: {column} would be '{row[column]}'",
                    "a value listed in dataset/README.md for that column",
                    "fix the override passed to add.py")
        target = common.file_path(row)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, target)
        rows.append(row)
        known[row["sha256"]] = row_id
        added.append(row)
    if own:
        common.write_manifest(rows)
    return added


def main() -> int:
    import pdfium  # noqa: E402

    parser = argparse.ArgumentParser(description="Register PDF files in the manifest.")
    parser.add_argument("paths", nargs="+", help="files or folders of PDF files")
    parser.add_argument("--set", required=True, help="the set the files belong to")
    parser.add_argument("--licence", required=True, help="SPDX id or terms:<name>")
    parser.add_argument("--source", required=True,
                        help="the source: a URL prefix the file name is appended to, or a "
                             "generator form such as synth:<pattern>")
    parser.add_argument("--tags", default="", help="tags to add, comma-separated")
    parser.add_argument("--drop-tags", default="",
                        help="tags the scan infers that provenance contradicts, comma-separated")
    parser.add_argument("--notes", default="")
    parser.add_argument("--expect", action="append", default=[],
                        help="an expectation override, column=value")
    args = parser.parse_args()

    files: list[Path] = []
    for name in args.paths:
        path = Path(name)
        if path.is_dir():
            files += sorted(p for p in path.rglob("*") if p.suffix.lower() == ".pdf")
        elif path.exists():
            files.append(path)
        else:
            raise common.ToolError(f"{path} does not exist", "a file or folder", "check the path")
    sources = {s["set"] for s in common.read_sources()}
    if args.set not in sources:
        raise common.ToolError(
            f"set {args.set} has no row in sources.tsv", "a registered set",
            "add the set's row to dataset/sources.tsv first")
    overrides = dict(item.split("=", 1) for item in args.expect)

    def source_for(path: Path) -> str:
        if args.source.endswith("/"):
            return args.source + path.name
        return args.source

    with pdfium.Pool(4) as pool:
        added = register(files, args.set, args.licence, source_for, pool,
                         extra_tags=common.split_tags(args.tags),
                         drop_tags=common.split_tags(args.drop_tags), overrides=overrides,
                         notes=args.notes)
    print(json.dumps([{"id": r["id"], "tags": r["tags"], "expect_open": r["expect_open"]}
                      for r in added], indent=1))
    return 0


if __name__ == "__main__":
    common.run_tool(main)
