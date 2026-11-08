# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# The bitonal gate judged by the reference panel: every bitonal image (JBIG2 and
# CCITT, as tests/pdfium/image_agreement.py counts them) of a run's image
# census (<run>/images/images.tsv, written by that script) is compared pixel for
# pixel with the pixel members' answers (PDFium's rendered bitmap, Poppler's
# pdfimages and MuPDF's mutool extract), read from the answers cache under the
# request the collection asked with (tests/panel/evidence.py). pdfkit's pixels
# are the census's dumps: <run>/images/work/<id>/pdfkit/ when the census kept
# them, else the census executable is run once per file with --dump into
# <run>/panel-pixels/<id>/. An image's verdict is tests/panel/judge.py
# pixel_verdict: at least two voters at the image's size, a stencil mask
# compared by what it paints, Poppler abstaining on stencil masks
# (tests/panel/members.tsv `abstains`). The gate: every image with a majority
# agrees with it, the bitonal gate's threshold. It writes a JSON and a markdown
# report named after this script into the run folder: agreed over decided, every disagreement
# and every undecided image with why, and the verdicts on qpdf-0026,
# synth-0263 and synth-0265, the three images PDFium's bitmap call disputes.
"""The bitonal gate under the panel: python tests/dataset/gates/phase6_panel.py --run <run-id> [--smoke]."""

from __future__ import annotations

import argparse
import concurrent.futures
import json
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[3]
for folder in (REPO / "tests" / "panel", REPO / "tests" / "pdfium", REPO / "dataset" / "tools"):
    sys.path.insert(0, str(folder))

import collect  # noqa: E402
import common  # noqa: E402
import evidence  # noqa: E402
import judge  # noqa: E402
import readers  # noqa: E402

THRESHOLD = 1.0
NAMED = ("qpdf-0026", "synth-0263", "synth-0265")


def census_tsv(run_dir: Path) -> Path:
    path = run_dir / "images" / "images.tsv"
    if not path.exists():
        raise common.ToolError(f"{path} does not exist", "the run's image census",
                               f"run `python tests/pdfium/image_agreement.py --out {run_dir / 'images'}` first")
    return path


def dump_folder(run_dir: Path, file_id: str, path: Path, census: Path | None) -> Path | None:
    """The folder holding pdfkit's dumps of a file's images, made by the census when missing."""
    kept = run_dir / "images" / "work" / file_id / "pdfkit"
    if kept.is_dir():
        return kept
    folder = run_dir / "panel-pixels" / file_id
    if folder.is_dir() and any(folder.iterdir()):
        return folder
    if census is None:
        return None
    import image_agreement  # noqa: PLC0415

    image_agreement.run_census(census, path, folder)
    return folder


def pdfkit_pixels(folder: Path | None, image: dict) -> dict | None:
    return None if folder is None else evidence.read_dump(folder / f"{image['identity'][4:]}.pix")


def judge_file(file_id: str, images: list[dict], rows: dict, run_dir: Path, panel: evidence.Panel,
               census: Path | None) -> list[dict]:
    path = common.file_path(rows[file_id])
    requests = evidence.pixel_requests(panel, path, images)
    answers = evidence.pixel_answers(panel, path, requests) if requests else {}
    folder = dump_folder(run_dir, file_id, path, census)
    out = []
    for image in images:
        if image["reason"] != "bitonal":
            continue
        record = {"id": file_id, "object": image["object"], "identity": image["identity"],
                  "encoding": image["encoding"], "colour_space": image["colour_space"], "size": image["size"]}
        mine = pdfkit_pixels(folder, image)
        if mine is None:
            why = "pdfkit decoded no pixels" if folder is not None else (
                "pdfkit's dumps are missing and no pdfkit_image_census was found; build it with "
                f"`cmake --build --preset {common.build_presets()[0]}` or pass --census")
            out.append({**record, "verdict": "undecided", "why": why, "voters": [],
                        "abstained": {}, "majority": []})
            continue
        stencil = image["colour_space"] == "stencil"
        votes = {}
        for name in evidence.PIXEL_MEMBERS:
            answer = answers.get(name)
            if name in panel.absent:
                votes[name] = {"error": "absent"}
            elif answer is None:
                votes[name] = {"error": "no answer"}
            elif not answer.get("ok"):
                votes[name] = {"error": answer.get("error") or "refused"}
            else:
                votes[name] = panel.samples(name, (answer.get("images") or {}).get(str(image["object"])))
        abstain = {n for n in evidence.PIXEL_MEMBERS
                   if stencil and n in panel.members and "stencil" in readers.abstains(n, "image_pixels")}
        verdict = judge.pixel_verdict(mine, votes, stencil=stencil, abstain=abstain)
        out.append({**record, **verdict})
    return out


