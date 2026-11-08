# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# Tests of the panel's adapters over member outputs written in code, so no
# reader runs: each adapter parses its member's output into the common shape,
# pages split where the member splits them, the pixel formats every member
# writes read into one form, a refusal is told apart from a timeout and a
# crash, and a member whose binary is absent or differs from its pin is
# skipped by name with the command that supplies it.
# Run: python -m unittest discover -s tests/panel -p "test_*.py".

from __future__ import annotations

import struct
import sys
import tempfile
import unittest
import zlib
from pathlib import Path
from unittest import mock

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import readers  # noqa: E402
from readers import extract, lopdf, mupdf, oxide, pdfium_member, pixels, poppler, qpdf, rust_member  # noqa: E402

PYTHON = sys.executable


def png(width: int, height: int, colour: int, depth: int, rows: list[bytes], filters: list[int],
        palette: bytes | None = None) -> bytes:
    """A PNG written in code: each row already filtered with the filter type given."""
    def chunk(kind: bytes, body: bytes) -> bytes:
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))
    raw = b"".join(bytes([f]) + r for f, r in zip(filters, rows))
    out = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, depth, colour, 0, 0, 0))
    if palette:
        out += chunk(b"PLTE", palette)
    return out + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b"")


class PopplerTest(unittest.TestCase):
    def test_text_splits_on_the_form_feed_ending_each_page(self):
        answer = poppler.parse_text("Once upon a time.\n\n\f\f\fpotato\n\f".encode("utf-8"))
        self.assertEqual([p["text"] for p in answer["pages"]], ["Once upon a time.\n\n", "", "", "potato\n"])

    def test_a_file_of_no_page_text_keeps_its_pages(self):
        self.assertEqual(len(poppler.parse_text(b"\f\f")["pages"]), 2)

    def test_pdfinfo_boxes_per_page(self):
        info = (b"Pages:           2\nPage    1 size:  612 x 792 pts (letter)\nPage    1 rot:   90\n"
                b"Page    1 MediaBox:      0.00     0.00   612.00   792.00\n"
                b"Page    1 CropBox:      10.00    10.00   600.00   780.00\n"
                b"Page    1 BleedBox:      0.00     0.00   612.00   792.00\n"
                b"Page    2 size:  150.72 x 95.12 pts\nPage    2 rot:   0\n"
                b"Page    2 MediaBox:      0.00     0.00   150.72    95.12\n")
        answer = poppler.parse_info(info)
        self.assertEqual(answer["count"], 2)
        self.assertEqual(answer["pages"][0], {"size": [612.0, 792.0], "rotate": 90,
                                              "media": [0.0, 0.0, 612.0, 792.0], "crop": [10.0, 10.0, 600.0, 780.0]})
        self.assertEqual(answer["pages"][1]["size"], [150.72, 95.12])

    def test_the_image_list_names_objects_in_draw_order(self):
        listing = (b"page   num  type   width height color comp bpc  enc interp  object ID x-ppi y-ppi size ratio\n"
                   b"-----------------------------------------------------------------------------\n"
                   b"   7     0 image    2500  3250  gray    1   1  ccitt  no        15  0   300   300 3600B 0.4%\n"
                   b"   7     1 smask     100   100  gray    1   8  image  no        16  0    72    72  10K 100%\n"
                   b"   7     2 stencil    52    49  -       1   1  ccitt  no  [inline]     19    24   39B  12%\n")
        rows = poppler.parse_list(listing)
        self.assertEqual([(r["num"], r["type"], r["object"]) for r in rows],
                         [(0, "image", 15), (1, "smask", 16), (2, "stencil", None)])


