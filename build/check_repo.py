# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# Checks what git holds (CLAUDE.md section 7.2): no committed file whose bytes
# start with %PDF, whatever its name, because unit fixtures are written in code
# and the dataset lives outside git; and no file over 5 MB outside
# third_party/, because a large blob in history is there for good.
"""Check the committed files: python build/check_repo.py [--root <tree>]."""

from __future__ import annotations

import subprocess
from pathlib import Path

import checklib

MAX_BYTES = 5 * 1024 * 1024


def tracked_files(root: Path) -> list[str] | None:
    try:
        out = subprocess.run(["git", "ls-files", "-z"], cwd=root, capture_output=True,
                             check=True).stdout
    except (OSError, subprocess.CalledProcessError):
        return None
    return [p for p in out.decode("utf-8").split("\0") if p]


def check(root: Path) -> list[checklib.Finding]:
    files = tracked_files(root)
    if files is None:
        return [checklib.Finding(".", 0, "git ls-files could not list the tracked files",
                                 "a git checkout", "run the check inside the repository")]
    findings: list[checklib.Finding] = []
    for path in files:
        file = root / path
        if not file.is_file():
            continue
        with file.open("rb") as handle:
            head = handle.read(4)
        if head == b"%PDF":
            findings.append(checklib.Finding(
                path, 1, "a committed file starts with %PDF",
                "no PDF in git; fixtures are written in code, the dataset lives outside git",
                "git rm --cached the file; write the fixture with tests/unit/support/ or add the "
                "file to the dataset manifest"))
        size = file.stat().st_size
        if size > MAX_BYTES and not path.startswith("third_party/"):
            findings.append(checklib.Finding(
                path, 0, f"a committed file is {size} bytes",
                f"files outside third_party/ of at most {MAX_BYTES} bytes",
                "keep large inputs in the shared cache, outside git"))
    return findings


def main() -> int:
    args = checklib.parse_args(__doc__)
    return checklib.report("check_repo", check(Path(args.root)))


if __name__ == "__main__":
    checklib.run_main(main)
