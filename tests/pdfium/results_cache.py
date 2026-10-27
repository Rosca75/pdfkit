# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# PDFium's answers about the dataset's input files, kept between runs. An input
# file never changes under its SHA-256 and PDFium never changes under its pinned
# release, so what PDFium says about an input (its text, its Info fields, its
# page count, the hashes of its rendered pages, its image objects) is computed
# once and read back by every later gate run, in every worktree: the cache lives
# in build/cache/pdfium-results/ of the main checkout (CLAUDE.md section 11).
# Only the outputs a run writes, which change with pdfkit, are asked of PDFium
# each time.
#
# A record's key is the input's SHA-256, the PDFium release and its pinned
# archive digest (tests/pdfium/fetch.py), the request (operations and
# parameters, such as the render resolution) and SCHEMA, which a change to what
# a worker answers raises. A transient failure (a timeout, a crashed worker) is
# never kept, so the next run asks again. Records are written to a temporary
# name and renamed, so concurrent writers never leave a torn file. Delete the
# folder to start again; nothing else holds state.

from __future__ import annotations

import functools
import hashlib
import json
import os
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[1] / "dataset" / "tools"))

import common  # noqa: E402

# Raised whenever a worker's answer for a cached request changes shape or meaning.
SCHEMA = 1
# Failures that say nothing about the file: a worker that hung or died.
TRANSIENT = {"timeout", "crash", "worker"}


@functools.lru_cache(maxsize=1)
def pdfium_release() -> str:
    """The pinned PDFium release and archive digest, as a folder name."""
    import importlib.util

    spec = importlib.util.spec_from_file_location("pdfium_fetch", HERE / "fetch.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return f"{module.TAG.replace('/', '-')}-{module.SHA256[:12]}"


def root() -> Path:
    """build/cache/pdfium-results/<release> of the main checkout, or $PDFKIT_PDFIUM_RESULTS."""
    override = os.environ.get("PDFKIT_PDFIUM_RESULTS")
    base = Path(override) if override else common.pdfium_cache_dir().parent / "pdfium-results"
    return base / pdfium_release()


@functools.lru_cache(maxsize=4096)
def _digest(path: str, size: int, mtime_ns: int) -> str:
    del size, mtime_ns  # part of the memo key only
    hasher = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            hasher.update(block)
    return hasher.hexdigest()


def file_digest(path: Path) -> str:
    """The SHA-256 of a file's bytes, computed once per process and version of the file."""
    stat = os.stat(path)
    return _digest(str(path), stat.st_size, stat.st_mtime_ns)


def request_key(kind: str, request: dict) -> str:
    """A short digest of everything besides the input that decides the answer."""
    text = json.dumps({"kind": kind, "schema": SCHEMA, "request": request}, sort_keys=True)
    return hashlib.sha256(text.encode("utf-8")).hexdigest()[:16]


def record_path(digest: str, kind: str, request: dict, suffix: str = ".json") -> Path:
    return root() / kind / digest[:2] / f"{digest}-{request_key(kind, request)}{suffix}"


def blob_path(kind: str, digest: str) -> Path:
    """Where content named by its own SHA-256 is kept (an image's encoded bytes)."""
    return root() / kind / digest[:2] / f"{digest}.bin"


def scratch() -> Path:
    """A folder for a worker's temporary files beside the cache, created on demand."""
    folder = root() / "tmp"
    folder.mkdir(parents=True, exist_ok=True)
    return folder


def read_json(path: Path):
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None


def write_bytes(path: Path, data: bytes) -> None:
    """Writes a record atomically: a temporary file beside it, then a rename."""
    path.parent.mkdir(parents=True, exist_ok=True)
    handle, temporary = tempfile.mkstemp(dir=path.parent, prefix=".tmp-")
    try:
        with os.fdopen(handle, "wb") as out:
            out.write(data)
        os.replace(temporary, path)
    except OSError:
        try:
            os.unlink(temporary)
        except OSError:
            pass


def write_json(path: Path, value) -> None:
    write_bytes(path, json.dumps(value, ensure_ascii=True).encode("ascii"))


def keepable(answer: dict) -> bool:
    """Whether an answer describes the file (success or the file's own failure)."""
    return bool(answer.get("ok")) or answer.get("error") not in TRANSIENT


def cached_probe(pool, path: Path, ops, **extra) -> dict:
    """pool.probe(path, ops, **extra) for an input file, answered from the cache when it can be."""
    request = {"ops": sorted(set(ops)), **extra}
    record = record_path(file_digest(path), "probe", request)
    answer = read_json(record)
    if isinstance(answer, dict):
        return answer
    answer = pool.probe(path, ops, **extra)
    if keepable(answer):
        write_json(record, answer)
    return answer
