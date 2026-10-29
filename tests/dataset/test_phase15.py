# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# Tests of the gate of the glued letters and the string occurrences: the
# measures it reads from the find member of an outcome and the regressions it
# finds between two runs' summaries.

from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE / "gates"))

import phase15  # noqa: E402


def find_part(not_words=0, strings=None) -> dict:
    return {"status": "ok", "glued": {"checked": 3, "not_words": not_words, "named": []},
            "strings": strings or {"status": "none", "found": 0, "tried": 0, "exact": 0, "failures": []}}


def write_run(folder: Path, summary: dict, parts: dict) -> Path:
    (folder / "outcomes").mkdir(parents=True)
    for row_id, part in parts.items():
        (folder / "outcomes" / f"{row_id}.json").write_text(json.dumps({"find": part}), encoding="utf-8")
    (folder / "summary.json").write_text(json.dumps(summary), encoding="utf-8")
    return folder


def summary(failures=(), runs=2, columns=("expect_open", "expect_find")) -> dict:
    return {"runs": runs, "crashes": [], "hangs": [], "nondeterministic": [],
            "columns": {c: {} for c in columns},
            "failures": [{"id": i, "column": c, "reason": "x"} for i, c in failures]}


GOOD = {"status": "ok", "found": 1, "tried": 1, "exact": 1, "failures": []}
ROWS = [{"id": "a", "expect_find": "agree"}, {"id": "b", "expect_find": "agree"}]


class MeasuresTest(unittest.TestCase):
    def judge(self, parts, base=None, baseline=None):
        with tempfile.TemporaryDirectory() as tmp:
            run = write_run(Path(tmp) / "run", base or summary(), parts)
            other = write_run(Path(tmp) / "old", baseline, {"a": find_part(), "b": find_part()}) if baseline else None
            return phase15.judge(run, ROWS, other)

    def test_a_clean_run_passes(self):
        report = self.judge({"a": find_part(strings=GOOD), "b": find_part()})
        self.assertTrue(report["passed"])

    def test_a_run_that_tries_no_string_is_reported_and_passes(self):
        report = self.judge({"a": find_part(), "b": find_part()})
        self.assertTrue(report["rows"][1]["shown"].startswith("not measured"))
        self.assertTrue(report["passed"])

    def test_a_glued_join_that_is_no_word_fails(self):
        report = self.judge({"a": find_part(not_words=1), "b": find_part()})
        self.assertFalse(report["passed"])
        self.assertEqual(report["rows"][0]["detail"]["files"][0]["id"], "a")

    def test_a_string_feedback_must_be_exact(self):
        bad = {"status": "ok", "found": 2, "tried": 1, "exact": 0, "failures": [{"value": "v", "page": 1, "why": "w"}]}
        report = self.judge({"a": find_part(strings=bad), "b": find_part(strings=GOOD)})
        self.assertEqual(report["rows"][1]["shown"], "1 / 2 = 50.00%")
        self.assertFalse(report["passed"])

    def test_one_run_does_not_show_determinism(self):
        report = self.judge({"a": find_part(strings=GOOD), "b": find_part()}, base=summary(runs=1))
        self.assertFalse(report["passed"])

    def test_a_check_that_passed_in_the_baseline_and_fails_now_is_a_regression(self):
        report = self.judge({"a": find_part(), "b": find_part()}, base=summary([("a", "expect_find")]),
                            baseline=summary())
        self.assertEqual(report["rows"][-1]["detail"]["regressions"][0]["id"], "a")
        self.assertFalse(report["passed"])

    def test_a_check_that_failed_in_the_baseline_is_not_a_regression(self):
        failed = summary([("a", "expect_find")])
        report = self.judge({"a": find_part(strings=GOOD), "b": find_part()}, base=failed, baseline=failed)
        self.assertTrue(report["passed"])


if __name__ == "__main__":
    unittest.main()
