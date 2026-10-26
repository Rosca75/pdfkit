# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# The independent check of an image treatment run: for every outcome whose
# redact_images output was written, PDFium opens the output and every image
# object of every page (forms descended, FPDFFormObj_GetObject) is aligned
# with the treatments by the SHA-256 of its encoded data
# (FPDFImageObj_GetImageDataRaw), which the write report names for each
# treated object and each rewritten inline image. Each treated image's bitmap
# (FPDFImageObj_GetBitmap; fpdf_edit.h and fpdfview.h,
# https://raw.githubusercontent.com/chromium/pdfium/main/public/, read) is
# checked inside every treated rectangle: a box or a region holds one colour
# (a captioned box two, the fill and the ink), a blur is constant on every
# block of its grid, which starts at the image's top-left corner. A JPEG
# re-encoded from a DCT source is judged on the 8 by 8 blocks that lie wholly
# inside a box, and within 2 levels per channel, because a JPEG block that
# straddles the box's edge mixes the fill with what is outside it; inside a
# captioned JPEG box a pixel may be any blend of the fill and the ink. A
# rectangle that carries any other sample is a survival of an original
# sample. The input's image objects give the original encoded bytes of every
# treated and removed identity by the SHA-256 pdfkit_redact_images reported;
# an output image object whose data hashes to one of them is an original left
# in the output. The PDFium side runs in worker processes (this file run with
# --serve), because PDFium can hang or crash on a hostile file. The report
# goes to <run>/pdfium-images.json.
"""Check a run's treated images with PDFium: python tests/pdfium/images.py --run <id>."""

from __future__ import annotations

import argparse
import concurrent.futures
import ctypes
import hashlib
import json
import sys
from ctypes import c_int, c_ulong, c_void_p
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parents[1] / "dataset" / "tools"))

import common  # noqa: E402
import pdfium  # noqa: E402

FPDF_PAGEOBJ_IMAGE = 3
FPDF_PAGEOBJ_FORM = 5
MAX_FORM_DEPTH = 32
# The difference per channel a re-encoded JPEG may show inside a block it
# keeps constant.
JPEG_TOLERANCE = 2
JPEG_BLOCK = 8

# ---------------------------------------------------------------------------
# The worker side


def bind_extra(library: pdfium.Library) -> None:
    """The PDFium functions this file needs beyond the shared binding."""
    lib = library.lib
    for name, restype, argtypes in (
            ("FPDFFormObj_CountObjects", c_int, [c_void_p]),
            ("FPDFFormObj_GetObject", c_void_p, [c_void_p, c_ulong]),
            ("FPDFBitmap_GetFormat", c_int, [c_void_p])):
        fn = getattr(lib, name)
        fn.restype = restype
        fn.argtypes = argtypes


def handle(library: pdfium.Library, request: dict) -> dict:
    """Every image object's digest and page; for the digests in `bitmaps` the
    decoded bitmap, for those in `raw` the encoded bytes, written into `out`."""
    lib = library.lib
    data = Path(request["path"]).read_bytes()
    bitmaps = set(request.get("bitmaps", []))
    raw_wanted = set(request.get("raw", []))
    out = Path(request["out"])
    try:
        doc = library.open(data)
    except pdfium.PdfiumError as err:
        return {"ok": False, "error": err.name}
    seen: dict[str, dict] = {}
    with doc:
        for index in range(doc.page_count()):
            page = lib.FPDF_LoadPage(doc.handle, index)
            if not page:
                continue
            try:
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

                walk(lib.FPDFPage_CountObjects(page), lambda j, p=page: lib.FPDFPage_GetObject(p, j), 0)
                for obj in found:
                    size = int(lib.FPDFImageObj_GetImageDataRaw(obj, None, 0))
                    buffer = ctypes.create_string_buffer(max(size, 1))
                    lib.FPDFImageObj_GetImageDataRaw(obj, buffer, size)
                    blob = buffer.raw[:size]
                    digest = hashlib.sha256(blob).hexdigest()
                    record = seen.setdefault(digest, {"pages": [], "bitmap": None, "raw": None})
                    record["pages"].append(index + 1)
                    if digest in raw_wanted and record["raw"] is None:
                        out.mkdir(parents=True, exist_ok=True)
                        target = out / f"{digest[:16]}.raw"
                        target.write_bytes(blob)
                        record["raw"] = str(target)
                    if digest not in bitmaps or record["bitmap"] is not None:
                        continue
                    bitmap = lib.FPDFImageObj_GetBitmap(obj)
                    if not bitmap:
                        record["bitmap"] = ""
                        continue
                    try:
                        width = lib.FPDFBitmap_GetWidth(bitmap)
                        height = lib.FPDFBitmap_GetHeight(bitmap)
                        stride = lib.FPDFBitmap_GetStride(bitmap)
                        fmt = lib.FPDFBitmap_GetFormat(bitmap)
                        pixels = ctypes.string_at(lib.FPDFBitmap_GetBuffer(bitmap), stride * height)
                        out.mkdir(parents=True, exist_ok=True)
                        target = out / f"{digest[:16]}.bmp"
                        target.write_bytes(f"{width} {height} {stride} {fmt}\n".encode("ascii") + pixels)
                        record["bitmap"] = str(target)
                    finally:
                        lib.FPDFBitmap_Destroy(bitmap)
            finally:
                lib.FPDF_ClosePage(page)
    return {"ok": True, "images": seen}


