# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# Tests of the reading and bitonal gates judged by the reference panel (their
# scripts under gates/) over a run folder written in code: outcomes, an
# image census and pdfkit's pixel dumps, with a fake panel standing in for the
# answers cache, so no member and no pdfkit executable runs. They hold the
# numbers each gate reports (the median and the share counting against pdfkit
# over decided files; agreed over decided images), the files and images it
# lists (undecided with why, against with its group) and its verdict.
# Run: python -m unittest discover -s tests/dataset -p "test_panel_gates.py".

from __future__ import annotations

import contextlib
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE / "gates"))
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parents[1] / "tests" / "panel"))
sys.path.insert(0, str(HERE.parents[1] / "dataset" / "tools"))

import phase5_panel  # noqa: E402
import phase6_panel  # noqa: E402

TEXT = "alpha bravo charlie delta echo foxtrot golf hotel india juliet"
OTHER = "alpha bravo charlie delta echo lima mike november oscar papa"


def ok(text: str) -> dict:
    return {"ok": True, "pages": [{"text": text}]}


class FakePanel:
    """The answers a gate reads, by (member, file id, kind); absent members are named."""

    def __init__(self, answers: dict, absent=None) -> None:
        self.answers = answers
        self.absent = absent or {}
        self.members = {name: None for name in ("pdfium", "poppler", "mupdf", "oxide", "extract", "qpdf")
                        if name not in self.absent}

    def stripped_copy(self, path, timeout=120.0):
        """The copy without annotations: answers about it are keyed by "<id>.stripped"."""
        del timeout
        return Path(path).with_name(Path(path).stem + ".stripped.pdf"), None

    def answer(self, name, path, kind, timeout=30.0, **request):
        del timeout, request
        return self.answers.get((name, Path(path).stem, kind))

    def samples(self, name, entry):
        del name
        return entry


def row(file_id: str, tags: str = "tounicode_correct") -> dict:
    return {"id": file_id, "set": "synth", "pages": "1", "tags": tags}


def outcome(text: str, present=()) -> dict:
    return {"text": {"status": "ok", "text": text, "hidden_classes": {}, "rungs": {}},
            "channels": {"present": list(present)}}


