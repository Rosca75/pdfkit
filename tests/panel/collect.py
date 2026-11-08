# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# Asks every member of the reference panel about every dataset file, through
# the answers cache (answers.py), so the judge reads the members' answers
# without starting a process. For each manifest file present on disk and each
# member whose binaries equal their pins (readers.pin_problem), every kind the
# member has (`text`, `pages`, `structure`, `image_pixels`) is answered within
# 30 s per started block of 200 pages. A member that is absent or differs from
# its pin is skipped by name with the command that supplies it, and the others
# go on; a manifest row without its file (the laptop-made `office` rows) is
# skipped by name.
#
# `image_pixels` is asked only for the images the judge needs: every bitonal
# image of an image census (JBIG2 and CCITT, the bitonal gate's images) and
# every image whose colour space is DeviceCMYK, a four-component ICCBased,
# Separation or DeviceN (the CMYK re-decision). The census is the images.tsv of
# a run of tests/pdfium/image_agreement.py (`--census <run-id>`, by default the
# newest run that has one); an image is named by its object number, and QPDF's
# raw stream bytes of that object (the `raw_streams` kind, cached like any
# answer) give the SHA-256 PDFium aligns its image objects by, with Poppler's
# per-page draw order (the `image_list` kind) where two objects hold the same
# bytes. The request each file was asked with (`request` for Poppler and MuPDF,
# `pdfium` for PDFium, which adds the draw order) is written into the report's
# pixel_requests, so the judge asks the cache with the same request.
#
# It writes dataset/reports/<run-id>/panel/collect.json and images.tsv and
# prints, per member and kind, the files answered, refused (by cause),
# transient and unsupported, the answers read back from the cache, the
# questions asked and the seconds spent, and the child processes started: a
# collection whose every answer is cached starts none.
"""Collect the panel's answers: python tests/panel/collect.py --run-id <id> [--kinds ...] [--smoke]."""

from __future__ import annotations

import argparse
import collections
import concurrent.futures
import csv
import json
import sys
import threading
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(REPO / "tests" / "pdfium"))
sys.path.insert(0, str(REPO / "dataset" / "tools"))

import common  # noqa: E402
import pdfium  # noqa: E402
import answers  # noqa: E402
import readers  # noqa: E402
from readers import pdfium_member  # noqa: E402

KINDS = ("text", "pages", "structure", "image_pixels")
SMOKE = HERE / "smoke.tsv"
BITONAL = {"JBIG2Decode", "CCITTFaxDecode"}
CMYK_SPACES = ("DeviceCMYK", "ICCBased/4", "Separation", "DeviceN")


def smoke_ids() -> list[str]:
    return [line.split("\t")[0] for line in SMOKE.read_text(encoding="utf-8").splitlines()[1:] if line.strip()]


def find_census(run_id: str | None) -> Path | None:
    """images.tsv of the named run, or of the newest run that has one."""
    if run_id:
        path = common.reports_dir() / run_id / "images" / "images.tsv"
        if not path.exists():
            raise common.ToolError(f"{path} does not exist", "an image census of that run",
                                   "run `python tests/pdfium/image_agreement.py --out "
                                   f"{common.reports_dir() / run_id / 'images'}` first")
        return path
    found = sorted(common.reports_dir().glob("*/images/images.tsv"), key=lambda p: p.stat().st_mtime)
    return found[-1] if found else None


def pixel_reason(row: dict) -> str | None:
    if row["encoding"] in BITONAL:
        return "bitonal"
    if row["colour_space"].startswith(CMYK_SPACES):
        return "cmyk"
    return None


def wanted_images(census: Path | None) -> dict[str, list[dict]]:
    """id -> the images whose pixels the panel is asked for, by object number, in object order."""
    out: dict[str, list[dict]] = collections.defaultdict(list)
    if census is None:
        return out
    with census.open(encoding="utf-8", newline="") as handle:
        for row in csv.DictReader(handle, delimiter="\t"):
            reason = pixel_reason(row)
            if reason is None or not row["object"].isdigit():
                continue
            pages = sorted({int(p) for p in row["pages"].split(",") if p.strip().isdigit()})
            if not pages:
                continue
            out[row["id"]].append({"object": int(row["object"]), "pages": pages, "reason": reason,
                                   "identity": row["identity"], "encoding": row["encoding"],
                                   "colour_space": row["colour_space"], "size": row["size"]})
    for images in out.values():
        images.sort(key=lambda i: i["object"])
    return out


def pixel_request(images: list[dict], streams: dict) -> list[dict]:
    """The request every pixel member is asked with: object, raw digest and pages, by object."""
    return [{"object": i["object"], "raw": (streams.get(str(i["object"])) or {}).get("sha256"),
             "pages": i["pages"]} for i in images]


