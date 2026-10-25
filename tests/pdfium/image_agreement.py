# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# The image agreement: the decoder gate of the images layer. It runs
# pdfkit_image_census (tests/dataset/image_census.cpp)
# over every manifest file whose open expectation is opens or repaired, each
# file in its own child process with a 120 s limit, and asks PDFium for the
# bitmap of the same images: PDFium's image objects (pages and the forms they
# draw, walked through FPDFFormObj_GetObject) are aligned with pdfkit's images
# by the SHA-256 of their encoded data (FPDFImageObj_GetImageDataRaw), and
# FPDFImageObj_GetBitmap gives the decoded bitmap (fpdf_edit.h and
# fpdfview.h, https://raw.githubusercontent.com/chromium/pdfium/main/public/,
# read). The census tool's --compare mode measures each pair: the bitonal
# encodings (JBIG2, CCITT) must agree pixel for pixel, the others within a
# mean absolute difference of 2 per channel. The PDFium side runs in worker
# processes (this file run with --serve, a subclass of the pool's worker),
# because PDFium can hang or crash on a hostile file. With --repeat the census
# runs twice and the identities and PNG bytes must be equal. An image whose
# soft mask states /Matte is not compared: pdfkit undoes the premultiplication
# (docs/DESIGN.md section 9.4) and PDFium's bitmap keeps it. It writes
# images.tsv (one row per image) and summary.md (the gate's numbers per
# encoding and per colour space) into --out. --write-manifest fills the
# manifest's expect_images column from the run: decoded (every image
# decodes), partial:<encodings that failed>, or n/a (no image, or not
# opened); --check compares the run with that column. With --listing it
# measures the listing instead: it runs `pdfkit images` on each file (the C
# ABI's pdfkit_images, the content interpreter's occurrences), counts the
# decoded occurrences per encoding, checks that every undecodable image
# carries its warning, and compares each page's occurrence count with the
# image objects PDFium's page holds (forms descended); it writes listing.tsv
# and listing.md.
"""Run the image agreement: python tests/pdfium/image_agreement.py --out <folder> [--repeat] [--write-manifest | --check]."""

from __future__ import annotations

import argparse
import collections
import concurrent.futures
import ctypes
import hashlib
import json
import os
import shutil
import subprocess
import sys
import time
from ctypes import c_int, c_ulong, c_void_p
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(REPO / "dataset" / "tools"))

import pdfium  # noqa: E402

# The census decodes every image three times (base, mask, extraction) and
# writes its pixels, so a file gets four times the dataset tier's 30 s.
TIMEOUT = 120.0
BITONAL = {"JBIG2Decode", "CCITTFaxDecode"}
CONTINUOUS_LIMIT = 2.0
FPDF_PAGEOBJ_IMAGE = 3
FPDF_PAGEOBJ_FORM = 5
MAX_FORM_DEPTH = 32

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


def image_objects(library: pdfium.Library, doc) -> list[tuple[int, int]]:
    """(page index, object handle) of every image object, forms descended."""
    lib = library.lib
    found: list[tuple[int, object, object]] = []

    def walk(index, count, getter, depth):
        for i in range(count):
            obj = getter(i)
            kind = lib.FPDFPageObj_GetType(obj)
            if kind == FPDF_PAGEOBJ_IMAGE:
                found.append((index, obj))
            elif kind == FPDF_PAGEOBJ_FORM and depth < MAX_FORM_DEPTH:
                walk(index, lib.FPDFFormObj_CountObjects(obj),
                     lambda j, o=obj: lib.FPDFFormObj_GetObject(o, j), depth + 1)

    return found, walk


