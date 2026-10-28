# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# Tests of the dataset tier's judging rules for the write and select columns, the
# --checks subset of the summary and the page-count rule of an identity write.

from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import run  # noqa: E402

OK_REOPEN = {"status": "ok", "code": None, "pages": 3, "repairs": []}


def outcome(write=None, select=None, pages=3) -> dict:
    return {"id": "x", "sha256": "h", "open": {"status": "ok", "pages": pages, "repairs": []},
            "write": write, "select": select}


def write_part(reopen=None, pages=3, status="ok") -> dict:
    return {"status": status, "code": None, "output": "outputs/x.pdf", "pages": pages,
            "reopen": OK_REOPEN if reopen is None else reopen}


class WriteRuleTest(unittest.TestCase):
    def judge(self, part, expected="identical", pages=3):
        return run.check("expect_write", expected, outcome(write=part, pages=pages))

    def test_a_clean_identity_write_passes(self):
        self.assertEqual(self.judge(write_part())[0], "pass")

    def test_a_write_that_fails_is_a_failure(self):
        self.assertEqual(self.judge({"status": "error", "code": "PDFKIT_E_INTERNAL"})[0], "fail")

    def test_the_output_must_open_again(self):
        part = write_part(reopen={"status": "refused", "code": "no_page", "pages": 0, "repairs": []})
        self.assertEqual(self.judge(part)[0], "fail")

    def test_the_output_must_open_without_repairs(self):
        part = write_part(reopen={**OK_REOPEN, "repairs": ["startxref wrong"]})
        self.assertEqual(self.judge(part)[0], "fail")

    def test_the_reopened_page_count_must_match_the_write(self):
        part = write_part(reopen={**OK_REOPEN, "pages": 2})
        self.assertEqual(self.judge(part)[0], "fail")

    def test_the_write_must_keep_the_input_page_count(self):
        part = write_part(reopen={**OK_REOPEN, "pages": 2}, pages=2)
        self.assertEqual(self.judge(part, pages=3)[0], "fail")

    def test_not_implemented_is_skipped_never_passed(self):
        part = {"status": "not_implemented", "code": "PDFKIT_E_NOT_IMPLEMENTED"}
        self.assertEqual(self.judge(part)[0], "skipped")

    def test_a_null_function_is_skipped(self):
        self.assertEqual(self.judge(None)[0], "skipped")

    def test_a_known_gap_passes_apart(self):
        self.assertEqual(self.judge(write_part(), "known_gap:optional_content"), ("pass", "known_gap"))

    def test_n_a_is_not_checked(self):
        self.assertEqual(self.judge(write_part(status="error"), "n/a")[0], "skipped")


class SelectRuleTest(unittest.TestCase):
    def judge(self, part, expected="ok"):
        return run.check("expect_select", expected, outcome(select=part))

    def part(self, pages=(3, 1), reopen_pages=2, repairs=()):
        return {"status": "ok", "code": None, "pages": list(pages), "output": "outputs/x.select.pdf",
                "reopen": {"status": "ok", "code": None, "pages": reopen_pages, "repairs": list(repairs)}}

    def test_the_output_has_as_many_pages_as_were_kept(self):
        self.assertEqual(self.judge(self.part())[0], "pass")
        self.assertEqual(self.judge(self.part(reopen_pages=3))[0], "fail")

    def test_the_output_must_open_without_repairs(self):
        self.assertEqual(self.judge(self.part(repairs=["x"]))[0], "fail")

    def test_a_skipped_selection_fails_when_open_did_not_succeed(self):
        self.assertEqual(self.judge({"status": "skipped", "code": None})[0], "fail")

    def test_no_input_page_count_rule_for_a_selection(self):
        self.assertEqual(self.judge(self.part(pages=(1,), reopen_pages=1))[0], "pass")


