# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# Writes every synthesised pattern and registers the files. Each pattern module
# under synth/patterns/ holds PATTERNS, a table from pattern name to a function
# returning specimens: the bytes, the tags the pattern stands for, the text the
# page draws and the text pdfkit's decoding ladder will report (they differ only
# where the file lies, as a wrong ToUnicode does). The expected text agreement
# is measured here: PDFium reads each specimen, and a specimen whose PDFium text
# scores under 0.90 F1 against the ladder's text expects `known_gap`, naming the
# score, because that is the disagreement the reading gate will meet. The
# writer is deterministic, so a second run writes the same bytes and registers
# nothing new.

from __future__ import annotations

import argparse
import importlib
import json
import pkgutil
import sys
from dataclasses import dataclass, field
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))
sys.path.insert(0, str(TOOLS.parents[1] / "tests" / "pdfium"))

import common  # noqa: E402

AGREE_F1 = 0.90


@dataclass
class Specimen:
    data: bytes
    tags: list[str]
    text: str = ""
    ladder_text: str | None = None
    notes: str = ""
    overrides: dict = field(default_factory=dict)
    drop_tags: list[str] = field(default_factory=list)


def pattern_table() -> dict:
    import synth.patterns as package

    table = {}
    for info in sorted(pkgutil.iter_modules(package.__path__), key=lambda i: i.name):
        module = importlib.import_module(f"synth.patterns.{info.name}")
        for name, fn in getattr(module, "PATTERNS", {}).items():
            if name in table:
                raise common.ToolError(f"pattern {name} is defined twice", "unique pattern names",
                                       f"rename one in synth/patterns/{info.name}.py")
            table[name] = fn
    return table


def staging_dir() -> Path:
    return common.cache_dir() / "synth-staging"


def restore(rows: list[dict], planned) -> int:
    """Writes back the files of registered rows that are absent, from the same bytes."""
    by_hash = {common.sha256_bytes(s.data): s.data for _, _, s in planned}
    count = 0
    for row in rows:
        target = common.file_path(row)
        if row["set"] == "synth" and not target.exists() and row["sha256"] in by_hash:
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(by_hash[row["sha256"]])
            count += 1
    return count


def main() -> int:
    import add
    import pdfium
    import words

    parser = argparse.ArgumentParser(description="Write and register the synthesised files.")
    parser.add_argument("--pattern", action="append", help="only these patterns")
    parser.add_argument("--list", action="store_true", help="list the patterns and exit")
    parser.add_argument("--dry-run", action="store_true", help="write the files, register nothing")
    parser.add_argument("--check", action="store_true",
                        help="fail when a registered synthesised row is not reproduced")
    parser.add_argument("--prune", action="store_true",
                        help="remove the rows of the chosen patterns whose bytes the generator does not write")
    args = parser.parse_args()

    table = pattern_table()
    if args.list:
        print("\n".join(sorted(table)))
        return 0
    chosen = sorted(args.pattern or table)
    unknown = [p for p in chosen if p not in table]
    if unknown:
        raise common.ToolError(f"unknown patterns {unknown}", f"one of {sorted(table)}",
                               "run with --list")
    staging = staging_dir()
    planned: list[tuple[Path, str, Specimen]] = []
    for name in chosen:
        for index, specimen in enumerate(table[name]()):
            path = staging / name / f"{name}-{index + 1:02d}.pdf"
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(specimen.data)
            planned.append((path, name, specimen))
    rows = common.read_manifest()
    written = {common.sha256_bytes(s.data) for _, _, s in planned}
    stale = [r for r in rows if r["set"] == "synth" and r["source"].split(":", 1)[1] in chosen
             and r["sha256"] not in written]
    if stale and args.check:
        for row in stale:
            print(f"{row['id']}: the generator does not reproduce these bytes ({row['source']})")
        return 1
    if stale and args.prune and not args.dry_run:
        for row in stale:
            common.file_path(row).unlink(missing_ok=True)
            print(f"pruned {row['id']} ({row['source']})", file=sys.stderr)
        rows = [r for r in rows if r not in stale]
        common.write_manifest(rows)
    restored = restore(rows, planned)
    if args.check:
        print(json.dumps({"patterns": len(chosen), "specimens": len(planned), "reproduced": True,
                          "restored": restored}))
        return 0
    known = {r["sha256"] for r in rows}
    summary = []
    with pdfium.Pool(8) as pool:
        paths = [p for p, _, _ in planned]
        analysis = add.analyse(paths, pool)
        per_file = {}
        for path, name, specimen in planned:
            probe = pool.probe(path, ["info", "text"])
            ladder = specimen.text if specimen.ladder_text is None else specimen.ladder_text
            overrides = dict(specimen.overrides)
            score = None
            if "expect_text" not in overrides:
                if not ladder.strip():
                    overrides["expect_text"] = "n/a"
                elif not probe.get("ok"):
                    overrides["expect_text"] = "n/a"
                else:
                    score = words.f1("\n".join(t or "" for t in probe["text"]), ladder)
                    overrides["expect_text"] = "agree" if score >= AGREE_F1 else f"known_gap:pdfium_f1_{score:.2f}"
            text_note = " ".join(specimen.text.split())[:80]
            notes = "; ".join(n for n in [specimen.notes, f"text: {text_note}" if text_note else ""] if n)
            per_file[path] = {"tags": specimen.tags, "drop_tags": specimen.drop_tags,
                              "overrides": overrides, "notes": notes, "source": f"synth:{name}"}
            summary.append({"file": path.name, "pdfium": probe.get("ok"), "f1": score,
                            "new": analysis[str(path)]["sha256"] not in known})
        if not args.dry_run:
            added = add.register(paths, "synth", "Apache-2.0", lambda p: "", pool, rows=rows,
                                 analysis=analysis, per_file=per_file)
            common.write_manifest(rows)
            print(f"registered {len(added)} synthesised files", file=sys.stderr)
    print(json.dumps(summary, indent=1))
    return 0


if __name__ == "__main__":
    common.run_tool(main)
