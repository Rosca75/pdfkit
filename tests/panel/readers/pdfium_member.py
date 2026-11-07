# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# PDFium as a member of the reference panel. Its text comes from the PDFium
# tier's own probe (tests/pdfium/pdfium.py, ops ["text"]) through that tier's
# results cache, so the answers the reading gate already holds are read back
# and never copied. What that probe does not answer runs in this file's own
# worker processes, a subclass of the tier's worker as the image agreement
# defines one: `pages` (the page count, each page's size as PDFium lays it out,
# its rotation and its media and crop boxes) and `image_pixels`. A pixel answer
# is FPDFImageObj_GetRenderedBitmap's, which takes the image mask and the
# /Decode array into account where FPDFImageObj_GetBitmap ignores the mask and
# the matrix (fpdf_edit.h); the image object's matrix is first set to its own
# pixel size (FPDFPageObj_SetMatrix with [w 0 0 h 0 0]), so the bitmap has the
# image's own width and height and no page transform. The document is in
# memory and never saved, so the matrix change touches nothing else. Images are
# found by the SHA-256 of their encoded data (FPDFImageObj_GetImageDataRaw),
# pages and the forms they draw walked, as the image agreement aligns them, and
# by the page's draw order where two objects hold the same bytes
# (handle_pixels).
#
# Sources: fpdf_edit.h, fpdfview.h and fpdf_transformpage.h at
# https://raw.githubusercontent.com/chromium/pdfium/main/public/ (read on
# 2026-11-07): FPDFImageObj_GetRenderedBitmap(document, page, image_object),
# FPDFImageObj_GetImagePixelSize, FPDFPageObj_GetMatrix and SetMatrix with
# FS_MATRIX {a, b, c, d, e, f} of floats, FPDFPage_GetRotation, and
# FPDFPage_GetMediaBox and GetCropBox (left, bottom, right, top).
"""PDFium's panel worker: python tests/panel/readers/pdfium_member.py --serve --dll <library>."""

from __future__ import annotations

import ctypes
import functools
import hashlib
import json
import sys
import threading
from ctypes import POINTER, Structure, c_float, c_int, c_uint, c_ulong, c_void_p
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
for folder in (HERE.parent, REPO / "tests" / "pdfium", REPO / "dataset" / "tools"):
    if str(folder) not in sys.path:
        sys.path.insert(0, str(folder))

import pdfium  # noqa: E402
import results_cache  # noqa: E402
import readers  # noqa: E402
from readers import pixels  # noqa: E402

NAME = "pdfium"
LICENCE = "BSD-3-Clause AND Apache-2.0"
ROLE = "text, pixels"
CAPABILITIES = ("text", "pages", "image_pixels")
CACHED_ELSEWHERE = ("text",)
FPDF_PAGEOBJ_IMAGE = 3
FPDF_PAGEOBJ_FORM = 5
MAX_FORM_DEPTH = 32


class Matrix(Structure):
    _fields_ = [(name, c_float) for name in ("a", "b", "c", "d", "e", "f")]


def version() -> str:
    return readers.pin(NAME)["version"]


def binary_digest() -> str:
    return readers.live_digest(readers.pin(NAME))


def missing() -> str | None:
    return readers.pin_problem(NAME)


# ---------------------------------------------------------------------------
# The worker side


def bind_extra(library: pdfium.Library) -> None:
    lib = library.lib
    for name, restype, argtypes in (
            ("FPDFFormObj_CountObjects", c_int, [c_void_p]),
            ("FPDFFormObj_GetObject", c_void_p, [c_void_p, c_ulong]),
            ("FPDFBitmap_GetFormat", c_int, [c_void_p]),
            ("FPDFImageObj_GetRenderedBitmap", c_void_p, [c_void_p, c_void_p, c_void_p]),
            ("FPDFImageObj_GetImagePixelSize", c_int, [c_void_p, POINTER(c_uint), POINTER(c_uint)]),
            ("FPDFPageObj_GetMatrix", c_int, [c_void_p, POINTER(Matrix)]),
            ("FPDFPageObj_SetMatrix", c_int, [c_void_p, POINTER(Matrix)]),
            ("FPDFPage_GetRotation", c_int, [c_void_p]),
            ("FPDFPage_GetMediaBox", c_int, [c_void_p] + [POINTER(c_float)] * 4),
            ("FPDFPage_GetCropBox", c_int, [c_void_p] + [POINTER(c_float)] * 4)):
        fn = getattr(lib, name)
        fn.restype = restype
        fn.argtypes = argtypes


