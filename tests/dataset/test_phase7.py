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
from unittest import mock
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

    def test_off_layer_markers_are_their_own_class(self):
        out = phase7.classify({mk(24), mk(25, 1), mk(90, 1)})
        self.assertEqual(out["stripped"], [mk(24)])
        self.assertEqual(out["off"], [mk(25, 1)])
        self.assertEqual(out["drawn"], [mk(90, 1)])

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


class PlantedScanTest(unittest.TestCase):
    def test_a_one_page_file_is_not_in_the_selection_leak_test(self):
        # A selection is made only from two pages or more, so a one-page
        # planted file has no selection to scan and is no leak.
        with tempfile.TemporaryDirectory() as tmp:
            run_dir = Path(tmp)
            source = run_dir / "source.pdf"
            source.write_bytes(b"%PDF-1.7\n" + mk(90, 1).encode() + b"\n")
            (run_dir / "out.pdf").write_bytes(source.read_bytes())
            rows = [{"id": "synth-one", "set": "synth", "expect_open": "opens"}]
            outcomes = {"synth-one": {"open": {"status": "ok", "pages": 1},
                                      "write": {"status": "ok", "output": "out.pdf"}, "select": None}}
            with mock.patch.object(phase7.common, "file_path", return_value=source):
                result = phase7.scan_planted(run_dir, rows, outcomes)
        self.assertEqual(result["files"], 1)
        self.assertEqual(result["leak_files"], 0)
        self.assertEqual(result["leaks"], [])


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
                "open": {"status": "ok", "code": None, "pages": pages},
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
        self.assertEqual(len(report["rows"]), 13)

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

    def test_the_pages_of_a_hung_file_count_against_the_share_and_are_named(self):
        # A file the run hung on has no outcome and no rendering: its pages stay in the
        # denominator, because a page with no output is not reproduced, and the report names
        # them with the reason they were not compared.
        with tempfile.TemporaryDirectory() as tmp:
            rows = [row("a-0001", set_name="qpdf"), row("a-0002", pages="3", set_name="qpdf")]
            report = self.judge(self.folder(tmp, ["a-0001"], {"hangs": ["a-0002"]}), rows)
        measure = self.measure(report, "identity write: pages pixel-identical")
        self.assertEqual(measure["value"], 0.25)
        self.assertEqual(measure["shown"], "1 / 4 = 25.00% (3 pages not compared, listed below)")
        self.assertEqual(report["uncompared_pages"]["write"],
                         [{"file": "a-0002", "pages": 3, "compared": 0,
                           "cause": "no outcome: the run hung on the file"}])
        text = phase7.markdown(report)
        self.assertIn("## Pages of the write check not compared (3)", text)
        self.assertIn("| a-0002 | 3 | 0 | no outcome: the run hung on the file |", text)

    def test_a_write_that_failed_or_a_page_pdfium_does_not_render_is_named(self):
        with tempfile.TemporaryDirectory() as tmp:
            rows = [row("a-0001", pages="2", set_name="qpdf"), row("a-0002", pages="2", set_name="qpdf"),
                    row("a-0003", set_name="qpdf")]
            run_dir = self.folder(tmp, ["a-0001", "a-0002", "a-0003"], {"crashes": [{"id": "a-0003", "exit": 4}]})
            (run_dir / "outcomes" / "a-0003.json").unlink()
            outcome = json.loads((run_dir / "outcomes" / "a-0002.json").read_text(encoding="utf-8"))
            outcome["write"] = {"status": "refused", "code": "limit_exceeded"}
            (run_dir / "outcomes" / "a-0002.json").write_text(json.dumps(outcome), encoding="utf-8")
            files = {"a-0001": {"input_opens": True, "output_opens": True, "pages": 1, "identical": 1,
                                "differing": []}}
            (run_dir / "pdfium-write.json").write_text(json.dumps({"files": files}), encoding="utf-8")
            report = self.judge(run_dir, rows)
        self.assertEqual(self.measure(report, "identity write: pages pixel-identical")["shown"],
                         "1 / 5 = 20.00% (4 pages not compared, listed below)")
        self.assertEqual(report["uncompared_pages"]["write"], [
            {"file": "a-0001", "pages": 1, "compared": 1, "cause": "PDFium renders 1 of the manifest's 2 pages"},
            {"file": "a-0002", "pages": 2, "compared": 0, "cause": "identity write refused (limit_exceeded)"},
            {"file": "a-0003", "pages": 1, "compared": 0, "cause": "no outcome: the run crashed or erred"}])

    def test_a_run_that_compares_every_page_names_none(self):
        with tempfile.TemporaryDirectory() as tmp:
            report = self.judge(self.folder(tmp, ["a-0001"]), [row("a-0001", set_name="qpdf")])
        self.assertEqual(report["uncompared_pages"]["write"], [])
        self.assertEqual(self.measure(report, "identity write: pages pixel-identical")["shown"], "1 / 1 = 100.00%")
        self.assertNotIn("not compared", phase7.markdown(report))

    def test_a_hung_multi_page_file_counts_its_kept_pages_against_the_selection_rows(self):
        # The run hung on a-0002 (three pages in the manifest): its selection is the one the
        # contract draws for that id and count, two pages, which stay in the denominator and
        # are named, and its missing selection fails the page-count row.
        with tempfile.TemporaryDirectory() as tmp:
            rows = [row("a-0001", pages="2", set_name="qpdf"), row("a-0002", pages="3", set_name="qpdf")]
            report = self.judge(self.folder(tmp, ["a-0001"], {"hangs": ["a-0002"]}, multi=("a-0001",)), rows)
        self.assertEqual(self.measure(report, "page selection: kept pages")["shown"],
                         "2 / 4 = 50.00% (2 pages not compared, listed below)")
        self.assertEqual(self.measure(report, "page selection, files of two")["shown"], "1 / 2 = 50.00%")
        self.assertEqual(report["uncompared_pages"]["select"],
                         [{"file": "a-0002", "pages": 2, "compared": 0,
                           "cause": "no outcome: the run hung on the file; kept pages drawn for the "
                                    "manifest's 3 pages"}])
        text = phase7.markdown(report)
        self.assertIn("## Pages of the select check not compared (2)", text)
        self.assertIn("| a-0002 | 2 | 0 | no outcome: the run hung on the file;", text)

    def test_a_selection_that_failed_counts_the_pages_it_would_keep(self):
        with tempfile.TemporaryDirectory() as tmp:
            rows = [row("qpdf-0001", pages="7", set_name="qpdf")]
            run_dir = self.folder(tmp, ["qpdf-0001"], multi=("qpdf-0001",))
            outcome = json.loads((run_dir / "outcomes" / "qpdf-0001.json").read_text(encoding="utf-8"))
            outcome["open"]["pages"] = 7
            outcome["select"] = {"status": "refused", "code": "limit_exceeded", "pages": [2, 1]}
            (run_dir / "outcomes" / "qpdf-0001.json").write_text(json.dumps(outcome), encoding="utf-8")
            (run_dir / "pdfium-select.json").write_text(json.dumps({"files": {}}), encoding="utf-8")
            report = self.judge(run_dir, rows)
        self.assertEqual(self.measure(report, "page selection: kept pages")["shown"],
                         "0 / 2 = 0.00% (2 pages not compared, listed below)")
        self.assertEqual(report["uncompared_pages"]["select"],
                         [{"file": "qpdf-0001", "pages": 2, "compared": 0,
                           "cause": "page selection refused (limit_exceeded)"}])

    def failed_selection(self, tmp: str, select: dict) -> dict:
        """The report of a run whose one seven-page file has the given selection member."""
        rows = [row("qpdf-0001", pages="7", set_name="qpdf")]
        run_dir = self.folder(tmp, ["qpdf-0001"], multi=("qpdf-0001",))
        outcome = json.loads((run_dir / "outcomes" / "qpdf-0001.json").read_text(encoding="utf-8"))
        outcome["open"]["pages"] = 7
        outcome["select"] = select
        (run_dir / "outcomes" / "qpdf-0001.json").write_text(json.dumps(outcome), encoding="utf-8")
        (run_dir / "pdfium-select.json").write_text(json.dumps({"files": {}}), encoding="utf-8")
        return self.judge(run_dir, rows)

    def test_a_failed_selection_counts_the_pages_its_outcome_names(self):
        # The verb writes the kept pages into a selection that failed; those count, not a new draw.
        with tempfile.TemporaryDirectory() as tmp:
            report = self.failed_selection(tmp, {"status": "error", "code": "internal", "pages": [5, 1, 3]})
        self.assertEqual(self.measure(report, "page selection: kept pages")["shown"],
                         "0 / 3 = 0.00% (3 pages not compared, listed below)")

    def test_a_selection_whose_output_was_not_stored_counts_the_drawn_pages(self):
        # An output the verb could not store names no pages: the contract's draw counts.
        with tempfile.TemporaryDirectory() as tmp:
            report = self.failed_selection(tmp, {"status": "error", "code": "output_failed"})
        self.assertEqual(report["uncompared_pages"]["select"],
                         [{"file": "qpdf-0001", "pages": 2, "compared": 0,
                           "cause": "page selection error (output_failed)"}])

    def test_a_clean_selection_run_names_no_page(self):
        with tempfile.TemporaryDirectory() as tmp:
            rows = [row("a-0001", pages="2", set_name="qpdf")]
            report = self.judge(self.folder(tmp, ["a-0001"], multi=("a-0001",)), rows)
        self.assertEqual(report["uncompared_pages"]["select"], [])
        self.assertEqual(self.measure(report, "page selection: kept pages")["shown"], "2 / 2 = 100.00%")

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
        self.assertEqual(text.count("| pass |") + text.count("| FAIL |"), 13)


