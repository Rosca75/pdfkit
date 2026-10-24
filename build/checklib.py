# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# The helpers every check script shares: the tree walk that skips build output
# and caches, the finding record, the report format (file and line, what
# failed, what was expected, how to fix it) and the comment extraction of each
# source language. Every check takes --root, so build/tests/test_checks.py can
# run it over a temporary tree with one planted violation.
"""Shared helpers of the build/check_*.py scripts."""

from __future__ import annotations

import argparse
import io
import re
import subprocess
import sys
import tokenize
from dataclasses import dataclass
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]

# Folders never walked: version control, editor state, build output, caches
# and the ignored dataset folders, which hold no source of ours.
SKIP_NAMES = {".git", ".claude", ".vs", ".vscode", ".idea", ".venv", "__pycache__",
              "node_modules", "out", "dist", "bin", "obj"}
SKIP_PATHS = {"build/cache", "build/_deps", "dataset/files", "dataset/cache", "dataset/reports"}

# The trees whose files are ours (CLAUDE.md section 6), and the source suffixes.
OUR_TREES = ("include", "src", "cli", "tests", "build", "dataset/tools")
C_SUFFIXES = {".h", ".hpp", ".cpp", ".cs"}
HASH_SUFFIXES = {".py", ".ps1", ".cmake"}
LAYERS = ("platform", "syntax", "fonts", "text", "images", "writer", "redact", "api")


@dataclass(frozen=True)
class Finding:
    path: str
    line: int
    failed: str
    expected: str
    fix: str


def parse_args(description: str, extra: list[tuple[str, dict]] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=description)
    parser.add_argument("--root", default=str(REPO_ROOT), help="the tree to check")
    for name, options in extra or []:
        parser.add_argument(name, **options)
    return parser.parse_args()


def walk(root: Path, under: tuple[str, ...] | None = None) -> list[str]:
    """Every file below root (or below the given top folders), as sorted POSIX paths."""
    found: list[str] = []
    starts = [root / top for top in under] if under else [root]
    for start in starts:
        if not start.exists():
            continue
        if start.is_file():
            found.append(start.relative_to(root).as_posix())
            continue
        stack = [start]
        while stack:
            folder = stack.pop()
            for entry in folder.iterdir():
                relative = entry.relative_to(root).as_posix()
                if entry.is_dir():
                    if entry.name in SKIP_NAMES or relative in SKIP_PATHS:
                        continue
                    stack.append(entry)
                elif entry.is_file():
                    found.append(relative)
    return sorted(set(found))


def is_source(path: str) -> bool:
    name = path.rsplit("/", 1)[-1]
    suffix = Path(name).suffix
    return suffix in C_SUFFIXES or suffix in HASH_SUFFIXES or name == "CMakeLists.txt"


def our_sources(root: Path) -> list[str]:
    """Our source files under the trees of CLAUDE.md section 6."""
    return [p for p in walk(root, OUR_TREES) if is_source(p)]


def read_text(root: Path, path: str) -> str:
    return (root / path).read_text(encoding="utf-8", errors="replace")


def report(check: str, findings: list[Finding]) -> int:
    for finding in findings:
        print(f"{finding.path}:{finding.line}: {finding.failed}")
        print(f"    expected: {finding.expected}")
        print(f"    fix: {finding.fix}")
    if findings:
        print(f"{check}: {len(findings)} finding(s)")
        return 1
    print(f"{check}: ok")
    return 0


def line_of(text: str, offset: int) -> int:
    return text.count("\n", 0, offset) + 1


# ---- Comments and code ------------------------------------------------------

_RAW_STRING = re.compile(r'R"([^()\\ ]{0,16})\(')


