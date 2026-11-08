# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# Three reading choices once taken on PDFium's word alone, re-decided by the
# reference panel's majority, and the word cuts the owner asked the panel to
# judge. Each measure reads the members' cached answers (evidence.py) and a
# dataset run's outcomes; it writes <run>/redecide/<measure>.json and prints
# its verdict.
#
# - `type3` (Type 3 glyph names that restate their code, the files tagged
#   differences_cdd and type3 that pdfkit reads with unreadable codes): a
#   member's surplus is the letters and digits its text holds beyond pdfkit's
#   (the difference of their multisets), which are the characters it reads
#   where pdfkit reads none; it reads the names when its surplus holds at
#   least 10 characters, or every unreadable code of a file with fewer, and
#   agrees with another member's surplus at 0.90 or more by the F1 of the
#   two multisets. Characters, not words, because these fonts draw each glyph
#   apart and the members cut the same letters into different words
#   ("Property" and "P ro p e rty"). The reading the members use is told from
#   the /Differences arrays, per file: a name /Cxx whose hexadecimal or
#   decimal number is its code restates the code. Each member's surplus is
#   checked against the codes themselves: `pdfkit text --glyphs` names every
#   unreadable glyph's code, and where the names restate their codes the
#   surplus must hold as many characters (`count`) and the very characters of
#   those codes (`value`); the check is skipped for a file the binary reads
#   otherwise than the run did.
# - `cmyk` (CMYK through PDFium's Adobe table): judge.colour_distance of
#   pdfkit's RGB against each pixel member's over the DeviceCMYK,
#   four-component ICCBased, Separation and DeviceN images the collection
#   asked for; an image holds when a majority of its voters lie within a mean
#   of 2 per channel, and the table holds when a majority of the images with
#   two voters or more hold. The members' distances to each other are given
#   beside, so the conversion the majority sits closest to is named.
# - `media_box` (a page with no /MediaBox read as US Letter): the `pages`
#   members' sizes of the three specimens of specimens.py (MuPDF's from its
#   `page_sizes` answer; a member that gives no size abstains) beside pdfkit's
#   (`pdfkit inspect`), and the dataset files where lopdf finds a page with no
#   reachable /MediaBox, with each member's size of that page.
# - `cuts` (tests/panel/word_cuts.tsv): for each place, the pieces the row
#   names (`06|51`) are found in pdfkit's text and in each member's, NFKC and
#   case folded; a member reads them `joined` (nothing between), `apart`
#   (white space between, or both pieces present and never side by side), or
#   gives no reading (a piece missing). The verdict is `pdfkit right` when
#   pdfkit reads as the strict majority of the members with a reading,
#   `pdfkit wrong` when the majority reads the other way, `split` on a tie.
"""Re-decide by the panel: python tests/panel/redecide.py {type3,cmyk,media_box,cuts,all} --run <run-id>."""

from __future__ import annotations

import argparse
import collections
import concurrent.futures
import json
import re
import subprocess
import sys
import unicodedata
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
for folder in (HERE, REPO / "tests" / "pdfium", REPO / "tests" / "dataset", REPO / "tests" / "dataset" / "gates",
               REPO / "dataset" / "tools"):
    if str(folder) not in sys.path:
        sys.path.insert(0, str(folder))

import collect  # noqa: E402
import common  # noqa: E402
import evidence  # noqa: E402
import judge  # noqa: E402
import specimens  # noqa: E402

CUTS = HERE / "word_cuts.tsv"
CUT_COLUMNS = ("id", "place", "pieces", "pdfkit", "joined", "apart", "silent", "verdict", "evidence")
PAGE_MEMBERS = ("pdfium", "poppler", "mupdf", "oxide")
# MuPDF's `pages` answer prints only the boxes a page dictionary states; the size it lays a
# page out on is its `page_sizes` answer.
SIZE_KIND = {"mupdf": "page_sizes"}
CHAR_AGREE = 0.90
MIN_CHARS = 10
COLOUR_LIMIT = 2.0
LETTER = (612.0, 792.0)
DIFFERENCES = re.compile(rb"/Differences\s*\[([^\]]*)\]")
TOKEN = re.compile(rb"(\d+)|/([^\s/\[\]()<>]+)")