class Tally:
    """Counts per member and kind, shared by the collection's threads."""

    def __init__(self) -> None:
        self.lock = threading.Lock()
        self.counts: dict[tuple[str, str], collections.Counter] = collections.defaultdict(collections.Counter)
        self.causes: dict[tuple[str, str], collections.Counter] = collections.defaultdict(collections.Counter)
        self.seconds: dict[tuple[str, str], float] = collections.defaultdict(float)
        self.stats: dict[tuple[str, str], answers.Stats] = collections.defaultdict(answers.Stats)
        self.transient: list[dict] = []
        self.images: dict[tuple[str, str], collections.Counter] = collections.defaultdict(collections.Counter)

    def add(self, member: str, kind: str, file_id: str, answer: dict, seconds: float) -> None:
        key = (member, kind)
        with self.lock:
            self.seconds[key] += seconds
            if answer.get("ok"):
                self.counts[key]["answered"] += 1
                for entry in (answer.get("images") or {}).values():
                    self.images[key]["pixels" if "blob" in entry else entry.get("error", "error")] += 1
            elif answer.get("error") in readers.TRANSIENT:
                self.counts[key]["transient"] += 1
                self.transient.append({"member": member, "kind": kind, "id": file_id,
                                       "error": answer.get("error"), "detail": answer.get("detail")})
            elif answer.get("error") == "unsupported":
                self.counts[key]["unsupported"] += 1
            else:
                self.counts[key]["refused"] += 1
                self.causes[key][cause_of(answer)] += 1


def cause_of(answer: dict) -> str:
    """A short cause for a refusal: the member's first line, its numbers dropped."""
    detail = (answer.get("detail") or answer.get("error") or "").strip().splitlines()
    first = detail[-1] if detail else "refused"
    text = "".join("#" if c.isdigit() else c for c in first)
    return text[:90]


def one_file(row: dict, members: list, kinds: tuple, wanted: list[dict], tally: Tally,
             requests: dict) -> None:
    path = common.file_path(row)
    pages = int(row["pages"]) if str(row["pages"]).isdigit() else 1
    timeout = readers.timeout_for(pages)
    pixel = None
    if "image_pixels" in kinds and wanted and any("image_pixels" in m.CAPABILITIES for m in members):
        started = time.monotonic()
        raw = answers.cached_answer(readers.load("qpdf"), path, "raw_streams", timeout=timeout,
                                    stats=tally.stats[("qpdf", "raw_streams")],
                                    objects=[i["object"] for i in wanted])
        tally.add("qpdf", "raw_streams", row["id"], raw, time.monotonic() - started)
        pixel = {"images": pixel_request(wanted, raw.get("streams") or {})}
        ordered = dict(pixel)
        if any(m.NAME == "poppler" for m in members) and any(m.NAME == "pdfium" for m in members):
            started = time.monotonic()
            listing = answers.cached_answer(readers.load("poppler"), path, "image_list", timeout=timeout,
                                            stats=tally.stats[("poppler", "image_list")],
                                            pages=sorted({p for i in wanted for p in i["pages"]}))
            tally.add("poppler", "image_list", row["id"], listing, time.monotonic() - started)
            if listing.get("ok"):
                ordered["order"] = listing["order"]
        with tally.lock:
            requests[row["id"]] = {"request": pixel, "pdfium": ordered}
    for member in members:
        for kind in kinds:
            if kind not in member.CAPABILITIES or (kind == "image_pixels" and pixel is None):
                continue
            request = {}
            if kind == "image_pixels":
                request = ordered if member.NAME == "pdfium" else pixel
            started = time.monotonic()
            # A pixel answer decodes every image asked: 30 s per block of pages and 2 s per image.
            limit = timeout + 2.0 * len(wanted) if kind == "image_pixels" else timeout
            answer = answers.cached_answer(member, path, kind, timeout=limit,
                                           stats=tally.stats[(member.NAME, kind)], **request)
            tally.add(member.NAME, kind, row["id"], answer, time.monotonic() - started)


def summary_rows(tally: Tally, applicable: dict) -> list[dict]:
    rows = []
    for (member, kind), counts in sorted(tally.counts.items()):
        stats = tally.stats[(member, kind)]
        rows.append({"member": member, "kind": kind, "applicable": applicable.get(kind, 0),
                     "answered": counts["answered"], "refused": counts["refused"],
                     "transient": counts["transient"], "unsupported": counts["unsupported"],
                     "cached": stats.cached, "asked": stats.asked,
                     "seconds": round(tally.seconds[(member, kind)], 1),
                     "causes": dict(tally.causes[(member, kind)].most_common()),
                     "images": dict(tally.images[(member, kind)])})
    return rows


