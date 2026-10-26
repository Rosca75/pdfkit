# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# Tests of the shared helpers: the manifest round trip (stable sort by id, LF,
# UTF-8, cells cleaned of tabs and newlines), the id sequence, the expectation
# vocabulary, the shared-root override and the run-id format.

from __future__ import annotations

import os
import re
import tempfile
import unittest
from pathlib import Path

import support  # noqa: F401  (puts the tools on the path)

import common


class ManifestTest(unittest.TestCase):
    def row(self, row_id: str, **extra) -> dict:
        base = {c: "" for c in common.MANIFEST_COLUMNS}
        base.update({"id": row_id, "set": row_id.split("-")[0], "expect_open": "opens",
                     "expect_text": "agree", "expect_images": "n/a", "expect_redact": "n/a",
                     "expect_metadata": "n/a", "expect_fonts": "n/a"})
        base.update(extra)
        return base

    def test_round_trip_sorts_by_id_and_writes_lf_utf8(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "manifest.tsv"
            rows = [self.row("qpdf-0002", notes="café\ttab\nline"), self.row("pdfjs-0001")]
            common.write_tsv(path, common.MANIFEST_COLUMNS, rows, "id")
            raw = path.read_bytes()
            self.assertNotIn(b"\r", raw)
            back = common.read_tsv(path, common.MANIFEST_COLUMNS)
            self.assertEqual([r["id"] for r in back], ["pdfjs-0001", "qpdf-0002"])
            self.assertEqual(back[1]["notes"], "café tab line")

    def test_crlf_checkout_reads_the_same(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "manifest.tsv"
            common.write_tsv(path, common.MANIFEST_COLUMNS, [self.row("qpdf-0001", notes="x")], "id")
            path.write_bytes(path.read_bytes().replace(b"\n", b"\r\n"))
            self.assertEqual(common.read_tsv(path, common.MANIFEST_COLUMNS)[0]["notes"], "x")

    def test_wrong_header_is_refused_with_a_fix(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "manifest.tsv"
            path.write_text("id\tset\n", encoding="utf-8")
            with self.assertRaises(common.ToolError) as caught:
                common.read_tsv(path, common.MANIFEST_COLUMNS)
            self.assertIn("header", caught.exception.fix)

    def test_next_id_follows_the_highest(self):
        rows = [self.row("synth-0001"), self.row("synth-0007"), self.row("qpdf-0100")]
        self.assertEqual(common.next_id(rows, "synth"), "synth-0008")
        self.assertEqual(common.next_id(rows, "office"), "office-0001")

    def test_expectation_vocabulary(self):
        self.assertTrue(common.check_expectation("expect_open", "refused:password_required"))
        self.assertFalse(common.check_expectation("expect_open", "refused:no_such_code"))
        self.assertTrue(common.check_expectation("expect_text", "known_gap:pdfium_f1_0.00"))
        self.assertFalse(common.check_expectation("expect_text", "known_gap:"))
        self.assertFalse(common.check_expectation("expect_images", "maybe"))


class WriterExpectationTest(unittest.TestCase):
    def test_columns_sit_before_notes(self):
        columns = list(common.MANIFEST_COLUMNS)
        self.assertEqual(columns[-5:], ["expect_write", "expect_select", "expect_redact_text",
                                        "expect_set_metadata", "notes"])

    def test_accepted_values(self):
        for value in ("identical", "known_gap:optional_content", "n/a"):
            self.assertTrue(common.check_expectation("expect_write", value), value)
        for value in ("ok", "known_gap:x", "n/a"):
            self.assertTrue(common.check_expectation("expect_select", value), value)
        self.assertFalse(common.check_expectation("expect_write", "ok"))
        self.assertFalse(common.check_expectation("expect_select", "identical"))
        self.assertFalse(common.check_expectation("expect_write", "known_gap:"))

    def test_write_follows_the_open_expectation(self):
        self.assertEqual(common.default_expect_write("opens"), "identical")
        self.assertEqual(common.default_expect_write("repaired"), "identical")
        self.assertEqual(common.default_expect_write("refused:not_pdf"), "n/a")

    def test_select_needs_two_pages_and_a_readable_file(self):
        self.assertEqual(common.default_expect_select("opens", "2"), "ok")
        self.assertEqual(common.default_expect_select("repaired", "30"), "ok")
        self.assertEqual(common.default_expect_select("opens", "1"), "n/a")
        self.assertEqual(common.default_expect_select("opens", ""), "n/a")
        self.assertEqual(common.default_expect_select("refused:password_required", "5"), "n/a")

    def test_committed_manifest_follows_the_rule(self):
        for row in common.read_manifest():
            self.assertEqual(row["expect_write"], common.default_expect_write(row["expect_open"]), row["id"])
            self.assertEqual(row["expect_select"],
                             common.default_expect_select(row["expect_open"], row["pages"]), row["id"])


class LocationTest(unittest.TestCase):
    def test_override_names_the_shared_root(self):
        with tempfile.TemporaryDirectory() as tmp:
            old = os.environ.get("PDFKIT_DATASET_ROOT")
            os.environ["PDFKIT_DATASET_ROOT"] = tmp
            try:
                self.assertEqual(common.files_dir(), Path(tmp).resolve() / "files")
                self.assertEqual(common.reports_dir(), Path(tmp).resolve() / "reports")
            finally:
                if old is None:
                    del os.environ["PDFKIT_DATASET_ROOT"]
                else:
                    os.environ["PDFKIT_DATASET_ROOT"] = old

    def test_main_checkout_holds_the_git_directory(self):
        self.assertTrue((common.main_checkout() / ".git").exists())

    def test_run_id_format(self):
        self.assertRegex(common.run_id(), r"^\d{8}-\d{6}-[0-9a-z]+$")
        self.assertTrue(re.match(r"^\d{8}", common.run_id()))


if __name__ == "__main__":
    unittest.main()
