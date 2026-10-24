# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# Checks the vendored components (CLAUDE.md section 6): every folder under
# third_party/ has a row in third_party/inventory.tsv and a licence file, its
# licence is on the permissive list, THIRD-PARTY-NOTICES.md names it, and its
# bytes are the ones recorded. The tree hash is always checked, because it
# catches a local edit to a vendored file; the archive hash is checked too
# when the upstream archive is in build/cache/third_party/ of the main
# checkout. Run with --print-tree-hash <folder> to compute a tree hash.
"""Check the vendored components: python build/check_third_party.py [--root <tree>]."""

from __future__ import annotations

import hashlib
import re
from pathlib import Path

import checklib

COLUMNS = ["path", "name", "version", "upstream", "archive", "archive_sha256", "tree_sha256",
           "licence", "test_only"]
# The licences of CLAUDE.md section 6, as SPDX identifiers. LicenseRef-Adobe-AFM
# is the notice Adobe's Core 14 AFM files carry.
ALLOWED_LICENCES = {"MIT", "BSD-2-Clause", "BSD-3-Clause", "Apache-2.0", "Zlib", "IJG",
                    "Unicode-3.0", "OFL-1.1", "LicenseRef-Adobe-AFM",
                    "Apache-2.0 WITH LLVM-exception"}
LICENCE_FILE = re.compile(r"^(LICEN[CS]E|COPYING|OFL|NOTICE)", re.IGNORECASE)
# Our own build glue inside a vendored folder; it is not part of the upstream tree.
GLUE = {"CMakeLists.txt"}


def tree_sha256(folder: Path) -> str:
    """SHA-256 over every vendored file of folder: for each file in sorted
    POSIX-path order, the line '<path>\\0<sha256 of its bytes>\\n'."""
    digest = hashlib.sha256()
    files = sorted(p for p in folder.rglob("*") if p.is_file() and p.name not in GLUE)
    for path in files:
        relative = path.relative_to(folder).as_posix()
        digest.update(f"{relative}\0{hashlib.sha256(path.read_bytes()).hexdigest()}\n".encode())
    return digest.hexdigest()


def licence_allowed(licence: str) -> bool:
    parts = re.split(r"\s+(?:AND|OR)\s+", licence.strip("() "))
    return bool(parts) and all(part.strip("() ") in ALLOWED_LICENCES for part in parts)


def finding(path: str, line: int, failed: str, expected: str, fix: str) -> checklib.Finding:
    return checklib.Finding(path, line, failed, expected, fix)


def check(root: Path) -> list[checklib.Finding]:
    findings: list[checklib.Finding] = []
    inventory = root / "third_party" / "inventory.tsv"
    folders = sorted(p.name for p in (root / "third_party").iterdir() if p.is_dir()) \
        if (root / "third_party").is_dir() else []
    if not inventory.is_file():
        if folders:
            findings.append(finding("third_party/inventory.tsv", 0, "the inventory is missing",
                                    "one row per vendored component", "add the inventory"))
        return findings
    lines = inventory.read_text(encoding="utf-8").splitlines()
    rows: dict[str, tuple[int, dict[str, str]]] = {}
    header_seen = False
    for number, line in enumerate(lines, start=1):
        if not line.strip() or line.startswith("#"):
            continue
        fields = line.split("\t")
        if not header_seen:
            header_seen = True
            if fields != COLUMNS:
                findings.append(finding("third_party/inventory.tsv", number,
                                        "the header row is not the inventory's columns",
                                        "\t".join(COLUMNS), "restore the header row"))
                return findings
            continue
        if len(fields) != len(COLUMNS):
            findings.append(finding("third_party/inventory.tsv", number,
                                    f"the row has {len(fields)} fields", f"{len(COLUMNS)} fields "
                                    "separated by tabs", "fill every column; use - for none"))
            continue
        row = dict(zip(COLUMNS, fields))
        rows[row["path"]] = (number, row)

    notices_path = root / "THIRD-PARTY-NOTICES.md"
    notices = notices_path.read_text(encoding="utf-8").lower() if notices_path.is_file() else ""
    cache = checklib.main_checkout(root) / "build" / "cache" / "third_party"
    for folder in folders:
        path = f"third_party/{folder}"
        if path not in rows:
            findings.append(finding(path, 0, f"{path}/ has no row in third_party/inventory.tsv",
                                    "a row per vendored component with version, upstream, "
                                    "licence and hashes", "add the row (CLAUDE.md section 6)"))
    for path, (number, row) in sorted(rows.items()):
        folder = root / path
        where = "third_party/inventory.tsv"
        if not folder.is_dir():
            findings.append(finding(where, number, f"{path} does not exist",
                                    "a row per folder under third_party/",
                                    "remove the row or restore the folder"))
            continue
        if not any(LICENCE_FILE.match(p.name) for p in folder.iterdir() if p.is_file()):
            findings.append(finding(path, 0, "the component has no licence file",
                                    "the upstream LICENSE, COPYING or OFL file in the folder",
                                    "copy the upstream licence file into the folder"))
        if not licence_allowed(row["licence"]):
            findings.append(finding(where, number, f"the licence {row['licence']} is not allowed",
                                    "one of " + ", ".join(sorted(ALLOWED_LICENCES)),
                                    "choose a permissively licensed component (CLAUDE.md "
                                    "section 6)"))
        if row["name"].lower() not in notices:
            findings.append(finding("THIRD-PARTY-NOTICES.md", 0,
                                    f"{row['name']} is not named in the notices",
                                    "a section naming each vendored component",
                                    "add its section to THIRD-PARTY-NOTICES.md"))
        actual_tree = tree_sha256(folder)
        if actual_tree != row["tree_sha256"]:
            findings.append(finding(where, number,
                                    f"the tree hash of {path} is {actual_tree}",
                                    f"the recorded tree hash {row['tree_sha256']}",
                                    "restore the vendored files from the upstream archive, or "
                                    "re-vendor and record the new hashes"))
        archive = cache / row["archive"]
        if row["archive"] != "-" and archive.is_file():
            actual = hashlib.sha256(archive.read_bytes()).hexdigest()
            if actual != row["archive_sha256"]:
                findings.append(finding(where, number,
                                        f"the cached archive {row['archive']} hashes to {actual}",
                                        f"the recorded archive hash {row['archive_sha256']}",
                                        "fetch the archive again from the upstream URL"))
    return findings


def main() -> int:
    args = checklib.parse_args(__doc__, [("--print-tree-hash", {"metavar": "FOLDER"})])
    if args.print_tree_hash:
        print(tree_sha256(Path(args.print_tree_hash)))
        return 0
    return checklib.report("check_third_party", check(Path(args.root)))


if __name__ == "__main__":
    checklib.run_main(main)