def split_c_like(text: str) -> tuple[str, list[tuple[int, str]]]:
    """The code of a C, C++ or C# file with comments and literals blanked
    (newlines kept, so offsets and lines still match), and its comments as
    (line, text) pairs."""
    code = list(text)
    comments: list[tuple[int, str]] = []
    i = 0
    n = len(text)

    def blank(start: int, end: int) -> None:
        for k in range(start, end):
            if code[k] != "\n":
                code[k] = " "

    while i < n:
        c = text[i]
        if text.startswith("//", i):
            end = text.find("\n", i)
            end = n if end < 0 else end
            comments.append((line_of(text, i), text[i + 2:end]))
            blank(i, end)
            i = end
        elif text.startswith("/*", i):
            end = text.find("*/", i + 2)
            end = n if end < 0 else end + 2
            body = text[i + 2:end - 2]
            first = line_of(text, i)
            for k, part in enumerate(body.split("\n")):
                comments.append((first + k, part))
            blank(i, end)
            i = end
        elif (raw := _RAW_STRING.match(text, i)) and (i == 0 or not (text[i - 1].isalnum() or text[i - 1] == "_")):
            close = ")" + raw.group(1) + '"'
            end = text.find(close, raw.end())
            end = n if end < 0 else end + len(close)
            blank(i + 1, end - 1)
            i = end
        elif c in "\"'":
            j = i + 1
            while j < n and text[j] != c and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            end = min(j + 1, n)
            blank(i + 1, end - 1)
            i = end
        else:
            i += 1
    return "".join(code), comments


def python_comments(text: str) -> list[tuple[int, str]]:
    comments: list[tuple[int, str]] = []
    try:
        for token in tokenize.generate_tokens(io.StringIO(text).readline):
            if token.type == tokenize.COMMENT:
                comments.append((token.start[0], token.string[1:]))
    except (tokenize.TokenError, IndentationError, SyntaxError):
        pass
    return comments


def hash_comments(text: str, block: bool = False) -> list[tuple[int, str]]:
    """Comments of CMake, PowerShell and shell files: '#' to the end of the line
    outside quotes, and PowerShell's <# #> blocks when block is set."""
    comments: list[tuple[int, str]] = []
    in_block = False
    for number, line in enumerate(text.split("\n"), start=1):
        if block and in_block:
            end = line.find("#>")
            comments.append((number, line if end < 0 else line[:end]))
            in_block = end < 0
            continue
        quote = ""
        for k, ch in enumerate(line):
            if quote:
                if ch == quote:
                    quote = ""
            elif ch in "\"'":
                quote = ch
            elif block and line.startswith("<#", k):
                end = line.find("#>", k + 2)
                comments.append((number, line[k + 2:] if end < 0 else line[k + 2:end]))
                in_block = end < 0
                break
            elif ch == "#":
                comments.append((number, line[k + 1:]))
                break
    return comments


def comments_of(path: str, text: str) -> list[tuple[int, str]]:
    suffix = Path(path).suffix
    if suffix in C_SUFFIXES:
        return split_c_like(text)[1]
    if suffix == ".py":
        return python_comments(text)
    if suffix == ".ps1":
        return hash_comments(text, block=True)
    return hash_comments(text)


# ---- Git and the shared cache ----------------------------------------------

def main_checkout(root: Path) -> Path:
    """The main checkout that holds the shared caches (CLAUDE.md section 11):
    the parent of git's common directory, or root itself outside git."""
    try:
        common = subprocess.run(
            ["git", "rev-parse", "--path-format=absolute", "--git-common-dir"],
            cwd=root, capture_output=True, text=True, check=True,
        ).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return root
    return Path(common).parent


def pending_paths(root: Path) -> dict[str, str]:
    """The rows of build/paths_pending.tsv: path to the phase that creates it."""
    table = root / "build" / "paths_pending.tsv"
    rows: dict[str, str] = {}
    if not table.is_file():
        return rows
    for line in table.read_text(encoding="utf-8").splitlines():
        if not line.strip() or line.startswith("#") or line.startswith("path\t"):
            continue
        fields = line.split("\t")
        rows[fields[0]] = fields[1] if len(fields) > 1 else ""
    return rows


def run_main(main) -> None:  # noqa: ANN001
    sys.exit(main())