def serve(dll: Path) -> None:
    library = pdfium.Library(dll)
    bind_extra(library)
    for line in sys.stdin.buffer:
        if not line.strip():
            continue
        try:
            response = handle(library, json.loads(line))
        except Exception as err:  # a worker answers every request, whatever fails
            response = {"ok": False, "error": "worker", "detail": repr(err)}
        sys.stdout.write(json.dumps(response) + "\n")
        sys.stdout.flush()


class ImageWorker(pdfium.Worker):
    script = Path(__file__).resolve()


def pool(workers: int) -> pdfium.Pool:
    return pdfium.Pool(workers, worker_class=ImageWorker, timeout=120.0)


# ---------------------------------------------------------------------------
# The caller side


def read_bitmap(path: str) -> tuple[int, int, int, list] | None:
    """(width, height, channels, rows of pixel tuples) of a worker's bitmap file."""
    blob = Path(path).read_bytes()
    header, _, pixels = blob.partition(b"\n")
    width, height, stride, fmt = (int(v) for v in header.split())
    channels = {1: 1, 2: 3, 3: 4, 4: 4}.get(fmt)
    if channels is None:
        return None
    used = 1 if fmt == 1 else 3
    rows = []
    for y in range(height):
        row = pixels[y * stride:y * stride + width * channels]
        rows.append([tuple(row[x * channels:x * channels + used]) for x in range(width)])
    return width, height, used, rows


def close(a: tuple, b: tuple, tolerance: int) -> bool:
    return all(abs(p - q) <= tolerance for p, q in zip(a, b))