class MupdfTest(unittest.TestCase):
    def test_pages_read_boxes_and_rotation(self):
        out = (b'file.pdf:\n<page pagenum="1">\n<MediaBox l="0" b="0" r="612" t="792" />\n'
               b'<CropBox l="10" b="10" r="600" t="780" />\n<Rotate v="270" />\n</page>\n'
               b'<page pagenum="2">\n<MediaBox l="0" b="0" r="300" t="400" />\n</page>\n')
        answer = mupdf.parse_pages(out)
        self.assertEqual(answer["count"], 2)
        self.assertEqual(answer["pages"][0], {"media": [0.0, 0.0, 612.0, 792.0], "crop": [10.0, 10.0, 600.0, 780.0],
                                              "rotate": 270})
        self.assertEqual(answer["pages"][1], {"media": [0.0, 0.0, 300.0, 400.0], "rotate": 0})

    def test_page_sizes_are_the_sizes_mupdf_lays_each_page_out_on(self):
        out = (b'<?xml version="1.0"?>\n<document name="a.pdf">\n<page id="page1" width="612" height="792">\n'
               b'<block bbox="40 726.2 206.75 759.17">\n</block>\n</page>\n'
               b'<page id="page2" width="300.5" height="400">\n</page>\n</document>\n')
        self.assertEqual(mupdf.parse_sizes(out), {"ok": True, "count": 2,
                                                  "pages": [{"size": [612.0, 792.0]}, {"size": [300.5, 400.0]}]})

    def test_a_page_file_loses_its_closing_form_feed(self):
        self.assertEqual(mupdf.page_text(b"Once upon a qoww.\n\n\f\n"), "Once upon a qoww.\n\n")
        self.assertEqual(mupdf.page_text("café\f".encode("utf-8")), "café")


class QpdfTest(unittest.TestCase):
    def test_check_reads_version_encryption_and_status(self):
        out = (b"checking f.pdf\nPDF Version: 1.4\nR = 3\nP = -4\nUser password = \n"
               b"File is not linearized\nNo syntax or stream encoding errors found\n")
        result = qpdf.parse_check(0, out, b"")
        self.assertEqual(result, {"check": "ok", "version": "1.4", "encrypted": True, "linearized": False})
        damaged = qpdf.parse_check(3, b"checking f.pdf\nPDF Version: 1.7\nFile is not encrypted\n",
                                   b"WARNING: f.pdf: file is damaged\n")
        self.assertEqual(damaged["check"], "warnings")
        self.assertFalse(damaged["encrypted"])
        self.assertIn("damaged", damaged["messages"])


class RustMembersTest(unittest.TestCase):
    def test_oxide_splits_text_and_boxes(self):
        answer = {"ok": True, "pages": [{"text": "a", "box": [0, 0, 612, 792]},
                                        {"text": None, "error": "panic", "box": None}]}
        self.assertEqual(oxide.shape(answer, "text"), {"ok": True, "pages": [{"text": "a"},
                                                                              {"text": None, "error": "panic"}]})
        pages = oxide.shape(answer, "pages")
        self.assertEqual(pages["count"], 2)
        self.assertEqual(pages["pages"][0], {"visible": [0, 0, 612, 792], "size": [612, 792]})
        self.assertEqual(pages["pages"][1], {"visible": None})
        refused = {"ok": False, "error": "refused", "detail": "Invalid PDF header"}
        self.assertEqual(oxide.shape(refused, "text"), refused)

    def test_an_adapter_answers_only_its_kinds(self):
        for module, kind in ((extract, "pages"), (lopdf, "text"), (oxide, "structure"), (qpdf, "text")):
            self.assertEqual(module.answer(Path("x.pdf"), kind, 1.0, {})["error"], "unsupported")

    def test_a_binary_refusal_and_a_missing_answer(self):
        with tempfile.TemporaryDirectory() as folder:
            script = Path(folder) / "member.py"
            script.write_text("import json, sys\nout = sys.argv[2]\n"
                              "if 'refuse' in sys.argv[1]:\n"
                              "    open(out, 'w').write(json.dumps({'ok': False, 'error': 'Invalid header'}))\n"
                              "    sys.exit(2)\n"
                              "if 'silent' in sys.argv[1]:\n    sys.exit(1)\n"
                              "open(out, 'w').write(json.dumps({'ok': True, 'pages': []}))\n")
            with mock.patch.object(rust_member, "binary", return_value=Path(folder) / "run"), \
                    mock.patch.object(readers, "run", side_effect=lambda cmd, timeout, cwd=None:
                                      real_run([PYTHON, str(script)] + cmd[1:], timeout, cwd)):
                self.assertEqual(rust_member.run("oxide", Path("ok.pdf"), 10), {"ok": True, "pages": []})
                refused = rust_member.run("oxide", Path("refuse.pdf"), 10)
                self.assertEqual((refused["error"], refused["detail"], refused["exit"]),
                                 ("refused", "Invalid header", 2))
                self.assertEqual(rust_member.run("oxide", Path("silent.pdf"), 10)["error"], "crash")


real_run = readers.run


