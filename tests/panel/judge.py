# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# The reference panel's majority judge: pure functions over answers already in
# hand, so it starts no process and its rules are tested on answers written in
# code (test_judge.py). A file or an image counts against pdfkit only when
# pdfkit disagrees with the panel's majority, because one reader's quirks
# counted as truth make pdfkit's misses and the reference's indistinguishable
# (CLAUDE.md section 6, "Reference readers judge as a panel").
#
# Text. The voters are the members whose text answer is ok; any other answer
# (a refusal, a timeout, a crash twice, no answer cached) abstains. pdfkit's
# word F1 against each voter uses the reading gate's metric (tests/pdfium/
# words.py), and a file counts against pdfkit when that F1 is below 0.90
# against a strict majority of at least three voters; with fewer voters, or
# exactly half below, it is undecided and counted neither way. Three rules
# keep the comparison on one surface, because the members read differently on
# purpose where pdfkit makes a reading choice of its own:
# - a word holding a right-to-left letter (bidirectional class R or AL) reads
#   the same in either direction, because MuPDF, pdf_oxide and pdf-extract
#   write Hebrew and Arabic in visual order and pdfkit in logical order;
# - pdfkit may offer other readings beside its full text (its visible text
#   without the hidden blocks; its text with every duplicate copy), and a
#   member's score is the best of them, because whether off-page, clipped or
#   twice-drawn text belongs in a text is a choice the members split on and
#   pdfkit reads both ways (docs/DESIGN.md section 7);
# - a member whose text holds annotation and form-field appearances (Poppler,
#   MuPDF and pdf_oxide, measured on synth-0011) is scored on an annotated
#   file by the better of F1 and pdfkit's precision (the share of pdfkit's
#   words its text holds), because pdfkit leaves content outside the page
#   content streams out by design (CLAUDE.md section 2) and no member can be
#   told to leave annotations out.
#
# Pixels. The voters on an image are the members whose answer has the image's
# own width and height (with /Decode applied, as tests/panel/members.tsv
# records per member), less a member that abstains on the image's class (the
# `abstains` column). A bilevel or neutral image reads as gray; a stencil mask
# is compared by what it paints (255 where it paints), because pdfkit's census
# dump and MuPDF write that while PDFium's rendered bitmap is black where the
# mask paints (measured on synth-0263). The majority is the sample array held
# identically by more than half of at least two voters; pdfkit agrees when its
# samples equal it.

from __future__ import annotations

import collections
import hashlib
import itertools
import operator
import statistics
import sys
import unicodedata
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "pdfium"))

import words  # noqa: E402

# The reading gate's agreement threshold (tests/pdfium/text_agreement.py AGREE).
AGREE = 0.90
TEXT_QUORUM = 3
PIXEL_QUORUM = 2
# Members whose rendered stencil is dark where the mask paints; every other
# member writes 255 there (tests/panel/members.tsv `colour_output`).
STENCIL_PAINTS_DARK = frozenset({"pdfium"})


# ---------------------------------------------------------------------------
# Text


def rtl(word: str) -> bool:
    return any(unicodedata.bidirectional(c) in ("R", "AL") for c in word)


def bag(text: str | None) -> collections.Counter:
    """The words of a text (words.py), a right-to-left word keyed the same in either direction."""
    out: collections.Counter = collections.Counter()
    for word, count in words.words(text or "").items():
        out[min(word, word[::-1]) if rtl(word) else word] += count
    return out


def f1(mine: collections.Counter, theirs: collections.Counter) -> float:
    """words.f1 over two bags: 1.0 when both are empty, 0.0 when one is."""
    total_a, total_b = sum(mine.values()), sum(theirs.values())
    if total_a == 0 and total_b == 0:
        return 1.0
    common = sum((mine & theirs).values())
    if common == 0:
        return 0.0
    precision, recall = common / total_a, common / total_b
    return 2 * precision * recall / (precision + recall)