def main() -> int:
    parser = argparse.ArgumentParser(description="Collect the reference panel's answers into the cache.")
    parser.add_argument("--kinds", default=",".join(KINDS), help="comma-separated kinds (default every one)")
    parser.add_argument("--members", help="comma-separated members (default every present one)")
    parser.add_argument("--set", action="append", help="only files of this set (repeatable)")
    parser.add_argument("--id", action="append", help="only this file id (repeatable)")
    parser.add_argument("--smoke", action="store_true", help="only the files of tests/panel/smoke.tsv")
    parser.add_argument("--census", help="the run id whose images/images.tsv names the images to ask for")
    parser.add_argument("--workers", type=int, default=pdfium.default_workers())
    parser.add_argument("--run-id", required=True, help="the report folder under dataset/reports/")
    args = parser.parse_args()

    kinds = tuple(k for k in args.kinds.split(",") if k)
    unknown = [k for k in kinds if k not in KINDS]
    if unknown:
        raise common.ToolError(f"unknown kinds {unknown}", f"some of {list(KINDS)}", "fix --kinds")
    names = args.members.split(",") if args.members else list(readers.MODULES)
    members, absent = [], {}
    for name in names:
        module = readers.load(name)
        problem = module.missing()
        if problem:
            absent[module.NAME] = problem
            print(f"skipped: {problem}", file=sys.stderr)
        else:
            members.append(module)
    if "image_pixels" in kinds and any("image_pixels" in m.CAPABILITIES for m in members) \
            and readers.load("qpdf").missing():
        raise common.ToolError("QPDF is absent", "QPDF, which aligns the images by their stream bytes",
                               readers.load("qpdf").missing())

    rows = common.read_manifest()
    if args.smoke:
        chosen = set(smoke_ids())
        rows = [r for r in rows if r["id"] in chosen]
    if args.set:
        rows = [r for r in rows if r["set"] in args.set]
    if args.id:
        rows = [r for r in rows if r["id"] in args.id]
    missing_files = [r["id"] for r in rows if not common.file_path(r).exists()]
    rows = [r for r in rows if common.file_path(r).exists()]
    for file_id in missing_files:
        print(f"skipped: {file_id} has no file on this host", file=sys.stderr)

    census = find_census(args.census) if "image_pixels" in kinds else None
    wanted = wanted_images(census)
    applicable = {kind: len(rows) for kind in kinds}
    applicable["image_pixels"] = sum(1 for r in rows if wanted.get(r["id"]))
    applicable["raw_streams"] = applicable["image_list"] = applicable["image_pixels"]

    pdfium_member.set_workers(args.workers)
    tally, requests = Tally(), {}
    before = readers.started()
    started = time.monotonic()
    try:
        with concurrent.futures.ThreadPoolExecutor(max(1, args.workers)) as pool:
            jobs = [pool.submit(one_file, row, members, kinds, wanted.get(row["id"], []), tally, requests)
                    for row in rows]
            for job in jobs:
                job.result()
    finally:
        pdfium_member.close()
    seconds = round(time.monotonic() - started, 1)
    processes = readers.started() - before

    out_dir = common.reports_dir() / args.run_id / "panel"
    out_dir.mkdir(parents=True, exist_ok=True)
    summary = summary_rows(tally, applicable)
    report = {
        "run_id": args.run_id, "files": len(rows), "kinds": list(kinds), "seconds": seconds,
        "workers": args.workers, "child_processes": processes,
        "members": {m.NAME: {"version": m.version(), "digest": m.binary_digest()} for m in members},
        "absent": absent, "files_missing": missing_files,
        "census": str(census) if census else None,
        "summary": summary, "transient": sorted(tally.transient, key=lambda t: (t["member"], t["kind"], t["id"])),
        "pixel_requests": dict(sorted(requests.items())),
    }
    (out_dir / "collect.json").write_text(json.dumps(report, indent=1, ensure_ascii=False) + "\n", encoding="utf-8")
    with (out_dir / "images.tsv").open("w", encoding="utf-8", newline="") as handle:
        handle.write("id\tobject\treason\tidentity\tencoding\tcolour_space\tsize\tpages\n")
        for file_id in sorted(wanted):
            if file_id not in {r["id"] for r in rows}:
                continue
            for image in wanted[file_id]:
                handle.write("\t".join([file_id, str(image["object"]), image["reason"], image["identity"],
                                        image["encoding"], image["colour_space"], image["size"],
                                        ",".join(map(str, image["pages"]))]) + "\n")

    common.print_table(
        ["member", "kind", "applicable", "answered", "refused", "transient", "unsupported", "cached",
         "asked", "seconds"],
        [[r["member"], r["kind"], r["applicable"], r["answered"], r["refused"], r["transient"],
          r["unsupported"], r["cached"], r["asked"], r["seconds"]] for r in summary])
    print(f"files: {len(rows)}; seconds: {seconds}; child processes started: {processes}; "
          f"report: {out_dir / 'collect.json'}")
    for name, problem in sorted(absent.items()):
        print(f"absent: {problem}")
    return 0


if __name__ == "__main__":
    common.run_tool(main)
