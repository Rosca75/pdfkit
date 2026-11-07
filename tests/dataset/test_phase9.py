# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# Tests of the image treatment gate's re-open row over a run folder written by
# hand: a file the run hung on has no outcome, counts against the row, and is
# named with the reason it has none.

from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE / "gates"))
sys.path.insert(0, str(HERE.parents[1] / "dataset" / "tools"))

import phase9  # noqa: E402


def row(row_id: str, expect: str = "clean") -> dict:
    return {"id": row_id, "set": "qpdf", "pages": "1", "expect_open": "opens", "expect_redact_images": expect}


def folder(tmp: str, ids, summary: dict) -> Path:
    run_dir = Path(tmp) / "run"
    (run_dir / "outcomes").mkdir(parents=True)
    base = {"files": len(ids), "runs": 2, "nondeterministic": [], "crashes": [], "hangs": []}
    (run_dir / "summary.json").write_text(json.dumps(base | summary), encoding="utf-8")
    for row_id in ids:
        (run_dir / "outcomes" / f"{row_id}.json").write_text(json.dumps({
            "open": {"status": "ok", "pages": 1},
            "redact_images": {"status": "ok", "output": f"outputs/{row_id}.pdf",
                              "reopen": {"status": "ok", "pages": 1, "repairs": []}}}), encoding="utf-8")
    return run_dir


class ReopenTest(unittest.TestCase):
    def reopen(self, report):
        return next(m for m in report["rows"] if m["measure"].startswith("outputs written that re-open"))

    def test_a_hung_file_counts_against_the_row_and_is_named_as_hung(self):
        with tempfile.TemporaryDirectory() as tmp:
            rows = [row("a-0001"), row("a-0002"), row("a-0003", "known_gap:unterminated_inline_image")]
            report = phase9.judge(folder(tmp, ["a-0001"], {"hangs": ["a-0002"]}), rows, 1, False)
        measure = self.reopen(report)
        self.assertEqual(measure["shown"], "1 / 2 = 50.00%")
        self.assertEqual([(f["id"], f["cause"]) for f in measure["detail"]["failed"]],
                         [("a-0002", "no outcome: the run hung on the file")])
        self.assertIn("no outcome: the run hung on the file", phase9.markdown(report))

    def test_a_written_output_that_fails_to_reopen_names_its_reopen(self):
        with tempfile.TemporaryDirectory() as tmp:
            run_dir = folder(tmp, ["a-0001"], {})
            outcome = json.loads((run_dir / "outcomes" / "a-0001.json").read_text(encoding="utf-8"))
            outcome["redact_images"]["reopen"] = {"status": "ok", "pages": 1, "repairs": ["xref"]}
            (run_dir / "outcomes" / "a-0001.json").write_text(json.dumps(outcome), encoding="utf-8")
            report = phase9.judge(run_dir, [row("a-0001")], 1, False)
        failed = self.reopen(report)["detail"]["failed"]
        self.assertEqual(failed[0]["cause"], "the output re-opens with a repair or other pages")


if __name__ == "__main__":
    unittest.main()
