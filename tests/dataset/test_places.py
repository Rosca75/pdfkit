# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# Tests of what the gates count as an occurrence and as the input's text: the
# copies of a word drawn at one place are one occurrence, and an occurrence in
# a switched-off optional-content layer, which every write removes, is none
# (places.py); the located rows of the text redaction gate, the feedback row
# of the find gate and the text-unchanged rows of the writer and image gates
# judge by those rules.

from __future__ import annotations

import collections
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE / "gates"))
sys.path.insert(0, str(HERE.parents[1] / "dataset" / "tools"))
sys.path.insert(0, str(HERE.parents[1] / "tests" / "pdfium"))

import phase7  # noqa: E402
import phase8  # noqa: E402
import phase9  # noqa: E402
import phase12  # noqa: E402
import places  # noqa: E402

OFF = "optional_content_off"


def word(box, hidden=()):
    return {"page": 1, "box": list(box), "hidden": list(hidden)}


class PlaceCountTest(unittest.TestCase):
    def test_copies_at_one_place_are_one_occurrence(self):
        found = [word([10, 10, 40, 20]), word([10.2, 10.1, 40.3, 20], ["duplicate"]),
                 word([10, 50, 40, 60])]
        self.assertEqual(places.place_count(found), 2)

    def test_a_copy_at_size_zero_inside_the_word_is_the_same_place(self):
        self.assertEqual(places.place_count([word([10, 10, 40, 20]), word([20, 15, 20, 15])]), 1)

    def test_one_place_on_two_pages_is_two_occurrences(self):
        footer = [word([10, 10, 40, 20]), dict(word([10, 10, 40, 20]), page=2)]
        self.assertEqual(places.place_count(footer), 2)
        self.assertEqual(places.extra_copies(footer, [10, 10, 40, 20], 1), 0)

    def test_an_occurrence_in_a_switched_off_layer_is_none(self):
        self.assertEqual(places.place_count([word([10, 10, 40, 20]), word([10, 50, 40, 60], [OFF])]), 1)

    def test_find_occurrences_are_placed_by_their_first_rectangle(self):
        found = [{"rectangles": [[10, 10, 40, 20]], "hidden": []},
                 {"rectangles": [[11, 10, 41, 20]], "hidden": ["duplicate"]}]
        self.assertEqual(places.place_count(found), 1)

    def test_the_text_without_its_switched_off_layers(self):
        self.assertEqual(places.f1_without_off("one two secret layer", "one two", "secret layer"), 1.0)
        self.assertLess(places.f1_without_off("one two three", "one two", ""), 1.0)

    def test_an_off_layer_word_joined_to_a_drawn_one_is_cut_from_it(self):
        # PDFium reads an off-layer "x" and the drawn word after it as one word.
        self.assertEqual(places.f1_without_off("ALPHA25 xMARK90 shown", "MARK90 shown", "ALPHA25 x"), 1.0)


def run_folder(tmp: str, outcomes: dict, extra: dict | None = None) -> Path:
    run_dir = Path(tmp) / "run"
    (run_dir / "outcomes").mkdir(parents=True)
    (run_dir / "summary.json").write_text(json.dumps(
        {"run": "run", "runs": 2, "nondeterministic": [], "crashes": [], "hangs": []}), encoding="utf-8")
    for row_id, outcome in outcomes.items():
        (run_dir / "outcomes" / f"{row_id}.json").write_text(json.dumps(outcome), encoding="utf-8")
    for name, value in (extra or {}).items():
        (run_dir / name).write_text(json.dumps(value), encoding="utf-8")
    return run_dir


def located(before, after, places_before, places_after, form="glyphs", same=None):
    return {"status": "ok", "code": None, "word": "MARK", "page": 1, "glyphs": [0, 4],
            "box": [10, 10, 40, 20], "form": form, "repeated": False, "before": before,
            "places_before": places_before, "after": after, "places_after": places_after,
            "same_glyphs": same, "self_check": "passed", "output": "outputs/x.located.pdf"}


