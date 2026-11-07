# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# pdf_oxide as a member of the reference panel, through panel-oxide
# (tests/panel/rust/src/bin/panel-oxide.rs): per page the text of
# `PdfDocument::extract_text` and the visible box of
# `PdfDocument::get_page_visible_box`, the crop box reduced to the media box.
# `text` keeps the texts, `pages` the boxes, from one run each. Its positioned
# words are sometimes joined ("1Scope", the owner's evaluation of 0.3.78): the
# majority absorbs a member's quirk, and this adapter repairs nothing.

from __future__ import annotations

from pathlib import Path

import readers
from readers import rust_member

NAME = "oxide"
LICENCE = "MIT OR Apache-2.0"
ROLE = "text"
CAPABILITIES = ("text", "pages")


def version() -> str:
    return readers.pin(NAME)["version"]


def binary_digest() -> str:
    return readers.live_digest(readers.pin(NAME))


def missing() -> str | None:
    return readers.pin_problem(NAME)


def shape(answer: dict, kind: str) -> dict:
    if not answer.get("ok"):
        return answer
    pages = answer.get("pages", [])
    if kind == "text":
        return {"ok": True, "pages": [{key: page[key] for key in ("text", "error") if key in page}
                                      for page in pages]}
    out = []
    for page in pages:
        box = page.get("box")
        out.append({"visible": box, "size": [round(box[2] - box[0], 3), round(box[3] - box[1], 3)]}
                   if box and None not in box else {"visible": None})
    return {"ok": True, "count": len(out), "pages": out}


def answer(path: Path, kind: str, timeout: float, request: dict) -> dict:
    del request
    if kind not in CAPABILITIES:
        return readers.unsupported(NAME, kind)
    return shape(rust_member.run(NAME, path, timeout), kind)
