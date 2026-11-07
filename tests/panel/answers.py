# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# The reference panel's answers about the dataset's input files, kept between
# runs. An input file never changes under its SHA-256 and a member never
# changes under its version and its binaries' digest, so what a member says
# about an input is computed once and read back by every later collection and
# judgement, in every worktree: the cache lives in build/cache/panel-results/
# of the main checkout (CLAUDE.md section 11), or $PDFKIT_PANEL_RESULTS.
#
# A record's path holds its whole key: the member, its version and the first 12
# hex digits of its binaries' digest in the folder name, then the kind, then the
# input's SHA-256 and a digest of the request and SCHEMA, which a change to what
# an adapter answers raises:
#   <member>-<version>-<digest12>/<kind>/<sha[:2]>/<sha>-<request key>.json
# The pixels of an image answer are kept beside them as blobs named by the
# SHA-256 of the samples (zlib-compressed: <kind>/blobs/<sha[:2]>/<sha>.bin),
# so the record stays small and equal pixels are stored once. A transient
# failure (a timeout, a crash) is never kept, so the next collection asks
# again; a member's refusal of a file is its answer and is kept. Records are
# written to a temporary name and renamed, so concurrent writers never leave a
# torn file. PDFium's text and page count are read from PDFium's own cache
# (tests/pdfium/results_cache.py) and never copied here. Delete the folder to
# start again; nothing else holds state.

from __future__ import annotations

import hashlib
import json
import os
import sys
import tempfile
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / "pdfium"))
sys.path.insert(0, str(HERE.parents[1] / "dataset" / "tools"))

import common  # noqa: E402
import readers  # noqa: E402
import results_cache  # noqa: E402

# Raised whenever an adapter's answer for a cached request changes shape or meaning.
SCHEMA = 1
TRANSIENT = readers.TRANSIENT


def root() -> Path:
    """build/cache/panel-results of the main checkout, or $PDFKIT_PANEL_RESULTS."""
    override = os.environ.get("PDFKIT_PANEL_RESULTS")
    return Path(override) if override else common.pdfium_cache_dir().parent / "panel-results"


def safe(text: str) -> str:
    """A version string as a folder name: anything but letters, digits, '.', '+' and '_' is '-'."""
    return "".join(c if c.isalnum() or c in "._+" else "-" for c in text)


def member_folder(name: str, version: str, digest: str) -> str:
    return f"{name}-{safe(version)}-{digest[:12]}"


def request_key(kind: str, request: dict) -> str:
    """A short digest of everything besides the input that decides the answer."""
    text = json.dumps({"kind": kind, "schema": SCHEMA, "request": request}, sort_keys=True)
    return hashlib.sha256(text.encode("utf-8")).hexdigest()[:16]


def record_path(folder: str, file_digest: str, kind: str, request: dict) -> Path:
    return root() / folder / kind / file_digest[:2] / f"{file_digest}-{request_key(kind, request)}.json"


def blob_path(folder: str, kind: str, digest: str) -> Path:
    return root() / folder / kind / "blobs" / digest[:2] / f"{digest}.bin"


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
        raise


def write_json(path: Path, value) -> None:
    write_bytes(path, json.dumps(value, ensure_ascii=True, sort_keys=True).encode("ascii"))


def keepable(answer: dict) -> bool:
    """Whether an answer describes the file (success or the member's own refusal)."""
    return bool(answer.get("ok")) or answer.get("error") not in TRANSIENT


def store_pixels(folder: str, kind: str, answer: dict) -> dict:
    """Moves each image's samples into a blob and leaves its digest in the answer."""
    for entry in (answer.get("images") or {}).values():
        samples = entry.pop("samples", None)
        if samples is None:
            continue
        digest = hashlib.sha256(samples).hexdigest()
        target = blob_path(folder, kind, digest)
        if not target.exists():
            write_bytes(target, zlib.compress(samples, 6))
        entry["blob"] = digest
    return answer


def read_blob(folder: str, kind: str, digest: str) -> bytes | None:
    """The samples a blob holds, or None when it is missing or does not match its name."""
    try:
        samples = zlib.decompress(blob_path(folder, kind, digest).read_bytes())
    except (OSError, zlib.error):
        return None
    return samples if hashlib.sha256(samples).hexdigest() == digest else None


class Stats:
    """What a collection did per member and kind: answers read back, asked, kept."""

    def __init__(self) -> None:
        self.cached = 0
        self.asked = 0


def cached_answer(member, path: Path, kind: str, *, timeout: float = 30.0, stats: Stats | None = None,
                  **request) -> dict:
    """member.answer(path, kind, timeout, request) for an input file, from the cache when it can be.

    `member` is an adapter module of tests/panel/readers/. The timeout is not part of the key:
    an answer that arrived within any limit is the same answer."""
    own = getattr(member, "cached_elsewhere", None)
    if own is not None and kind in getattr(member, "CACHED_ELSEWHERE", ()):
        return own(path, kind, timeout, request, stats)
    folder = member_folder(member.NAME, member.version(), member.binary_digest())
    record = record_path(folder, results_cache.file_digest(path), kind, request)
    answer = read_json(record)
    if isinstance(answer, dict):
        if stats is not None:
            stats.cached += 1
        return answer
    if stats is not None:
        stats.asked += 1
    answer = member.answer(path, kind, timeout, request)
    if keepable(answer):
        answer = store_pixels(folder, kind, answer)
        write_json(record, answer)
    return answer
