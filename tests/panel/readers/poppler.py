# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# Poppler as a member of the reference panel, through its command-line tools.
# `text`: `pdftotext -enc UTF-8 <file> -`, whose output ends every page with a
# form feed, split into pages. `pages`: `pdfinfo -box -f 1 -l <n>`, one size,
# rotation and media and crop box per page. `image_pixels`: for each page that
# draws a wanted image, `pdfimages -list -f <p> -l <p>` names the images in
# draw order with their object numbers, and `pdfimages -f <p> -l <p>` writes
# them in the same order, numbered from 0, in its default formats (PBM for a
# bilevel image, PGM or PPM otherwise, CMYK converted to RGB), which
# readers/pixels.py reads. `image_list`: per page asked, the object numbers of
# the images the page draws, in draw order, which aligns PDFium's image objects
# where two objects hold the same bytes. A non-zero exit with Poppler's message
# is its refusal of the file.
#
# Sources: the usage text of pdftotext, pdfinfo and pdfimages 24.02.0 (each run
# with -h on the Linux host, read on 2026-11-07); the form feed after each
# page, the numbering from 0 within a page range, the listing's columns and the
# default formats were seen on qpdf-0026, synth-0263 and dccc-0003.

from __future__ import annotations

import re
from pathlib import Path

import readers
from readers import pixels

NAME = "poppler"
LICENCE = "GPL-2.0-or-later"
ROLE = "text, pixels"
CAPABILITIES = ("text", "pages", "image_pixels", "image_list")
EXECUTABLES = ("pdftotext", "pdfinfo", "pdfimages", "pdftoppm")

PAGE_LINE = re.compile(r"^Page\s+(\d+)\s+(size|rot|MediaBox|CropBox):\s*(.*)$")
LIST_ROW = re.compile(r"^\s*(\d+)\s+(\d+)\s+(\S+)\s+(\d+)\s+(\d+)\s+(\S+)\s+(\d+)\s+(\d+)\s+(\S+)\s+(\S+)"
                      r"\s+(\d+|\[inline\])\s+(\d+|-)")


def version() -> str:
    return readers.pin(NAME)["version"]


def binary_digest() -> str:
    return readers.live_digest(readers.pin(NAME))


def missing() -> str | None:
    return readers.pin_problem(NAME)


def tool(name: str) -> str:
    """The pinned path of one of Poppler's executables."""
    for path, _ in readers.binaries(readers.pin(NAME)):
        if path.stem == name:
            return str(path)
    return name


def parse_text(stdout: bytes) -> dict:
    return {"ok": True, "pages": [{"text": page}
                                  for page in readers.split_pages(stdout.decode("utf-8", "replace"))]}


def parse_info(stdout: bytes) -> dict:
    pages: dict[int, dict] = {}
    for line in stdout.decode("utf-8", "replace").splitlines():
        match = PAGE_LINE.match(line)
        if not match:
            continue
        number, field, value = int(match.group(1)), match.group(2), match.group(3)
        page = pages.setdefault(number, {})
        if field == "size":
            size = re.match(r"([\d.]+) x ([\d.]+)", value)
            if size:
                page["size"] = [float(size.group(1)), float(size.group(2))]
        elif field == "rot":
            page["rotate"] = int(value.split()[0])
        else:
            page["media" if field == "MediaBox" else "crop"] = [float(v) for v in value.split()]
    ordered = [pages[n] for n in sorted(pages)]
    return {"ok": True, "count": len(ordered), "pages": ordered}


def parse_list(stdout: bytes) -> list[dict]:
    """The rows of `pdfimages -list`: page, num, type, width, height, colour, bpc, object."""
    rows = []
    for line in stdout.decode("utf-8", "replace").splitlines():
        match = LIST_ROW.match(line)
        if match:
            rows.append({"page": int(match.group(1)), "num": int(match.group(2)), "type": match.group(3),
                         "width": int(match.group(4)), "height": int(match.group(5)),
                         "color": match.group(6), "bpc": int(match.group(8)), "enc": match.group(9),
                         "object": None if match.group(11) == "[inline]" else int(match.group(11))})
    return rows


def finished_or_refused(outcome: dict) -> dict | None:
    """None for a clean exit; else the transient failure or Poppler's refusal."""
    if readers.failed(outcome):
        return outcome
    if outcome["exit"] != 0:
        return readers.refusal(outcome["stderr"], outcome["exit"])
    return None


def page_pixels(path: Path, page: int, objects: set[int], timeout: float) -> dict:
    """object -> pixel answer for the wanted objects one page draws."""
    listed = readers.run([tool("pdfimages"), "-list", "-f", str(page), "-l", str(page), str(path)], timeout)
    problem = finished_or_refused(listed)
    if problem is not None:
        return {"__problem__": problem}
    rows = parse_list(listed["stdout"])
    out: dict = {}
    with readers.scratch() as folder:
        written = readers.run([tool("pdfimages"), "-f", str(page), "-l", str(page), str(path),
                               str(folder / "img")], timeout)
        problem = finished_or_refused(written)
        if problem is not None:
            return {"__problem__": problem}
        files = {int(f.stem.rsplit("-", 1)[1]): f for f in folder.iterdir() if f.stem.startswith("img-")}
        for row in rows:
            if row["object"] not in objects or str(row["object"]) in out or row["type"] in ("smask", "mask"):
                continue
            image = files.get(row["num"])
            if image is None:
                out[str(row["object"])] = {"error": "not_written"}
                continue
            try:
                out[str(row["object"])] = dict(pixels.compact(pixels.read_any(image.read_bytes())), type=row["type"])
            except pixels.PixelError as err:
                out[str(row["object"])] = {"error": "unreadable_output", "detail": str(err)}
    return out


def answer(path: Path, kind: str, timeout: float, request: dict) -> dict:
    if kind == "text":
        outcome = readers.run([tool("pdftotext"), "-enc", "UTF-8", str(path), "-"], timeout)
        return finished_or_refused(outcome) or parse_text(outcome["stdout"])
    if kind == "pages":
        outcome = readers.run([tool("pdfinfo"), "-box", "-f", "1", "-l", "1000000", str(path)], timeout)
        return finished_or_refused(outcome) or parse_info(outcome["stdout"])
    if kind == "image_pixels":
        images: dict = {}
        by_page: dict[int, set[int]] = {}
        for item in request.get("images", []):
            by_page.setdefault(min(item["pages"]), set()).add(int(item["object"]))
        for page, objects in sorted(by_page.items()):
            found = page_pixels(path, page, objects, timeout)
            if "__problem__" in found:
                return found["__problem__"]
            images.update(found)
        for item in request.get("images", []):
            images.setdefault(str(item["object"]), {"error": "not_found"})
        return {"ok": True, "images": images}
    if kind == "image_list":
        order = {}
        for page in sorted({int(p) for p in request.get("pages", [])}):
            listed = readers.run([tool("pdfimages"), "-list", "-f", str(page), "-l", str(page), str(path)],
                                 timeout)
            problem = finished_or_refused(listed)
            if problem is not None:
                return problem
            order[str(page)] = drawn_objects(parse_list(listed["stdout"]))
        return {"ok": True, "order": order}
    return readers.unsupported(NAME, kind)


def drawn_objects(rows: list[dict]) -> list:
    """The object numbers of the images a page draws, in draw order: a soft mask or a mask is
    listed by pdfimages after its image but is not drawn by itself."""
    return [row["object"] for row in rows if row["type"] not in ("smask", "mask")]