def handle_images(library: pdfium.Library, request: dict) -> dict:
    lib = library.lib
    data = Path(request["path"]).read_bytes()
    want = set(request.get("want", []))
    out = Path(request["out"])
    out.mkdir(parents=True, exist_ok=True)
    try:
        doc = library.open(data)
    except pdfium.PdfiumError as err:
        return {"ok": False, "error": err.name}
    seen: dict[str, dict] = {}
    counts: dict[int, int] = {}
    with doc:
        for index in range(doc.page_count()):
            page = lib.FPDF_LoadPage(doc.handle, index)
            if not page:
                continue
            try:
                found, walk = image_objects(library, doc)
                walk(index, lib.FPDFPage_CountObjects(page),
                     lambda j, p=page: lib.FPDFPage_GetObject(p, j), 0)
                for _, obj in found:
                    size = lib.FPDFImageObj_GetImageDataRaw(obj, None, 0)
                    buffer = ctypes.create_string_buffer(max(int(size), 1))
                    lib.FPDFImageObj_GetImageDataRaw(obj, buffer, size)
                    digest = hashlib.sha256(buffer.raw[:int(size)]).hexdigest()
                    record = seen.setdefault(digest, {"pages": [], "bitmap": None})
                    record["pages"].append(index + 1)
                    counts[index + 1] = counts.get(index + 1, 0) + 1
                    if digest not in want or record["bitmap"] is not None:
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
                        target = out / f"{digest[:16]}.bmp"
                        target.write_bytes(f"{width} {height} {stride} {fmt}\n".encode("ascii") + pixels)
                        record["bitmap"] = str(target)
                    finally:
                        lib.FPDFBitmap_Destroy(bitmap)
            finally:
                lib.FPDF_ClosePage(page)
    return {"ok": True, "images": seen, "counts": counts}


def serve(dll: Path) -> None:
    library = pdfium.Library(dll)
    bind_extra(library)
    for line in sys.stdin.buffer:
        if not line.strip():
            continue
        try:
            response = handle_images(library, json.loads(line))
        except Exception as err:  # a worker answers every request, whatever fails
            response = {"ok": False, "error": "worker", "detail": repr(err)}
        sys.stdout.write(json.dumps(response) + "\n")
        sys.stdout.flush()


class ImageWorker(pdfium.Worker):
    script = Path(__file__).resolve()


# ---------------------------------------------------------------------------
# The caller side


def find_census(explicit: str | None) -> Path | None:
    candidates = [Path(explicit)] if explicit else []
    for preset in ("mingw-release", "mingw-debug"):
        candidates += [REPO / "out" / preset / "pdfkit_image_census.exe",
                       REPO / "out" / preset / "pdfkit_image_census"]
    return next((c for c in candidates if c.is_file()), None)


