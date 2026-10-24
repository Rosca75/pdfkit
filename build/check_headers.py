# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# Checks that every source file of ours opens with the licence header (the
# SPDX line, then the copyright line) and a purpose header after it: a comment
# block holding at least one full sentence that says what the file is for
# (CLAUDE.md sections 6 and 9). Vendored files under third_party/ keep their
# own headers and are not read.
"""Check the licence and purpose headers: python build/check_headers.py [--root <tree>]."""

from __future__ import annotations

import re
from pathlib import Path

import checklib

SPDX = "SPDX-License-Identifier: Apache-2.0"
COPYRIGHT = re.compile(r"^Copyright \d{4} The pdfkit authors$")
SENTENCE = re.compile(r"\b\w+\b(?:\W+\b\w+\b){2,}[^.]*\.")


def marker_for(path: str) -> str:
    return "//" if Path(path).suffix in checklib.C_SUFFIXES else "#"


def check_file(root: Path, path: str) -> list[checklib.Finding]:
    marker = marker_for(path)
    lines = checklib.read_text(root, path).split("\n")
    start = 0
    while start < len(lines) and (lines[start].startswith("#!") or "coding" in lines[start][:20]):
        start += 1
    expected_licence = f"'{marker} {SPDX}' then '{marker} Copyright <year> The pdfkit authors'"
    fix_licence = f"start the file with the two licence lines, each beginning with '{marker} '"

    def finding(line: int, failed: str, expected: str, fix: str) -> list[checklib.Finding]:
        return [checklib.Finding(path, line, failed, expected, fix)]

    if start >= len(lines) or lines[start].strip() != f"{marker} {SPDX}":
        return finding(start + 1, "the file does not start with the SPDX licence line",
                       expected_licence, fix_licence)
    if start + 1 >= len(lines) or not lines[start + 1].startswith(marker + " ") or \
            not COPYRIGHT.match(lines[start + 1][len(marker) + 1:].strip()):
        return finding(start + 2, "the copyright line does not follow the SPDX line",
                       expected_licence, fix_licence)
    purpose: list[str] = []
    for line in lines[start + 2:]:
        stripped = line.strip()
        if not stripped.startswith(marker):
            break
        purpose.append(stripped[len(marker):].strip())
    if not SENTENCE.search(" ".join(purpose)):
        return finding(start + 3, "the licence lines are not followed by a purpose header",
                       f"a comment block after the licence lines, each line beginning with "
                       f"'{marker}', with at least one sentence saying what the file is for",
                       "add a few sentences saying what the file is for and why it exists")
    return []


def main() -> int:
    args = checklib.parse_args(__doc__)
    root = Path(args.root)
    findings: list[checklib.Finding] = []
    paths = checklib.our_sources(root)
    paths += [p for p in ("CMakeLists.txt",) if (root / p).is_file()]
    for path in sorted(set(paths)):
        findings += check_file(root, path)
    return checklib.report("check_headers", findings)


if __name__ == "__main__":
    checklib.run_main(main)