def precision(mine: collections.Counter, theirs: collections.Counter) -> float:
    """The share of pdfkit's words the member's text holds; 1.0 when pdfkit reads no word,
    because pdfkit then claims nothing the member does not hold."""
    total = sum(mine.values())
    return 1.0 if total == 0 else sum((mine & theirs).values()) / total


def member_text(answer: dict) -> str:
    return "\n".join(page.get("text") or "" for page in answer.get("pages") or [])


def abstention(answer: dict | None) -> str | None:
    """Why an answer casts no vote, or None for a vote."""
    if answer is None:
        return "no answer"
    if answer.get("ok"):
        return None
    return str(answer.get("error") or "refused")


def text_verdict(pdfkit_text: str, answers: dict, *, readings=(), one_sided=()) -> dict:
    """The panel's verdict on pdfkit's text of one file.

    `answers` maps a member to its text answer (or None when none is cached); `readings` are
    pdfkit's other texts of the file, `one_sided` the members scored by the better of F1 and
    precision. Returns the voters, each voter's score and the reading it came from (0 the full
    text), the voters below 0.90, the median score, the abstentions and the verdict: `agrees`,
    `counts_against` or `undecided` with `why`."""
    mine = [bag(pdfkit_text)] + [bag(text) for text in readings]
    voters, scores, reading, abstained = [], {}, {}, {}
    for member in sorted(answers):
        reason = abstention(answers[member])
        if reason is not None:
            abstained[member] = reason
            continue
        theirs = bag(member_text(answers[member]))
        best, which = max((f1(m, theirs), i) for i, m in reversed(list(enumerate(mine))))
        if member in one_sided:
            best = max(best, precision(mine[0], theirs))
        voters.append(member)
        scores[member] = round(best, 6)
        reading[member] = which
    below = [m for m in voters if scores[m] < AGREE]
    out = {"voters": voters, "scores": scores, "reading": reading, "below": below,
           "abstained": abstained,
           "median": round(statistics.median(scores.values()), 6) if scores else None}
    if len(voters) < TEXT_QUORUM:
        out["verdict"], out["why"] = "undecided", f"{len(voters)} voters, {TEXT_QUORUM} needed"
    elif 2 * len(below) > len(voters):
        out["verdict"] = "counts_against"
    elif 2 * len(below) == len(voters):
        out["verdict"], out["why"] = "undecided", f"no majority: {len(below)} of {len(voters)} voters below {AGREE:.2f}"
    else:
        out["verdict"] = "agrees"
    return out


def member_agreement(answers_by_file: dict) -> dict:
    """(member, member) -> {"files", "median"}: the median F1 between two text members over
    the files both read. It is reported, never used to drop a member."""
    pairs: dict[tuple[str, str], list[float]] = collections.defaultdict(list)
    for answers in answers_by_file.values():
        read = {m: bag(member_text(a)) for m, a in answers.items() if abstention(a) is None}
        for a, b in itertools.combinations(sorted(read), 2):
            pairs[(a, b)].append(f1(read[a], read[b]))
    return {pair: {"files": len(values), "median": round(statistics.median(values), 4)}
            for pair, values in sorted(pairs.items())}


# ---------------------------------------------------------------------------
# Pixels

CHANNELS = {"gray": 1, "rgb": 3, "rgba": 4}


def as_gray(image: dict) -> dict:
    """An RGB image whose every pixel is neutral, as gray; anything else as it is."""
    if image.get("layout") != "rgb":
        return image
    samples = image["samples"]
    red, green, blue = samples[0::3], samples[1::3], samples[2::3]
    if red == green == blue:
        return {**image, "layout": "gray", "samples": bytes(red)}
    return image


def painted(image: dict, member: str | None) -> dict:
    """A stencil mask as 255 where it paints and 0 elsewhere."""
    image = as_gray(image)
    samples = image["samples"]
    if image["layout"] != "gray":
        # Painted in a colour: a pixel paints where any channel is not white.
        step = CHANNELS[image["layout"]]
        white = bytes(255 if v == 255 else 0 for v in range(256))
        lowest = samples[0::step].translate(white)
        for channel in range(1, 3):
            lowest = bytes(map(min, lowest, samples[channel::step].translate(white)))
        samples = lowest
    if member in STENCIL_PAINTS_DARK:
        table = bytes(0 if v == 255 else 255 for v in range(256))
    else:
        table = bytes(255 if v == 255 else 0 for v in range(256))
    return {**image, "layout": "gray", "samples": samples.translate(table)}


