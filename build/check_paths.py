# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# Checks that every path the authoritative documents cite in backticks
# resolves (CLAUDE.md, docs/*.md and dataset/README.md), so a reader never
# follows a dead reference. A path a later phase creates is listed in
# build/paths_pending.tsv with that phase; a pending row whose path now exists
# fails, so each phase removes its rows and the list only shrinks. Paths git
# ignores (the shared caches and the dataset files) resolve by being ignored.
"""Check the paths cited by the documents: python build/check_paths.py [--root <tree>]."""

from __future__ import annotations

import re
import subprocess
from pathlib import Path

import checklib

TOKEN = re.compile(r"`([^`\n]+)`")
PATH_CHARS = re.compile(r"^[A-Za-z0-9_.\-/]+$")
BARE_SUFFIXES = (".md", ".tsv")
NOT_PATHS = {"n/a"}


def documents(root: Path) -> list[str]:
    docs = ["CLAUDE.md"] + [p for p in checklib.walk(root, ("docs",))
                            if p.endswith(".md") and not p.startswith("docs/archive/")]
    if (root / "dataset" / "README.md").is_file():
        docs.append("dataset/README.md")
    return [d for d in docs if (root / d).is_file()]


def cited_paths(text: str) -> list[tuple[int, str]]:
    found: list[tuple[int, str]] = []
    for m in TOKEN.finditer(text):
        token = m.group(1).strip()
        if token in NOT_PATHS or token.startswith(("/", "-")) or not PATH_CHARS.match(token):
            continue
        if "/" not in token and not token.endswith(BARE_SUFFIXES):
            continue
        found.append((checklib.line_of(text, m.start()), token))
    return found


def ignored_by_git(root: Path, path: str) -> bool:
    try:
        result = subprocess.run(["git", "check-ignore", "-q", "--no-index", path], cwd=root,
                                capture_output=True, check=False)
    except OSError:
        return False
    return result.returncode == 0


def resolves(root: Path, document: str, token: str) -> bool:
    bases = [root, (root / document).parent, root / "src", root / "include"]
    for base in bases:
        candidate = base / token.rstrip("/")
        if candidate.exists():
            return True
        parent = candidate.parent
        if parent.is_dir() and any(p.name.startswith(candidate.name + ".") for p in parent.iterdir()):
            return True
    return False


def check(root: Path) -> list[checklib.Finding]:
    findings: list[checklib.Finding] = []
    pending = checklib.pending_paths(root)
    for document in documents(root):
        text = checklib.read_text(root, document)
        for line, token in cited_paths(text):
            if token in pending:
                continue
            if resolves(root, document, token) or ignored_by_git(root, token):
                continue
            findings.append(checklib.Finding(
                document, line, f"the cited path {token} does not exist",
                "a path that exists, or a row of build/paths_pending.tsv naming the phase that "
                "creates it", "correct the path, or add the pending row"))
    for path, phase in sorted(pending.items()):
        if resolves(root, "CLAUDE.md", path):
            findings.append(checklib.Finding(
                "build/paths_pending.tsv", 0,
                f"the pending path {path} (phase {phase}) now exists",
                "only paths that do not exist yet in the pending list",
                f"remove the row for {path} in the change that created it"))
    return findings


def main() -> int:
    args = checklib.parse_args(__doc__)
    return checklib.report("check_paths", check(Path(args.root)))


if __name__ == "__main__":
    checklib.run_main(main)
