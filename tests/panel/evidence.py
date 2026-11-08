# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# What the panel's judge weighs, gathered for the gate scripts and the
# re-decisions: the members' answers read back from the answers cache without
# starting a process (a missing answer abstains, unless the caller asks the
# member through answers.cached_answer), a copy of a file without its
# annotations and its form written by QPDF (readers/qpdf.py `copy`) into
# build/cache/panel/stripped/ of the main checkout, keyed by the input's
# SHA-256 and QPDF's digest, so the members' answers about it are cached like
# any answer; pdfkit's other readings of a file (its visible text without the
# hidden blocks, and its text with every duplicate copy) from `pdfkit text`,
# with the full text that binary reads, which the reading gate holds equal to
# the run's outcome so the readings are the run's own; pdfkit's decoded pixels from the image
# census's dumps (tests/dataset/image_census.cpp, "<identity>.pix": a line
# "width height channels" then 8-bit samples), and the request each image
# answer was cached under, rebuilt from the census as the collection builds
# it (collect.py), so the judge asks the cache the collection's question.

from __future__ import annotations

import json
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
for folder in (HERE, REPO / "tests" / "pdfium", REPO / "dataset" / "tools"):
    if str(folder) not in sys.path:
        sys.path.insert(0, str(folder))

import answers  # noqa: E402
import collect  # noqa: E402
import readers  # noqa: E402
import results_cache  # noqa: E402

TEXT_MEMBERS = ("pdfium", "poppler", "mupdf", "oxide", "extract")
PIXEL_MEMBERS = ("pdfium", "poppler", "mupdf")
# The hidden classes whose text pdfkit's visible reading leaves out; a duplicate copy is
# already read once in the full text, and its own reading adds every copy.
HIDDEN = ("render_invisible", "white_fill", "clipped", "off_media", "off_crop",
          "optional_content_off", "covered")
LAYOUT = {1: "gray", 3: "rgb", 4: "rgba"}
# Raised whenever the stripped copy QPDF writes changes for the same input and QPDF.
COPY_SCHEMA = 1
# The request QPDF's annotation survey is asked and cached under; its rule raises it (the
# appearance's form XObjects are followed since rule 2).
ANNOTATIONS_REQUEST = {"rule": 2}


class Panel:
    """The present members of a role, each with the folder its answers are cached in."""

    def __init__(self, names, collect_missing: bool = False) -> None:
        self.members, self.absent, self.folders = {}, {}, {}
        self.collect_missing = collect_missing
        for name in names:
            module = readers.load(name)
            problem = module.missing()
            if problem:
                self.absent[name] = problem
                continue
            self.members[name] = module
            self.folders[name] = answers.member_folder(module.NAME, module.version(), module.binary_digest())

    def answer(self, name: str, path: Path, kind: str, timeout: float = 30.0, **request) -> dict | None:
        """The member's cached answer, or None when none is cached and the caller collects nothing."""
        module = self.members.get(name)
        if module is None:
            return None
        if self.collect_missing:
            return answers.cached_answer(module, path, kind, timeout=timeout, **request)
        if kind in getattr(module, "CACHED_ELSEWHERE", ()):
            record = results_cache.record_path(results_cache.file_digest(path), "probe", {"ops": [kind]})
            probe = results_cache.read_json(record)
            return None if probe is None else module.text_shape(probe)
        record = answers.record_path(self.folders[name], results_cache.file_digest(path), kind, request)
        value = answers.read_json(record)
        return value if isinstance(value, dict) else None

    def stripped_copy(self, path: Path, timeout: float = 120.0) -> tuple[Path | None, str | None]:
        """The file without its annotations and its form, written by QPDF once and kept: (its
        path, None), or (None, why it could not be written)."""
        module = self.members.get("qpdf")
        if module is None:
            return None, "QPDF is absent"
        digest = results_cache.file_digest(path)
        target = (readers.cache_dir() / "stripped" / f"qpdf-{self.folders['qpdf'].rsplit('-', 1)[-1]}-{COPY_SCHEMA}"
                  / digest[:2] / f"{digest}.pdf")
        if target.exists():
            return target, None
        problem = module.copy(path, target, timeout)
        if problem is not None:
            return None, str(problem.get("error") or "refused")
        return target, None

    def samples(self, name: str, entry: dict | None) -> dict | None:
        """One image of a pixel answer with its samples read from the blob, or the entry as it is
        (an error) when it holds none."""
        if not entry or "blob" not in entry:
            return entry
        data = answers.read_blob(self.folders[name], "image_pixels", entry["blob"])
        if data is None:
            return {"error": "blob_missing"}
        return {"width": entry["width"], "height": entry["height"], "layout": entry["layout"], "samples": data}