class ReadingGateTest(unittest.TestCase):
    def run_gate(self, files: dict, answers: dict, causes: dict | None = None, absent=None,
                 readings=None) -> dict:
        with tempfile.TemporaryDirectory() as tmp:
            run_dir = Path(tmp) / "r"
            (run_dir / "outcomes").mkdir(parents=True)
            for file_id, (_, result) in files.items():
                (run_dir / "outcomes" / f"{file_id}.json").write_text(json.dumps(result), encoding="utf-8")
            causes_path = Path(tmp) / "causes.tsv"
            causes_path.write_text("id\tgroup\tevidence\n" + "".join(
                f"{k}\t{g}\t{e}\n" for k, (g, e) in (causes or {}).items()), encoding="utf-8")
            with mock.patch.object(phase5_panel.common, "reports_dir", return_value=Path(tmp)), \
                    mock.patch.object(phase5_panel.common, "read_manifest",
                                      return_value=[r for r, _ in files.values()]), \
                    mock.patch.object(phase5_panel.evidence, "Panel", return_value=FakePanel(answers, absent)), \
                    mock.patch.object(phase5_panel, "PANEL_CAUSES", causes_path), \
                    mock.patch.object(phase5_panel, "PDFIUM_CAUSES", Path(tmp) / "none.tsv"), \
                    mock.patch.object(phase5_panel.evidence, "other_readings", side_effect=readings), \
                    mock.patch.object(phase5_panel.evidence, "binary_digest", return_value="d" * 64), \
                    mock.patch("run.find_pdfkit", return_value=["pdfkit"]):
                code = phase5_panel.main(["--run", "r"])
            if not (run_dir / "phase5_panel.json").exists():
                return {"exit": code}
            summary = json.loads((run_dir / "phase5_panel.json").read_text(encoding="utf-8"))
            self.assertTrue((run_dir / "phase5_panel.md").exists())
            summary["exit"] = code
            return summary

    def test_numbers_lists_and_verdict(self):
        files = {f"f{i}": (row(f"f{i}"), outcome(TEXT)) for i in range(20)}
        files["bad"] = (row("bad"), outcome(TEXT))
        files["thin"] = (row("thin"), outcome(TEXT))
        answers = {}
        for file_id in files:
            for member in ("pdfium", "poppler", "mupdf", "oxide", "extract"):
                answers[(member, file_id, "text")] = ok(OTHER if file_id == "bad" else TEXT)
        for member in ("poppler", "mupdf", "oxide"):
            answers[(member, "thin", "text")] = {"ok": False, "error": "refused"}
        summary = self.run_gate(files, answers, {"bad": ("layout", "a word cut")})
        self.assertEqual((summary["decided"], summary["undecided"], summary["counts_against"]), (21, 1, 1))
        self.assertEqual(summary["share_against"], round(1 / 21, 4))
        self.assertEqual(summary["median"], 1.0)
        self.assertEqual(summary["against"][0]["id"], "bad")
        self.assertEqual(summary["against"][0]["group"], "layout")
        self.assertEqual(summary["undecided_files"][0], {"id": "thin", "why": "2 voters, 3 needed",
                                                         "voters": ["extract", "pdfium"],
                                                         "abstained": {"mupdf": "refused", "oxide": "refused",
                                                                       "poppler": "refused"}})
        self.assertEqual(summary["against_by_group"], {"layout": 1})
        self.assertEqual(summary["gate"], "passed")
        self.assertEqual(summary["exit"], 0)

    def test_the_share_against_past_five_percent_fails(self):
        files = {f"f{i}": (row(f"f{i}"), outcome(TEXT)) for i in range(10)}
        answers = {(m, f"f{i}", "text"): ok(OTHER if i == 0 else TEXT)
                   for i in range(10) for m in ("pdfium", "poppler", "mupdf")}
        summary = self.run_gate(files, answers)
        self.assertEqual(summary["share_against"], 0.1)
        self.assertEqual(summary["against"][0]["group"], "unclassified")
        self.assertEqual(summary["gate"], "failed")
        self.assertEqual(summary["exit"], 1)

    def test_a_legitimate_file_is_listed_and_not_counted(self):
        files = {f"f{i}": (row(f"f{i}"), outcome(TEXT)) for i in range(10)}
        answers = {(m, f"f{i}", "text"): ok(OTHER if i == 0 else TEXT)
                   for i in range(10) for m in ("pdfium", "poppler", "mupdf")}
        summary = self.run_gate(files, answers, {"f0": ("legitimate", "the majority cuts every word")})
        self.assertEqual((summary["outvoted"], summary["counts_against"], summary["share_against"]), (1, 0, 0.0))
        self.assertEqual(summary["against"], [])
        self.assertEqual([(a["id"], a["evidence"]) for a in summary["legitimate"]],
                         [("f0", "the majority cuts every word")])
        self.assertEqual(summary["against_by_group"], {})
        self.assertEqual(summary["gate"], "passed")

    def annotated(self, draws_text: bool, pdfkit: str) -> dict:
        widget = TEXT + " lima mike november oscar papa quebec"
        answers = {(m, "a", "text"): ok(widget) for m in ("poppler", "mupdf", "oxide")}
        answers.update({(m, "a", "text"): ok(TEXT) for m in ("pdfium", "extract")})
        answers.update({(m, "a.stripped", "text"): ok(TEXT) for m in ("poppler", "mupdf", "oxide")})
        answers[("qpdf", "a", "annotations")] = {"ok": True, "draws_text": draws_text, "annotations": 1}
        return self.run_gate({"a": (row("a"), outcome(pdfkit, ["ch_annotation"]))}, answers)

    def test_members_drawing_annotations_read_the_copy_without_them(self):
        summary = self.annotated(True, TEXT)
        self.assertEqual(summary["counts_against"], 0)
        self.assertEqual(summary["files"][0]["read_copy"], ["mupdf", "oxide", "poppler"])
        self.assertEqual(summary["literal"]["counts_against"], 1)

    def test_link_only_annotations_keep_the_members_own_text(self):
        summary = self.annotated(False, TEXT)
        self.assertEqual(summary["counts_against"], 1)
        self.assertEqual(summary["files"][0]["read_copy"], [])

    def test_an_empty_pdfkit_text_counts_against_it_on_an_annotated_file(self):
        summary = self.annotated(True, "")
        self.assertEqual(summary["counts_against"], 1)
        self.assertEqual(summary["against"][0]["scores"],
                         {"extract": 0.0, "mupdf": 0.0, "oxide": 0.0, "pdfium": 0.0, "poppler": 0.0})

    def test_the_literal_median_is_reported(self):
        files = {f"f{i}": (row(f"f{i}"), outcome(TEXT)) for i in range(3)}
        answers = {(m, f"f{i}", "text"): ok(OTHER if i else TEXT) for i in range(3) for m in ("pdfium", "poppler", "mupdf")}
        summary = self.run_gate(files, answers)
        self.assertEqual(summary["literal"]["median"], 0.5)

    def test_every_member_absent_skips_by_name(self):
        absent = {m: f"{m}: missing; supply it with `x`" for m in ("pdfium", "poppler", "mupdf", "oxide", "extract")}
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            summary = self.run_gate({"a": (row("a"), outcome(TEXT))}, {}, absent=absent)
        self.assertEqual(summary, {"exit": 0})
        self.assertIn("SKIPPED", out.getvalue())
        self.assertIn("supply it with", out.getvalue())

    def test_other_readings_from_a_binary_that_reads_the_file_otherwise_are_refused(self):
        hidden = outcome(TEXT)
        hidden["text"]["hidden_classes"] = {"off_media": 2}
        answers = {(m, "a", "text"): ok(TEXT) for m in ("pdfium", "poppler", "mupdf")}
        with self.assertRaises(phase5_panel.common.ToolError) as caught:
            self.run_gate({"a": (row("a"), hidden)}, answers,
                          readings=lambda command, path: {"full": "another text", "readings": [TEXT, TEXT]})
        self.assertIn("a", str(caught.exception.failed))
        same = self.run_gate({"a": (row("a"), hidden)}, answers,
                             readings=lambda command, path: {"full": TEXT, "readings": [TEXT, TEXT]})
        self.assertEqual(same["pdfkit_binary"], "d" * 64)


