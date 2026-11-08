# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# Tests of the panel's collection (collect.py) that run no member: a repeated
# failure is counted in its own column, and the census that names the images
# is the one the last collection read unless --census names another, so a
# warm collection asks with the requests it cached.
# Run: python -m unittest discover -s tests/panel -p "test_*.py".

from __future__ import annotations

import json
import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import collect  # noqa: E402
import common  # noqa: E402


class TallyTest(unittest.TestCase):
    def test_a_repeated_failure_has_its_own_column(self):
        tally = collect.Tally()
        tally.add("qpdf", "structure", "a", {"ok": True}, 0.1)
        tally.add("qpdf", "structure", "b", {"ok": False, "error": "refused", "detail": "x"}, 0.1)
        tally.add("qpdf", "structure", "c", {"ok": False, "error": "repeated_failure", "repeated": "crash"}, 0.1)
        (row,) = collect.summary_rows(tally, {"structure": 3})
        self.assertEqual((row["answered"], row["refused"], row["repeated"], row["transient"]), (1, 1, 1, 0))
        self.assertEqual(row["answered"] + row["refused"] + row["repeated"], row["applicable"])

    def test_a_cause_is_the_last_line(self):
        self.assertEqual(collect.cause_of({"detail": "warning: a\nerror: cannot draw 12"}), "error: cannot draw ##")


class CensusTest(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.addCleanup(self.folder.cleanup)
        self.root = Path(self.folder.name) / "dataset"
        patcher = mock.patch.dict(os.environ, {"PDFKIT_DATASET_ROOT": str(self.root)})
        patcher.start()
        self.addCleanup(patcher.stop)
        for run in ("old", "new"):
            census = self.root / "reports" / run / "images" / "images.tsv"
            census.parent.mkdir(parents=True)
            census.write_text("set\tid\n", encoding="utf-8")

    def test_the_named_run_wins(self):
        self.assertEqual(collect.find_census("new"), self.root / "reports" / "new" / "images" / "images.tsv")

    def test_the_last_collection_names_the_census(self):
        report = self.root / "reports" / "panel" / "panel" / "collect.json"
        report.parent.mkdir(parents=True)
        report.write_text(json.dumps({"census": str(self.root / "reports" / "old" / "images" / "images.tsv")}))
        self.assertEqual(collect.find_census(None), self.root / "reports" / "old" / "images" / "images.tsv")

    def test_without_a_collection_the_census_is_required(self):
        with self.assertRaises(common.ToolError):
            collect.find_census(None)


class ArchiveTest(unittest.TestCase):
    def test_an_archive_is_re_hashed_or_skipped_by_name(self):
        import fetch

        with tempfile.TemporaryDirectory() as folder:
            archive = Path(folder) / "pdfium-linux-x64.tgz"
            archive.write_bytes(b"archive bytes")
            digest = common.sha256_file(archive)
            self.assertTrue(fetch.verify_archive("pdfium archive", archive, digest)["ok"])
            changed = fetch.verify_archive("pdfium archive", archive, "0" * 64)
            self.assertFalse(changed["ok"])
            self.assertIn(digest, changed["problem"])
            absent = fetch.verify_archive("rust toolchain archive", Path(folder) / "gone.tar.xz", digest)
            self.assertTrue(absent["ok"])
            self.assertIn("absent", absent["skipped"])


if __name__ == "__main__":
    unittest.main()