def interior(rect: list[int], jpeg: bool) -> list[int]:
    """The part of a box judged: all of it, or for a JPEG the 8 by 8 blocks wholly inside it."""
    left, top, right, bottom = rect
    if not jpeg:
        return [left, top, right, bottom]
    step = JPEG_BLOCK
    return [-(-left // step) * step, -(-top // step) * step, (right // step) * step, (bottom // step) * step]


def judge_box(rows, rect: list[int], jpeg: bool, caption: bool) -> dict | None:
    """None when the rectangle holds the box's fill alone, or with a caption the
    fill and the ink (for a JPEG, the blends between them its blocks make); the
    first other pixel otherwise."""
    left, top, right, bottom = interior(rect, jpeg)
    tolerance = JPEG_TOLERANCE if jpeg else 0
    pixels = [(x, y, rows[y][x]) for y in range(max(top, 0), min(bottom, len(rows)))
              for x in range(max(left, 0), min(right, len(rows[y])))]
    if not pixels:
        return None
    if not caption:
        fill = pixels[0][2]
        for x, y, pixel in pixels:
            if not close(pixel, fill, tolerance):
                return {"x": x, "y": y, "pixel": list(pixel), "fill": list(fill)}
        return None
    # The fill is the commonest colour; the ink the one farthest from it.
    counts: dict = {}
    for _, _, pixel in pixels:
        counts[pixel] = counts.get(pixel, 0) + 1
    fill = max(counts, key=lambda c: (counts[c], c))
    ink = max(counts, key=lambda c: (sum((a - b) ** 2 for a, b in zip(c, fill)), c))
    span = [b - a for a, b in zip(fill, ink)]
    length = sum(v * v for v in span)
    for x, y, pixel in pixels:
        if not jpeg:
            if pixel != fill and pixel != ink:
                return {"x": x, "y": y, "pixel": list(pixel), "fill": list(fill), "ink": list(ink)}
            continue
        # A blend of the two lies on the segment between them, within the
        # JPEG's ringing.
        t = sum((p - a) * v for p, a, v in zip(pixel, fill, span)) / length if length else 0.0
        t = min(1.0, max(0.0, t))
        nearest = [a + t * v for a, v in zip(fill, span)]
        if any(abs(p - q) > 3 * JPEG_TOLERANCE for p, q in zip(pixel, nearest)):
            return {"x": x, "y": y, "pixel": list(pixel), "fill": list(fill), "ink": list(ink)}
    return None


def judge_blur(rows, rect: list[int], block: int, jpeg: bool, boxes: list) -> dict | None:
    """None when every block of the grid is constant inside the rectangle; a
    pixel under a region box painted after the mosaic is not judged here."""
    left, top, right, bottom = rect
    tolerance = JPEG_TOLERANCE if jpeg else 0
    for row0 in range(top - top % block, bottom, block):
        for col0 in range(left - left % block, right, block):
            y0, y1 = max(row0, top), min(row0 + block, bottom, len(rows))
            x0, x1 = max(col0, left), min(col0 + block, right)
            if jpeg and any(bx0 < x1 + JPEG_BLOCK and x0 - JPEG_BLOCK < bx1 and
                            by0 < y1 + JPEG_BLOCK and y0 - JPEG_BLOCK < by1
                            for bx0, by0, bx1, by1 in boxes):
                # A JPEG block a region box touches mixes the two fills.
                continue
            first = None
            for y in range(y0, y1):
                row = rows[y]
                for x in range(x0, min(x1, len(row))):
                    if any(bx0 <= x < bx1 and by0 <= y < by1 for bx0, by0, bx1, by1 in boxes):
                        continue
                    if first is None:
                        first = row[x]
                    elif not close(row[x], first, tolerance):
                        return {"x": x, "y": y, "pixel": list(row[x]), "block_value": list(first)}
    return None


def check_file(images_pool, row: dict, run_dir: Path, part: dict) -> dict:
    output = run_dir / part["output"]
    work = run_dir / "pdfium-images" / row["id"]
    treatments = part.get("treatments") or []
    region = part.get("region") or {}
    new_digests: dict[str, str] = {}
    for entry in part.get("images") or []:
        for digest in entry.get("sha256") or []:
            new_digests[digest] = entry["identity"]
    originals = {t["encoded"]["sha256"]: t["identity"] for t in treatments if t.get("encoded")}
    answer = images_pool.ask({"path": str(output), "bitmaps": sorted(new_digests), "out": str(work)})
    result = {"opens": bool(answer.get("ok")), "rectangles": 0, "survivals": [], "left": [],
              "unmatched": [], "size_differs": []}
    if not answer.get("ok"):
        return result
    seen = answer["images"]
    for digest, identity in originals.items():
        # A treatment that wrote exactly an original's bytes (a blur of an
        # image whose blocks were already constant) left nothing it hid.
        if digest in seen and digest not in new_digests:
            result["left"].append({"identity": identity, "pages": seen[digest]["pages"][:10]})
    by_identity = {t["identity"]: t for t in treatments}
    for digest, identity in sorted(new_digests.items()):
        treatment = by_identity.get(identity)
        if treatment is None:
            continue
        record = seen.get(digest)
        if record is None or not record.get("bitmap"):
            result["unmatched"].append({"identity": identity, "digest": digest[:16],
                                        "drawn": record is not None})
            continue
        decoded = read_bitmap(record["bitmap"])
        if decoded is None:
            result["unmatched"].append({"identity": identity, "digest": digest[:16], "format": "unknown"})
            continue
        width, height, _, rows = decoded
        if [width, height] != [treatment.get("width"), treatment.get("height")]:
            result["size_differs"].append({"identity": identity, "pdfium": [width, height],
                                           "pdfkit": [treatment.get("width"), treatment.get("height")]})
            continue
        jpeg = treatment.get("encoding") == "DCTDecode"
        checks = []
        if treatment["kind"] == "blur":
            checks += [("blur", rect) for rect in treatment.get("pixels") or []]
        elif treatment["kind"] == "box":
            checks += [("caption" if treatment.get("caption") else "box", rect)
                       for rect in treatment.get("pixels") or []]
        if region.get("identity") == identity and region.get("status") == "ok":
            checks += [("box", rect) for rect in region.get("pixels") or []]
        regions = [rect for kind, rect in checks if kind == "box" and treatment["kind"] == "blur"]
        for kind, rect in checks:
            result["rectangles"] += 1
            if kind == "blur":
                odd = judge_blur(rows, rect, int(treatment.get("block") or 8), jpeg, regions)
            else:
                odd = judge_box(rows, rect, jpeg, kind == "caption")
            if odd is not None:
                result["survivals"].append({"identity": identity, "kind": kind, "rect": rect, **odd})
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run", help="the dataset run id")
    parser.add_argument("--id", action="append", help="only these ids")
    parser.add_argument("--workers", type=int, default=4)
    parser.add_argument("--serve", action="store_true", help="run as a worker on stdin/stdout")
    parser.add_argument("--dll", help="the pdfium.dll to load (default: the pinned cache)")
    args = parser.parse_args()
    if args.serve:
        serve(Path(args.dll) if args.dll else pdfium.default_dll())
        return 0
    if not args.run:
        raise common.ToolError("no --run", "the id of a dataset run", "pass --run <run-id>")
    try:
        if not pdfium.default_dll().exists():
            raise FileNotFoundError
    except (FileNotFoundError, common.ToolError):
        print("SKIPPED image check: PDFium is absent; run `python tests/pdfium/fetch.py`")
        return 0
    run_dir = common.reports_dir() / args.run
    rows = common.read_manifest()
    if args.id:
        rows = [r for r in rows if r["id"] in args.id]
    jobs = []
    for row in rows:
        path = run_dir / "outcomes" / f"{row['id']}.json"
        if not path.exists():
            continue
        part = json.loads(path.read_text(encoding="utf-8")).get("redact_images") or {}
        if part.get("status") == "ok" and part.get("output"):
            jobs.append((row, part))
    results = {}
    with pool(args.workers) as images_pool, concurrent.futures.ThreadPoolExecutor(args.workers) as threads:
        futures = {row["id"]: threads.submit(check_file, images_pool, row, run_dir, part) for row, part in jobs}
        for row_id, future in futures.items():
            results[row_id] = future.result()
    survivals = {k: v["survivals"] for k, v in results.items() if v["survivals"]}
    left = {k: v["left"] for k, v in results.items() if v["left"]}
    summary = {"files": len(results), "unopened": sorted(k for k, v in results.items() if not v["opens"]),
               "rectangles": sum(v["rectangles"] for v in results.values()),
               "survivals": sum(len(v) for v in survivals.values()),
               "survival_files": sorted(survivals),
               "originals_left": sum(len(v) for v in left.values()), "left_files": sorted(left),
               "unmatched": sum(len(v["unmatched"]) for v in results.values()),
               "size_differs": sum(len(v["size_differs"]) for v in results.values())}
    (run_dir / "pdfium-images.json").write_text(
        json.dumps({"summary": summary, "files": dict(sorted(results.items()))}, indent=1), encoding="utf-8")
    print(json.dumps(summary, indent=1))
    if not results:
        print("SKIPPED image check: no outcome of this run carries a redact_images output", file=sys.stderr)
    return 1 if survivals or left else 0


if __name__ == "__main__":
    common.run_tool(main)