def outcome(run_dir: Path, file_id: str) -> dict | None:
    path = run_dir / "outcomes" / f"{file_id}.json"
    return json.loads(path.read_text(encoding="utf-8")) if path.exists() else None


def folded(text: str) -> str:
    return unicodedata.normalize("NFKC", text or "").casefold()


def characters(text: str) -> collections.Counter:
    return collections.Counter(c for c in folded(text) if c.isalnum())


def char_f1(a: collections.Counter, b: collections.Counter) -> float:
    return judge.f1(a, b)


# ---------------------------------------------------------------------------
# Type 3 glyph names that restate their code


def restating(data: bytes) -> dict:
    """How the /Differences names /Cxx of a file relate to their codes: counts of names whose
    number read as hexadecimal is the code, read as decimal is the code, and neither."""
    out = collections.Counter()
    for match in DIFFERENCES.finditer(data):
        code = None
        for number, name in TOKEN.findall(match.group(1)):
            if number:
                code = int(number)
                continue
            if code is None:
                continue
            text = name.decode("latin-1")
            found = re.fullmatch(r"[Cc]([0-9A-Fa-f]+)", text)
            if found:
                digits = found.group(1)
                if int(digits, 16) == code:
                    out["hexadecimal"] += 1
                if digits.isdigit() and int(digits) == code:
                    out["decimal"] += 1
                if int(digits, 16) != code and not (digits.isdigit() and int(digits) == code):
                    out["neither"] += 1
            code += 1
    return dict(out)


def name_reading(names: dict) -> str:
    """How a file's /Cxx names restate their codes: in hexadecimal, in decimal (the names /C0 to
    /C9 restate it both ways), none found, or mixed."""
    if not names:
        return "no /Cxx names found"
    if names.get("neither"):
        return "mixed"
    if names.get("hexadecimal", 0) > names.get("decimal", 0):
        return "hexadecimal"
    return "decimal"


def expected_characters(glyphs: list[dict]) -> collections.Counter:
    """The letters and digits of the codes pdfkit counts unreadable, each read as the character
    of its code, as a name restating that code reads it; folded as `characters` folds."""
    out: collections.Counter = collections.Counter()
    for item in glyphs:
        if item.get("rung") == "unreadable" and isinstance(item.get("code"), int) and item["code"] < 0x110000:
            out.update(characters(chr(item["code"])))
    return out


def code_check(surplus: collections.Counter, expected: collections.Counter) -> dict:
    """Whether a member's characters beyond pdfkit's are the unreadable codes: as many, and the
    same characters."""
    return {"count": sum(surplus.values()) == sum(expected.values()), "value": surplus == expected,
            "surplus": sum(surplus.values()), "expected": sum(expected.values()),
            "f1": round(char_f1(surplus, expected), 3)}


def pdfkit_glyphs(command: list[str] | None, path: Path) -> tuple[list[dict], str] | None:
    """Every glyph record `pdfkit text --glyphs` gives and the full text it reads, or None."""
    if command is None:
        return None
    proc = subprocess.run(command + ["text", str(path), "--glyphs"], capture_output=True, timeout=300)
    try:
        result = json.loads(proc.stdout)
    except ValueError:
        return None
    if proc.returncode != 0:
        return None
    return [g for page in result["pages"] for g in page.get("glyphs") or []], evidence.page_text(result)