def run_census(census: Path, path: Path, dump: Path | None) -> dict:
    command = [str(census), str(path)]
    if dump is not None:
        dump.mkdir(parents=True, exist_ok=True)
        command += ["--dump", str(dump)]
    started = time.monotonic()
    try:
        proc = subprocess.run(command, capture_output=True, timeout=TIMEOUT)
    except subprocess.TimeoutExpired:
        return {"status": "hang", "seconds": TIMEOUT}
    seconds = round(time.monotonic() - started, 3)
    if proc.returncode != 0:
        return {"status": "crash", "exit": proc.returncode, "seconds": seconds,
                "message": proc.stderr.decode("utf-8", "replace")[-300:]}
    try:
        result = json.loads(proc.stdout.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as err:
        return {"status": "crash", "seconds": seconds, "message": f"bad output: {err}"}
    result["seconds"] = seconds
    return result


def compare_pairs(census: Path, pairs: list[tuple[str, str]], folder: Path) -> dict[str, dict]:
    if not pairs:
        return {}
    listing = folder / "pairs.txt"
    listing.write_text("".join(f"{a} {b}\n" for a, b in pairs), encoding="utf-8")
    proc = subprocess.run([str(census), "--compare", str(listing)], capture_output=True,
                          timeout=600)
    out = {}
    for line in proc.stdout.decode("utf-8", "replace").splitlines():
        if line.startswith("{"):
            record = json.loads(line)
            out[record["mine"]] = record
    return out


def stable(first: dict, second: dict) -> bool:
    def key(result):
        return [(i["identity"], i.get("png_sha256"), i.get("extracted_sha256"),
                 i.get("mask_png_sha256")) for i in result.get("images", [])]
    return first.get("status") == second.get("status") and key(first) == key(second)


def one_file(census: Path, pool, row: dict, work: Path, repeat: bool, keep: bool) -> dict:
    import common
    path = common.file_path(row)
    folder = work / row["id"]
    result = run_census(census, path, folder / "pdfkit")
    images = result.get("images", [])
    record = {"row": row, "census": result, "pdfium": None, "compared": {}, "stable": None}
    try:
        decoded = [i for i in images if i.get("decoded")]
        if decoded:
            want = sorted({i["raw_sha256"] for i in decoded})
            answer = pool.ask({"path": str(path), "want": want, "out": str(folder / "pdfium")})
            record["pdfium"] = answer
            pairs = []
            if answer.get("ok"):
                by_digest = answer["images"]
                for image in decoded:
                    theirs = by_digest.get(image["raw_sha256"], {}).get("bitmap")
                    if theirs:
                        mine = folder / "pdfkit" / f"{image['identity'][4:]}.pix"
                        pairs.append((str(mine), theirs))
            compared = compare_pairs(census, pairs, folder)
            for image in decoded:
                mine = str(folder / "pdfkit" / f"{image['identity'][4:]}.pix")
                if mine in compared:
                    record["compared"][image["identity"]] = compared[mine]
        if repeat:
            record["stable"] = stable(result, run_census(census, path, None))
    finally:
        if not keep:
            shutil.rmtree(folder, ignore_errors=True)
    return record


def expectation(result: dict) -> str:
    images = result.get("images") or []
    if result.get("status") != "ok" or not images:
        return "n/a"
    failed = sorted({i["encoding"] for i in images if not i.get("decoded")})
    return "decoded" if not failed else "partial:" + "+".join(failed)


def agrees(image: dict, comparison: dict | None) -> bool | None:
    if comparison is None or comparison.get("status") != "compared":
        return None
    if (image["colour_space"].startswith("Indexed") and image.get("channels") == 3
            and comparison.get("pdfium_format") == 1):
        # FPDFImageObj_GetBitmap widens a 1-bit image to an 8-bit bitmap that
        # keeps the palette, and the API exposes the indices but not the
        # palette, so the colours cannot be compared.
        return None
    if image["encoding"] in BITONAL:
        return comparison["differing"] == 0
    return comparison["mad"] <= CONTINUOUS_LIMIT


def percent(part: int, whole: int) -> str:
    return f"{100.0 * part / whole:.2f}%" if whole else "n/a"


def find_cli(explicit: str | None) -> Path | None:
    candidates = [Path(explicit)] if explicit else []
    for preset in ("mingw-release", "mingw-debug"):
        candidates += [REPO / "out" / preset / "pdfkit.exe", REPO / "out" / preset / "pdfkit"]
    return next((c for c in candidates if c.is_file()), None)


def listing_file(cli: Path, pool, row: dict, work: Path) -> dict:
    """pdfkit images over one file (the C ABI's listing) and PDFium's image
    objects per page, pages and drawn forms walked."""
    import common
    path = common.file_path(row)
    started = time.monotonic()
    try:
        proc = subprocess.run([str(cli), "images", str(path)], capture_output=True,
                              timeout=TIMEOUT)
    except subprocess.TimeoutExpired:
        return {"status": "hang"}
    seconds = round(time.monotonic() - started, 3)
    try:
        listing = json.loads(proc.stdout.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError):
        return {"status": "crash", "exit": proc.returncode, "seconds": seconds}
    if proc.returncode != 0:
        return {"status": "refused", "code": listing.get("code"), "seconds": seconds}
    answer = pool.ask({"path": str(path), "want": [], "out": str(work / row["id"])})
    shutil.rmtree(work / row["id"], ignore_errors=True)
    return {"status": "ok", "listing": listing, "pdfium": answer, "seconds": seconds}


def run_listing(args, rows: list[dict], out_dir: Path) -> int:
    """The listing half of the gate: occurrences from the content interpreter,
    decoded per encoding, every undecoded one with its warning, and the
    occurrences per page against PDFium's image objects per page."""
    cli = find_cli(args.pdfkit)
    if cli is None:
        print("SKIPPED image listing: no pdfkit executable found; build it with "
              "`cmake --build --preset mingw-release`, or pass --pdfkit <path>.")
        return 0
    work = out_dir / "work"
    results: dict[str, dict] = {}
    with pdfium.Pool(size=max(1, args.workers // 2), worker_class=ImageWorker) as pool, \
            concurrent.futures.ThreadPoolExecutor(args.workers) as executor:
        jobs = {executor.submit(listing_file, cli, pool, r, work): r["id"] for r in rows}
        for job in concurrent.futures.as_completed(jobs):
            results[jobs[job]] = job.result()
    shutil.rmtree(work, ignore_errors=True)
    by_encoding: dict[str, collections.Counter] = collections.defaultdict(collections.Counter)
    pages_compared = pages_equal = 0
    unwarned, crashes, mismatched = [], [], []
    lines = ["set\tid\tidentity\tsource\tencoding\tcolour_space\toccurrences\tpattern_pages\t"
             "decodable\twarnings"]
    for row in rows:
        result = results[row["id"]]
        if result["status"] in ("hang", "crash"):
            crashes.append((row["id"], result["status"]))
            continue
        if result["status"] != "ok":
            continue
        listing = result["listing"]
        mine: collections.Counter = collections.Counter()
        for image in listing["images"]:
            occurrences = len(image["occurrences"])
            counter = by_encoding[image["encoding"]]
            counter["images"] += 1
            counter["occurrences"] += occurrences
            counter["decoded"] += occurrences if image["decodable"] else 0
            counter["in_pattern"] += 1 if image["pattern_pages"] else 0
            if not image["decodable"] and "undecodable_image" not in image["warnings"]:
                unwarned.append((row["id"], image["identity"]))
            for occurrence in image["occurrences"]:
                mine[occurrence["page"]] += 1
            lines.append("\t".join(str(v) for v in (
                row["set"], row["id"], image["identity"], image["source"], image["encoding"],
                image["colour_space"], occurrences, ",".join(map(str, image["pattern_pages"])),
                image["decodable"], ",".join(image["warnings"]))))
        pdfium_answer = result["pdfium"]
        if pdfium_answer.get("ok"):
            theirs = {int(k): v for k, v in pdfium_answer.get("counts", {}).items()}
            for page in sorted(set(mine) | set(theirs)):
                pages_compared += 1
                if mine.get(page, 0) == theirs.get(page, 0):
                    pages_equal += 1
                else:
                    mismatched.append((row["id"], page, mine.get(page, 0), theirs.get(page, 0)))
    (out_dir / "listing.tsv").write_text("\n".join(lines) + "\n", encoding="utf-8")
    total = collections.Counter()
    for counter in by_encoding.values():
        total.update(counter)
    summary = [
        "# Image listing",
        "",
        f"Files: {len(rows)}; crashes or hangs: {len(crashes)}.",
        "",
        f"1. Decoded occurrences: {total['decoded']} of {total['occurrences']} "
        f"({percent(total['decoded'], total['occurrences'])}); images listed {total['images']}, "
        f"{total['in_pattern']} reached inside a pattern or soft mask.",
        f"2. Undecodable images without their warning: {len(unwarned)}.",
        f"3. Pages whose occurrence count equals PDFium's image objects: {pages_equal} of "
        f"{pages_compared} ({percent(pages_equal, pages_compared)}).",
        "",
        "| Encoding | Images | Occurrences | Decoded occurrences |",
        "|---|---|---|---|",
    ]
    for name in sorted(by_encoding):
        c = by_encoding[name]
        summary.append(f"| {name} | {c['images']} | {c['occurrences']} | {c['decoded']} |")
    summary += ["", "## Pages whose counts differ (file, page, pdfkit, PDFium)", ""]
    summary += [f"- {i} p{p}: {a} / {b}" for i, p, a, b in mismatched] or ["none"]
    summary += ["", "## Crashes and hangs", ""] + ([f"- {i}: {s}" for i, s in crashes] or ["none"])
    (out_dir / "listing.md").write_text("\n".join(summary) + "\n", encoding="utf-8")
    print("\n".join(summary[:8]))
    return 1 if crashes or unwarned else 0


def main() -> int:
    import common
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", required=True, help="the report folder")
    parser.add_argument("--census", help="the pdfkit_image_census executable (default: out/)")
    parser.add_argument("--workers", type=int, default=os.cpu_count() or 4)
    parser.add_argument("--set", action="append", help="only these sets")
    parser.add_argument("--id", action="append", help="only these ids")
    parser.add_argument("--repeat", action="store_true", help="run the census twice per file")
    parser.add_argument("--keep", action="store_true", help="keep the decoded dumps")
    parser.add_argument("--listing", action="store_true",
                        help="measure the listing (pdfkit images) instead of the decoders")
    parser.add_argument("--pdfkit", help="the pdfkit executable for --listing (default: out/)")
    parser.add_argument("--write-manifest", action="store_true",
                        help="write the run's values into the manifest's expect_images column")
    parser.add_argument("--check", action="store_true",
                        help="fail when a file's value differs from expect_images")
    args = parser.parse_args()

    if args.listing:
        import common
        rows = [r for r in common.read_manifest() if r["expect_open"] in ("opens", "repaired")]
        if args.set:
            rows = [r for r in rows if r["set"] in args.set]
        if args.id:
            rows = [r for r in rows if r["id"] in args.id]
        rows = [r for r in rows if common.file_path(r).exists()]
        out_dir = Path(args.out)
        out_dir.mkdir(parents=True, exist_ok=True)
        return run_listing(args, rows, out_dir)
    census = find_census(args.census)
    if census is None:
        print("SKIPPED image agreement: no pdfkit_image_census executable found. Build it with "
              "`cmake --preset mingw-release && cmake --build --preset mingw-release`, or pass "
              "--census <path>.")
        return 0
    all_rows = common.read_manifest()
    rows = [r for r in all_rows if r["expect_open"] in ("opens", "repaired")]
    if args.set:
        rows = [r for r in rows if r["set"] in args.set]
    if args.id:
        rows = [r for r in rows if r["id"] in args.id]
    missing = [r["id"] for r in rows if not common.file_path(r).exists()]
    if missing:
        print(f"SKIPPED {len(missing)} rows whose files are absent (first {missing[0]}); "
              "run `python dataset/tools/fetch.py`", file=sys.stderr)
        rows = [r for r in rows if r["id"] not in set(missing)]
    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)
    work = out_dir / "work"

    started = time.monotonic()
    records: dict[str, dict] = {}
    with pdfium.Pool(size=max(1, args.workers // 2), worker_class=ImageWorker) as pool, \
            concurrent.futures.ThreadPoolExecutor(args.workers) as executor:
        jobs = {executor.submit(one_file, census, pool, r, work, args.repeat, args.keep): r["id"]
                for r in rows}
        for job in concurrent.futures.as_completed(jobs):
            records[jobs[job]] = job.result()
    seconds = round(time.monotonic() - started, 1)
    if not args.keep:
        shutil.rmtree(work, ignore_errors=True)

    lines = ["set\tid\tidentity\tobject\tpages\tencoding\tcolour_space\tsize\tbits\tmask\t"
             "decoded\tdamaged\tfault\tpdfium\tmad\tdiffering\tagrees"]
    by_encoding: dict[str, collections.Counter] = collections.defaultdict(collections.Counter)
    by_space: dict[str, collections.Counter] = collections.defaultdict(collections.Counter)
    failures, crashes, unstable, disagreements, by_design = [], [], [], [], []
    for row in rows:
        record = records[row["id"]]
        result = record["census"]
        if result.get("status") not in ("ok", "refused"):
            crashes.append((row["id"], result.get("status"), result.get("message", "")))
        if record["stable"] is False:
            unstable.append(row["id"])
        for image in result.get("images", []):
            comparison = record["compared"].get(image["identity"])
            verdict = agrees(image, comparison)
            if image.get("matte") and verdict is False:
                # /Matte is undone by design (DESIGN.md 9.4); PDFium's bitmap
                # keeps the premultiplied colours.
                by_design.append((row["id"], image["identity"], comparison.get("mad")))
                verdict = None
            occurrences = 0 if image["in_pattern"] else len(image["pages"])
            for table, name in ((by_encoding, image["encoding"]), (by_space, image["colour_space"])):
                counter = table[name]
                counter["images"] += 1
                counter["occurrences"] += occurrences
                counter["decoded"] += 1 if image["decoded"] else 0
                counter["decoded_occurrences"] += occurrences if image["decoded"] else 0
                counter["compared"] += 0 if verdict is None else 1
                counter["agree"] += 1 if verdict else 0
            if not image["decoded"]:
                failures.append((row["id"], image["identity"], image["encoding"], image["fault"]))
            if verdict is False:
                disagreements.append((row["id"], image["identity"], image["encoding"],
                                      image["colour_space"], comparison.get("mad"),
                                      comparison.get("differing"), comparison.get("pixels")))
            lines.append("\t".join(str(v) for v in (
                row["set"], row["id"], image["identity"], image["object"],
                ",".join(map(str, image["pages"])), image["encoding"], image["colour_space"],
                f"{image['width']}x{image['height']}", image["bits"], image["mask"],
                image["decoded"], image["damaged"], image["fault"].replace("\t", " "),
                "" if comparison is None else comparison.get("status"),
                "" if comparison is None else comparison.get("mad", ""),
                "" if comparison is None else comparison.get("differing", ""),
                "" if verdict is None else verdict)))
    (out_dir / "images.tsv").write_text("\n".join(lines) + "\n", encoding="utf-8")

    total = collections.Counter()
    for counter in by_encoding.values():
        total.update(counter)
    bitonal = collections.Counter()
    continuous = collections.Counter()
    for name, counter in by_encoding.items():
        (bitonal if name in BITONAL else continuous).update(counter)
    summary = [
        "# Image agreement",
        "",
        f"Files: {len(rows)} (open expectation opens or repaired); seconds: {seconds}; "
        f"crashes or hangs: {len(crashes)}; unstable: "
        f"{len(unstable) if args.repeat else 'not measured'}.",
        "",
        f"1. Decoded: {total['decoded']} of {total['images']} images "
        f"({percent(total['decoded'], total['images'])}); {total['decoded_occurrences']} of "
        f"{total['occurrences']} page occurrences reached through resources "
        f"({percent(total['decoded_occurrences'], total['occurrences'])}).",
        f"2. Bitonal agreement (pixel for pixel): {bitonal['agree']} of {bitonal['compared']} "
        f"({percent(bitonal['agree'], bitonal['compared'])}).",
        f"3. Continuous-tone agreement (MAD <= {CONTINUOUS_LIMIT}): {continuous['agree']} of "
        f"{continuous['compared']} ({percent(continuous['agree'], continuous['compared'])}).",
        "",
        "| Encoding | Images | Decoded | Occurrences | Decoded occ. | Compared | Agree |",
        "|---|---|---|---|---|---|---|",
    ]
    for name in sorted(by_encoding):
        c = by_encoding[name]
        summary.append(f"| {name} | {c['images']} | {c['decoded']} | {c['occurrences']} | "
                       f"{c['decoded_occurrences']} | {c['compared']} | {c['agree']} |")
    summary += ["", "| Colour space | Images | Decoded | Compared | Agree |", "|---|---|---|---|---|"]
    for name in sorted(by_space):
        c = by_space[name]
        summary.append(f"| {name} | {c['images']} | {c['decoded']} | {c['compared']} | {c['agree']} |")
    summary += ["", "## Undecoded images", ""]
    summary += [f"- {i} {ident} {enc}: {fault}" for i, ident, enc, fault in sorted(failures)] or ["none"]
    summary += ["", "## Disagreements with PDFium", ""]
    summary += [f"- {i} {ident} {enc} {space}: MAD {mad}, {diff} of {px} pixels differ"
                for i, ident, enc, space, mad, diff, px in sorted(disagreements)] or ["none"]
    summary += ["", "## Differences by design (/Matte undone), not compared", ""]
    summary += [f"- {i} {ident}: MAD {mad}" for i, ident, mad in sorted(by_design)] or ["none"]
    summary += ["", "## Crashes and hangs", ""]
    summary += [f"- {i}: {status} {message}" for i, status, message in crashes] or ["none"]
    if args.repeat:
        summary += ["", "## Unstable files", ""] + ([f"- {i}" for i in unstable] or ["none"])
    (out_dir / "summary.md").write_text("\n".join(summary) + "\n", encoding="utf-8")
    print("\n".join(summary[:12]))

    status = 0
    if args.write_manifest or args.check:
        values = {row["id"]: expectation(records[row["id"]]["census"]) for row in rows}
        if args.write_manifest:
            for row in all_rows:
                # A file whose resources reach no image keeps its value: an
                # inline image or an image drawn only by an annotation is not
                # reached through resources.
                if row["id"] in values and values[row["id"]] != "n/a":
                    row["expect_images"] = values[row["id"]]
                elif row["expect_open"] not in ("opens", "repaired"):
                    row["expect_images"] = "n/a"
            common.write_manifest(all_rows)
        else:
            differing = [(i, v) for i, v in values.items() if v != "n/a"
                         and next(r for r in all_rows if r["id"] == i)["expect_images"] != v]
            for ident, value in differing:
                print(f"{ident}: expect_images differs; the run gives {value}")
            status = 1 if differing else 0
    return 1 if crashes else status


if __name__ == "__main__":
    if len(sys.argv) >= 2 and sys.argv[1] == "--serve":
        serve(Path(sys.argv[sys.argv.index("--dll") + 1]))
    else:
        sys.exit(main())
