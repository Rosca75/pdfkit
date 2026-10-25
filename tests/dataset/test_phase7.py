# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# Tests of the writer gate: the classification of planted markers (stripped
# channels against drawn content), the surviving, lost and leaked sets of an
# identity write and of a page selection, and the judging of a run folder written
# by hand, so that a clean run passes and every kind of fault fails its own row.

from __future__ import annotations

import json
import sys
import tempfile
import unittest
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE / "gates"))
sys.path.insert(0, str(HERE.parents[1] / "dataset" / "tools"))

import phase7  # noqa: E402


def mk(row: int, n: int = 1) -> str:
    return f"PDFKITMARK{row:02d}X{n:02d}"


class ClassifyTest(unittest.TestCase):
    def test_rows_split_into_stripped_drawn_and_other(self):
        out = phase7.classify({mk(1), mk(23), mk(90, 2), mk(93, 4), mk(50)})
        self.assertEqual(out["stripped"], [mk(1), mk(23)])
        self.assertEqual(out["drawn"], [mk(90, 2), mk(93, 4)])
        self.assertEqual(out["other"], [mk(50)])

    def test_parse(self):
        self.assertEqual(phase7.parse_marker(mk(91, 3)), (91, 3))
        with self.assertRaises(ValueError):
            phase7.parse_marker("PDFKITMARK9X1")

    def test_markers_in_bytes_and_in_a_decoded_stream(self):
        data = b"head " + mk(2).encode() + b" tail"
        self.assertEqual(phase7.markers_in([data, zlib.decompress(zlib.compress(mk(90, 3).encode()))]),
                         {mk(2), mk(90, 3)})

    def test_surviving_reports_only_stripped_channels(self):
        self.assertEqual(phase7.surviving({mk(9), mk(90, 1), mk(13, 2)}), [mk(9), mk(13, 2)])

    def test_missing_drawn_is_what_the_input_drew_and_the_output_lost(self):
        before = {mk(90, 1), mk(91, 1), mk(3)}
        self.assertEqual(phase7.missing_drawn(before, {mk(90, 1)}), [mk(91, 1)])
        self.assertEqual(phase7.missing_drawn(before, before), [])


class SelectionTest(unittest.TestCase):
    source = {mk(row, n) for row in (1, 9, 90, 91, 92, 93) for n in (1, 2, 3, 4)}

    def drawn(self, pages):
        return {mk(row, n) for row in (90, 91, 92, 93) for n in pages}

    def test_a_clean_selection_has_no_violation(self):
        result = phase7.selection_violations(self.source, self.drawn([3, 1]), [3, 1])
        self.assertEqual(result, {"leaked": [], "lost": []})

    def test_a_removed_pages_marker_is_a_leak(self):
        result = phase7.selection_violations(self.source, self.drawn([3, 1]) | {mk(91, 2)}, [3, 1])
        self.assertEqual(result["leaked"], [mk(91, 2)])

    def test_a_stripped_channel_marker_is_a_leak_even_for_a_kept_page(self):
        result = phase7.selection_violations(self.source, self.drawn([1]) | {mk(9, 1)}, [1])
        self.assertEqual(result["leaked"], [mk(9, 1)])

    def test_a_kept_pages_lost_marker_is_reported(self):
        output = self.drawn([1]) - {mk(92, 1)}
        result = phase7.selection_violations(self.source, output, [1])
        self.assertEqual(result["lost"], [mk(92, 1)])
        self.assertEqual(result["leaked"], [])


def row(row_id: str, pages: str = "1", set_name: str = "synth", expect_open: str = "opens") -> dict:
    return {"id": row_id, "set": set_name, "pages": pages, "expect_open": expect_open,
            "expect_write": "identical", "expect_select": "n/a"}


def write_outcome(pages=1, reopen_pages=None, output="outputs/{id}.pdf") -> dict:
    return {"status": "ok", "code": None, "output": output, "pages": pages,
            "reopen": {"status": "ok", "code": None, "pages": pages if reopen_pages is None else reopen_pages,
                       "repairs": []}}


