# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# Tests of the reference panel's majority judge (judge.py) over answers written
# in code, so they run no member: a file counts against pdfkit only when its
# word F1 is below 0.90 against a strict majority of at least three voters; a
# member that refused, timed out or crashed abstains; a two-two tie is
# undecided; an image needs two voters at its own size whose samples agree;
# a stencil is compared by what it paints. The comparison's own rules are held
# too: a word in a right-to-left script reads the same in either direction for
# a member that writes visual order (MuPDF, pdf_oxide, pdf-extract) and only
# then, pdfkit's other readings (its visible text, every duplicate copy) count
# when they agree better, and an empty text of pdfkit's scores 0.
# Run: python -m unittest discover -s tests/panel -p "test_*.py".

from __future__ import annotations

import sys
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import judge  # noqa: E402

TEN = "alpha bravo charlie delta echo foxtrot golf hotel india juliet"
NINE_OF_TEN = "alpha bravo charlie delta echo foxtrot golf hotel india kilo"  # F1 exactly 0.90
FIVE_OF_TEN = "alpha bravo charlie delta echo lima mike november oscar papa"  # F1 0.50


def text(value: str) -> dict:
    return {"ok": True, "pages": [{"text": value}]}


REFUSED = {"ok": False, "error": "refused", "detail": "cannot read"}
TIMEOUT = {"ok": False, "error": "timeout", "detail": "no answer within 30 s"}
REPEATED = {"ok": False, "error": "repeated_failure", "repeated": "crash"}