class RunTest(unittest.TestCase):
    def test_a_refusal_a_timeout_and_a_crash_are_told_apart(self):
        refused = readers.run([PYTHON, "-c", "import sys; sys.stderr.write('Syntax Error'); sys.exit(1)"], 10)
        self.assertEqual(poppler.finished_or_refused(refused),
                         {"ok": False, "error": "refused", "detail": "Syntax Error", "exit": 1})
        slow = readers.run([PYTHON, "-c", "import time; time.sleep(10)"], 0.3, retry=False)
        self.assertEqual(slow["error"], "timeout")
        kill = [PYTHON, "-c", "import os, signal; os.kill(os.getpid(), signal.SIGKILL)"]
        killed = readers.run(kill, 10, retry=False)
        self.assertEqual(killed["error"], "crash")
        self.assertEqual(poppler.finished_or_refused(killed)["error"], "crash")

    def test_a_timeout_or_crash_that_repeats_is_a_repeated_failure(self):
        before = readers.started()
        slow = readers.run([PYTHON, "-c", "import time; time.sleep(10)"], 0.3)
        self.assertEqual((slow["error"], slow["repeated"], slow["limit"]), ("repeated_failure", "timeout", 0.3))
        self.assertIn("0.6 s", slow["detail"])
        killed = readers.run([PYTHON, "-c", "import os, signal; os.kill(os.getpid(), signal.SIGKILL)"], 10)
        self.assertEqual((killed["error"], killed["repeated"]), ("repeated_failure", "crash"))
        self.assertEqual(readers.started(), before + 4)

    def test_a_failure_that_does_not_repeat_stays_transient(self):
        crash = {"ok": False, "error": "crash", "detail": "killed by signal 11"}
        self.assertEqual(readers.repeated(crash, {"exit": 0, "stdout": b"", "stderr": b""})["exit"], 0)
        timeout = {"ok": False, "error": "timeout", "detail": "no answer within 60 s"}
        self.assertEqual(readers.repeated(crash, timeout), timeout)
        self.assertIn("timeout", readers.TRANSIENT)
        self.assertIn("crash", readers.TRANSIENT)
        self.assertNotIn("refused", readers.TRANSIENT)

    def test_every_run_is_counted(self):
        before = readers.started()
        readers.run([PYTHON, "-c", "pass"], 10)
        self.assertEqual(readers.started(), before + 1)

    def test_the_time_limit_grows_per_block_of_200_pages(self):
        self.assertEqual([readers.timeout_for(n) for n in (1, 200, 201, 394, 0)], [30.0, 30.0, 60.0, 60.0, 30.0])


class PdfiumTest(unittest.TestCase):
    def test_the_probe_becomes_the_text_shape(self):
        self.assertEqual(pdfium_member.text_shape({"ok": True, "pages": 2, "text": ["a", None]}),
                         {"ok": True, "pages": [{"text": "a"}, {"text": None}]})
        self.assertEqual(pdfium_member.text_shape({"ok": False, "error": "password", "code": 4}),
                         {"ok": False, "error": "refused", "detail": "password"})
        self.assertEqual(pdfium_member.text_shape({"ok": False, "error": "timeout"})["error"], "timeout")


