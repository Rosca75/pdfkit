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
# bitmap crosses the pipe or touches the disk. Each file is read in a library
# reset with only that file open, because PDFium carries font state from one
# document to the next (pdfium.Library.reset). A request that times out or
# crashes is recorded per page as a failure with its cause. The input side of a pair ("before") is an
# unchanged dataset file, so its pages are rendered once: the worker keeps each
# input page's size and the SHA-256 of its pixels in the results cache
# (results_cache.py), keyed by the file's SHA-256, the PDFium release and the
# resolution. An output page whose pixels hash equal to its input page's is
# identical without rendering the input again; only a page that differs
# renders its input to count the differing pixels.

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import sys
from dataclasses import dataclass
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import pdfium  # noqa: E402
import results_cache  # noqa: E402

DPI = 100.0
DILATION = 1
CHUNK = 6
CHUNK_TIMEOUT = 90.0
PAGE_TIMEOUT = 30.0


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
        differing = [x for x, (p, q) in enumerate(zip(words_a, words_b)) if p != q]
        total += len(differing)
        covering = [(x0, x1) for x0, y0, x1, y1 in boxes if y0 <= y < y1]
        if not covering:
            outside += len(differing)
            continue
        outside += sum(1 for x in differing if not any(x0 <= x < x1 for x0, x1 in covering))
    return total, outside


def pixels_digest(bitmap: Bitmap) -> str:
    """The SHA-256 of a bitmap's pixels, row after row with the padding left out: equal
    digests are equal pages for compare_bitmaps."""
    if bitmap.stride == bitmap.width * 4:
        return hashlib.sha256(bitmap.data[:bitmap.stride * bitmap.height]).hexdigest()
    hasher = hashlib.sha256()
    for y in range(bitmap.height):
        hasher.update(bitmap.row(y))
    return hasher.hexdigest()


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


def render_bitmap(document, index: int, dpi: float) -> Bitmap:
    image = document.render(index, dpi, annotations=False)
    return Bitmap(image["width"], image["height"], image["stride"], image["bgra"])


def open_alone(library: pdfium.Library, data: bytes):
    """A document opened in a library just reset, so nothing another file left in PDFium's font
    state reaches its pages (pdfium.Library.reset). The caller closes it before the next."""
    library.reset()
    return library.open(data)


class InputPages:
    """What the results cache holds of an input file: its page count, and per page its size and
    the SHA-256 of its pixels, or the error rendering it raised. Without a digest it holds
    nothing and every page is rendered."""

    def __init__(self, digest: str | None, dpi: float):
        self.digest, self.dpi = digest, dpi

    def count_record(self) -> Path | None:
        return results_cache.record_path(self.digest, "render-document", {}) if self.digest else None

    def page_record(self, index: int) -> Path | None:
        if self.digest is None:
            return None
        return results_cache.record_path(self.digest, "render-page", {"dpi": self.dpi, "page": index})

    def count(self) -> dict | None:
        record = self.count_record()
        cached = results_cache.read_json(record) if record else None
        return cached if isinstance(cached, dict) else None

    def page(self, index: int) -> dict | None:
        record = self.page_record(index)
        cached = results_cache.read_json(record) if record else None
        if isinstance(cached, dict) and (cached.get("error") is not None or cached.get("pixels")):
            return cached
        return None

    def keep(self, record: Path | None, value: dict) -> None:
        if record is not None:
            results_cache.write_json(record, value)


def page_answer(document, index: int, dpi: float) -> tuple[dict, Bitmap | None]:
    """A page's {"width", "height", "pixels"} and its bitmap, or {"error"} and None."""
    try:
        bitmap = render_bitmap(document, index, dpi)
    except pdfium.PdfiumError as err:
        return {"error": f"render:{err.name}"}, None
    return {"width": bitmap.width, "height": bitmap.height, "pixels": pixels_digest(bitmap)}, bitmap


def same_bitmap(rectangles) -> dict:
    """The answer compare_bitmaps gives for two equal bitmaps."""
    answer = {"identical": True, "differing_pixels": 0, "size_differs": False, "error": None}
    if rectangles is not None:
        answer.update({"outside_pixels": 0, "stable": True})
    return answer


def render_failure(error: str) -> dict:
    return {"identical": False, "differing_pixels": None, "size_differs": False, "error": error}