class TextVerdictTest(unittest.TestCase):
    def test_three_of_five_below_count_against(self):
        verdict = judge.text_verdict(TEN, {"a": text(TEN), "b": text(TEN), "c": text(FIVE_OF_TEN),
                                           "d": text(FIVE_OF_TEN), "e": text(FIVE_OF_TEN)})
        self.assertEqual(verdict["verdict"], "counts_against")
        self.assertEqual(verdict["voters"], ["a", "b", "c", "d", "e"])
        self.assertEqual(verdict["below"], ["c", "d", "e"])
        self.assertEqual(verdict["median"], 0.5)

    def test_two_of_five_below_agree(self):
        verdict = judge.text_verdict(TEN, {"a": text(TEN), "b": text(TEN), "c": text(TEN),
                                           "d": text(FIVE_OF_TEN), "e": text(FIVE_OF_TEN)})
        self.assertEqual(verdict["verdict"], "agrees")
        self.assertEqual(verdict["median"], 1.0)

    def test_a_tie_of_two_and_two_with_one_abstention_is_undecided(self):
        verdict = judge.text_verdict(TEN, {"a": text(TEN), "b": text(TEN), "c": text(FIVE_OF_TEN),
                                           "d": text(FIVE_OF_TEN), "e": REFUSED})
        self.assertEqual(verdict["verdict"], "undecided")
        self.assertEqual(verdict["why"], "no majority: 2 of 4 voters below 0.90")
        self.assertEqual(verdict["abstained"], {"e": "refused"})

    def test_a_timeout_abstains_and_two_voters_are_too_few(self):
        verdict = judge.text_verdict(TEN, {"a": text(FIVE_OF_TEN), "b": text(FIVE_OF_TEN), "c": TIMEOUT,
                                           "d": REPEATED, "e": None})
        self.assertEqual(verdict["verdict"], "undecided")
        self.assertEqual(verdict["voters"], ["a", "b"])
        self.assertEqual(verdict["abstained"], {"c": "timeout", "d": "repeated_failure", "e": "no answer"})
        self.assertEqual(verdict["why"], "2 voters, 3 needed")

    def test_exactly_090_is_agreement(self):
        self.assertAlmostEqual(judge.f1(judge.bag(TEN), judge.bag(NINE_OF_TEN)), 0.9)
        verdict = judge.text_verdict(TEN, {m: text(NINE_OF_TEN) for m in "abc"})
        self.assertEqual(verdict["verdict"], "agrees")
        self.assertEqual(verdict["below"], [])

    def test_just_below_090_counts(self):
        pdfkit = TEN + " mike"
        self.assertLess(judge.f1(judge.bag(pdfkit), judge.bag(NINE_OF_TEN)), 0.9)
        self.assertEqual(judge.text_verdict(pdfkit, {m: text(NINE_OF_TEN) for m in "abc"})["verdict"],
                         "counts_against")

    def test_pages_are_joined(self):
        answer = {"ok": True, "pages": [{"text": "alpha bravo"}, {"text": "charlie delta"}, {"text": None}]}
        verdict = judge.text_verdict("alpha bravo charlie delta", {"a": answer, "b": answer, "c": answer})
        self.assertEqual(verdict["scores"], {"a": 1.0, "b": 1.0, "c": 1.0})

    def test_a_right_to_left_word_reads_either_way_for_a_member_writing_visual_order(self):
        logical = "שלום עולם"
        visual = "םולש םלוע"
        self.assertEqual(judge.bag(logical, fold=True), judge.bag(visual, fold=True))
        self.assertNotEqual(judge.bag(logical), judge.bag(visual))
        self.assertNotEqual(judge.bag("abc", fold=True), judge.bag("cba", fold=True))
        verdict = judge.text_verdict(logical, {m: text(visual) for m in ("mupdf", "oxide", "extract")})
        self.assertEqual(verdict["verdict"], "agrees")
        # PDFium and Poppler write logical order, so a reversed word from them is another word.
        verdict = judge.text_verdict(logical, {m: text(visual) for m in ("pdfium", "poppler", "mupdf")})
        self.assertEqual(verdict["verdict"], "counts_against")
        self.assertEqual(verdict["below"], ["pdfium", "poppler"])

    def test_another_reading_of_pdfkit_counts_when_it_agrees_better(self):
        # pdfkit reads the off-page words too; the members leave them out.
        full = TEN + " lima mike november oscar papa"
        verdict = judge.text_verdict(full, {m: text(TEN) for m in "abc"})
        self.assertEqual(verdict["verdict"], "counts_against")
        verdict = judge.text_verdict(full, {m: text(TEN) for m in "abc"}, readings=[TEN])
        self.assertEqual(verdict["verdict"], "agrees")
        self.assertEqual(verdict["reading"], {"a": 1, "b": 1, "c": 1})

    def test_an_empty_pdfkit_text_scores_zero_against_a_member_with_words(self):
        verdict = judge.text_verdict("", {m: text(TEN) for m in "abc"})
        self.assertEqual(verdict["verdict"], "counts_against")
        self.assertEqual(verdict["scores"], {"a": 0.0, "b": 0.0, "c": 0.0})
        self.assertFalse(hasattr(judge, "precision"))


def gray(width: int, height: int, values) -> dict:
    return {"width": width, "height": height, "layout": "gray", "samples": bytes(values)}