def summarise(results: list[dict]) -> dict:
    decided = [r for r in results if r["verdict"] != "undecided"]
    agreed = [r for r in decided if r["verdict"] == "agrees"]
    share = round(len(agreed) / len(decided), 4) if decided else None
    keep = ("id", "object", "identity", "encoding", "colour_space", "size", "verdict", "voters", "majority",
            "abstained", "why")
    return {
        "images": len(results), "decided": len(decided), "agreed": len(agreed), "share_agreed": share,
        "undecided": len(results) - len(decided),
        "disagreements": [{k: r.get(k) for k in keep} for r in decided if r["verdict"] != "agrees"],
        "undecided_images": [{k: r.get(k) for k in keep} for r in results if r["verdict"] == "undecided"],
        "named": [{k: r.get(k) for k in keep} for r in results if r["id"] in NAMED],
        "gate": "passed" if share is not None and share >= THRESHOLD else "failed",
    }


def write_report(run_dir: Path, summary: dict, absent: dict) -> None:
    (run_dir / "phase6_panel.json").write_text(json.dumps(summary, indent=1) + "\n", encoding="utf-8")
    s = summary

    def line(r):
        abstained = ", ".join(f"{m}: {w}" for m, w in sorted((r.get("abstained") or {}).items()))
        return (f"| {r['id']} | {r['object']} | {r['encoding']} | {r['colour_space']} | {r['verdict']} | "
                f"{', '.join(r.get('majority') or [])} | {', '.join(r.get('voters') or [])} | "
                f"{r.get('why') or ''} | {abstained} |")

    head = ["| id | object | encoding | colour space | verdict | majority | voters | why | abstained |",
            "|---|---|---|---|---|---|---|---|---|"]
    md = [f"# Bitonal gate under the panel, run {run_dir.name}", "",
          f"- bitonal images: {s['images']}; decided: {s['decided']}; undecided: {s['undecided']}",
          f"- agreeing with the pixel majority: {s['agreed']} of {s['decided']} ({s['share_agreed']}; "
          f"gate {THRESHOLD})", f"- gate: **{s['gate']}**", ""]
    if absent:
        md += ["Absent members: " + "; ".join(absent.values()), ""]
    md += ["## The images PDFium's bitmap call disputes", ""] + head + [line(r) for r in s["named"]]
    md += ["", "## Disagreements with the majority", ""] + head + [line(r) for r in s["disagreements"]]
    md += ["", "## Undecided images", ""] + head + [line(r) for r in s["undecided_images"]]
    (run_dir / "phase6_panel.md").write_text("\n".join(md) + "\n", encoding="utf-8")


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description="The bitonal gate judged by the reference panel.")
    parser.add_argument("--run", required=True, help="the dataset run id whose image census is judged")
    parser.add_argument("--smoke", action="store_true", help="only the files of tests/panel/smoke.tsv")
    parser.add_argument("--id", action="append", help="only these ids")
    parser.add_argument("--collect", action="store_true", help="ask a member for an answer the cache lacks")
    parser.add_argument("--census", help="the pdfkit_image_census executable (default: out/)")
    parser.add_argument("--workers", type=int, default=2)
    args = parser.parse_args(argv)

    run_dir = common.reports_dir() / args.run
    wanted = collect.wanted_images(census_tsv(run_dir))
    import image_agreement  # noqa: PLC0415

    census = image_agreement.find_census(args.census)
    rows = {r["id"]: r for r in common.read_manifest()}
    ids = sorted(i for i, images in wanted.items() if any(x["reason"] == "bitonal" for x in images))
    if args.smoke:
        chosen = set(collect.smoke_ids())
        ids = [i for i in ids if i in chosen]
    if args.id:
        ids = [i for i in ids if i in args.id]
    panel = evidence.Panel(evidence.PIXEL_MEMBERS + ("qpdf",), collect_missing=args.collect)
    with concurrent.futures.ThreadPoolExecutor(max(1, args.workers)) as pool:
        results = [r for batch in pool.map(
            lambda i: judge_file(i, wanted[i], rows, run_dir, panel, census), ids) for r in batch]
    summary = summarise(results)
    summary["absent"] = panel.absent
    write_report(run_dir, summary, panel.absent)
    print(json.dumps({k: summary[k] for k in ("images", "decided", "agreed", "share_agreed", "undecided", "gate")},
                     indent=1))
    for r in summary["named"]:
        print(f"{r['id']} object {r['object']}: {r['verdict']} (majority {r['majority']}, voters {r['voters']})")
    return 0 if summary["gate"] == "passed" else 1


if __name__ == "__main__":
    common.run_tool(main)
