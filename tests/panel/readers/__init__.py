# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# The reference panel's adapters share one interface, so the collection and the
# judge never know a member's command line. Each member module names itself
# (NAME, LICENCE, ROLE, CAPABILITIES), says whether its binaries are present
# and equal to their pins (`missing()`), gives the digest that keys its cached
# answers (`binary_digest()`) and answers one kind about one file
# (`answer(path, kind, timeout, request)`), as {"ok": true, ...} or
# {"ok": false, "error": <cause>, "detail": ...}. This module holds what the
# adapters share: the pins read from members.tsv, the child process run with
# its time limit and the count of child processes started (a warm collection
# starts none), the split of a run's outcome into an answer, a refusal or a
# transient failure, and the scratch folder each answer works in.
#
# Failure causes: `timeout` (no answer within the limit) and `crash` (killed by
# a signal) are asked once more at twice the limit; a failure that does not
# repeat says nothing about the file and is never cached. The same failure twice
# is a `repeated_failure`, cached and read as an abstention. `refused` (the
# member's own non-zero exit with its message) is the member's answer about the
# file and is cached like a success (answers.py).

from __future__ import annotations

import contextlib
import functools
import hashlib
import importlib
import os
import shutil
import subprocess
import sys
import tempfile
import threading
from pathlib import Path

HERE = Path(__file__).resolve().parent
PANEL = HERE.parent
REPO = PANEL.parents[1]
sys.path.insert(0, str(REPO / "dataset" / "tools"))

import common  # noqa: E402

MEMBERS_TSV = PANEL / "members.tsv"
MEMBER_COLUMNS = ("member", "platform", "role", "licence", "version", "upstream", "archive_sha256",
                  "packages", "binaries", "binary_digest", "capabilities", "native_size",
                  "decode_applied", "colour_output", "abstains", "notes")
# The member modules, in the order reports list them.
MODULES = ("pdfium_member", "poppler", "mupdf", "oxide", "extract", "qpdf", "lopdf")
KINDS = ("text", "pages", "structure", "image_pixels", "raw_streams")
TRANSIENT = {"timeout", "crash", "worker"}
# Where a member's error text is cut, so a cached refusal stays small.
DETAIL_LIMIT = 400
NOT_RECORDED = "to record on the laptop"


def platform() -> str:
    """The platform whose pins apply: `linux` on a Linux host, else `windows`."""
    return "linux" if sys.platform.startswith("linux") else "windows"


class _Counter:
    """The child processes started by this process, for the warm collection's proof."""

    def __init__(self) -> None:
        self.lock = threading.Lock()
        self.value = 0

    def add(self, count: int = 1) -> None:
        with self.lock:
            self.value += count


STARTED = _Counter()


def started() -> int:
    return STARTED.value


def read_members(path: Path | None = None) -> list[dict]:
    """The rows of members.tsv, every column present."""
    return common.read_tsv(path or MEMBERS_TSV, MEMBER_COLUMNS)


@functools.lru_cache(maxsize=None)
def pin(member: str, host: str | None = None) -> dict:
    """The members.tsv row of `member` for this host's platform."""
    host = host or platform()
    for row in read_members():
        if row["member"] == member and row["platform"] == host:
            return row
    raise common.ToolError(
        f"tests/panel/members.tsv has no row for {member} on {host}",
        "one row per member and platform",
        "add the row with the member's version, binaries and their SHA-256")


def binaries(row: dict) -> list[tuple[Path, str]]:
    """(path, pinned SHA-256) of each binary a row names; a relative path is resolved under
    build/cache/ of the main checkout, where PDFium's folder and the panel's sit side by side."""
    out = []
    for item in filter(None, (row.get("binaries") or "").split(";")):
        name, _, digest = item.strip().rpartition(":")
        path = Path(name)
        if not path.is_absolute():
            path = shared_cache() / name
        out.append((path, digest))
    return out


