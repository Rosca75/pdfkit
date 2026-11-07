# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# pdf-extract as a member of the reference panel, through panel-extract
# (tests/panel/rust/src/bin/panel-extract.rs): per page the text of
# `extract_text_from_mem_by_pages`. That call stops at the first page it cannot
# read, so a document can answer with fewer pages than it holds; the judge reads
# the page count it is given and this adapter repairs nothing.

from __future__ import annotations

from pathlib import Path

import readers
from readers import rust_member

NAME = "extract"
LICENCE = "MIT"
ROLE = "text"
CAPABILITIES = ("text",)


def version() -> str:
    return readers.pin(NAME)["version"]


def binary_digest() -> str:
    return readers.live_digest(readers.pin(NAME))


def missing() -> str | None:
    return readers.pin_problem(NAME)


def answer(path: Path, kind: str, timeout: float, request: dict) -> dict:
    del request
    if kind != "text":
        return readers.unsupported(NAME, kind)
    return rust_member.run(NAME, path, timeout)
