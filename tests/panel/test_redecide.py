# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# Tests of the re-decisions' pure parts (redecide.py) over values written in
# code, so no member and no pdfkit executable runs: the Type 3 check that a
# member's characters beyond pdfkit's are the codes pdfkit counts unreadable,
# by count and by value, and the reading of /Cxx names that restate their code.
# Run: python -m unittest discover -s tests/panel -p "test_*.py".

from __future__ import annotations

import collections
import sys
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import redecide  # noqa: E402


def glyph(code: int, rung: str = "unreadable") -> dict:
    return {"code": code, "rung": rung}


class CodeCheckTest(unittest.TestCase):
    GLYPHS = [glyph(0x50), glyph(0x72), glyph(0x6F), glyph(0x41, "glyph_name"), glyph(0x20)]

    def test_the_expected_characters_are_the_unreadable_codes(self):
        expected = redecide.expected_characters(self.GLYPHS)
        self.assertEqual(expected, collections.Counter("pro"))

    def test_a_member_reading_the_codes_passes_on_count_and_value(self):
        expected = redecide.expected_characters(self.GLYPHS)
        self.assertEqual(redecide.code_check(collections.Counter("pro"), expected),
                         {"count": True, "value": True, "surplus": 3, "expected": 3, "f1": 1.0})

    def test_other_characters_of_the_same_count_fail_on_value(self):
        expected = redecide.expected_characters(self.GLYPHS)
        self.assertEqual(redecide.code_check(collections.Counter("xyz"), expected),
                         {"count": True, "value": False, "surplus": 3, "expected": 3, "f1": 0.0})
        self.assertEqual(redecide.code_check(collections.Counter("pr"), expected)["count"], False)

    def test_names_restating_their_code(self):
        self.assertEqual(redecide.restating(b"/Differences [65 /C41 /C42 80 /C50]"), {"hexadecimal": 3})
        self.assertEqual(redecide.restating(b"/Differences [9 /C9 10 /C10]"), {"hexadecimal": 1, "decimal": 2})
        self.assertEqual(redecide.restating(b"/Differences [65 /C99]"), {"neither": 1})


if __name__ == "__main__":
    unittest.main()