class SummaryTest(unittest.TestCase):
    def rows_and_runs(self, body):
        rows = [{"id": "x", "set": "synth", "sha256": "h", "expect_open": "opens", "expect_text": "n/a",
                 "expect_images": "n/a", "expect_redact": "n/a", "expect_metadata": "n/a",
                 "expect_write": "identical", "expect_select": "n/a", "expect_fonts": "n/a",
                 "expect_redact_text": "n/a", "expect_set_metadata": "n/a",
                 "expect_redact_images": "n/a", "expect_find": "n/a"}]
        written = {"x": {"id": "x", "result": "written", "bytes": json.dumps(body).encode(), "seconds": 0.1}}
        return rows, written

    def test_checks_limit_the_judged_columns(self):
        rows, written = self.rows_and_runs(outcome(write=write_part()))
        with tempfile.TemporaryDirectory() as tmp:
            summary = run.summarise(rows, written, {}, Path(tmp) / "run", 1.0, ("write",))
        self.assertEqual(list(summary["columns"]), ["expect_write"])
        self.assertEqual(summary["checks"], ["write"])
        self.assertEqual(summary["runs"], 1)
        self.assertEqual(summary["columns"]["expect_write"], {"pass": 1})

    def test_a_second_run_that_differs_is_nondeterministic(self):
        rows, written = self.rows_and_runs(outcome(write=write_part()))
        other = {"x": dict(written["x"], bytes=b"{}")}
        with tempfile.TemporaryDirectory() as tmp:
            summary = run.summarise(rows, written, other, Path(tmp) / "run", 1.0)
        self.assertEqual(summary["nondeterministic"], ["x"])
        self.assertEqual(summary["runs"], 2)

    def test_expect_fonts_counts_the_unreadable_codes_of_the_text(self):
        text = {"status": "ok", "code": None,
                "pages": [{"page": 1, "text": "a", "unreadable": 2, "hidden": 0},
                          {"page": 2, "text": "b", "unreadable": 1, "hidden": 0}]}
        self.assertEqual(run.check("expect_fonts", "unreadable:3", {"text": text})[0], "pass")
        self.assertEqual(run.check("expect_fonts", "loads", {"text": text})[0], "fail")
        clean = dict(text, pages=[{"page": 1, "text": "a", "unreadable": 0, "hidden": 0}])
        self.assertEqual(run.check("expect_fonts", "loads", {"text": clean})[0], "pass")

    def test_every_expectation_column_is_known_to_the_harness(self):
        import common
        # expect_fonts reads the text outcome's unreadable codes.
        self.assertEqual(set(run.COLUMNS), set(common.EXPECTATIONS))
        self.assertEqual(run.COLUMNS["expect_fonts"], "text")
        self.assertEqual(set(run.CHECKS), {"open", "text", "images", "redact", "metadata", "write", "select",
                                           "redact_text", "set_metadata", "redact_images", "find"})


class TimeoutTest(unittest.TestCase):
    def test_the_timeout_grows_with_the_page_count(self):
        self.assertEqual(run.timeout_for({"pages": "1"}), run.TIMEOUT)
        self.assertEqual(run.timeout_for({"pages": ""}), run.TIMEOUT)
        self.assertEqual(run.timeout_for({"pages": "200"}), run.TIMEOUT)
        self.assertEqual(run.timeout_for({"pages": "201"}), 2 * run.TIMEOUT)
        self.assertEqual(run.timeout_for({"pages": "910"}), 5 * run.TIMEOUT)

    def test_the_scale_multiplies_the_limit(self):
        self.assertEqual(run.timeout_for({"pages": "201"}, 3.0), 6 * run.TIMEOUT)


class SelectTest(unittest.TestCase):
    ROWS = [{"id": "a-1", "set": "a"}, {"id": "a-2", "set": "a"}, {"id": "b-1", "set": "b"}]

    def ids(self, sets=None, ids=None):
        return [r["id"] for r in run.select_rows(self.ROWS, sets, ids)]

    def test_no_filter_keeps_every_row(self):
        self.assertEqual(self.ids(), ["a-1", "a-2", "b-1"])

    def test_a_comma_list_of_ids_is_split(self):
        self.assertEqual(self.ids(ids=["a-1,b-1"]), ["a-1", "b-1"])
        self.assertEqual(self.ids(ids=["a-2", " b-1 "]), ["a-2", "b-1"])

    def test_a_comma_list_of_sets_is_split(self):
        self.assertEqual(self.ids(sets=["a, b"]), ["a-1", "a-2", "b-1"])

    def test_an_id_the_manifest_lacks_fails_naming_it(self):
        import common
        with self.assertRaises(common.ToolError) as caught:
            self.ids(ids=["a-1,zz-9"])
        self.assertIn("zz-9", caught.exception.failed)

    def test_a_filter_that_selects_nothing_fails(self):
        import common
        with self.assertRaises(common.ToolError):
            self.ids(sets=["a"], ids=["b-1"])
        with self.assertRaises(common.ToolError):
            self.ids(sets=["c"])


class SanitizerTest(unittest.TestCase):
    def test_a_report_on_a_finished_run_is_named_and_fails_the_run(self):
        rows, written = SummaryTest.rows_and_runs(None, outcome(write=write_part()))
        reported = {"x": dict(written["x"], sanitizer="runtime error: signed integer overflow")}
        with tempfile.TemporaryDirectory() as tmp:
            summary = run.summarise(rows, reported, {}, Path(tmp) / "run", 1.0, ("write",))
        self.assertEqual([r["id"] for r in summary["sanitizer_reports"]], ["x"])
        self.assertEqual(summary["columns"]["expect_write"], {"pass": 1})

    def test_the_report_pattern_finds_asan_and_ubsan_and_nothing_else(self):
        self.assertTrue(run.SANITIZER_REPORT.search(b"==1==ERROR: AddressSanitizer: heap-buffer-overflow"))
        self.assertTrue(run.SANITIZER_REPORT.search(b"x.cpp:3:5: runtime error: shift exponent 40"))
        self.assertIsNone(run.SANITIZER_REPORT.search(b'{"code": "PDFKIT_E_REFUSED", "failed": "runtime"}'))