def measure_type3(run_dir: Path, panel: evidence.Panel, rows: list[dict], command=None) -> dict:
    files = []
    for row in rows:
        tags = set(common.split_tags(row["tags"]))
        result = outcome(run_dir, row["id"])
        if not {"differences_cdd", "type3"} <= tags or result is None:
            continue
        text = result.get("text") or {}
        unreadable = (text.get("rungs") or {}).get("unreadable", 0)
        if text.get("status") != "ok" or not unreadable:
            continue
        path = common.file_path(row)
        pdfkit_chars = characters(text.get("text", ""))
        chars = {}
        for name in evidence.TEXT_MEMBERS:
            answer = panel.answer(name, path, "text")
            if answer and answer.get("ok"):
                chars[name] = characters(judge.member_text(answer)) - pdfkit_chars
        reads = {}
        enough = min(MIN_CHARS, unreadable)
        for name, mine in chars.items():
            best = max((char_f1(mine, theirs) for other, theirs in chars.items() if other != name), default=0.0)
            reads[name] = sum(mine.values()) >= enough and best >= CHAR_AGREE
        voters = len(reads)
        readers_of_names = sorted(n for n, r in reads.items() if r)
        names = restating(path.read_bytes())
        check, note = {}, None
        glyphs = pdfkit_glyphs(command, path)
        if glyphs is None:
            note = "no pdfkit glyphs: the code check is skipped"
        elif glyphs[1] != text.get("text", ""):
            note = "the pdfkit binary reads the file otherwise than the run: the code check is skipped"
        elif name_reading(names) in ("hexadecimal", "decimal"):
            expected = expected_characters(glyphs[0])
            check = {name: code_check(surplus, expected) for name, surplus in sorted(chars.items())}
        else:
            note = f"names that do not restate their codes ({name_reading(names)}): the code check is skipped"
        verdict = ("undecided" if voters < judge.TEXT_QUORUM else
                   "read" if 2 * len(readers_of_names) > voters else
                   "unread" if 2 * len(readers_of_names) < voters else "split")
        files.append({"id": row["id"], "unreadable": unreadable, "voters": sorted(reads),
                      "read_names": readers_of_names, "surplus": {n: sum(c.values()) for n, c in chars.items()},
                      "verdict": verdict, "names": names, "code_check": check, "note": note})
    counts = collections.Counter(f["verdict"] for f in files)
    decided = counts["read"] + counts["unread"]
    names = collections.Counter()
    for f in files:
        names.update(f["names"])
    if decided == 0:
        outcome_text = "no majority: kept"
    elif 2 * counts["read"] > decided:
        outcome_text = "against the rule: CR5 ordered"
    elif 2 * counts["read"] < decided:
        outcome_text = "confirms the rule"
    else:
        outcome_text = "split: kept"
    read = [f for f in files if f["verdict"] == "read"]
    restated = {f["id"]: name_reading(f["names"]) for f in read}
    reading = ("the code itself" if read and all(r != "mixed" for r in restated.values()) else
               "mixed: see each file")
    return {"files": files, "counts": dict(counts), "names": dict(names), "reading": reading,
            "restated_by_file": restated,
            "outcome": outcome_text}


# ---------------------------------------------------------------------------
# CMYK through PDFium's Adobe table


