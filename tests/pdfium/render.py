# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# Page rendering through the pinned PDFium and the pixel comparison the writer,
# page selection and redaction gates share. A page is rendered at 100 dpi with
# annotations off (FPDF_RenderPageBitmap with flags 0, so FPDF_ANNOT is not set;
# fpdfview.h, https://raw.githubusercontent.com/chromium/pdfium/main/public/fpdfview.h)
# on a white background into BGRA pixels. Two pages are compared exactly, or
# outside a set of rectangles (page space in points, lower-left origin, x0 y0 x1
# y1) widened by one pixel on every side, which tolerates the anti-aliasing
# around a changed rectangle. PDFium can hang or crash on a hostile file, so the
# rendering runs in worker processes (this file run with --serve, a subclass of
# the pool's worker): a request names two files and a list of page pairs, the
# worker renders both pages of each pair and answers the comparison, so no
# bitmap crosses the pipe or touches the disk. A worker keeps the last two
# documents it opened. A request that times out or crashes is recorded per page
# as a failure with its cause.

from __future__ import annotations

import argparse
import json
import math
import os
import sys
from dataclasses import dataclass
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import pdfium  # noqa: E402

DPI = 100.0
DILATION = 1
CHUNK = 6
CHUNK_TIMEOUT = 90.0
PAGE_TIMEOUT = 30.0
CACHE_SIZE = 2


@dataclass
class Bitmap:
    """Rendered pixels: `stride` bytes per row, four bytes (B, G, R, A) per pixel."""

    width: int
    height: int
    stride: int
    data: bytes

    def row(self, y: int) -> bytes:
        return self.data[y * self.stride:y * self.stride + self.width * 4]


def pixel_boxes(rectangles, width: int, height: int, dpi: float = DPI, dilation: int = DILATION) -> list:
    """Pixel boxes (x0, y0, x1, y1; end exclusive) of page-space rectangles, widened by `dilation`.

    A rectangle's lower-left origin maps to the bitmap's top-left origin: pixel row
    y covers page heights from (height - y - 1) to (height - y) pixels."""
    scale = dpi / 72.0
    boxes = []
    for x0, y0, x1, y1 in rectangles:
        left, right = math.floor(x0 * scale), math.ceil(x1 * scale)
        top, bottom = math.floor(height - y1 * scale), math.ceil(height - y0 * scale)
        boxes.append((max(0, left - dilation), max(0, top - dilation),
                      min(width, right + dilation), min(height, bottom + dilation)))
    return boxes


def differing_pixels(a: Bitmap, b: Bitmap, boxes=()) -> tuple[int, int]:
    """(pixels that differ, of those the ones outside every box) for two bitmaps of one size."""
    total = outside = 0
    for y in range(a.height):
        row_a, row_b = a.row(y), b.row(y)
        if row_a == row_b:
            continue
        words_a, words_b = memoryview(row_a).cast("I"), memoryview(row_b).cast("I")
        covering = [(x0, x1) for x0, y0, x1, y1 in boxes if y0 <= y < y1]
        for x in range(a.width):
            if words_a[x] == words_b[x]:
                continue
            total += 1
            if not any(x0 <= x < x1 for x0, x1 in covering):
                outside += 1
    return total, outside


def compare_bitmaps(a: Bitmap, b: Bitmap, rectangles=None, dpi: float = DPI) -> dict:
    """The comparison of two rendered pages.

    Exact: `identical` is true when sizes and every pixel agree. With `rectangles`
    (a list, possibly empty) the answer also holds `outside_pixels`, the differing
    pixels outside every rectangle widened by one pixel, and `stable`, true when
    that count is zero and the sizes agree."""
    if (a.width, a.height) != (b.width, b.height):
        answer = {"identical": False, "differing_pixels": None, "size_differs": True, "error": None}
        if rectangles is not None:
            answer.update({"outside_pixels": None, "stable": False})
        return answer
    boxes = pixel_boxes(rectangles, a.width, a.height, dpi) if rectangles is not None else ()
    total, outside = differing_pixels(a, b, boxes)
    answer = {"identical": total == 0, "differing_pixels": total, "size_differs": False, "error": None}
    if rectangles is not None:
        answer.update({"outside_pixels": outside, "stable": outside == 0})
    return answer


# ---------------------------------------------------------------------------
# The worker side


class Opened:
    """The last documents a worker opened, keyed by path, size and modification time."""

    def __init__(self, library: pdfium.Library):
        self.library = library
        self.documents: dict = {}

    def get(self, path: str):
        stat = os.stat(path)
        key = (path, stat.st_size, stat.st_mtime_ns)
        if key in self.documents:
            document = self.documents.pop(key)
            self.documents[key] = document
            return document
        document = self.library.open(Path(path).read_bytes())
        self.documents[key] = document
        while len(self.documents) > CACHE_SIZE:
            oldest = next(iter(self.documents))
            self.documents.pop(oldest).close()
        return document


def render_bitmap(document, index: int, dpi: float) -> Bitmap:
    image = document.render(index, dpi, annotations=False)
    return Bitmap(image["width"], image["height"], image["stride"], image["bgra"])


