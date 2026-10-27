# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# Runs the pinned clang-format over our C and C++ sources (a formatting
# difference fails), and clang-tidy over the sources of the compile database in
# out/mingw-debug (every finding fails, .clang-tidy says which checks run).
# The tools are found on PATH or in the per-user scripts folder that pip
# installs them into (build/toolchain.md), and their versions must equal the
# pins in build/requirements-dev.txt, because another version formats
# differently. clang-tidy takes most of the check's time, so each source's
# findings are cached under a key of everything that decides them (see
# check_tidy), and --changed-since <ref> limits the analysis to the sources
# that reach a file changed since that git reference; --no-cache runs the whole
# analysis again, which the full check does.
"""Check formatting and tidy: python build/check_format.py [--root <tree>] [--no-tidy] [--fix]
[--changed-since <ref>] [--no-cache]."""

from __future__ import annotations

import hashlib
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
INCLUDE_LINE = re.compile(r'^[ \t]*#[ \t]*include[ \t]*([<"])([^>"\n]+)[>"]', re.MULTILINE)
INCLUDE_FLAG = re.compile(r'(?:^|\s)(?:-I|-isystem\s+)"?(?P<dir>[^"\s]+)"?')
# A change to any of these changes how every source is analysed, so it selects them all.
TIDY_GLOBAL = {".clang-tidy", "build/check_format.py", "build/checklib.py", "build/requirements-dev.txt",
               "CMakePresets.json"}


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


class IncludeGraph:
    """The project files each source reaches through #include, transitively.

    Every #include line counts, whatever conditional surrounds it, so the set is a
    superset of what the compiler reads; a name resolves against the including file's
    folder (quoted names) and the compile command's -I and -isystem folders that lie
    inside the tree, so system headers, which change only with the toolchain, are left
    out."""

    def __init__(self, root: Path):
        self.root = root.resolve()
        self.closures: dict[tuple, frozenset[str]] = {}
        self.direct: dict[tuple, list[Path]] = {}
        self.names: dict[Path, list[tuple[bool, str]]] = {}
        self.exists: dict[Path, bool] = {}

    def is_file(self, candidate: Path) -> bool:
        if candidate not in self.exists:
            self.exists[candidate] = candidate.is_file()
        return self.exists[candidate]

    def folders(self, command: str) -> tuple[Path, ...]:
        found = []
        for m in INCLUDE_FLAG.finditer(command):
            folder = Path(m.group("dir"))
            try:
                folder.resolve().relative_to(self.root)
            except (ValueError, OSError):
                continue
            found.append(folder.resolve())
        return tuple(found)

    def includes(self, path: Path, folders: tuple[Path, ...]) -> list[Path]:
        key = (path, folders)
        if key not in self.direct:
            if path not in self.names:
                try:
                    text = path.read_text(encoding="utf-8", errors="replace")
                except OSError:
                    text = ""
                self.names[path] = [(m.group(1) == '"', m.group(2)) for m in INCLUDE_LINE.finditer(text)]
            out = []
            for quoted, name in self.names[path]:
                for folder in ((path.parent,) if quoted else ()) + folders:
                    candidate = Path(os.path.normpath(folder / name))
                    if self.is_file(candidate):
                        out.append(candidate)
                        break
            self.direct[key] = out
        return self.direct[key]

    def relative(self, path: Path) -> str:
        try:
            return path.relative_to(self.root).as_posix()
        except ValueError:
            return path.as_posix()

    def closure(self, source: Path, folders: tuple[Path, ...]) -> frozenset[str]:
        """The source and every project file it reaches, as paths relative to the root."""
        key = (source, folders)
        if key not in self.closures:
            seen: set[Path] = set()
            stack = [source.resolve()]
            while stack:
                current = stack.pop()
                if current in seen:
                    continue
                seen.add(current)
                stack += self.includes(current, folders)
            self.closures[key] = frozenset(self.relative(p) for p in seen)
        return self.closures[key]


def changed_files(root: Path, ref: str) -> set[str] | None:
    """The files that differ from `ref` (committed, staged, unstaged or untracked); None when
    git cannot say, which makes every file count as changed."""
    try:
        diff = subprocess.run(["git", "diff", "--name-only", ref], cwd=root, capture_output=True,
                              text=True, check=True).stdout
        untracked = subprocess.run(["git", "ls-files", "--others", "--exclude-standard"], cwd=root,
                                   capture_output=True, text=True, check=True).stdout
    except (OSError, subprocess.CalledProcessError):
        return None
    return {line.strip() for line in (diff + untracked).splitlines() if line.strip()}