def shared_cache() -> Path:
    """build/cache of the main checkout (CLAUDE.md section 11)."""
    return common.pdfium_cache_dir().parent


def cache_dir() -> Path:
    """build/cache/panel of the main checkout, beside PDFium's folder."""
    return shared_cache() / "panel"


@functools.lru_cache(maxsize=256)
def _sha256(path: str, size: int, mtime_ns: int) -> str:
    del size, mtime_ns  # part of the memo key only
    hasher = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            hasher.update(block)
    return hasher.hexdigest()


def sha256_of(path: Path) -> str:
    stat = os.stat(path)
    return _sha256(str(path), stat.st_size, stat.st_mtime_ns)


def combined_digest(digests: list[str]) -> str:
    """One digest for a member's binaries: the SHA-256 of their digests, sorted, one per line."""
    return hashlib.sha256("".join(d + "\n" for d in sorted(digests)).encode("ascii")).hexdigest()


def live_digest(row: dict) -> str:
    """The digest of the member's binaries as they are on disk."""
    return combined_digest([sha256_of(path) for path, _ in binaries(row)])


def abstains(member: str, kind: str, host: str | None = None) -> set[str]:
    """The image classes on which a member abstains for a kind, from the row's `abstains` column
    (`kind:class,class;kind:class`), measured with the pixel facts."""
    out: set[str] = set()
    for item in filter(None, (pin(member, host).get("abstains") or "").split(";")):
        name, _, classes = item.strip().partition(":")
        if name == kind:
            out.update(c for c in classes.split(",") if c)
    return out


def package_versions(names: list[str]) -> dict[str, str | None]:
    """The installed version of each Debian package, None when absent (dpkg-query)."""
    out: dict[str, str | None] = {name: None for name in names}
    if not names or shutil.which("dpkg-query") is None:
        return out
    proc = subprocess.run(["dpkg-query", "-W", "-f=${Package} ${Version} ${db:Status-Abbrev}\\n",
                           *names], capture_output=True, text=True)
    for line in proc.stdout.splitlines():
        parts = line.split()
        if len(parts) >= 3 and parts[2].startswith("ii"):
            out[parts[0].split(":")[0]] = parts[1]
    return out


def packages(row: dict) -> dict[str, str]:
    """package -> pinned version, from the row's `packages` column (`name=version;...`)."""
    out = {}
    for item in filter(None, (row.get("packages") or "").split(";")):
        name, _, version = item.strip().partition("=")
        out[name] = version
    return out


@functools.lru_cache(maxsize=None)
def pin_problem(member: str) -> str | None:
    """Why the member cannot answer here, with the command that supplies it; None when every
    package and binary is present and equal to its pin."""
    row = pin(member)
    supply = row["notes"].split("supply: ", 1)[1].split(";", 1)[0] if "supply: " in row["notes"] \
        else "see tests/panel/members.tsv"
    wanted = packages(row)
    if wanted:
        installed = package_versions(sorted(wanted))
        for name, version in sorted(wanted.items()):
            if installed.get(name) != version:
                return (f"{member}: package {name} is {installed.get(name) or 'absent'}, pinned "
                        f"{version}; supply it with `{supply}`")
    listed = binaries(row)
    if not listed:
        return f"{member}: no binary is pinned for {platform()}; supply it with `{supply}`"
    for path, digest in listed:
        if not path.is_file():
            return f"{member}: {path} is missing; supply it with `{supply}`"
        if digest == NOT_RECORDED or len(digest) != 64:
            return f"{member}: {path} has no recorded SHA-256; record it in tests/panel/members.tsv"
        if sha256_of(path) != digest:
            return (f"{member}: {path} has SHA-256 {sha256_of(path)}, pinned {digest}; supply the "
                    f"pinned build with `{supply}`")
    return None