def gray_dump(path: Path, width: int, height: int, values) -> None:
    path.write_bytes(f"{width} {height} 1\n".encode("ascii") + bytes(values))


def pixels(width, height, values) -> dict:
    return {"width": width, "height": height, "layout": "gray", "samples": bytes(values)}


class BitonalGateTest(unittest.TestCase):
    CENSUS = ("set\tid\tidentity\tobject\tpages\tencoding\tcolour_space\tsize\tbits\tmask\tdecoded\tdamaged\tfault\t"
              "pdfium\tmad\tdiffering\tagrees\n")

    def census_row(self, file_id, obj, ident, encoding, space) -> str:
        return (f"synth\t{file_id}\timg:{ident}\t{obj}\t1\t{encoding}\t{space}\t2x2\t1\tnone\tTrue\tFalse\t\t\t\t\t\n")

    def test_numbers_named_images_and_stencils(self):
        with tempfile.TemporaryDirectory() as tmp:
            run_dir = Path(tmp) / "r"
            (run_dir / "images").mkdir(parents=True)
            (run_dir / "images" / "images.tsv").write_text(
                self.CENSUS
                + self.census_row("synth-0263", 3, "aaaa", "CCITTFaxDecode", "stencil")
                + self.census_row("synth-0265", 4, "bbbb", "JBIG2Decode", "DeviceGray/1")
                + self.census_row("x-0001", 5, "cccc", "CCITTFaxDecode", "DeviceGray/1")
                + self.census_row("x-0001", 6, "dddd", "CCITTFaxDecode", "DeviceGray/1"), encoding="utf-8")
            for file_id, ident, values in (("synth-0263", "aaaa", [255, 0, 0, 255]),
                                           ("synth-0265", "bbbb", [0, 255, 255, 0]),
                                           ("x-0001", "cccc", [0, 0, 0, 0]), ("x-0001", "dddd", [9, 9, 9, 9])):
                folder = run_dir / "panel-pixels" / file_id
                folder.mkdir(parents=True, exist_ok=True)
                gray_dump(folder / f"{ident}.pix", 2, 2, values)
            answers = {}
            for file_id, objs in (("synth-0263", [3]), ("synth-0265", [4]), ("x-0001", [5, 6])):
                answers[("qpdf", file_id, "raw_streams")] = {"ok": True, "streams": {str(o): {"sha256": "x"}
                                                                                     for o in objs}}
                answers[("poppler", file_id, "image_list")] = {"ok": True, "order": {}}
            # A stencil: PDFium black where it paints, MuPDF 255 there, Poppler abstains.
            answers[("pdfium", "synth-0263", "image_pixels")] = {"ok": True, "images": {"3": pixels(2, 2, [0, 255, 255, 0])}}
            answers[("mupdf", "synth-0263", "image_pixels")] = {"ok": True, "images": {"3": pixels(2, 2, [255, 0, 0, 255])}}
            answers[("poppler", "synth-0263", "image_pixels")] = {"ok": True, "images": {"3": pixels(2, 2, [1, 2, 3, 4])}}
            for member in ("pdfium", "poppler", "mupdf"):
                answers[(member, "synth-0265", "image_pixels")] = {"ok": True, "images": {"4": pixels(2, 2, [0, 255, 255, 0])}}
            # Object 5: two of three against pdfkit; object 6: only one voter.
            answers[("pdfium", "x-0001", "image_pixels")] = {"ok": True, "images": {"5": pixels(2, 2, [255] * 4),
                                                                                     "6": {"error": "not_found"}}}
            answers[("mupdf", "x-0001", "image_pixels")] = {"ok": True, "images": {"5": pixels(2, 2, [255] * 4),
                                                                                    "6": pixels(2, 2, [9] * 4)}}
            answers[("poppler", "x-0001", "image_pixels")] = {"ok": True, "images": {"5": pixels(2, 2, [0] * 4),
                                                                                      "6": pixels(4, 1, [9] * 4)}}
            rows = [{"id": i, "set": "synth"} for i in ("synth-0263", "synth-0265", "x-0001")]
            import image_agreement  # noqa: PLC0415

            # The abstentions are read from the Linux rows of members.tsv, whose pixel facts are
            # measured, so the test answers alike on every host.
            linux_abstains = phase6_panel.readers.abstains
            with mock.patch.object(phase6_panel.readers, "abstains",
                                   lambda member, kind, host=None: linux_abstains(member, kind, "linux")), \
                    mock.patch.object(phase6_panel.common, "reports_dir", return_value=Path(tmp)), \
                    mock.patch.object(phase6_panel.common, "read_manifest", return_value=rows), \
                    mock.patch.object(phase6_panel.evidence, "Panel", return_value=FakePanel(answers)), \
                    mock.patch.object(image_agreement, "find_census", return_value=None):
                code = phase6_panel.main(["--run", "r"])
            summary = json.loads((run_dir / "phase6_panel.json").read_text(encoding="utf-8"))
            self.assertTrue((run_dir / "phase6_panel.md").exists())
        self.assertEqual((summary["images"], summary["decided"], summary["agreed"], summary["undecided"]), (4, 3, 2, 1))
        self.assertEqual(summary["share_agreed"], round(2 / 3, 4))
        self.assertEqual(summary["gate"], "failed")
        self.assertEqual(code, 1)
        named = {(r["id"], r["object"]): r for r in summary["named"]}
        self.assertEqual(named[("synth-0263", 3)]["verdict"], "agrees")
        self.assertEqual(named[("synth-0263", 3)]["abstained"], {"poppler": "abstains on this class"})
        self.assertEqual(named[("synth-0265", 4)]["majority"], ["mupdf", "pdfium", "poppler"])
        self.assertEqual(summary["disagreements"][0]["object"], 5)
        self.assertEqual(summary["undecided_images"][0]["why"], "1 voter, 2 needed")
        self.assertEqual(summary["undecided_images"][0]["abstained"], {"pdfium": "not_found", "poppler": "size 4x1"})

    def test_a_missing_census_executable_is_named(self):
        with tempfile.TemporaryDirectory() as tmp:
            run_dir = Path(tmp) / "r"
            (run_dir / "images").mkdir(parents=True)
            (run_dir / "images" / "images.tsv").write_text(
                self.CENSUS + self.census_row("x-0002", 5, "eeee", "CCITTFaxDecode", "DeviceGray/1"),
                encoding="utf-8")
            answers = {("qpdf", "x-0002", "raw_streams"): {"ok": True, "streams": {"5": {"sha256": "x"}}}}
            import image_agreement  # noqa: PLC0415

            with mock.patch.object(phase6_panel.common, "reports_dir", return_value=Path(tmp)), \
                    mock.patch.object(phase6_panel.common, "read_manifest",
                                      return_value=[{"id": "x-0002", "set": "synth"}]), \
                    mock.patch.object(phase6_panel.evidence, "Panel", return_value=FakePanel(answers)), \
                    mock.patch.object(image_agreement, "find_census", return_value=None):
                phase6_panel.main(["--run", "r"])
            summary = json.loads((run_dir / "phase6_panel.json").read_text(encoding="utf-8"))
        why = summary["undecided_images"][0]["why"]
        self.assertIn("no pdfkit_image_census", why)
        self.assertIn("cmake --build --preset", why)


if __name__ == "__main__":
    unittest.main()
