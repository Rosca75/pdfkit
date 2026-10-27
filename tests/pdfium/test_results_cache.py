# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# Unit tests of the results cache and of the render worker's use of it, with a
# fake PDFium (no DLL needed): a probe of an input is asked once and read back
# after, a transient failure is never kept, a changed file is a new key, and a
# page comparison against a cached input page answers exactly what comparing
# the two rendered bitmaps answers, rendering the input again only when the
# output page differs.

from __future__ import annotations

import os
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import pdfium  # noqa: E402
import render  # noqa: E402
import results_cache  # noqa: E402

WHITE = bytes([255, 255, 255, 255])
BLACK = bytes([0, 0, 0, 255])


class FakePool:
    def __init__(self, answer: dict):
        self.answer = answer
        self.asked = 0

    def probe(self, path, ops=("info",), **extra):
        self.asked += 1
        return dict(self.answer)


class CacheTest(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        os.environ["PDFKIT_PDFIUM_RESULTS"] = self.folder.name
        self.input = Path(self.folder.name) / "input.pdf"
        self.input.write_bytes(b"%PDF-1.7 one")

    def tearDown(self):
        os.environ.pop("PDFKIT_PDFIUM_RESULTS", None)
        self.folder.cleanup()

    def test_a_probe_is_asked_once(self):
        pool = FakePool({"ok": True, "pages": 1, "text": ["hello"]})
        first = results_cache.cached_probe(pool, self.input, ["text"])
        second = results_cache.cached_probe(pool, self.input, ["text"])
        self.assertEqual(first, second)
        self.assertEqual(pool.asked, 1)

    def test_other_operations_are_another_key(self):
        pool = FakePool({"ok": True, "pages": 1})
        results_cache.cached_probe(pool, self.input, ["text"])
        results_cache.cached_probe(pool, self.input, ["meta"])
        self.assertEqual(pool.asked, 2)

    def test_a_file_failure_is_kept_and_a_transient_one_is_not(self):
        broken = FakePool({"ok": False, "error": "format", "code": 3})
        results_cache.cached_probe(broken, self.input, ["text"])
        results_cache.cached_probe(broken, self.input, ["text"])
        self.assertEqual(broken.asked, 1)
        hung = FakePool({"ok": False, "error": "timeout"})
        results_cache.cached_probe(hung, self.input, ["meta"])
        results_cache.cached_probe(hung, self.input, ["meta"])
        self.assertEqual(hung.asked, 2)

    def test_changed_bytes_are_a_new_key(self):
        pool = FakePool({"ok": True, "pages": 1})
        results_cache.cached_probe(pool, self.input, ["info"])
        self.input.write_bytes(b"%PDF-1.7 two, longer")
        results_cache.cached_probe(pool, self.input, ["info"])
        self.assertEqual(pool.asked, 2)


def page(width: int, height: int, dots=()) -> dict:
    rows = []
    for y in range(height):
        row = bytearray(WHITE * width)
        for x, dot_y in dots:
            if dot_y == y:
                row[4 * x:4 * x + 4] = BLACK
        rows.append(bytes(row))
    return {"width": width, "height": height, "stride": width * 4, "bgra": b"".join(rows)}


class FakeDocument:
    def __init__(self, pages: list[dict], log: list):
        self.pages, self.log = pages, log

    def page_count(self) -> int:
        return len(self.pages)

    def render(self, index: int, dpi: float, annotations: bool = False) -> dict:
        self.log.append(index)
        if self.pages[index] is None:
            raise pdfium.PdfiumError(6)
        return self.pages[index]

    def close(self) -> None:
        pass


class FakeLibrary:
    """Opens a file by its bytes; counts the resets, which must precede every open."""

    def __init__(self, documents: dict):
        self.documents = documents
        self.resets = self.opens = 0

    def reset(self) -> None:
        self.resets += 1

    def open(self, data: bytes):
        self.opens += 1
        return self.documents[data]


class RenderCacheTest(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        os.environ["PDFKIT_PDFIUM_RESULTS"] = self.folder.name
        self.before_log, self.after_log = [], []
        self.paths = {}
        for name in ("in", "out"):
            self.paths[name] = Path(self.folder.name) / f"{name}.pdf"
            self.paths[name].write_bytes(name.encode())

    def tearDown(self):
        os.environ.pop("PDFKIT_PDFIUM_RESULTS", None)
        self.folder.cleanup()

    def compare(self, before_pages, after_pages, rectangles=None, digest="ab" * 32):
        self.library = FakeLibrary({b"in": FakeDocument(before_pages, self.before_log),
                                    b"out": FakeDocument(after_pages, self.after_log)})
        request = {"before": str(self.paths["in"]), "after": str(self.paths["out"]), "dpi": 72.0,
                   "pairs": [[i, i] for i in range(len(before_pages))]}
        if digest:
            request["before_digest"] = digest
        if rectangles is not None:
            request["rectangles"] = rectangles
        return render.handle_compare(self.library, request)

    def test_a_cached_input_page_is_not_rendered_again_when_the_output_is_equal(self):
        pages = [page(6, 4, [(1, 1)]), page(6, 4)]
        first = self.compare(pages, pages)
        self.assertEqual(self.before_log, [0, 1])
        second = self.compare(pages, pages)
        self.assertEqual(self.before_log, [0, 1])
        self.assertEqual(first, second)
        self.assertTrue(all(p["identical"] for p in second["pairs"]))
        self.assertEqual(second["before_pages"], 2)

    def test_every_file_is_read_in_a_library_just_reset(self):
        pages = [page(6, 4), page(6, 4, [(2, 2)])]
        self.compare(pages, pages)
        self.assertEqual(self.library.resets, self.library.opens)
        # The input, then the output, each opened once in a library of its own.
        self.assertEqual(self.library.opens, 2)
        # With the input's pages cached and the output equal, only the output is opened.
        self.compare(pages, pages)
        self.assertEqual((self.library.resets, self.library.opens), (1, 1))

    def test_an_open_failure_fails_every_pair_as_before(self):
        before = [page(6, 4), page(6, 4)]
        uncached = self.compare(before, before, digest=None)
        self.library.documents[b"out"] = None
        self.assertTrue(all(p["identical"] for p in uncached["pairs"]))

        class Broken(FakeLibrary):
            def open(self, data):
                if data == b"out":
                    raise pdfium.PdfiumError(3)
                return super().open(data)

        library = Broken({b"in": FakeDocument(before, [])})
        request = {"before": str(self.paths["in"]), "after": str(self.paths["out"]), "dpi": 72.0,
                   "pairs": [[0, 0], [1, 1]], "before_digest": "cd" * 32}
        answer = render.handle_compare(library, request)
        self.assertEqual(answer["before_pages"], 2)
        self.assertIsNone(answer["after_pages"])
        self.assertEqual([p["error"] for p in answer["pairs"]], ["after_open:format"] * 2)

    def test_a_differing_page_answers_as_the_uncached_comparison(self):
        before = [page(20, 10), page(20, 10)]
        after = [page(20, 10, [(5, 4), (15, 0)]), page(20, 10)]
        rectangles = [[(4, 4, 8, 6)], []]
        uncached = self.compare(before, after, rectangles, digest=None)
        self.compare(before, before, rectangles)
        cached = self.compare(before, after, rectangles)
        self.assertEqual(uncached, cached)
        self.assertEqual(cached["pairs"][0]["differing_pixels"], 2)
        self.assertEqual(cached["pairs"][0]["outside_pixels"], 1)
        self.assertTrue(cached["pairs"][1]["stable"])

    def test_a_size_difference_and_a_render_failure_are_kept(self):
        before = [page(6, 4), None]
        after = [page(7, 4), page(6, 4)]
        uncached = self.compare(before, after, digest=None)
        first = self.compare(before, after)
        second = self.compare(before, after)
        self.assertEqual(uncached, first)
        self.assertEqual(first, second)
        self.assertTrue(second["pairs"][0]["size_differs"])
        self.assertEqual(second["pairs"][1]["error"], "render:page")


if __name__ == "__main__":
    unittest.main()
