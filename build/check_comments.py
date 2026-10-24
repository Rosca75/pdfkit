# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# Checks that comments describe the code as it is: no comment cites a build
# plan, a numbered stage of the build, a ticket or a change request, and none
# uses the delta words of CLAUDE.md section 9. A to-do note is allowed only
# with a reference to the owner question it waits on, written as
# TODO(OPEN-QUESTIONS.md <section>). Only comments are read; strings and code
# may name anything.
"""Check comments for citations and delta words: python build/check_comments.py [--root <tree>]."""

from __future__ import annotations

import re
from pathlib import Path

import checklib

RULES: list[tuple[re.Pattern[str], str]] = [
    (re.compile(r"\bphase\s*\d", re.IGNORECASE), "names a build phase"),
    (re.compile(r"\bbuild-\d+(?:\.md)?\b", re.IGNORECASE), "cites a build file"),
    (re.compile(r"\b(?:ticket|issue)\s*#?\d+", re.IGNORECASE), "cites a ticket"),
    (re.compile(r"\bchange[- ]order\b|\bchange-\d+\b", re.IGNORECASE), "cites a change order"),
    (re.compile(r"(?<!\bis )(?<!\bare )(?<!\bbe )(?<!\bbeing )\bused to\b", re.IGNORECASE),
     "uses the delta words 'used to'"),
    (re.compile(r"\bno longer\b", re.IGNORECASE), "uses the delta words 'no longer'"),
    (re.compile(r"\bfor now\b", re.IGNORECASE), "uses the delta words 'for now'"),
    (re.compile(r"\bnot yet\b", re.IGNORECASE), "uses the delta words 'not yet'"),
    (re.compile(r"\bTODO\b(?!\(OPEN-QUESTIONS)"), "holds a TODO without its owner question"),
]


def check_file(root: Path, path: str) -> list[checklib.Finding]:
    findings: list[checklib.Finding] = []
    text = checklib.read_text(root, path)
    for line, comment in checklib.comments_of(path, text):
        for pattern, what in RULES:
            if pattern.search(comment):
                findings.append(checklib.Finding(
                    path, line, f"the comment {what}: '{comment.strip()[:80]}'",
                    "a comment that states the code's intent in the present tense, naming no "
                    "build plan, stage, ticket or past version",
                    "state the reason itself instead of where it came from; a TODO names its "
                    "question as TODO(OPEN-QUESTIONS.md <section>)"))
    return findings


def main() -> int:
    args = checklib.parse_args(__doc__)
    root = Path(args.root)
    findings: list[checklib.Finding] = []
    paths = checklib.our_sources(root)
    paths += [p for p in ("CMakeLists.txt",) if (root / p).is_file()]
    hooks = root / "build" / "hooks"
    if hooks.is_dir():
        paths += [f"build/hooks/{entry.name}" for entry in hooks.iterdir() if entry.is_file()]
    for path in sorted(set(paths)):
        findings += check_file(root, path)
    return checklib.report("check_comments", findings)


if __name__ == "__main__":
    checklib.run_main(main)
