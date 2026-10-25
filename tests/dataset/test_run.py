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
                 "expect_redact_text": "n/a"}]
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
                                           "redact_text"})


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