class JudgeTest(unittest.TestCase):
    def folder(self, tmp: str, ids, summary=None, pixel=True, text=True, multi=()) -> Path:
        """A run folder of clean outcomes; the ids in `multi` have two pages and a selection of both."""
        run_dir = Path(tmp) / "run"
        (run_dir / "outcomes").mkdir(parents=True)
        (run_dir / "outputs").mkdir()
        base = {"run": "run", "files": len(ids), "runs": 2, "nondeterministic": [], "crashes": [], "hangs": []}
        base.update(summary or {})
        (run_dir / "summary.json").write_text(json.dumps(base), encoding="utf-8")
        for row_id in ids:
            (run_dir / "outputs" / f"{row_id}.pdf").write_bytes(b"%PDF-1.7\n")
            pages = 2 if row_id in multi else 1
            select = None
            if row_id in multi:
                (run_dir / "outputs" / f"{row_id}.select.pdf").write_bytes(b"%PDF-1.7\n")
                select = {"status": "ok", "code": None, "pages": [2, 1], "output": f"outputs/{row_id}.select.pdf",
                          "reopen": {"status": "ok", "code": None, "pages": 2, "repairs": []}}
            (run_dir / "outcomes" / f"{row_id}.json").write_text(json.dumps({
                "write": write_outcome(pages, output=f"outputs/{row_id}.pdf"), "select": select}),
                encoding="utf-8")
        if pixel:
            files = {i: {"input_opens": True, "output_opens": True, "pages": 2 if i in multi else 1,
                         "identical": 2 if i in multi else 1, "differing": []} for i in ids}
            (run_dir / "pdfium-write.json").write_text(json.dumps({"files": files}), encoding="utf-8")
            chosen = {i: {"input_opens": True, "output_opens": True, "pages": 2, "output_pages": 2,
                          "identical": 2, "differing": [], "pages_right": True} for i in multi}
            (run_dir / "pdfium-select.json").write_text(json.dumps({"files": chosen}), encoding="utf-8")
        if text:
            files = {i: {"input_opens": True, "output_opens": True, "f1": 1.0} for i in ids}
            (run_dir / "pdfium-write-text.json").write_text(json.dumps({"files": files}), encoding="utf-8")
        return run_dir

    def judge(self, run_dir, rows):
        # The planted-marker scan reads the dataset's synth files, which these rows do not have.
        return phase7.judge(run_dir, rows)

    def measure(self, report, start):
        return next(m for m in report["rows"] if m["measure"].startswith(start))

    def test_a_clean_run_passes_every_row(self):
        with tempfile.TemporaryDirectory() as tmp:
            rows = [row("a-0001", set_name="qpdf"), row("a-0002", pages="2", set_name="qpdf")]
            report = self.judge(self.folder(tmp, ["a-0001", "a-0002"], multi=("a-0002",)), rows)
        self.assertTrue(report["passed"], [m for m in report["rows"] if not m["pass"]])
        self.assertEqual(len(report["rows"]), 11)

    def test_refused_files_are_not_in_the_population(self):
        with tempfile.TemporaryDirectory() as tmp:
            rows = [row("a-0001", pages="2", set_name="qpdf"),
                    row("a-0002", set_name="qpdf", expect_open="refused:not_pdf")]
            report = self.judge(self.folder(tmp, ["a-0001"], multi=("a-0001",)), rows)
        self.assertEqual(report["population"], 1)
        self.assertTrue(report["passed"])

    def test_a_missing_outcome_fails_the_reopen_row(self):
        with tempfile.TemporaryDirectory() as tmp:
            rows = [row("a-0001", set_name="qpdf"), row("a-0002", set_name="qpdf")]
            report = self.judge(self.folder(tmp, ["a-0001"]), rows)
        self.assertFalse(self.measure(report, "identity write re-opens")["pass"])

    def test_a_differing_page_is_listed_with_an_unclassified_cause(self):
        with tempfile.TemporaryDirectory() as tmp:
            rows = [row("a-0001", pages="2", set_name="qpdf")]
            run_dir = self.folder(tmp, ["a-0001"])
            files = {"a-0001": {"input_opens": True, "output_opens": True, "pages": 2, "identical": 1,
                                "differing": [{"page": 2, "source_page": 2, "pixels": 40, "cause": None}]}}
            (run_dir / "pdfium-write.json").write_text(json.dumps({"files": files}), encoding="utf-8")
            report = self.judge(run_dir, rows)
            (run_dir / "causes.json").write_text(json.dumps({"a-0001:2": "an OFF layer"}), encoding="utf-8")
            classified = self.judge(run_dir, rows)
        self.assertFalse(self.measure(report, "identity write: pages pixel-identical")["pass"])
        self.assertEqual(report["differing_pages"]["write"],
                         [{"file": "a-0001", "page": 2, "pixels": 40, "cause": "unclassified"}])
        self.assertEqual(classified["differing_pages"]["write"][0]["cause"], "an OFF layer")

    def test_one_run_does_not_measure_determinism(self):
        with tempfile.TemporaryDirectory() as tmp:
            report = self.judge(self.folder(tmp, ["a-0001"], {"runs": 1}), [row("a-0001", set_name="qpdf")])
        self.assertFalse(self.measure(report, "two runs")["pass"])

    def test_a_crash_fails_the_last_row(self):
        with tempfile.TemporaryDirectory() as tmp:
            run_dir = self.folder(tmp, ["a-0001"], {"crashes": [{"id": "a-0001", "exit": 3}]})
            report = self.judge(run_dir, [row("a-0001", set_name="qpdf")])
        self.assertFalse(self.measure(report, "crashes")["pass"])

    def test_missing_pdfium_files_fail_their_rows(self):
        with tempfile.TemporaryDirectory() as tmp:
            run_dir = self.folder(tmp, ["a-0001"], pixel=False, text=False)
            report = self.judge(run_dir, [row("a-0001", set_name="qpdf")])
        self.assertFalse(report["passed"])
        self.assertFalse(self.measure(report, "text agreement")["pass"])

    def test_the_markdown_table_has_a_row_per_measure(self):
        with tempfile.TemporaryDirectory() as tmp:
            report = self.judge(self.folder(tmp, ["a-0001"]), [row("a-0001", set_name="qpdf")])
        text = phase7.markdown(report)
        self.assertIn("| Measure | Value | Threshold | Result |", text)
        self.assertEqual(text.count("| pass |") + text.count("| FAIL |"), 11)


if __name__ == "__main__":
    unittest.main()