class PixelVerdictTest(unittest.TestCase):
    PDFKIT = gray(2, 2, [0, 255, 255, 0])
    OTHER = gray(2, 2, [255, 255, 255, 0])

    def test_two_of_three_agree_with_pdfkit(self):
        verdict = judge.pixel_verdict(self.PDFKIT, {"a": self.PDFKIT, "b": self.PDFKIT, "c": self.OTHER})
        self.assertEqual(verdict["verdict"], "agrees")
        self.assertEqual(verdict["majority"], ["a", "b"])

    def test_two_of_three_against_pdfkit(self):
        verdict = judge.pixel_verdict(self.PDFKIT, {"a": self.OTHER, "b": self.OTHER, "c": self.PDFKIT})
        self.assertEqual(verdict["verdict"], "counts_against")

    def test_a_member_at_the_wrong_size_abstains(self):
        wrong = gray(4, 1, [0, 255, 255, 0])
        verdict = judge.pixel_verdict(self.PDFKIT, {"a": self.PDFKIT, "b": wrong, "c": {"error": "not_found"}})
        self.assertEqual(verdict["verdict"], "undecided")
        self.assertEqual(verdict["abstained"], {"b": "size 4x1", "c": "not_found"})
        self.assertEqual(verdict["why"], "1 voter, 2 needed")

    def test_two_voters_that_differ_have_no_majority(self):
        verdict = judge.pixel_verdict(self.PDFKIT, {"a": self.PDFKIT, "b": self.OTHER})
        self.assertEqual(verdict["verdict"], "undecided")
        self.assertEqual(verdict["why"], "no majority among 2 voters")

    def test_neutral_rgb_reads_as_gray(self):
        rgb = {"width": 2, "height": 2, "layout": "rgb", "samples": bytes([0, 0, 0, 255, 255, 255] * 2)}
        pdfkit = gray(2, 2, [0, 255, 0, 255])
        self.assertEqual(judge.pixel_verdict(pdfkit, {"a": rgb, "b": rgb})["verdict"], "agrees")

    def test_a_stencil_is_compared_by_what_it_paints(self):
        # pdfkit and MuPDF write 255 where the mask paints; PDFium's rendered bitmap is black there.
        pdfkit = gray(2, 1, [255, 0])
        verdict = judge.pixel_verdict(pdfkit, {"pdfium": gray(2, 1, [0, 255]), "mupdf": gray(2, 1, [255, 0])},
                                      stencil=True)
        self.assertEqual(verdict["verdict"], "agrees")
        # A fill colour other than black still paints.
        verdict = judge.pixel_verdict(pdfkit, {"pdfium": gray(2, 1, [96, 255]), "mupdf": gray(2, 1, [255, 0])},
                                      stencil=True)
        self.assertEqual(verdict["verdict"], "agrees")

    def test_a_stencil_painted_in_a_colour_still_paints(self):
        pdfkit = gray(2, 1, [255, 0])
        red = {"width": 2, "height": 1, "layout": "rgb", "samples": bytes([200, 0, 0, 255, 255, 255])}
        verdict = judge.pixel_verdict(pdfkit, {"pdfium": red, "mupdf": gray(2, 1, [255, 0])}, stencil=True)
        self.assertEqual(verdict["verdict"], "agrees")

    def test_a_member_abstaining_on_a_class_is_left_out(self):
        verdict = judge.pixel_verdict(self.PDFKIT, {"a": self.OTHER, "b": self.PDFKIT, "c": self.PDFKIT},
                                      abstain={"a"})
        self.assertEqual(verdict["voters"], ["b", "c"])
        self.assertEqual(verdict["abstained"], {"a": "abstains on this class"})


class ColourDistanceTest(unittest.TestCase):
    def test_mean_absolute_difference_of_the_worst_channel(self):
        pdfkit = {"width": 2, "height": 1, "layout": "rgb", "samples": bytes([10, 20, 30, 10, 20, 30])}
        member = {"width": 2, "height": 1, "layout": "rgb", "samples": bytes([12, 20, 30, 10, 26, 30])}
        distances = judge.colour_distance(pdfkit, {"a": member, "b": pdfkit, "c": {"error": "too_large"},
                                                   "d": gray(2, 1, [20, 20])})
        self.assertEqual(distances, {"a": 3.0, "b": 0.0, "d": 10.0})


class MemberAgreementTest(unittest.TestCase):
    def test_median_f1_of_each_pair_over_files_both_read(self):
        files = {"f1": {"a": text(TEN), "b": text(TEN), "c": text(FIVE_OF_TEN)},
                 "f2": {"a": text(TEN), "b": text(NINE_OF_TEN), "c": REFUSED}}
        table = judge.member_agreement(files)
        self.assertEqual(table[("a", "b")], {"files": 2, "median": 0.95})
        self.assertEqual(table[("a", "c")], {"files": 1, "median": 0.5})
        self.assertEqual(table[("b", "c")], {"files": 1, "median": 0.5})


if __name__ == "__main__":
    unittest.main()