def load(name: str):
    """The adapter module of a member, by member name or module name."""
    module = {"pdfium": "pdfium_member"}.get(name, name)
    return importlib.import_module(f"readers.{module}")


def all_members() -> list:
    return [load(name) for name in MODULES]


def timeout_for(pages: int, base: float = 30.0) -> float:
    """30 s per started block of 200 pages, as the dataset tier allows (CLAUDE.md section 8.1)."""
    return base * max(1, -(-max(pages, 1) // 200))


@contextlib.contextmanager
def scratch():
    """A fresh folder under build/cache/panel/tmp/, deleted after the answer."""
    root = cache_dir() / "tmp"
    root.mkdir(parents=True, exist_ok=True)
    folder = Path(tempfile.mkdtemp(dir=root))
    try:
        yield folder
    finally:
        shutil.rmtree(folder, ignore_errors=True)


def run(command: list[str], timeout: float, cwd: Path | None = None, retry: bool = True) -> dict:
    """Runs one member command: {"exit", "stdout", "stderr"}, a transient failure, or, when the
    command times out or crashes again on its one retry (at twice the limit), the member's
    refusal of the file with the repeated failure named."""
    first = _run_once(command, timeout, cwd)
    if not retry or not failed(first):
        return first
    second = _run_once(command, 2 * timeout, cwd)
    return repeated(first, second, limit=timeout)


def repeated(first: dict, second: dict, limit: float | None = None) -> dict:
    """The outcome of a retry after a transient failure: the retry's own result when it finished
    or failed differently; a `repeated_failure` when it failed the same way. That is kept in the
    cache, because a member that crashes or hangs on a file twice would otherwise be asked at
    every collection, and is read as an abstention, never as the member's own refusal of the
    file. It names the first time limit, so a collection run with a larger one asks again."""
    if not failed(second) or second.get("error") != first.get("error"):
        return second
    answer = {"ok": False, "error": "repeated_failure", "repeated": first["error"],
              "detail": f"{first['error']} twice: {first.get('detail')}; then {second.get('detail')}"}
    if limit is not None:
        answer["limit"] = limit
    return answer


def _run_once(command: list[str], timeout: float, cwd: Path | None) -> dict:
    STARTED.add()
    try:
        proc = subprocess.run(command, capture_output=True, timeout=timeout, cwd=cwd,
                              stdin=subprocess.DEVNULL)
    except subprocess.TimeoutExpired:
        return {"ok": False, "error": "timeout", "detail": f"no answer within {timeout:g} s"}
    except OSError as err:
        return {"ok": False, "error": "worker", "detail": f"could not start {command[0]}: {err}"}
    if proc.returncode < 0:
        return {"ok": False, "error": "crash", "detail": f"killed by signal {-proc.returncode}",
                "stderr": tail(proc.stderr)}
    return {"exit": proc.returncode, "stdout": proc.stdout, "stderr": proc.stderr}


def tail(data: bytes | str) -> str:
    text = data.decode("utf-8", "replace") if isinstance(data, bytes) else data
    text = text.strip()
    return text if len(text) <= DETAIL_LIMIT else text[-DETAIL_LIMIT:]


def refusal(detail: bytes | str, exit_code: int | None = None) -> dict:
    """The member's own refusal of a file, kept in the cache as its answer."""
    answer = {"ok": False, "error": "refused", "detail": tail(detail)}
    if exit_code is not None:
        answer["exit"] = exit_code
    return answer


def failed(outcome: dict) -> bool:
    """Whether run() returned a transient failure rather than a finished process."""
    return "exit" not in outcome


def split_pages(text: str) -> list[str]:
    """Pages of a text a member ends with a form feed each (Poppler's pdftotext)."""
    pages = text.split("\f")
    if pages and pages[-1].strip() == "":
        pages.pop()
    return pages


def unsupported(member: str, kind: str) -> dict:
    return {"ok": False, "error": "unsupported", "detail": f"{member} does not answer {kind}"}