def page_box(lib, page, getter) -> list[float] | None:
    values = [c_float() for _ in range(4)]
    if not getter(page, *[ctypes.byref(v) for v in values]):
        return None
    return [round(v.value, 3) for v in values]


def handle_pages(library: pdfium.Library, doc) -> dict:
    lib = library.lib
    pages = []
    for index in range(doc.page_count()):
        page = lib.FPDF_LoadPage(doc.handle, index)
        if not page:
            pages.append({"error": "page"})
            continue
        try:
            pages.append({
                "size": [round(float(lib.FPDF_GetPageWidthF(page)), 3),
                         round(float(lib.FPDF_GetPageHeightF(page)), 3)],
                "rotate": int(lib.FPDFPage_GetRotation(page)) * 90,
                "media": page_box(lib, page, lib.FPDFPage_GetMediaBox),
                "crop": page_box(lib, page, lib.FPDFPage_GetCropBox),
            })
        finally:
            lib.FPDF_ClosePage(page)
    return {"ok": True, "count": len(pages), "pages": pages}


def image_objects(lib, page) -> list:
    found = []

    def walk(count, getter, depth):
        for i in range(count):
            obj = getter(i)
            kind = lib.FPDFPageObj_GetType(obj)
            if kind == FPDF_PAGEOBJ_IMAGE:
                found.append(obj)
            elif kind == FPDF_PAGEOBJ_FORM and depth < MAX_FORM_DEPTH:
                walk(lib.FPDFFormObj_CountObjects(obj),
                     lambda j, o=obj: lib.FPDFFormObj_GetObject(o, j), depth + 1)

    walk(lib.FPDFPage_CountObjects(page), lambda j: lib.FPDFPage_GetObject(page, j), 0)
    return found


def rendered(lib, doc, page, obj) -> dict:
    """The image object's rendered bitmap at its own pixel size, in the common pixel form."""
    width, height = c_uint(), c_uint()
    if not lib.FPDFImageObj_GetImagePixelSize(obj, ctypes.byref(width), ctypes.byref(height)):
        return {"error": "no_size"}
    matrix = Matrix(float(width.value), 0.0, 0.0, float(height.value), 0.0, 0.0)
    if not lib.FPDFPageObj_SetMatrix(obj, ctypes.byref(matrix)):
        return {"error": "no_matrix"}
    bitmap = lib.FPDFImageObj_GetRenderedBitmap(doc.handle, page, obj)
    if not bitmap:
        return {"error": "no_bitmap"}
    try:
        w, h = lib.FPDFBitmap_GetWidth(bitmap), lib.FPDFBitmap_GetHeight(bitmap)
        stride, fmt = lib.FPDFBitmap_GetStride(bitmap), lib.FPDFBitmap_GetFormat(bitmap)
        data = ctypes.string_at(lib.FPDFBitmap_GetBuffer(bitmap), stride * h)
    finally:
        lib.FPDFBitmap_Destroy(bitmap)
    result = pixels.from_bgra(w, h, stride, data, fmt)
    result["bitmap_format"] = fmt
    result["pixel_size"] = [width.value, height.value]
    return result


