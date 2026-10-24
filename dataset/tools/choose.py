# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# Chooses which files of a fetched public set enter the dataset and registers
# them through add.py. The `select` cell of the set's row in sources.tsv names
# the rule: `take=<n>|all`, `prefer=<groups>`, `max_mb=<size>`, `seed=<n>`,
# `random` (a plain seeded sample, for the wild sample). A curated set is chosen
# greedily for coverage: each pick is the candidate whose tags fill the most
# taxonomy tags still under their minimum, tags of a preferred group counting
# double, ties broken by a seeded order, so the same cache gives the same pick.
# A file PDFium cannot open is admitted only when the scan names a damage or an
# encryption that explains it, because an unexplained refusal is an
# expectation nobody can state.

from __future__ import annotations

import argparse
import json
import random
import sys
import zipfile
from pathlib import Path
from urllib.parse import quote

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tests" / "pdfium"))

import add  # noqa: E402
import common  # noqa: E402
import fetch  # noqa: E402

DEFAULT_SEED = 20261024


def parse_rule(text: str) -> dict:
    rule = {"take": "all", "prefer": [], "max_mb": 20.0, "seed": DEFAULT_SEED, "random": False}
    for part in text.split():
        key, _, value = part.partition("=")
        if key == "take":
            rule["take"] = value
        elif key == "prefer":
            rule["prefer"] = [v for v in value.split(",") if v]
        elif key == "max_mb":
            rule["max_mb"] = float(value)
        elif key == "seed":
            rule["seed"] = int(value)
        elif key == "random":
            rule["random"] = True
    return rule


def git_candidates(source: dict) -> list[tuple[Path, str]]:
    """(path, source URL) of every PDF in the set's sparse folders."""
    repo = fetch.set_cache(source["set"]) / "repo"
    _, paths = fetch.parse_fetch(source["fetch"])
    include = [p for p in paths if not p.startswith("!")] or [""]
    exclude = [p[1:].strip("/") for p in paths if p.startswith("!")]
    out = []
    for folder in include:
        base = repo / folder if folder else repo
        for path in sorted(base.rglob("*")):
            if path.suffix.lower() != ".pdf" or not path.is_file() or path.is_symlink():
                continue
            rel = path.relative_to(repo).as_posix()
            if rel.startswith(".git/") or any(rel.startswith(e + "/") for e in exclude):
                continue
            if "googlesource.com" in source["url"]:
                url = f"{source['url']}/+/{source['pin']}/{quote(rel)}"
            else:
                url = f"{source['url']}/blob/{source['pin']}/{quote(rel)}"
            out.append((path, url))
    return out


def archive_candidates(source: dict, rule: dict) -> list[tuple[Path, str]]:
    """Members of a zip set, extracted under cache/<set>/extracted/."""
    archive = fetch.set_cache(source["set"]) / fetch.archive_name(source)
    out_dir = fetch.set_cache(source["set"]) / "extracted"
    limit = rule["max_mb"] * 1024 * 1024
    with zipfile.ZipFile(archive) as zf:
        members = sorted(i.filename for i in zf.infolist()
                         if i.filename.lower().endswith(".pdf") and 0 < i.file_size <= limit)
        if rule["random"] and rule["take"] != "all":
            members = sorted(random.Random(rule["seed"]).sample(members, min(len(members), int(rule["take"]) * 2)))
        out = []
        for name in members:
            target = out_dir / name
            if not target.exists():
                target.parent.mkdir(parents=True, exist_ok=True)
                with zf.open(name) as src:
                    target.write_bytes(src.read())
            out.append((target, f"{source['url']}#{name}"))
    return out


def groups_of(taxonomy: list[dict]) -> dict[str, str]:
    return {t["tag"]: t["group"] for t in taxonomy}