class PixelsTest(unittest.TestCase):
    def test_pbm_one_is_black(self):
        image = pixels.read_pnm(b"P4\n# a comment\n10 2\n" + bytes([0b10000000, 0b01000000, 0xFF, 0xC0]))
        self.assertEqual((image["width"], image["height"], image["layout"]), (10, 2, "gray"))
        self.assertEqual(list(image["samples"][:10]), [0] + [255] * 8 + [0])
        self.assertEqual(list(image["samples"][10:]), [0] * 10)

    def test_pgm_ppm_and_pam(self):
        self.assertEqual(pixels.read_pnm(b"P5\n2 1\n255\n\x00\x80")["samples"], b"\x00\x80")
        rgb = pixels.read_pnm(b"P6\n1 1\n255\n\x01\x02\x03")
        self.assertEqual((rgb["layout"], rgb["samples"]), ("rgb", b"\x01\x02\x03"))
        pam = pixels.read_any(b"P7\nWIDTH 1\nHEIGHT 1\nDEPTH 4\nMAXVAL 255\nTUPLTYPE CMYK\nENDHDR\n\x00\x10\x20\x30")
        self.assertEqual((pam["layout"], pam["samples"]), ("cmyk", b"\x00\x10\x20\x30"))
        with self.assertRaises(pixels.PixelError):
            pixels.read_pnm(b"P5\n1 1\n65535\n\x00\x00")

    def test_png_every_row_filter(self):
        # Two RGB pixels per row; each row's bytes after filtering by its type.
        rows = [b"\x0a\x14\x1e\x28\x32\x3c", b"\x01\x01\x01\x01\x01\x01", b"\x00\x00\x00\x00\x00\x00",
                b"\x02\x02\x02\x02\x02\x02", b"\x00\x00\x00\x00\x00\x00"]
        image = pixels.read_png(png(2, 5, 2, 8, rows, [0, 1, 2, 3, 4]))
        samples = image["samples"]
        self.assertEqual(samples[0:6], b"\x0a\x14\x1e\x28\x32\x3c")
        self.assertEqual(samples[6:12], b"\x01\x01\x01\x02\x02\x02")  # Sub: left neighbour added
        self.assertEqual(samples[12:18], b"\x01\x01\x01\x02\x02\x02")  # Up: the row above
        self.assertEqual(samples[18:24], b"\x02\x02\x02\x04\x04\x04")  # Average: (left + up) / 2
        self.assertEqual(samples[24:30], b"\x02\x02\x02\x04\x04\x04")  # Paeth of zeros: predicted

    def test_png_bilevel_and_palette(self):
        gray = pixels.read_png(png(3, 1, 0, 1, [bytes([0b10100000])], [0]))
        self.assertEqual(gray["samples"], b"\xff\x00\xff")
        indexed = pixels.read_png(png(2, 1, 3, 8, [b"\x01\x00"], [0], palette=b"\x00\x00\x00\xff\x80\x00"))
        self.assertEqual((indexed["layout"], indexed["samples"]), ("rgb", b"\xff\x80\x00\x00\x00\x00"))

    def test_a_pdfium_bitmap_is_composited_over_white(self):
        bgra = bytes([1, 2, 3, 255, 4, 5, 6, 0]) + b"\xee" * 4  # one row of two pixels, stride 12
        image = pixels.from_bgra(2, 1, 12, bgra, 4)
        self.assertEqual((image["layout"], image["samples"]), ("rgb", bytes([3, 2, 1, 255, 255, 255])))
        half = pixels.from_bgra(1, 1, 4, bytes([0, 0, 0, 128]), 4)
        self.assertEqual(half["samples"], bytes([127, 127, 127]))
        bgr = pixels.from_bgra(1, 1, 4, b"\x01\x02\x03\x00", 2)
        self.assertEqual(bgr["samples"], b"\x03\x02\x01")

    def test_a_neutral_rgb_image_is_kept_as_gray(self):
        neutral = pixels.image(2, 1, "rgb", bytes([9, 9, 9, 200, 200, 200]), "ppm")
        self.assertEqual(pixels.compact(neutral)["layout"], "gray")
        self.assertEqual(pixels.compact(neutral)["samples"], bytes([9, 200]))
        coloured = pixels.image(1, 1, "rgb", bytes([9, 9, 8]), "ppm")
        self.assertEqual(pixels.compact(coloured)["layout"], "rgb")