def hidden_count(text: dict) -> int:
    return sum((text.get("hidden_classes") or {}).get(k, 0) for k in HIDDEN)


def binary_digest(command: list[str]) -> str | None:
    """The SHA-256 of the pdfkit executable a command runs."""
    found = shutil.which(command[0]) or command[0]
    return readers.sha256_of(Path(found)) if Path(found).is_file() else None


def other_readings(command: list[str], path: Path, timeout: float = 120.0) -> dict | None:
    """{"full": the text this binary reads (pages joined by form feeds, as the dataset outcome
    holds it), "readings": [the visible text, the text with every duplicate copy]} from `pdfkit
    text`; None when the command fails. The words of a line are joined by spaces, which the word
    metric reads alike."""
    try:
        proc = subprocess.run(command + ["text", str(path)], capture_output=True, timeout=timeout)
        result = json.loads(proc.stdout)
    except (subprocess.TimeoutExpired, OSError, ValueError):
        return None
    if proc.returncode != 0 or "pages" not in result:
        return None
    visible, every = [], []
    for page in result["pages"]:
        for key in ("blocks", "hidden_blocks"):
            for block in page.get(key) or []:
                for line in block.get("lines") or []:
                    for word in line.get("words") or []:
                        hidden = word.get("hidden") or []
                        if key == "blocks" and not hidden:
                            visible.append(word["text"])
                        if "duplicate" in hidden:
                            every.append(word["text"])
        visible.append("\n")
    return {"full": page_text(result),
            "readings": [" ".join(visible), page_text(result) + " " + " ".join(every)]}


def page_text(result: dict) -> str:
    return "\f".join(page.get("text") or "" for page in result["pages"])


def read_dump(path: Path) -> dict | None:
    """A census dump as {"width", "height", "layout", "samples"}, or None when unreadable."""
    try:
        data = path.read_bytes()
        end = data.index(b"\n")
        width, height, channels = (int(v) for v in data[:end].split())
    except (OSError, ValueError):
        return None
    samples = data[end + 1:]
    if channels not in LAYOUT or len(samples) != width * height * channels:
        return None
    return {"width": width, "height": height, "layout": LAYOUT[channels], "samples": samples}


def pixel_requests(panel: Panel, path: Path, wanted: list[dict]) -> dict | None:
    """The requests the collection asked a file's pixels with: {"request", "pdfium"}, or None when
    QPDF's raw streams are not cached (the images cannot be named)."""
    raw =panel.answer("qpdf", path, "raw_streams", objects=[i["object"] for i in wanted])
    if not raw or not raw.get("ok"):
        return None
    request = {"images": collect.pixel_request(wanted, raw.get("streams") or {})}
    ordered = dict(request)
    listing = panel.answer("poppler", path, "image_list", pages=sorted({p for i in wanted for p in i["pages"]}))
    if listing and listing.get("ok"):
        ordered["order"] = listing["order"]
    return {"request": request, "pdfium": ordered}


def pixel_answers(panel: Panel, path: Path, requests: dict) -> dict[str, dict | None]:
    """member -> its image_pixels answer for the file, asked with the collection's request."""
    out = {}
    for name in PIXEL_MEMBERS:
        request = requests["pdfium"] if name == "pdfium" else requests["request"]
        out[name] = panel.answer(name, path, "image_pixels", **request)
    return out