class MetadataTest(unittest.TestCase):
    outcome = {"open": {"status": "ok", "pages": 2}}

    def part(self, **extra):
        base = {"status": "ok", "code": None, "self_check": "passed", "mismatches": [],
                "originals": ["Old title"], "reopen": {"status": "ok", "pages": 2, "repairs": []}}
        base.update(extra)
        return base

    def test_metadata_is_read_unless_the_packet_does_not_scan(self):
        read = {"status": "ok", "code": None, "info": True, "xmp": "read"}
        self.assertEqual(run.check("expect_metadata", "read", {"metadata": read})[0], "pass")
        broken = dict(read, xmp="unreadable")
        self.assertEqual(run.check("expect_metadata", "read", {"metadata": broken})[0], "fail")
        self.assertEqual(run.check("expect_metadata", "known_gap:xmp_unreadable",
                                   {"metadata": broken})[0], "pass")

    def test_a_rewrite_is_clean_when_every_field_reads_back(self):
        outcome = dict(self.outcome, set_metadata=self.part())
        self.assertEqual(run.check("expect_set_metadata", "clean", outcome)[0], "pass")
        outcome = dict(self.outcome, set_metadata=self.part(mismatches=["Title"]))
        self.assertEqual(run.check("expect_set_metadata", "clean", outcome)[0], "fail")
        outcome = dict(self.outcome, set_metadata=self.part(self_check="not_run"))
        self.assertEqual(run.check("expect_set_metadata", "clean", outcome)[0], "fail")
        outcome = dict(self.outcome, set_metadata=self.part(self_check="not_run", originals=[]))
        self.assertEqual(run.check("expect_set_metadata", "clean", outcome)[0], "pass")
        refused = {"status": "error", "code": "self_check", "message": "x"}
        outcome = dict(self.outcome, set_metadata=refused)
        self.assertEqual(run.check("expect_set_metadata", "clean", outcome)[0], "fail")
        self.assertEqual(run.check("expect_set_metadata", "known_gap:title_in_string", outcome)[0],
                         "pass")


class RedactImagesTest(unittest.TestCase):
    def part(self, survivors=(), repairs=(), pages=3, treatments=1):
        return {"status": "ok", "code": None, "pages": pages, "survivors": list(survivors),
                "treatments": [{"identity": "img:0", "kind": "box"}] * treatments,
                "reopen": {"status": "ok", "code": None, "pages": pages, "repairs": list(repairs)}}

    def judge(self, part, expected="clean"):
        return run.check("expect_redact_images", expected, {"open": {"status": "ok", "pages": 3},
                                                            "redact_images": part})

    def test_a_clean_treatment_passes(self):
        self.assertEqual(self.judge(self.part())[0], "pass")

    def test_a_surviving_identity_fails(self):
        result, reason = self.judge(self.part(survivors=["img:0123"]))
        self.assertEqual(result, "fail")
        self.assertIn("img:0123", reason)

    def test_no_treatment_and_a_bad_output_fail(self):
        self.assertEqual(self.judge(self.part(treatments=0))[0], "fail")
        self.assertEqual(self.judge(self.part(repairs=["x"]))[0], "fail")
        self.assertEqual(self.judge({"status": "error", "code": "PDFKIT_E_INTERNAL"})[0], "fail")

    def test_n_a_and_known_gap(self):
        self.assertEqual(self.judge(self.part(), "n/a")[0], "skipped")
        self.assertEqual(self.judge({"status": "error", "code": "x"}, "known_gap:y")[0], "pass")


class RedactTextTest(unittest.TestCase):
    def part(self, self_check="passed", repairs=(), pages=3):
        return {"status": "ok", "code": None, "self_check": self_check, "pages": pages,
                "reopen": {"status": "ok", "code": None, "pages": pages, "repairs": list(repairs)}}

    def judge(self, part, expected="clean"):
        return run.check("expect_redact_text", expected, {"open": {"status": "ok", "pages": 3},
                                                          "redact_text": part})

    def test_a_clean_redaction_passes(self):
        self.assertEqual(self.judge(self.part())[0], "pass")

    def test_a_refused_self_check_fails(self):
        refused = {"status": "error", "code": "PDFKIT_E_SELF_CHECK", "message": "found"}
        self.assertEqual(self.judge(refused)[0], "fail")
        self.assertEqual(self.judge(self.part(self_check=None))[0], "fail")

    def test_an_output_that_reopens_with_repairs_or_other_pages_fails(self):
        self.assertEqual(self.judge(self.part(repairs=["x"]))[0], "fail")
        self.assertEqual(self.judge(self.part(pages=2))[0], "fail")

    def test_a_known_gap_passes_apart(self):
        refused = {"status": "error", "code": "PDFKIT_E_SELF_CHECK", "message": "found"}
        self.assertEqual(self.judge(refused, "known_gap:metadata"), ("pass", "known_gap"))


if __name__ == "__main__":
    unittest.main()