def handle_compare(opened: Opened, request: dict) -> dict:
    """Compares page pairs of two files: {"ok", "before_pages", "after_pages", "pairs": [...]}."""
    dpi = float(request.get("dpi", DPI))
    pairs = request["pairs"]
    rectangles = request.get("rectangles")
    documents, pages, failures = {}, {}, {}
    for side in ("before", "after"):
        try:
            documents[side] = opened.get(request[side])
            pages[side] = documents[side].page_count()
        except pdfium.PdfiumError as err:
            pages[side] = None
            failures[side] = f"{side}_open:{err.name}"
        except OSError as err:
            pages[side] = None
            failures[side] = f"{side}_open:{err.strerror}"
    results = []
    for number, (before_index, after_index) in enumerate(pairs):
        rects = rectangles[number] if rectangles else None
        if failures:
            results.append({"identical": False, "differing_pixels": None, "size_differs": False,
                            "error": "; ".join(sorted(failures.values()))})
            continue
        try:
            bitmaps = [render_bitmap(documents["before"], before_index, dpi),
                       render_bitmap(documents["after"], after_index, dpi)]
        except pdfium.PdfiumError as err:
            results.append({"identical": False, "differing_pixels": None, "size_differs": False,
                            "error": f"render:{err.name}"})
            continue
        results.append(compare_bitmaps(bitmaps[0], bitmaps[1], rects, dpi))
    return {"ok": True, "before_pages": pages["before"], "after_pages": pages["after"], "pairs": results}


def serve(dll: Path) -> None:
    """Reads one JSON request per line on stdin and answers one JSON line each."""
    library = pdfium.Library(dll)
    opened = Opened(library)
    for line in sys.stdin.buffer:
        if not line.strip():
            continue
        try:
            request = json.loads(line)
            # A request without page pairs is one of the plain probes (page count, text).
            response = handle_compare(opened, request) if "pairs" in request                 else pdfium.handle_request(library, request)
        except Exception as err:  # a worker answers every request, whatever fails
            response = {"ok": False, "error": "worker", "detail": repr(err)}
        sys.stdout.write(json.dumps(response, ensure_ascii=True) + "\n")
        sys.stdout.flush()


# ---------------------------------------------------------------------------
# The caller side


class RenderWorker(pdfium.Worker):
    """A pool worker whose process runs this file."""

    script = Path(__file__).resolve()


def render_pool(size: int = 4, dll: Path | None = None) -> pdfium.Pool:
    return pdfium.Pool(size, dll, worker_class=RenderWorker)


def failure(cause: str, detail: str = "") -> dict:
    return {"identical": False, "differing_pixels": None, "size_differs": False,
            "error": f"{cause}:{detail}" if detail else cause}


def compare_pages(pool: pdfium.Pool, before: Path, after: Path, pairs: list, rectangles=None,
                  dpi: float = DPI) -> dict:
    """Compares page pairs [before_index, after_index] (0-based) of two files.

    Returns {"before_pages", "after_pages", "pairs": [one answer per pair]}; each
    answer holds identical, differing_pixels, size_differs and error (None when
    the pair was compared). A chunk that times out or crashes is asked again one
    pair at a time, so a failure lands on the page that caused it, with its cause."""
    results: list = [None] * len(pairs)
    counts = {"before_pages": None, "after_pages": None}

    def ask(indexes: list, timeout: float) -> dict:
        request = {"before": str(before), "after": str(after), "dpi": dpi,
                   "pairs": [pairs[i] for i in indexes]}
        if rectangles is not None:
            request["rectangles"] = [rectangles[i] for i in indexes]
        return pool.ask(request, timeout)

    for start in range(0, len(pairs), CHUNK):
        indexes = list(range(start, min(start + CHUNK, len(pairs))))
        answer = ask(indexes, CHUNK_TIMEOUT)
        if answer.get("ok"):
            counts["before_pages"] = answer["before_pages"]
            counts["after_pages"] = answer["after_pages"]
            for index, pair in zip(indexes, answer["pairs"]):
                results[index] = pair
            continue
        for index in indexes:
            single = ask([index], PAGE_TIMEOUT)
            if single.get("ok"):
                results[index] = single["pairs"][0]
            else:
                results[index] = failure(single.get("error", "worker"), single.get("detail", ""))
    return {**counts, "pairs": results}


def page_counts(pool: pdfium.Pool, path: Path) -> int | None:
    """PDFium's page count of a file, or None when it does not open."""
    answer = pool.probe(path, ["info"])
    return answer["pages"] if answer.get("ok") else None


def main() -> int:
    parser = argparse.ArgumentParser(description="Render pages through PDFium and compare them.")
    parser.add_argument("--serve", action="store_true", help="run as a worker on stdin/stdout")
    parser.add_argument("--dll", help="the pdfium.dll to load (default: the pinned cache)")
    parser.add_argument("--self-compare", nargs="+", metavar="FILE",
                        help="render every page of each file twice and report the pages that differ")
    parser.add_argument("--compare", nargs=2, metavar=("BEFORE", "AFTER"),
                        help="compare the pages of two files pairwise")
    args = parser.parse_args()
    dll = Path(args.dll) if args.dll else pdfium.default_dll()
    if args.serve:
        serve(dll)
        return 0
    with render_pool(2, dll) as pool:
        if args.compare:
            before, after = (Path(p) for p in args.compare)
            count = page_counts(pool, before) or 0
            result = compare_pages(pool, before, after, [[i, i] for i in range(count)])
            print(json.dumps(result, indent=1))
        for name in args.self_compare or []:
            path = Path(name)
            count = page_counts(pool, path) or 0
            result = compare_pages(pool, path, path, [[i, i] for i in range(count)])
            identical = sum(1 for p in result["pairs"] if p["identical"])
            print(json.dumps({"file": name, "pages": count, "identical": identical,
                              "errors": [p["error"] for p in result["pairs"] if p["error"]]}))
    return 0


if __name__ == "__main__":
    os.environ.setdefault("PYTHONIOENCODING", "utf-8")
    sys.exit(main())
