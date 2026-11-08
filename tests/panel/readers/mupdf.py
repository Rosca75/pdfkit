# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# MuPDF as a member of the reference panel, through `mutool`. `text`: `mutool
# draw -q -F txt -o <folder>/%d.txt <file>`, one file per page, each ending in a
# form feed, a page MuPDF cannot draw leaving no file, so its place is kept
# with no text and marked `missing` (pages after the last one written are not
# known); a non-zero exit after some pages were written is an answer with its
# messages, and a refusal only when no page was. `pages`: `mutool pages
# <file>`, one <page> element per page with its <MediaBox>, and <CropBox> and
# <Rotate> when the page states them.
# `image_pixels`: `mutool extract -r <file> <object>...` in a scratch folder,
# which writes image-<object>.png (or .pam), the image decoded (-r makes a
# JPEG or CMYK image decoded to RGB rather than written as its own bytes); a
# gray or bilevel image stays gray. A non-zero exit with MuPDF's message is its
# refusal of the file. The Linux build of mupdf-tools prints "ICC support is
# not available": its colour conversions are MuPDF's own formulas.
#
# Sources: the usage text of `mutool draw`, `mutool pages` and `mutool extract`
# 1.23.10 (each run without arguments on the Linux host, read on 2026-11-07);
# the output names and formats were seen on dataset files.

from __future__ import annotations

import re
from pathlib import Path

import readers
from readers import pixels

NAME = "mupdf"
LICENCE = "AGPL-3.0-or-later"
ROLE = "text, pixels"
CAPABILITIES = ("text", "pages", "image_pixels")

PAGE = re.compile(r'<page pagenum="(\d+)">(.*?)</page>', re.S)
BOX = re.compile(r'<(MediaBox|CropBox) l="([^"]*)" b="([^"]*)" r="([^"]*)" t="([^"]*)"')
ROTATE = re.compile(r'<Rotate v="(-?\d+)"')


def version() -> str:
    return readers.pin(NAME)["version"]


def binary_digest() -> str:
    return readers.live_digest(readers.pin(NAME))


def missing() -> str | None:
    return readers.pin_problem(NAME)


def mutool() -> str:
    return str(readers.binaries(readers.pin(NAME))[0][0])


def refused_or_failed(outcome: dict) -> dict | None:
    if readers.failed(outcome):
        return outcome
    if outcome["exit"] != 0:
        return readers.refusal(outcome["stderr"], outcome["exit"])
    return None


def page_text(raw: bytes) -> str:
    text = raw.decode("utf-8", "replace")
    return text[:-2] if text.endswith("\f\n") else text.rstrip("\f")


def parse_pages(stdout: bytes) -> dict:
    pages = []
    for match in PAGE.finditer(stdout.decode("utf-8", "replace")):
        body, page = match.group(2), {}
        for box in BOX.finditer(body):
            page["media" if box.group(1) == "MediaBox" else "crop"] = [float(v) for v in box.group(2, 3, 4, 5)]
        rotate = ROTATE.search(body)
        page["rotate"] = int(rotate.group(1)) if rotate else 0
        pages.append(page)
    return {"ok": True, "count": len(pages), "pages": pages}


def text_pages(written: dict[int, bytes]) -> list[dict]:
    """The pages in page order up to the last one MuPDF wrote; a page it wrote no file for keeps
    its place, marked `missing`, so every page is found by its number."""
    return [{"page": n, "text": page_text(written[n])} if n in written
            else {"page": n, "text": None, "missing": True}
            for n in range(1, max(written, default=0) + 1)]


def answer(path: Path, kind: str, timeout: float, request: dict, store=None) -> dict:
    if kind == "text":
        with readers.scratch() as folder:
            outcome = readers.run([mutool(), "draw", "-q", "-F", "txt", "-o", str(folder / "%d.txt"),
                                   str(path)], timeout)
            if readers.failed(outcome):
                return outcome
            written = {int(f.stem): f.read_bytes() for f in folder.glob("*.txt") if f.stem.isdigit()}
            if outcome["exit"] != 0 and not written:
                return readers.refusal(outcome["stderr"], outcome["exit"])
            answer = {"ok": True, "pages": text_pages(written)}
            if outcome["exit"] != 0:
                # MuPDF stops a page it cannot finish and says so; the pages it wrote are its answer.
                answer["exit"] = outcome["exit"]
                answer["messages"] = readers.tail(outcome["stderr"])
            return answer
    if kind == "pages":
        outcome = readers.run([mutool(), "pages", str(path)], timeout)
        return refused_or_failed(outcome) or parse_pages(outcome["stdout"])
    if kind == "image_pixels":
        objects = sorted({int(item["object"]) for item in request.get("images", [])})
        images: dict = {}
        with readers.scratch() as folder:
            outcome = readers.run([mutool(), "extract", "-r", str(path), *map(str, objects)], timeout,
                                  cwd=folder)
            problem = refused_or_failed(outcome)
            if problem is not None:
                return problem
            for number in objects:
                written = sorted(folder.glob(f"image-{number:04d}.*"))
                images[str(number)] = pixels.entry(written[0].read_bytes(), store) if written \
                    else {"error": "not_written"}
        return {"ok": True, "images": images}
    return readers.unsupported(NAME, kind)