def choose(candidates: list[tuple[Path, str]], analysis: dict, rule: dict, rows: list[dict],
           taxonomy: list[dict]) -> tuple[list[tuple[Path, str]], dict]:
    group = groups_of(taxonomy)
    minimum = {t["tag"]: int(t["minimum"]) for t in taxonomy}
    counts: dict[str, int] = {}
    for row in rows:
        for tag in common.split_tags(row["tags"]):
            counts[tag] = counts.get(tag, 0) + 1
    known = {r["sha256"] for r in rows}
    limit = rule["max_mb"] * 1024 * 1024
    eligible, skipped = [], {"duplicate": 0, "too_large": 0, "pdfium_unexplained": 0, "hang_or_crash": 0}
    seen = set()
    for path, url in candidates:
        entry = analysis[str(path)]
        if entry["sha256"] in known or entry["sha256"] in seen:
            skipped["duplicate"] += 1
            continue
        if entry["bytes"] > limit:
            skipped["too_large"] += 1
            continue
        probe = entry.get("pdfium", {})
        if probe.get("error") in ("timeout", "crash", "worker"):
            skipped["hang_or_crash"] += 1
            continue
        if not probe.get("ok"):
            explained = any(group.get(t) in ("damage", "encryption") for t in entry["tags"])
            if not explained:
                skipped["pdfium_unexplained"] += 1
                continue
        seen.add(entry["sha256"])
        eligible.append((path, url))
    order = list(eligible)
    random.Random(rule["seed"]).shuffle(order)
    if rule["take"] == "all":
        return sorted(order, key=lambda c: str(c[0])), skipped
    take = int(rule["take"])
    if rule["random"]:
        return sorted(order[:take], key=lambda c: str(c[0])), skipped
    chosen = []
    remaining = list(order)
    while remaining and len(chosen) < take:
        best, best_score = None, -1.0
        for index, (path, _) in enumerate(remaining):
            score = 0.0
            for tag in analysis[str(path)]["tags"]:
                need = minimum.get(tag, 0) - counts.get(tag, 0)
                weight = 2.0 if group.get(tag) in rule["prefer"] else 1.0
                score += weight * (3.0 if need > 0 else 0.02)
            if score > best_score:
                best, best_score = index, score
        path, url = remaining.pop(best)
        chosen.append((path, url))
        for tag in analysis[str(path)]["tags"]:
            counts[tag] = counts.get(tag, 0) + 1
    return sorted(chosen, key=lambda c: str(c[0])), skipped


def main() -> int:
    import pdfium  # noqa: E402

    parser = argparse.ArgumentParser(description="Select and register the files of fetched sets.")
    parser.add_argument("--set", action="append", required=True)
    parser.add_argument("--dry-run", action="store_true", help="print the choice, register nothing")
    parser.add_argument("--workers", type=int, default=8)
    args = parser.parse_args()

    sources = {s["set"]: s for s in common.read_sources()}
    taxonomy = common.read_taxonomy()
    rows = common.read_manifest()
    summary = []
    with pdfium.Pool(args.workers) as pool:
        for set_name in args.set:
            source = sources.get(set_name)
            if source is None or source["status"] != "admitted" or not source["url"]:
                raise common.ToolError(
                    f"set {set_name} is not an admitted public set",
                    "a row of sources.tsv with status admitted and a URL",
                    "fix sources.tsv or name another set")
            if any(r["set"] == set_name for r in rows):
                summary.append({"set": set_name, "status": "already registered"})
                continue
            rule = parse_rule(source["select"])
            kind, _ = fetch.parse_fetch(source["fetch"])
            candidates = git_candidates(source) if kind == "git" else archive_candidates(source, rule)
            cache = fetch.set_cache(set_name) / "analysis.json"
            analysis = add.analyse([p for p, _ in candidates], pool, args.workers, cache)
            chosen, skipped = choose(candidates, analysis, rule, rows, taxonomy)
            urls = dict(chosen)
            if args.dry_run:
                summary.append({"set": set_name, "candidates": len(candidates), "chosen": len(chosen),
                                "skipped": skipped})
                continue
            added = add.register([p for p, _ in chosen], set_name, source["licence"],
                                 lambda p: urls[p], pool, rows=rows, analysis=analysis)
            common.write_manifest(rows)
            summary.append({"set": set_name, "candidates": len(candidates), "added": len(added),
                            "skipped": skipped})
    print(json.dumps(summary, indent=1))
    return 0


if __name__ == "__main__":
    common.run_tool(main)