def handle_pixels(library: pdfium.Library, doc, request: dict) -> dict:
    """request["images"]: [{"object", "raw", "pages"}]; request["order"]: page -> the object
    numbers of the images that page draws, in draw order (Poppler's listing, null for an inline
    image), when known.

    PDFium names no object number, so an image object is matched to a wanted object by its page
    and the SHA-256 of its encoded data. Two objects may hold the same bytes under different
    dictionaries (a /Decode, a /DecodeParms) and be drawn on one page, so where the page's draw
    order is known and lists as many images as PDFium walks, the k-th image PDFium walks is the
    k-th the order names, accepted only when its bytes hash to that object's raw digest. Without
    an order that fits, an object is matched by its bytes alone when exactly one wanted object of
    the page shares them with the drawn images, and those are one image or that object drawn as
    many times as the order names it; it is `ambiguous` otherwise."""
    lib = library.lib
    items = {str(item["object"]): item for item in request.get("images", []) if item.get("raw")}
    order = {int(page): numbers for page, numbers in (request.get("order") or {}).items()}
    answers: dict[str, dict] = {}
    pages = sorted({p for item in items.values() for p in item.get("pages", [])})
    for number in pages:
        if len(answers) == len(items) or number < 1 or number > doc.page_count():
            continue
        page = lib.FPDF_LoadPage(doc.handle, number - 1)
        if not page:
            continue
        try:
            walked = []
            for obj in image_objects(lib, page):
                size = lib.FPDFImageObj_GetImageDataRaw(obj, None, 0)
                buffer = ctypes.create_string_buffer(max(int(size), 1))
                lib.FPDFImageObj_GetImageDataRaw(obj, buffer, size)
                walked.append((obj, hashlib.sha256(buffer.raw[:int(size)]).hexdigest()))
            drawn = order.get(number)
            if drawn is not None and len(drawn) == len(walked):
                for (obj, digest), named in zip(walked, drawn):
                    item = items.get(str(named))
                    if item is None or str(named) in answers or number not in item.get("pages", []):
                        continue
                    if item["raw"] == digest:
                        answers[str(named)] = dict(rendered(lib, doc, page, obj), matched="draw_order")
                continue
            for digest in sorted({d for _, d in walked}):
                objects = [obj for obj, d in walked if d == digest]
                candidates = [key for key, item in items.items() if item["raw"] == digest
                              and number in item.get("pages", []) and key not in answers]
                if not candidates:
                    continue
                # One wanted object, and every drawn image with these bytes is that object when the
                # order names it as many times as they are drawn.
                alone = len(candidates) == 1 and (len(objects) == 1 or (
                    drawn is not None and sum(1 for n in drawn if str(n) == candidates[0]) == len(objects)))
                if alone:
                    answers[candidates[0]] = dict(rendered(lib, doc, page, objects[0]), matched="bytes")
                else:
                    for key in candidates:
                        answers[key] = {"error": "ambiguous", "detail": f"{len(objects)} drawn images and "
                                        f"{len(candidates)} wanted objects share these bytes on page {number}"}
        finally:
            lib.FPDF_ClosePage(page)
    for item in request.get("images", []):
        answers.setdefault(str(item["object"]), {"error": "not_found" if item.get("raw") else "no_raw_digest"})
    return {"ok": True, "images": answers}


def handle(library: pdfium.Library, request: dict) -> dict:
    data = Path(request["path"]).read_bytes()
    try:
        doc = library.open(data)
    except pdfium.PdfiumError as err:
        return {"ok": False, "error": "refused", "detail": err.name, "code": err.code}
    with doc:
        if request["kind"] == "pages":
            return handle_pages(library, doc)
        if request["kind"] == "image_pixels":
            answer = handle_pixels(library, doc, request.get("request", {}))
            for entry in answer["images"].values():
                if "samples" in entry:
                    entry["samples"] = entry["samples"].hex()
            return answer
    return readers.unsupported(NAME, request["kind"])


