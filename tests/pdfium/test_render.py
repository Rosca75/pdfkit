# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# Unit tests of the pixel comparison on synthetic bitmaps (no PDFium needed): an
# exact comparison counts differing pixels, a size difference is reported as such,
# and a comparison outside rectangles maps the page-space rectangle (points,
# lower-left origin) to pixels at 100 dpi, widens it by one pixel and counts only
# the differences beyond it.

from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import render  # noqa: E402

WHITE = bytes([255, 255, 255, 255])
BLACK = bytes([0, 0, 0, 255])


def bitmap(width: int, height: int, dots=(), stride_padding: int = 0) -> render.Bitmap:
    """A white bitmap with black pixels at the (x, y) of `dots` (top-left origin)."""
    rows = []
    for y in range(height):
        row = bytearray(WHITE * width)
        for dot_x, dot_y in dots:
            if dot_y == y:
                row[4 * dot_x:4 * dot_x + 4] = BLACK
        rows.append(bytes(row) + bytes(stride_padding))
    return render.Bitmap(width, height, width * 4 + stride_padding, b"".join(rows))


class ExactTest(unittest.TestCase):
    def test_equal_bitmaps_are_identical(self):
        answer = render.compare_bitmaps(bitmap(5, 4, [(1, 1)]), bitmap(5, 4, [(1, 1)]))
        self.assertTrue(answer["identical"])
        self.assertEqual(answer["differing_pixels"], 0)
        self.assertFalse(answer["size_differs"])
        self.assertIsNone(answer["error"])

    def test_every_differing_pixel_is_counted(self):
        answer = render.compare_bitmaps(bitmap(5, 4, [(1, 1)]), bitmap(5, 4, [(1, 1), (3, 2), (4, 3)]))
        self.assertFalse(answer["identical"])
        self.assertEqual(answer["differing_pixels"], 2)

    def test_a_size_difference_is_not_a_pixel_count(self):
        answer = render.compare_bitmaps(bitmap(5, 4), bitmap(6, 4))
        self.assertTrue(answer["size_differs"])
        self.assertFalse(answer["identical"])
        self.assertIsNone(answer["differing_pixels"])

    def test_row_padding_does_not_count(self):
        answer = render.compare_bitmaps(bitmap(3, 2, stride_padding=0), bitmap(3, 2, stride_padding=8))
        self.assertTrue(answer["identical"])


class RectangleTest(unittest.TestCase):
    # 72 dpi makes a point one pixel, so the arithmetic reads off the bitmap.
    def boxes(self, rectangles, width=20, height=10, dilation=1):
        return render.pixel_boxes(rectangles, width, height, 72.0, dilation)

    def test_lower_left_origin_maps_to_top_left_rows(self):
        # Points y 0..2 are the bottom two pixel rows of a 10-row bitmap (rows 8 and 9).
        self.assertEqual(self.boxes([(4, 0, 8, 2)], dilation=0), [(4, 8, 8, 10)])

    def test_the_box_widens_by_one_pixel_and_stays_inside_the_bitmap(self):
        self.assertEqual(self.boxes([(4, 4, 8, 6)]), [(3, 3, 9, 7)])
        self.assertEqual(self.boxes([(0, 0, 20, 10)]), [(0, 0, 20, 10)])

    def test_scale_follows_the_dpi(self):
        # At 144 dpi a point is two pixels.
        boxes = render.pixel_boxes([(2, 2, 4, 4)], 40, 20, 144.0, 0)
        self.assertEqual(boxes, [(4, 12, 8, 16)])

    def test_a_difference_inside_the_rectangle_is_tolerated(self):
        before = bitmap(20, 10)
        after = bitmap(20, 10, [(5, 4)])
        answer = render.compare_bitmaps(before, after, [(4, 4, 8, 6)], 72.0)
        self.assertEqual(answer["differing_pixels"], 1)
        self.assertEqual(answer["outside_pixels"], 0)
        self.assertTrue(answer["stable"])
        self.assertFalse(answer["identical"])

    def test_one_pixel_of_dilation_is_tolerated_and_two_is_not(self):
        # The rectangle covers x 4..8, y rows 4..6 (points 4..6 of 10 rows).
        rect = [(4, 4, 8, 6)]
        before = bitmap(20, 10)
        near = render.compare_bitmaps(before, bitmap(20, 10, [(3, 3)]), rect, 72.0)
        far = render.compare_bitmaps(before, bitmap(20, 10, [(2, 3)]), rect, 72.0)
        self.assertEqual(near["outside_pixels"], 0)
        self.assertEqual(far["outside_pixels"], 1)
        self.assertFalse(far["stable"])

    def test_only_the_outside_differences_are_counted(self):
        before = bitmap(20, 10)
        after = bitmap(20, 10, [(5, 4), (15, 0), (18, 9)])
        answer = render.compare_bitmaps(before, after, [(4, 4, 8, 6)], 72.0)
        self.assertEqual(answer["differing_pixels"], 3)
        self.assertEqual(answer["outside_pixels"], 2)

    def test_no_rectangle_means_everything_is_outside(self):
        answer = render.compare_bitmaps(bitmap(4, 4), bitmap(4, 4, [(1, 1)]), [], 72.0)
        self.assertEqual(answer["outside_pixels"], 1)
        self.assertFalse(answer["stable"])

    def test_a_size_difference_is_never_stable(self):
        answer = render.compare_bitmaps(bitmap(4, 4), bitmap(5, 4), [(0, 0, 4, 4)], 72.0)
        self.assertFalse(answer["stable"])
        self.assertTrue(answer["size_differs"])


class FailureTest(unittest.TestCase):
    def test_a_failure_names_its_cause(self):
        answer = render.failure("timeout", "no answer within 30 s")
        self.assertFalse(answer["identical"])
        self.assertEqual(answer["error"], "timeout:no answer within 30 s")
        self.assertIsNone(answer["differing_pixels"])


if __name__ == "__main__":
    unittest.main()