class SelectionPagesTest(unittest.TestCase):
    # The expected lists are the C++ verb's own draws, pinned by the unit tier's
    # tests/unit/cli/dataset_selection_test.cpp, so a change to either copy fails a test.
    def test_the_drawn_pages_match_the_contract(self):
        self.assertEqual(phase7.selection_pages("a-0002", 3), [2, 3])
        self.assertEqual(phase7.selection_pages("a-0001", 2), [2])
        self.assertEqual(phase7.selection_pages("qpdf-0001", 7), [2, 1])
        self.assertEqual(len(phase7.selection_pages("dccc-0129", 394)), 200)
        self.assertEqual(phase7.selection_pages("dccc-0129", 394)[:5], [141, 318, 136, 243, 207])

    def test_a_selection_is_a_proper_subset_of_distinct_pages(self):
        for pages in range(2, 12):
            for name in ("x-0001", "x-0002", "x-0003"):
                kept = phase7.selection_pages(name, pages)
                self.assertTrue(0 < len(kept) < pages)
                self.assertEqual(len(set(kept)), len(kept))
                self.assertTrue(all(1 <= p <= pages for p in kept))


class OptionalContentTest(unittest.TestCase):
    def test_the_names_oc_and_ocproperties_are_found_and_longer_names_are_not(self):
        for text in (b"/OC /oc1 BDC", b"<</OC 5 0 R>>", b"/OCProperties<<", b"end /OC"):
            self.assertIsNotNone(phase7.OC_NAME_RE.search(text), text)
        for text in (b"/OCGs [", b"/OCMD", b"/OCX 1"):
            self.assertIsNone(phase7.OC_NAME_RE.search(text), text)

    def test_a_writer_that_keeps_off_layers_is_caught(self):
        # A "pdfkit" that copies its input: every off marker survives and /OC stays.
        copier = [sys.executable, "-c", "import shutil, sys; shutil.copy(sys.argv[2], sys.argv[6])"]
        with tempfile.TemporaryDirectory() as tmp:
            family = phase7.off_layer_family(Path(tmp), copier)
        self.assertEqual(family["failed"], [])
        self.assertEqual(family["files"], 5)
        self.assertTrue(family["off_surviving"])
        self.assertEqual(family["missing_drawn"], [])
        self.assertEqual(len(family["oc_named"]), 5)

    def test_no_pdfkit_is_reported_as_not_measured(self):
        with tempfile.TemporaryDirectory() as tmp:
            family = phase7.off_layer_family(Path(tmp), None)
            report = phase7.judge(JudgeTest().folder(tmp, ["a-0001"]), [row("a-0001", set_name="qpdf")], family)
        measure = next(m for m in report["rows"] if m["measure"].startswith("planted markers in optional"))
        self.assertFalse(measure["pass"])
        self.assertIn("not measured", measure["shown"])


if __name__ == "__main__":
    unittest.main()