class LocatedRowsTest(unittest.TestCase):
    rows = [{"id": "synth-0905", "expect_open": "opens", "expect_redact_text": "clean"},
            {"id": "synth-0097", "expect_open": "opens", "expect_redact_text": "clean"}]

    def judge(self, leak=None):
        outcomes = {
            # The second occurrence lies in a layer every write removes.
            "synth-0905": {"redact_text": {"status": "ok", "self_check": "passed", "located": located(
                2, 0, [word([10, 10, 40, 20]), word([10, 50, 40, 60], [OFF])], [], "occurrence", True)}},
            # The word is drawn twice at one place, and once elsewhere.
            "synth-0097": {"redact_text": {"status": "ok", "self_check": "passed", "located": located(
                2, 0, [word([10, 10, 40, 20]), word([10.5, 10, 40, 20]), word([10, 80, 40, 90])],
                [word([10, 80, 40, 90])])}}}
        with tempfile.TemporaryDirectory() as tmp:
            return phase8.judge(run_folder(tmp, outcomes, {"pdfium-leak.json": leak} if leak else None),
                                self.rows)

    def row(self, report, start):
        return next(r for r in report["rows"] if r["measure"].startswith(start))

    def test_one_place_fewer_counts_places_and_leaves_off_layers_out(self):
        row = self.row(self.judge(), "located outputs holding exactly one occurrence fewer of the word (pdfkit")
        self.assertTrue(row["pass"], row)

    def test_pdfium_may_lose_the_copies_at_the_place_and_the_off_layer(self):
        leak = {"leaks": {}, "located": {"files": {
            "synth-0905": {"word": "MARK", "opens": True, "pdfium_before": 2, "pdfium_after": 0},
            "synth-0097": {"word": "MARK", "opens": True, "pdfium_before": 3, "pdfium_after": 1}}}}
        row = self.row(self.judge(leak), "located outputs holding exactly one occurrence fewer of the word (PDFium")
        self.assertTrue(row["pass"], row)

    def test_a_named_pdfium_reading_is_counted_apart(self):
        leak = {"leaks": {}, "located": {"files": {
            "synth-0905": {"word": "MARK", "opens": True, "pdfium_before": 2, "pdfium_after": 0},
            "synth-0097": {"word": "MARK", "opens": True, "pdfium_before": 21, "pdfium_after": 11}}}}
        with mock.patch.dict(phase8.PDFIUM_READINGS, {"synth-0097": "pdfium: a form cycle"}, clear=True):
            row = self.row(self.judge(leak), "located outputs holding exactly one occurrence fewer of the word (PDFium")
        self.assertTrue(row["pass"], row)
        self.assertEqual(row["detail"]["pdfium_readings"], {"synth-0097": "pdfium: a form cycle"})

    def test_a_word_pdfium_does_not_read_at_the_place_is_not_judged(self):
        leak = {"leaks": {}, "located": {"files": {
            "synth-0905": {"word": "MARK", "opens": True, "pdfium_before": 2, "pdfium_after": 1},
            "synth-0097": {"word": "MARK", "opens": True, "pdfium_before": 2, "pdfium_after": 2,
                           "read_at_location": False}}}}
        row = self.row(self.judge(leak), "located outputs holding exactly one occurrence fewer of the word (PDFium")
        self.assertTrue(row["pass"], row)
        self.assertIn("synth-0097", row["detail"]["not_read_by_pdfium"])

    def test_occurrence_form_resolving_to_the_same_glyphs(self):
        row = self.row(self.judge(), "occurrence-form matches resolving to the glyphs")
        self.assertEqual(row["shown"].split(" ")[0:3], ["1", "/", "1"])
        self.assertTrue(row["pass"])


class FeedbackRowTest(unittest.TestCase):
    def test_a_place_with_copies_fed_back_is_one_occurrence_fewer(self):
        copies = [{"rectangles": [[10, 10, 40, 20]], "hidden": []},
                  {"rectangles": [[10, 10, 40, 20]], "hidden": ["duplicate"]}]
        outcome = {"find": {"status": "ok", "agree": True, "feedback": {
            "status": "ok", "tried": 1, "exact": 0,
            "failures": [{"value": "MARK", "page": 1, "why": "the page held 2 occurrences before and 0 after"}],
            "items": [{"value": "MARK", "page": 1, "glyphs": [], "before": copies, "after": []}]}}}
        rows = [{"id": "synth-0251", "expect_find": "agree"}]
        with tempfile.TemporaryDirectory() as tmp:
            report = phase12.judge(run_folder(tmp, {"synth-0251": outcome}), rows)
        row = next(r for r in report["rows"] if r["measure"].startswith("occurrences fed back"))
        self.assertTrue(row["pass"], row)


    def test_an_occurrence_fed_back_from_a_switched_off_layer_removes_no_place(self):
        off = [{"rectangles": [[10, 10, 40, 20]], "hidden": [OFF]}]
        outcome = {"find": {"status": "ok", "agree": True, "feedback": {
            "status": "ok", "tried": 1, "exact": 0, "failures": [],
            "items": [{"value": "ALPHA", "page": 1, "glyphs": [], "hidden": [OFF], "before": off, "after": []}]}}}
        rows = [{"id": "synth-0145", "expect_find": "agree"}]
        with tempfile.TemporaryDirectory() as tmp:
            report = phase12.judge(run_folder(tmp, {"synth-0145": outcome}), rows)
        row = next(r for r in report["rows"] if r["measure"].startswith("occurrences fed back"))
        self.assertTrue(row["pass"], row)


class TextUnchangedTest(unittest.TestCase):
    def test_the_writer_gate_reads_the_input_without_its_off_layers(self):
        opened = [{"id": "synth-0904"}, {"id": "synth-0001"}]
        entries = {"synth-0904": {"f1": 0.8}, "synth-0001": {"f1": 1.0}}
        with mock.patch.object(phase7, "off_layer_f1", return_value=1.0) as recompute:
            perfect, adjusted = phase7.text_perfect(Path("run"), opened, entries, {})
        recompute.assert_called_once()
        self.assertEqual(sorted(perfect), ["synth-0001", "synth-0904"])
        self.assertEqual(adjusted, ["synth-0904"])

    def test_the_image_gate_reads_the_input_without_its_off_layers(self):
        self.assertEqual(phase9.score("one two layer", "one two", lambda: "layer"), 1.0)
        self.assertEqual(phase9.score("one two", "one two", lambda: self.fail("not needed")), 1.0)


if __name__ == "__main__":
    unittest.main()