def measure_cmyk(run_dir: Path, panel: evidence.Panel, census_exe: Path | None, workers: int) -> dict:
    import phase6_panel  # noqa: PLC0415

    wanted = collect.wanted_images(phase6_panel.census_tsv(run_dir))
    rows = {r["id"]: r for r in common.read_manifest()}
    ids = sorted(i for i, images in wanted.items() if any(x["reason"] == "cmyk" for x in images))

    def one(file_id: str) -> list[dict]:
        path = common.file_path(rows[file_id])
        requests = evidence.pixel_requests(panel, path, wanted[file_id])
        answers = evidence.pixel_answers(panel, path, requests) if requests else {}
        folder = phase6_panel.dump_folder(run_dir, file_id, path, census_exe)
        out = []
        for image in wanted[file_id]:
            if image["reason"] != "cmyk":
                continue
            mine = phase6_panel.pdfkit_pixels(folder, image)
            record = {"id": file_id, "object": image["object"], "colour_space": image["colour_space"],
                      "encoding": image["encoding"], "size": image["size"]}
            if mine is None:
                out.append({**record, "distance": {}, "pairs": {}, "why": "pdfkit decoded no pixels"})
                continue
            votes = {}
            for name in evidence.PIXEL_MEMBERS:
                answer = answers.get(name)
                if answer and answer.get("ok"):
                    votes[name] = panel.samples(name, (answer.get("images") or {}).get(str(image["object"])))
            distance = judge.colour_distance(mine, votes)
            pairs = {}
            names = sorted(n for n in distance)
            for i, a in enumerate(names):
                for b in names[i + 1:]:
                    pairs[f"{a} {b}"] = judge.colour_distance(votes[a], {b: votes[b]}).get(b)
            out.append({**record, "distance": distance, "pairs": pairs})
        return out

    with concurrent.futures.ThreadPoolExecutor(max(1, workers)) as pool:
        images = [r for batch in pool.map(one, ids) for r in batch]
    judged = [i for i in images if len(i["distance"]) >= 2]
    holding = [i for i in judged if 2 * sum(1 for d in i["distance"].values() if d <= COLOUR_LIMIT)
               > len(i["distance"])]
    within = collections.Counter()
    voted = collections.Counter()
    for image in judged:
        for name, value in image["distance"].items():
            voted[name] += 1
            within[name] += 1 if value <= COLOUR_LIMIT else 0
    pair_within = collections.Counter()
    pair_voted = collections.Counter()
    for image in judged:
        for pair, value in image["pairs"].items():
            if value is not None:
                pair_voted[pair] += 1
                pair_within[pair] += 1 if value <= COLOUR_LIMIT else 0

    def median(values):
        values = sorted(values)
        return values[len(values) // 2] if values else None

    summary = {
        "images": len(images), "judged": len(judged), "holding": len(holding),
        "outcome": "confirms the table" if judged and 2 * len(holding) > len(judged)
        else "against the table" if judged else "no majority: kept",
        "within_2_by_member": {n: f"{within[n]} of {voted[n]}" for n in sorted(voted)},
        "median_distance_by_member": {n: median([i["distance"][n] for i in judged if n in i["distance"]])
                                      for n in sorted(voted)},
        "within_2_by_pair": {p: f"{pair_within[p]} of {pair_voted[p]}" for p in sorted(pair_voted)},
        "median_distance_by_pair": {p: median([i["pairs"][p] for i in judged if i["pairs"].get(p) is not None])
                                    for p in sorted(pair_voted)},
        "by_colour_space": {},
        "images_list": images,
    }
    spaces = collections.defaultdict(lambda: [0, 0])
    for image in judged:
        key = image["colour_space"]
        spaces[key][0] += 1
        spaces[key][1] += 1 if image in holding else 0
    summary["by_colour_space"] = {k: f"{v[1]} of {v[0]} hold" for k, v in sorted(spaces.items())}
    return summary


# ---------------------------------------------------------------------------
# A page with no /MediaBox


def size_of(page: dict) -> tuple[float, float] | None:
    """A member's page size: its `size` (the box it lays the page out on), else its media box."""
    if page.get("size"):
        return (round(float(page["size"][0]), 2), round(float(page["size"][1]), 2))
    box = page.get("media")
    if box:
        return (round(float(box[2]) - float(box[0]), 2), round(float(box[3]) - float(box[1]), 2))
    return None


def pdfkit_media(command: list[str] | None, path: Path) -> list | None:
    if command is None:
        return None
    proc = subprocess.run(command + ["inspect", str(path)], capture_output=True, timeout=120)
    try:
        result = json.loads(proc.stdout)
    except ValueError:
        return None
    return [page.get("media_box") for page in result.get("pages") or []] if proc.returncode == 0 else None


def measure_media_box(run_dir: Path, panel: evidence.Panel, structure: evidence.Panel, command, rows) -> dict:
    del run_dir
    found = {}
    for name, path in specimens.write_all().items():
        sizes = {}
        for member in PAGE_MEMBERS:
            answer = panel.answer(member, path, SIZE_KIND.get(member, "pages"))
            if answer and answer.get("ok") and answer.get("pages"):
                sizes[member] = size_of(answer["pages"][0])
            else:
                sizes[member] = (answer or {}).get("error", "no answer")
        media = pdfkit_media(command, path)
        found[name] = {"members": sizes, "pdfkit_media_box": media[0] if media else None}
    missing = []
    for row in rows:
        answer = structure.answer("lopdf", common.file_path(row), "structure")
        pages = (answer or {}).get("mediabox_missing") or []
        if not pages:
            continue
        sizes = {}
        for member in PAGE_MEMBERS:
            reply = panel.answer(member, common.file_path(row), SIZE_KIND.get(member, "pages"))
            listed = (reply or {}).get("pages") or []
            sizes[member] = [size_of(listed[p - 1]) if 0 < p <= len(listed) else None for p in pages[:3]]
        media = pdfkit_media(command, common.file_path(row))
        missing.append({"id": row["id"], "pages": pages[:10], "members": sizes,
                        "pdfkit": [media[p - 1] if media and 0 < p <= len(media) else None for p in pages[:3]]})
    letter_votes = [m for m, s in found["no_media_box"]["members"].items() if isinstance(s, tuple)]
    letter = [m for m in letter_votes if found["no_media_box"]["members"][m] == LETTER]
    outcome_text = ("undecided: kept" if len(letter_votes) < judge.TEXT_QUORUM else
                    "confirms Letter" if 2 * len(letter) > len(letter_votes) else
                    "split: kept" if 2 * len(letter) == len(letter_votes) else "against Letter")
    return {"specimens": found, "dataset_pages_without_media_box": missing,
            "letter": letter, "voters": letter_votes, "outcome": outcome_text}


# ---------------------------------------------------------------------------
# Word cuts


def read_cuts(path: Path = CUTS) -> list[dict]:
    return common.read_tsv(path, CUT_COLUMNS) if path.exists() else []


def reading_at(text: str, pieces: list[str]) -> tuple[str | None, str]:
    """How a text reads the pieces of one place: `joined`, `apart` or None, with the snippet."""
    body = folded(text)
    parts = [folded(p) for p in pieces]
    pattern = re.compile(r"\s*".join(re.escape(p) for p in parts))
    hits = list(pattern.finditer(body))
    if hits:
        kinds = collections.Counter("joined" if not re.search(r"\s", h.group(0)) else "apart" for h in hits)
        kind = kinds.most_common(1)[0][0]
        hit = next(h for h in hits if ("joined" if not re.search(r"\s", h.group(0)) else "apart") == kind)
        return kind, body[max(0, hit.start() - 12):hit.end() + 12].replace("\n", "/").replace("\r", "")
    if all(p in body for p in parts):
        return "apart", "the pieces stand in different places"
    return None, "no reading"


def judge_cut(row: dict, pdfkit_text: str, member_texts: dict[str, str]) -> dict:
    pieces = row["pieces"].split("|")
    mine, _ = reading_at(pdfkit_text, pieces)
    joined, apart, silent, lines = [], [], [], []
    for name in sorted(member_texts):
        kind, snippet = reading_at(member_texts[name], pieces)
        (joined if kind == "joined" else apart if kind == "apart" else silent).append(name)
        lines.append(f"{name} {kind or 'none'} '{snippet}'")
    voters = len(joined) + len(apart)
    majority = ("joined" if 2 * len(joined) > voters else "apart" if 2 * len(apart) > voters else None)
    if voters == 0 or mine is None:
        verdict = "undecided"
    elif majority is None:
        verdict = "split"
    else:
        verdict = "pdfkit right" if majority == mine else "pdfkit wrong"
    return {**row, "pdfkit": mine or "none", "joined": ",".join(joined), "apart": ",".join(apart),
            "silent": ",".join(silent), "verdict": verdict, "evidence": "; ".join(lines)}


def measure_cuts(run_dir: Path, panel: evidence.Panel, write: bool) -> dict:
    rows = {r["id"]: r for r in common.read_manifest()}
    out = []
    texts_cache: dict[str, tuple[str, dict]] = {}
    for row in read_cuts():
        file_id = row["id"]
        if file_id not in texts_cache:
            result = outcome(run_dir, file_id) or {}
            path = common.file_path(rows[file_id])
            members = {}
            for name in evidence.TEXT_MEMBERS:
                answer = panel.answer(name, path, "text")
                if answer and answer.get("ok"):
                    members[name] = judge.member_text(answer)
            texts_cache[file_id] = ((result.get("text") or {}).get("text", ""), members)
        mine, members = texts_cache[file_id]
        out.append(judge_cut(row, mine, members))
    if write:
        lines = ["\t".join(CUT_COLUMNS)] + ["\t".join(r[c] for c in CUT_COLUMNS) for r in out]
        CUTS.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return {"places": out, "verdicts": dict(collections.Counter(r["verdict"] for r in out))}


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description="Re-decide reading choices by the reference panel.")
    parser.add_argument("measure", choices=("type3", "cmyk", "media_box", "cuts", "all"))
    parser.add_argument("--run", required=True, help="the dataset run id whose outcomes and census are read")
    parser.add_argument("--collect", action="store_true", help="ask a member for an answer the cache lacks")
    parser.add_argument("--write", action="store_true", help="cuts: rewrite tests/panel/word_cuts.tsv's verdicts")
    parser.add_argument("--pdfkit", help="the pdfkit executable (default: out/)")
    parser.add_argument("--census", help="the pdfkit_image_census executable (default: out/)")
    parser.add_argument("--workers", type=int, default=2)
    args = parser.parse_args(argv)
    run_dir = common.reports_dir() / args.run
    out_dir = run_dir / "redecide"
    out_dir.mkdir(parents=True, exist_ok=True)
    rows = [r for r in common.read_manifest() if common.file_path(r).exists()]
    import run as dataset_run  # noqa: PLC0415

    command = dataset_run.find_pdfkit(args.pdfkit)
    measures = ("type3", "cmyk", "media_box", "cuts") if args.measure == "all" else (args.measure,)
    for measure in measures:
        if measure == "type3":
            result = measure_type3(run_dir, evidence.Panel(evidence.TEXT_MEMBERS, args.collect), rows, command)
        elif measure == "cmyk":
            import image_agreement  # noqa: PLC0415

            panel = evidence.Panel(evidence.PIXEL_MEMBERS + ("qpdf",), args.collect)
            result = measure_cmyk(run_dir, panel, image_agreement.find_census(args.census), args.workers)
        elif measure == "media_box":
            # The specimens are new inputs, so their page sizes are always asked for.
            result = measure_media_box(run_dir, evidence.Panel(PAGE_MEMBERS, True),
                                       evidence.Panel(("lopdf",), args.collect), command, rows)
        else:
            result = measure_cuts(run_dir, evidence.Panel(evidence.TEXT_MEMBERS, args.collect), args.write)
        (out_dir / f"{measure}.json").write_text(json.dumps(result, indent=1, ensure_ascii=False) + "\n",
                                                 encoding="utf-8")
        brief = {k: v for k, v in result.items() if k not in ("files", "images_list", "places",
                                                               "dataset_pages_without_media_box")}
        print(f"== {measure}: " + json.dumps(brief, ensure_ascii=False, indent=1))
    return 0


if __name__ == "__main__":
    common.run_tool(main)
