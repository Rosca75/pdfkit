# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# Runs the pinned clang-format over our C and C++ sources (a formatting
# difference fails), and clang-tidy over the sources of the compile database in
# out/mingw-debug (every finding fails, .clang-tidy says which checks run).
# The tools are found on PATH or in the per-user scripts folder that pip
# installs them into (build/toolchain.md), and their versions must equal the
# pins in build/requirements-dev.txt, because another version formats
# differently.
"""Check formatting and tidy: python build/check_format.py [--root <tree>] [--no-tidy] [--fix]."""

from __future__ import annotations

import json
import os
import re
import shutil
import subprocess
import sysconfig
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import checklib

FORMAT_TREES = ("include", "src", "cli", "tests")
FORMAT_SUFFIXES = {".h", ".hpp", ".cpp"}
DIAGNOSTIC = re.compile(r"^(?P<path>[A-Za-z]:[^:]*|[^:]+):(?P<line>\d+):(?P<col>\d+): "
                        r"(?:error|warning): (?P<message>.*)$")
TIDY_ARGS = ["--extra-arg=--target=x86_64-w64-mingw32", "--extra-arg=-Wno-unknown-warning-option",
             "--quiet"]


def find_tool(name: str) -> str | None:
    found = shutil.which(name)
    if found:
        return found
    for scheme in (f"{os.name}_user", os.name):
        try:
            folder = Path(sysconfig.get_path("scripts", scheme))
        except KeyError:
            continue
        for candidate in (folder / f"{name}.exe", folder / name):
            if candidate.is_file():
                return str(candidate)
    return None


def pinned_version(root: Path, package: str) -> str | None:
    requirements = root / "build" / "requirements-dev.txt"
    if not requirements.is_file():
        return None
    m = re.search(rf"^{re.escape(package)}==([\d.]+)", requirements.read_text(encoding="utf-8"),
                  re.MULTILINE)
    return m.group(1) if m else None


def tool_or_finding(root: Path, name: str) -> tuple[str | None, list[checklib.Finding]]:
    tool = find_tool(name)
    install = "python -m pip install --user --require-hashes -r build/requirements-dev.txt"
    if tool is None:
        return None, [checklib.Finding(name, 0, f"{name} is not installed",
                                       f"the {name} pinned in build/requirements-dev.txt",
                                       install)]
    pin = pinned_version(root, name)
    printed = subprocess.run([tool, "--version"], capture_output=True, text=True,
                             check=False).stdout
    if pin and pin not in printed:
        return None, [checklib.Finding(name, 0, f"{name} reports '{printed.strip()}'",
                                       f"version {pin}, the pin of build/requirements-dev.txt",
                                       install)]
    return tool, []


def relative(root: Path, path: str) -> str:
    try:
        return Path(path).resolve().relative_to(root.resolve()).as_posix()
    except ValueError:
        return path


def check_formatting(root: Path, fix: bool) -> list[checklib.Finding]:
    tool, findings = tool_or_finding(root, "clang-format")
    if tool is None:
        return findings
    files = [p for p in checklib.walk(root, FORMAT_TREES) if Path(p).suffix in FORMAT_SUFFIXES]
    for start in range(0, len(files), 50):
        batch = files[start:start + 50]
        mode = ["-i"] if fix else ["--dry-run", "--Werror"]
        result = subprocess.run([tool, *mode, "--style=file", *batch], cwd=root,
                                capture_output=True, text=True, check=False)
        seen: set[str] = set()
        for line in result.stderr.splitlines():
            m = DIAGNOSTIC.match(line)
            if m and m.group("path") not in seen:
                seen.add(m.group("path"))
                findings.append(checklib.Finding(
                    relative(root, m.group("path")), int(m.group("line")),
                    "the file differs from its clang-format formatting",
                    "the formatting of .clang-format",
                    "run python build/check_format.py --fix, or clang-format -i on the file"))
    return findings


def check_tidy(root: Path, build_dir: Path) -> list[checklib.Finding]:
    database = build_dir / "compile_commands.json"
    if not database.is_file():
        return [checklib.Finding(relative(root, str(database)), 0, "there is no compile database",
                                 "out/mingw-debug/compile_commands.json from a configured build",
                                 "cmake --preset mingw-debug")]
    tool, findings = tool_or_finding(root, "clang-tidy")
    if tool is None:
        return findings
    entries = json.loads(database.read_text(encoding="utf-8"))
    files = sorted({relative(root, entry["file"]) for entry in entries})
    files = [f for f in files if f.startswith(("src/", "cli/", "tests/")) and f.endswith(".cpp")]

    def run(path: str) -> list[checklib.Finding]:
        result = subprocess.run([tool, "-p", str(build_dir), *TIDY_ARGS, path], cwd=root,
                                capture_output=True, text=True, check=False)
        found: list[checklib.Finding] = []
        for line in (result.stdout + result.stderr).splitlines():
            m = DIAGNOSTIC.match(line)
            if m:
                found.append(checklib.Finding(relative(root, m.group("path")), int(m.group("line")),
                                              m.group("message"), "no clang-tidy finding",
                                              "fix the code; .clang-tidy names the checks"))
        if result.returncode != 0 and not found:
            found.append(checklib.Finding(path, 0, "clang-tidy failed: " + result.stderr[-300:],
                                          "a clean clang-tidy run", "read the tool's output"))
        return found

    with ThreadPoolExecutor(max_workers=max(1, (os.cpu_count() or 2))) as pool:
        for found in pool.map(run, files):
            findings += found
    unique = {(f.path, f.line, f.failed): f for f in findings}
    return [unique[key] for key in sorted(unique)]


def main() -> int:
    args = checklib.parse_args(__doc__, [
        ("--no-tidy", {"action": "store_true", "help": "skip clang-tidy"}),
        ("--fix", {"action": "store_true", "help": "rewrite files with clang-format"}),
        ("--build-dir", {"default": None, "help": "the folder of compile_commands.json"}),
    ])
    root = Path(args.root)
    findings = check_formatting(root, args.fix)
    if not args.no_tidy:
        build_dir = Path(args.build_dir) if args.build_dir else root / "out" / "mingw-debug"
        findings += check_tidy(root, build_dir)
    return checklib.report("check_format", findings)


if __name__ == "__main__":
    checklib.run_main(main)