def tidy_cache_dir(root: Path) -> Path:
    return checklib.main_checkout(root) / "build" / "cache" / "clang-tidy"


def check_tidy(root: Path, build_dir: Path, changed_since: str | None = None,
               use_cache: bool = True) -> list[checklib.Finding]:
    """clang-tidy over the sources of the compile database.

    Each file's findings are kept in build/cache/clang-tidy/ of the main checkout under the
    SHA-256 of everything that decides them: the tool's version, .clang-tidy, the arguments,
    the compile command (this tree's paths written as <root>) and the content of every
    project file the source reaches. A file whose key is cached is not analysed again unless
    `use_cache` is false (the full scan). With `changed_since`, only the sources that reach a
    file changed since that git reference are analysed; a change to the analysis itself (the
    configuration, this script, a CMake file) selects every source."""
    database = build_dir / "compile_commands.json"
    if not database.is_file():
        return [checklib.Finding(relative(root, str(database)), 0, "there is no compile database",
                                 "out/mingw-debug/compile_commands.json from a configured build",
                                 "cmake --preset mingw-debug")]
    tool, findings = tool_or_finding(root, "clang-tidy")
    if tool is None:
        return findings
    version = subprocess.run([tool, "--version"], capture_output=True, text=True, check=False).stdout
    config = (root / ".clang-tidy").read_bytes() if (root / ".clang-tidy").is_file() else b""
    entries = json.loads(database.read_text(encoding="utf-8"))
    commands = {}
    for entry in entries:
        path = relative(root, entry["file"])
        if path.startswith(("src/", "cli/", "tests/")) and path.endswith(".cpp"):
            commands.setdefault(path, entry.get("command") or " ".join(entry.get("arguments", [])))
    files = sorted(commands)
    graph = IncludeGraph(root)
    closures = {path: graph.closure(root / path, graph.folders(commands[path])) for path in files}
    if changed_since:
        changed = changed_files(root, changed_since)
        if changed is not None and not any(c in TIDY_GLOBAL or c.endswith((".cmake", "CMakeLists.txt"))
                                           for c in changed):
            files = [path for path in files if closures[path] & changed]
        print(f"check_format: clang-tidy on {len(files)} of {len(commands)} sources "
              f"(those reaching a file changed since {changed_since})", flush=True)
    root_text = str(root.resolve())
    contents: dict[str, str] = {}

    def key_of(path: str) -> str:
        hasher = hashlib.sha256()
        command = commands[path]
        for spelling in {root_text, root_text.replace("\\", "/"), str(root), str(root).replace("\\", "/")}:
            command = command.replace(spelling, "<root>")
        hasher.update(json.dumps([version, config.hex(), TIDY_ARGS, command]).encode("utf-8"))
        for member in sorted(closures[path]):
            if member not in contents:
                try:
                    contents[member] = hashlib.sha256((root / member).read_bytes()).hexdigest()
                except OSError:
                    contents[member] = "absent"
            hasher.update(f"{member}\0{contents[member]}\n".encode("utf-8"))
        return hasher.hexdigest()

    cache = tidy_cache_dir(root)

    def run(path: str) -> list[checklib.Finding]:
        key = key_of(path)
        record = cache / key[:2] / f"{key}.json"
        if use_cache and record.is_file():
            try:
                return [checklib.Finding(*item) for item in json.loads(record.read_text(encoding="utf-8"))]
            except (OSError, ValueError, TypeError):
                pass
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
        record.parent.mkdir(parents=True, exist_ok=True)
        temporary = record.with_suffix(f".{os.getpid()}.tmp")
        temporary.write_text(json.dumps([[f.path, f.line, f.failed, f.expected, f.fix] for f in found]),
                             encoding="utf-8")
        os.replace(temporary, record)
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
        ("--changed-since", {"default": None,
                             "help": "tidy only the sources reaching a file changed since this git ref"}),
        ("--no-cache", {"action": "store_true", "help": "analyse every selected source again"}),
    ])
    root = Path(args.root)
    findings = check_formatting(root, args.fix)
    if not args.no_tidy:
        build_dir = Path(args.build_dir) if args.build_dir else root / "out" / "mingw-debug"
        findings += check_tidy(root, build_dir, args.changed_since, not args.no_cache)
    return checklib.report("check_format", findings)


if __name__ == "__main__":
    checklib.run_main(main)
