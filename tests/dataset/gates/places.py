# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# What the gates count as one occurrence and as the input's text, by the
# owner's rules: one occurrence is that place on that page, so every copy of a
# word drawn at one place (a duplicate, a second text layer, a copy at size 0)
# is one occurrence; and every write removes the content of a switched-off
# optional-content layer, so an occurrence there, or the text there, is not
# counted against the output. Two copies are at one place when the centre of
# either's box lies in the other's (the self-check's test of a glyph at a
# located match's place, docs/DESIGN.md section 18) on one page. An occurrence is a word of
# pdfkit_text (its `box`) or an occurrence of pdfkit_find (its first
# rectangle), each with its hidden classes.

from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[1] / "pdfium"))

OFF = "optional_content_off"


def box_of(occurrence: dict) -> list[float] | None:
    box = occurrence.get("box")
    if box is None:
        rectangles = occurrence.get("rectangles") or []
        box = rectangles[0] if rectangles else None
    return list(box) if box and len(box) == 4 else None


def centre_in(a: list[float], b: list[float]) -> bool:
    x, y = (a[0] + a[2]) / 2.0, (a[1] + a[3]) / 2.0
    return b[0] <= x <= b[2] and b[1] <= y <= b[3]


def at_one_place(a: list[float], b: list[float]) -> bool:
    return centre_in(a, b) or centre_in(b, a)


def is_off(occurrence: dict) -> bool:
    return OFF in (occurrence.get("hidden") or [])


def groups(occurrences: list[dict]) -> list[list[dict]]:
    """The occurrences outside switched-off layers, grouped by place, in their order."""
    out: list[list[dict]] = []
    for occurrence in occurrences or []:
        if is_off(occurrence):
            continue
        box = box_of(occurrence)
        home = None
        for group in out:
            if group[0].get("page") != occurrence.get("page"):
                continue
            if box is not None and any(at_one_place(box, b) for b in (box_of(o) for o in group) if b):
                home = group
                break
        if home is None:
            out.append([occurrence])
        else:
            home.append(occurrence)
    return out


def place_count(occurrences: list[dict] | None) -> int:
    """The number of places the occurrences are drawn at, switched-off layers left out."""
    return len(groups(occurrences or []))


def extra_copies(occurrences: list[dict] | None, box: list[float] | None, page=None) -> int:
    """The copies a reader of every glyph counts beyond one place's one: the other copies at the
    place of box on page, and every occurrence in a switched-off layer."""
    occurrences = occurrences or []
    off = sum(1 for o in occurrences if is_off(o))
    if box is None:
        return off
    for group in groups(occurrences):
        if group[0].get("page") != page:
            continue
        if any(b is not None and at_one_place(box, b) for b in (box_of(o) for o in group)):
            return off + len(group) - 1
    return off


def f1_without_off(before: str, after: str, off: str) -> float:
    """PDFium's word-bag F1 of the output's text against the input's, the input's words of the
    switched-off layers taken out (each once per time it occurs there)."""
    import words
    reference = words.words(before) - words.words(off)
    candidate = words.words(after)
    total_a, total_b = sum(candidate.values()), sum(reference.values())
    if total_a == 0 and total_b == 0:
        return 1.0
    if total_a == 0 or total_b == 0:
        return 0.0
    common = sum((candidate & reference).values())
    if common == 0:
        return 0.0
    precision, recall = common / total_a, common / total_b
    return 2 * precision * recall / (precision + recall)


def off_text(answer: dict) -> str:
    """The words of a pdfkit_text answer (with blocks) that lie in a switched-off layer."""
    out = []
    for page in answer.get("pages") or []:
        for key in ("blocks", "hidden_blocks"):
            for block in page.get(key) or []:
                for line in block.get("lines") or []:
                    for item in line.get("words") or []:
                        if OFF in (item.get("hidden") or []):
                            out.append(item.get("text") or "")
    return " ".join(out)


def off_text_of(command: list[str] | None, path: Path) -> str:
    """The switched-off layers' text of a file, read by pdfkit; empty when pdfkit is not built
    or does not answer."""
    if not command:
        return ""
    try:
        done = subprocess.run(command + ["text", str(path), "--options", '{"blocks": true}'],
                              capture_output=True, timeout=300, check=False)
    except (OSError, subprocess.TimeoutExpired):
        return ""
    if done.returncode != 0:
        return ""
    try:
        return off_text(json.loads(done.stdout.decode("utf-8")))
    except (ValueError, UnicodeDecodeError):
        return ""