def serve(dll: Path) -> None:
    library = pdfium.Library(dll)
    bind_extra(library)
    for line in sys.stdin.buffer:
        if not line.strip():
            continue
        try:
            library.reset()
            response = handle(library, json.loads(line))
        except Exception as err:  # a worker answers every request, whatever fails
            response = {"ok": False, "error": "worker", "detail": repr(err)}
        sys.stdout.write(json.dumps(response) + "\n")
        sys.stdout.flush()


# ---------------------------------------------------------------------------
# The caller side


class PanelWorker(pdfium.Worker):
    """A worker running this file; every start is counted as a child process."""
    script = Path(__file__).resolve()

    def start(self) -> None:
        readers.STARTED.add()
        super().start()


class ProbeWorker(pdfium.Worker):
    """The PDFium tier's own worker, counted the same way."""

    def start(self) -> None:
        readers.STARTED.add()
        super().start()


class _Pools:
    """The two worker pools, started on the first question a cache cannot answer, so a
    collection whose every answer is cached starts no PDFium process."""

    def __init__(self) -> None:
        self.lock = threading.Lock()
        self.size = 1
        self.pools: dict[str, pdfium.Pool] = {}

    def get(self, which: str) -> pdfium.Pool:
        with self.lock:
            if which not in self.pools:
                worker = PanelWorker if which == "panel" else ProbeWorker
                self.pools[which] = pdfium.Pool(self.size, worker_class=worker)
            return self.pools[which]

    def close(self) -> None:
        with self.lock:
            for pool in self.pools.values():
                pool.close()
            self.pools.clear()


POOLS = _Pools()


def set_workers(count: int) -> None:
    POOLS.size = max(1, count)


def close() -> None:
    POOLS.close()


class _TimedProbe:
    """What results_cache.cached_probe needs of a pool, with this answer's time limit."""

    def __init__(self, timeout: float) -> None:
        self.timeout = timeout

    def probe(self, path: Path, ops, **extra) -> dict:
        request = {"path": str(path), "ops": list(ops), **extra}
        return POOLS.get("probe").ask(request, self.timeout)


def text_shape(probe: dict) -> dict:
    """The PDFium tier's probe in the panel's text shape."""
    if not probe.get("ok"):
        error = probe.get("error")
        if error in readers.TRANSIENT:
            return probe
        return {"ok": False, "error": "refused", "detail": error}
    return {"ok": True, "pages": [{"text": text} for text in probe.get("text", [])]}


def cached_elsewhere(path: Path, kind: str, timeout: float, request: dict, stats) -> dict:
    """The text kind, through the PDFium tier's cache: the same record the reading gate reads."""
    del kind, request
    record = results_cache.record_path(results_cache.file_digest(path), "probe", {"ops": ["text"]})
    if stats is not None:
        if record.exists():
            stats.cached += 1
        else:
            stats.asked += 1
    return text_shape(results_cache.cached_probe(_TimedProbe(timeout), path, ["text"]))


def answer(path: Path, kind: str, timeout: float, request: dict) -> dict:
    if kind == "text":
        return text_shape(_TimedProbe(timeout).probe(path, ["text"]))
    if kind not in ("pages", "image_pixels"):
        return readers.unsupported(NAME, kind)
    result = POOLS.get("panel").ask({"path": str(path), "kind": kind, "request": request}, timeout)
    for entry in (result.get("images") or {}).values():
        if "samples" in entry:
            entry["samples"] = bytes.fromhex(entry["samples"])
    return result


@functools.lru_cache(maxsize=1)
def library_path() -> Path:
    return pdfium.default_dll()


def main() -> int:
    import argparse

    parser = argparse.ArgumentParser(description="PDFium's panel worker.")
    parser.add_argument("--serve", action="store_true", help="run as a worker on stdin/stdout")
    parser.add_argument("--dll", help="the PDFium library to load (default: the pinned cache)")
    args = parser.parse_args()
    if args.serve:
        serve(Path(args.dll) if args.dll else library_path())
    return 0


if __name__ == "__main__":
    sys.exit(main())