class PinsTest(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.addCleanup(self.folder.cleanup)
        root = Path(self.folder.name)
        self.binary = root / "reader"
        self.binary.write_bytes(b"a reader")
        digest = readers.sha256_of(self.binary)
        lines = ["\t".join(readers.MEMBER_COLUMNS)]
        for member, binary in (("present", f"{self.binary}:{digest}"), ("absent", f"{root / 'gone'}:{'0' * 64}"),
                               ("changed", f"{self.binary}:{'1' * 64}")):
            row = {c: "" for c in readers.MEMBER_COLUMNS}
            row.update(member=member, platform=readers.platform(), binaries=binary,
                       notes="supply: sudo apt-get install the-reader=1.0; a note")
            lines.append("\t".join(row[c] for c in readers.MEMBER_COLUMNS))
        table = root / "members.tsv"
        table.write_text("\n".join(lines) + "\n", encoding="utf-8")
        patcher = mock.patch.object(readers, "MEMBERS_TSV", table)
        patcher.start()
        self.addCleanup(patcher.stop)
        for cached in (readers.pin, readers.pin_problem):
            cached.cache_clear()
            self.addCleanup(cached.cache_clear)

    def test_an_absent_or_changed_member_is_named_with_its_supply_command(self):
        self.assertIsNone(readers.pin_problem("present"))
        absent = readers.pin_problem("absent")
        self.assertIn("absent", absent)
        self.assertIn("is missing", absent)
        self.assertIn("`sudo apt-get install the-reader=1.0`", absent)
        changed = readers.pin_problem("changed")
        self.assertIn("pinned " + "1" * 64, changed)
        self.assertIn("`sudo apt-get install the-reader=1.0`", changed)

    def test_the_combined_digest_does_not_depend_on_order(self):
        self.assertEqual(readers.combined_digest(["b", "a"]), readers.combined_digest(["a", "b"]))
        self.assertNotEqual(readers.combined_digest(["a"]), readers.combined_digest(["a", "b"]))


class ReviewTest(unittest.TestCase):
    def test_a_repeated_failure_names_its_limit(self):
        timeout = {"ok": False, "error": "timeout", "detail": "no answer within 30 s"}
        repeated = readers.repeated(timeout, dict(timeout, detail="no answer within 60 s"), limit=30.0)
        self.assertEqual((repeated["error"], repeated["repeated"], repeated["limit"]),
                         ("repeated_failure", "timeout", 30.0))
        self.assertNotIn("repeated_failure", readers.TRANSIENT)

    def test_qpdf_keeps_the_page_count_when_check_crashes(self):
        crashed = {"ok": False, "error": "repeated_failure", "repeated": "crash", "detail": "crash twice"}
        outcomes = iter([{"exit": 0, "stdout": b"30\n", "stderr": b""}, crashed])
        with mock.patch.object(readers, "run", side_effect=lambda *a, **k: next(outcomes)):
            answer = qpdf.answer(Path("f.pdf"), "structure", 30.0, {})
        self.assertEqual((answer["ok"], answer["pages"], answer["check"]), (True, 30, "crash"))

    def test_qpdf_refuses_on_an_empty_page_count(self):
        with mock.patch.object(readers, "run", return_value={"exit": 0, "stdout": b"", "stderr": b""}):
            answer = qpdf.answer(Path("f.pdf"), "structure", 30.0, {})
        self.assertEqual(answer["error"], "refused")

    def test_an_image_past_the_pixel_ceiling_is_too_large(self):
        with mock.patch.object(pixels, "MAX_PIXELS", 10):
            with self.assertRaises(pixels.TooLarge):
                pixels.read_pnm(b"P4\n10 2\n" + bytes(4))
            with self.assertRaises(pixels.TooLarge):
                pixels.read_png(png(4, 3, 0, 8, [bytes(4)] * 3, [0, 0, 0]))
            self.assertEqual(pixels.read_pnm(b"P5\n5 2\n255\n" + bytes(10))["width"], 5)

    def test_mupdf_marks_the_pages_it_did_not_write(self):
        pages = mupdf.text_pages({1: b"one\f\n", 3: b"three\f\n"})
        self.assertEqual(pages, [{"page": 1, "text": "one"}, {"page": 2, "text": None, "missing": True},
                                 {"page": 3, "text": "three"}])

    def test_pdfium_stores_each_image_as_it_reads_it(self):
        stored = []

        class Pool:
            def ask(self, request, timeout):
                out = Path(request["out"])
                images = {}
                for name in ("4", "5"):
                    (out / f"{name}.samples").write_bytes(name.encode() * 4)
                    images[name] = {"width": 2, "height": 2, "layout": "gray",
                                    "samples_file": str(out / f"{name}.samples")}
                return {"ok": True, "images": images}

        def store(samples):
            stored.append(samples)
            return f"digest-{len(stored)}"

        with mock.patch.object(pdfium_member.POOLS, "get", return_value=Pool()):
            answer = pdfium_member.answer(Path("f.pdf"), "image_pixels", 30.0, {}, store=store)
        self.assertEqual(stored, [b"4444", b"5555"])
        self.assertEqual(answer["images"]["5"]["blob"], "digest-2")
        self.assertNotIn("samples", answer["images"]["5"])

    def test_poppler_abstains_on_stencil_masks_by_its_row(self):
        self.assertIn("abstains", readers.MEMBER_COLUMNS)
        self.assertEqual(readers.abstains("poppler", "image_pixels", "linux"), {"stencil"})
        self.assertEqual(readers.abstains("mupdf", "image_pixels", "linux"), set())


if __name__ == "__main__":
    unittest.main()
