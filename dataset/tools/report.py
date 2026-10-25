# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# Prints what the dataset holds: the number of rows, the count per taxonomy tag
# against its minimum, the count per set and per licence, and the tags under
# their minimum. It is the dataset's coverage gate: a tag with no file is a
# blind spot, so the tool exits non-zero while a tag is under its minimum
# (unless --allow-under), while the manifest holds fewer rows than --min-rows,
# and, with --licences, while a row has no licence or names a set that
# dataset/sources.tsv does not mark admitted. The tag pages_multi is counted from
# the `pages` column (two or more pages) and never read from the tags column,
# because a page count is a measured fact and a tag written beside it could
# disagree with it.

from __future__ import annotations

import argparse
import collections
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import common  # noqa: E402


MULTI_PAGE_TAG = "pages_multi"


def has_many_pages(row: dict) -> bool:
    """True when PDFium counted two or more pages in the row's file."""
    pages = row.get("pages", "")
    return pages.isdigit() and int(pages) >= 2


def coverage(rows: list[dict], taxonomy: list[dict]) -> list[dict]:
    """One entry per taxonomy tag: its group, minimum, count and status."""
    counts = collections.Counter()
    for row in rows:
        for tag in common.split_tags(row["tags"]):
            counts[tag] += 1
        if has_many_pages(row):
            counts[MULTI_PAGE_TAG] += 1
    out = []
    for entry in taxonomy:
        minimum = int(entry["minimum"])
        count = counts.get(entry["tag"], 0)
        out.append({
            "tag": entry["tag"], "group": entry["group"], "minimum": minimum,
            "count": count, "ok": count >= minimum, "detector": entry["detector"],
        })
    return out


def unknown_tags(rows: list[dict], taxonomy: list[dict]) -> dict[str, list[str]]:
    """Tags used by rows that taxonomy.tsv does not list, with the ids using them."""
    known = {t["tag"] for t in taxonomy}
    found: dict[str, list[str]] = collections.defaultdict(list)
    for row in rows:
        for tag in common.split_tags(row["tags"]):
            if tag not in known:
                found[tag].append(row["id"])
    return dict(found)


def licence_problems(rows: list[dict], sources: list[dict]) -> list[str]:
    admitted = {s["set"] for s in sources if s["status"] == "admitted"}
    problems = []
    for row in rows:
        if not row["licence"]:
            problems.append(f"{row['id']}: no licence recorded")
        if row["set"] not in admitted:
            problems.append(f"{row['id']}: set {row['set']} is not admitted in sources.tsv")
    return problems


def expectation_problems(rows: list[dict]) -> list[str]:
    problems = []
    for row in rows:
        for column in common.EXPECTATIONS:
            if not common.check_expectation(column, row[column]):
                problems.append(f"{row['id']}: {column} holds '{row[column]}'")
    return problems


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--allow-under", action="store_true",
                        help="list the tags under their minimum without failing")
    parser.add_argument("--licences", action="store_true",
                        help="also fail on a row without a licence or outside an admitted set")
    parser.add_argument("--min-rows", type=int, default=300,
                        help="the manifest size the gate requires (default 300)")
    parser.add_argument("--json", action="store_true", help="print JSON instead of tables")
    args = parser.parse_args()

    rows = common.read_manifest()
    taxonomy = common.read_taxonomy()
    sources = common.read_sources()
    if not taxonomy:
        raise common.ToolError(
            f"{common.taxonomy_path()} is missing or empty",
            "the taxonomy table with one row per tag",
            "restore dataset/taxonomy.tsv from git",
        )

    cov = coverage(rows, taxonomy)
    under = [c for c in cov if not c["ok"]]
    share = (len(cov) - len(under)) / len(cov)
    per_set = collections.Counter(r["set"] for r in rows)
    per_licence = collections.Counter(r["licence"] for r in rows)
    unknown = unknown_tags(rows, taxonomy)
    lic = licence_problems(rows, sources) if args.licences else []
    expect = expectation_problems(rows)

    if args.json:
        print(json.dumps({
            "rows": len(rows), "tags": len(cov), "tags_at_minimum": len(cov) - len(under),
            "share_at_minimum": round(share, 4), "under": under, "per_set": dict(per_set),
            "per_licence": dict(per_licence), "unknown_tags": unknown,
            "licence_problems": lic, "expectation_problems": expect,
        }, indent=1, sort_keys=True))
    else:
        common.print_table(
            ["group", "tag", "minimum", "count", "status"],
            [[c["group"], c["tag"], c["minimum"], c["count"], "ok" if c["ok"] else "UNDER"] for c in cov],
        )
        print()
        common.print_table(["set", "files"], sorted(per_set.items()))
        print()
        common.print_table(["licence", "files"], sorted(per_licence.items()))
        print()
        print(f"rows: {len(rows)}")
        print(f"tags at or above minimum: {len(cov) - len(under)} of {len(cov)} ({share:.1%})")
        if under:
            print("under minimum: " + ", ".join(f"{c['tag']} ({c['count']}/{c['minimum']})" for c in under))
        for tag, ids in sorted(unknown.items()):
            print(f"unknown tag {tag} on {len(ids)} rows, first {ids[0]}")
        for problem in lic + expect:
            print(problem)

    failed = False
    if len(rows) < args.min_rows:
        print(f"FAIL: {len(rows)} rows, the gate needs at least {args.min_rows}; "
              "register more files with add.py, mutate.py, synth or generate_office.ps1",
              file=sys.stderr)
        failed = True
    if under and not args.allow_under:
        print(f"FAIL: {len(under)} tags under their minimum; fill them with synth/ or "
              "mutate.py, or rerun with --allow-under to list them", file=sys.stderr)
        failed = True
    if unknown:
        print("FAIL: tags missing from taxonomy.tsv; add a row for each or fix the rows",
              file=sys.stderr)
        failed = True
    if lic or expect:
        print("FAIL: licence or expectation problems listed above; fix the rows in "
              "manifest.tsv or the set's row in sources.tsv", file=sys.stderr)
        failed = True
    return 1 if failed else 0


if __name__ == "__main__":
    common.run_tool(main)