def normalised(image: dict, member: str | None, stencil: bool) -> dict:
    return painted(image, member) if stencil else as_gray(image)


def digest(image: dict) -> str:
    return hashlib.sha256(image["layout"].encode("ascii") + b"\0" + image["samples"]).hexdigest()


def pixel_verdict(pdfkit_pixels: dict, answers: dict, *, stencil: bool = False, abstain=()) -> dict:
    """The panel's verdict on pdfkit's pixels of one image.

    `pdfkit_pixels` and each answer are {"width", "height", "layout", "samples"}; an answer
    with `error`, or None, abstains, as does a member at another size or in `abstain` (the
    members that abstain on the image's class). Returns the voters, the members of the
    majority, the abstentions and the verdict."""
    width, height = pdfkit_pixels["width"], pdfkit_pixels["height"]
    mine = digest(normalised(pdfkit_pixels, None, stencil))
    voters, groups, abstained = [], collections.defaultdict(list), {}
    for member in sorted(answers):
        answer = answers[member]
        if member in abstain:
            abstained[member] = "abstains on this class"
        elif answer is None or "samples" not in answer:
            abstained[member] = str((answer or {}).get("error") or "no answer")
        elif (answer["width"], answer["height"]) != (width, height):
            abstained[member] = f"size {answer['width']}x{answer['height']}"
        else:
            voters.append(member)
            groups[digest(normalised(answer, member, stencil))].append(member)
    out = {"voters": voters, "abstained": abstained, "majority": [], "groups": len(groups)}
    if len(voters) < PIXEL_QUORUM:
        noun = "voter" if len(voters) == 1 else "voters"
        out["verdict"], out["why"] = "undecided", f"{len(voters)} {noun}, {PIXEL_QUORUM} needed"
        return out
    best_digest, best = max(groups.items(), key=lambda item: (len(item[1]), item[1]))
    if 2 * len(best) <= len(voters):
        out["verdict"], out["why"] = "undecided", f"no majority among {len(voters)} voters"
        return out
    out["majority"] = best
    out["verdict"] = "agrees" if best_digest == mine else "counts_against"
    return out


def as_rgb(image: dict) -> bytes:
    samples = image["samples"]
    if image["layout"] == "gray":
        out = bytearray(len(samples) * 3)
        out[0::3] = out[1::3] = out[2::3] = samples
        return bytes(out)
    if image["layout"] == "rgba":
        out = bytearray(len(samples) // 4 * 3)
        out[0::3], out[1::3], out[2::3] = samples[0::4], samples[1::4], samples[2::4]
        return bytes(out)
    return samples


def mad(mine: bytes, theirs: bytes) -> float:
    """The largest, over the three channels, of the mean absolute difference per pixel."""
    pixels = len(mine) // 3
    if pixels == 0:
        return 0.0
    worst = 0
    for channel in range(3):
        total = sum(map(abs, map(operator.sub, mine[channel::3], theirs[channel::3])))
        worst = max(worst, total)
    return round(worst / pixels, 3)


def colour_distance(pdfkit_rgb: dict, answers: dict) -> dict:
    """member -> the mean absolute difference per channel (the worst channel) of pdfkit's RGB
    against the member's, for every member at the image's size; the measure of the CMYK
    conversion's re-decision, as the image agreement measures a continuous-tone image."""
    mine = as_rgb(pdfkit_rgb)
    out = {}
    for member in sorted(answers):
        answer = answers[member]
        if not answer or "samples" not in answer:
            continue
        if (answer["width"], answer["height"]) != (pdfkit_rgb["width"], pdfkit_rgb["height"]):
            continue
        out[member] = mad(mine, as_rgb(answer))
    return out