def handle_compare(library: pdfium.Library, request: dict) -> dict:
    """Compares page pairs of two files: {"ok", "before_pages", "after_pages", "pairs": [...]}.

    Each file is read in a library just reset (open_alone), the input before the output, so a
    page's pixels depend on its own file only. With "before_digest" (the SHA-256 of the input
    file) the input's page count and pages come from the results cache when it holds them
    and go into it when it does not; an input page is rendered only when the cache lacks it,
    or when its output page differs and the differing pixels must be counted."""
    dpi = float(request.get("dpi", DPI))
    pairs = request["pairs"]
    rectangles = request.get("rectangles")
    cache = InputPages(request.get("before_digest"), dpi)
    data, pages, failures = {}, {"before": None, "after": None}, {}
    for side in ("before", "after"):
        try:
            data[side] = Path(request[side]).read_bytes()
        except OSError as err:
            failures[side] = f"{side}_open:{err.strerror}"
    wanted = list(dict.fromkeys(before_index for before_index, _ in pairs))
    inputs = {index: cache.page(index) for index in wanted}
    rendered: dict = {}
    if "before" in data:
        counted = cache.count()
        if counted is not None and all(inputs.values()):
            if counted.get("error") is not None:
                failures["before"] = f"before_open:{counted['error']}"
            else:
                pages["before"] = int(counted["pages"])
        else:
            try:
                document = open_alone(library, data["before"])
            except pdfium.PdfiumError as err:
                failures["before"] = f"before_open:{err.name}"
                cache.keep(cache.count_record(), {"error": err.name, "code": err.code})
            else:
                try:
                    pages["before"] = document.page_count()
                    cache.keep(cache.count_record(), {"pages": pages["before"]})
                    for index in wanted:
                        if inputs[index] is None:
                            inputs[index], rendered[index] = page_answer(document, index, dpi)
                            cache.keep(cache.page_record(index), inputs[index])
                finally:
                    document.close()
    results: list = [None] * len(pairs)
    pending = []
    if "after" in data:
        try:
            document = open_alone(library, data["after"])
        except pdfium.PdfiumError as err:
            failures["after"] = f"after_open:{err.name}"
        else:
            try:
                pages["after"] = document.page_count()
                for number, (before_index, after_index) in enumerate(pairs):
                    if failures:
                        break
                    rects = rectangles[number] if rectangles else None
                    page = inputs[before_index]
                    if page.get("error") is not None:
                        results[number] = render_failure(page["error"])
                        continue
                    try:
                        after = render_bitmap(document, after_index, dpi)
                    except pdfium.PdfiumError as err:
                        results[number] = render_failure(f"render:{err.name}")
                        continue
                    if (page["width"], page["height"]) == (after.width, after.height) \
                            and page["pixels"] == pixels_digest(after):
                        results[number] = same_bitmap(rects)
                    elif rendered.get(before_index) is not None:
                        results[number] = compare_bitmaps(rendered[before_index], after, rects, dpi)
                    else:
                        pending.append((number, before_index, after, rects))
            finally:
                document.close()
    if failures:
        failed = render_failure("; ".join(sorted(failures.values())))
        return {"ok": True, "before_pages": pages["before"], "after_pages": pages["after"],
                "pairs": [dict(failed) for _ in pairs]}
    if pending:
        document = open_alone(library, data["before"])
        try:
            for number, before_index, after, rects in pending:
                try:
                    first = render_bitmap(document, before_index, dpi)
                except pdfium.PdfiumError as err:
                    results[number] = render_failure(f"render:{err.name}")
                    continue
                results[number] = compare_bitmaps(first, after, rects, dpi)
        finally:
            document.close()
    return {"ok": True, "before_pages": pages["before"], "after_pages": pages["after"], "pairs": results}


def serve(dll: Path) -> None:
    """Reads one JSON request per line on stdin and answers one JSON line each."""
    library = pdfium.Library(dll)
    for line in sys.stdin.buffer:
        if not line.strip():
            continue
        try:
            request = json.loads(line)
            # A request without page pairs is one of the plain probes (page count, text).
            if "pairs" in request:
                response = handle_compare(library, request)
            else:
                library.reset()
                response = pdfium.handle_request(library, request)
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
                  dpi: float = DPI, cache: bool = True) -> dict:
    """Compares page pairs [before_index, after_index] (0-based) of two files; `before` is an
    input whose pages the results cache keeps, unless `cache` is false.

    Returns {"before_pages", "after_pages", "pairs": [one answer per pair]}; each
    answer holds identical, differing_pixels, size_differs and error (None when
    the pair was compared). A chunk that times out or crashes is asked again one
    pair at a time, so a failure lands on the page that caused it, with its cause."""
    results: list = [None] * len(pairs)
    counts = {"before_pages": None, "after_pages": None}

    digest = results_cache.file_digest(before) if cache else None

    def ask(indexes: list, timeout: float) -> dict:
        request = {"before": str(before), "after": str(after), "dpi": dpi,
                   "pairs": [pairs[i] for i in indexes]}
        if digest is not None:
            request["before_digest"] = digest
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


def page_counts(pool: pdfium.Pool, path: Path, cache: bool = False) -> int | None:
    """PDFium's page count of a file, or None when it does not open; an input's (`cache`) is
    answered from the results cache."""
    answer = results_cache.cached_probe(pool, path, ["info"]) if cache else pool.probe(path, ["info"])
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
            result = compare_pages(pool, before, after, [[i, i] for i in range(count)], cache=False)
            print(json.dumps(result, indent=1))
        for name in args.self_compare or []:
            path = Path(name)
            count = page_counts(pool, path) or 0
            result = compare_pages(pool, path, path, [[i, i] for i in range(count)], cache=False)
            identical = sum(1 for p in result["pairs"] if p["identical"])
            print(json.dumps({"file": name, "pages": count, "identical": identical,
                              "errors": [p["error"] for p in result["pairs"] if p["error"]]}))
    return 0


if __name__ == "__main__":
    os.environ.setdefault("PYTHONIOENCODING", "utf-8")
    sys.exit(main())
