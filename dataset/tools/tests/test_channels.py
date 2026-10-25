# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# Tests of the planted-marker synthesiser: every row of the channel table has a
# pattern, each specimen plants markers of its own row only (found by a byte scan
# of the raw file or of a decoded stream), the visible line never holds a marker,
# two builds write the same bytes, and the leak fixture owns a marker of each
# page-owned channel for every one of its pages.

from __future__ import annotations

import re
import unittest

import support  # noqa: F401

import common
import synth.run as synth_run
from synth.channels import kit, leak, rows

ROW_TAGS = set(kit.ROWS.values())


def build_all() -> dict:
    table = synth_run.pattern_table()
    return {name: fn() for name, fn in table.items() if name.startswith("mark_")}


class RowsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.specimens = build_all()

    def test_every_row_has_a_pattern(self):
        names = set(self.specimens)
        for tag in ROW_TAGS:
            self.assertIn(f"mark_{tag}", names, tag)
        self.assertIn("mark_ch_page_owned", names)

    def test_the_row_budget_holds(self):
        total = sum(len(v) for v in self.specimens.values())
        self.assertLessEqual(total, 30)

    def test_each_specimen_plants_its_own_row_only(self):
        for name, specimens in self.specimens.items():
            if name == "mark_ch_page_owned":
                continue
            row = next(r for r, tag in kit.ROWS.items() if f"mark_{tag}" == name)
            for specimen in specimens:
                found = kit.find_markers(kit.readable_blobs(specimen.data))
                self.assertTrue(found, name)
                self.assertEqual({int(m[10:12]) for m in found}, {row}, name)
                self.assertIn(kit.ROWS[row], specimen.tags)

    def test_visible_text_holds_no_marker(self):
        for name, specimens in self.specimens.items():
            for specimen in specimens:
                self.assertTrue(specimen.text.strip(), name)
                if name != "mark_ch_page_owned":
                    self.assertNotIn(kit.PREFIX, specimen.text)

    def test_markers_are_literal_strings_or_stream_data(self):
        # A marker never appears as a hex string or a name, so no marker is preceded by `/` or `<`.
        for specimens in self.specimens.values():
            for specimen in specimens:
                for blob in kit.readable_blobs(specimen.data):
                    for match in kit.MARK_RE.finditer(blob):
                        self.assertNotIn(blob[match.start() - 1:match.start()], (b"/", b"<"))

    def test_two_builds_write_the_same_bytes(self):
        again = build_all()
        for name, specimens in self.specimens.items():
            self.assertEqual([s.data for s in specimens], [s.data for s in again[name]], name)

    def test_revisions_keep_the_old_content_behind_an_update(self):
        data = self.specimens["mark_ch_revisions"][0].data
        self.assertEqual(data.count(b"%%EOF"), 2)
        self.assertIn(b"/Prev", data)

    def test_the_encrypted_specimens_keep_the_marker_in_clear(self):
        for specimen in self.specimens["mark_ch_encrypt"]:
            self.assertIn(b"/Encrypt", specimen.data)
            self.assertRegex(specimen.data, rb"/PDFKitVendor \(PDFKITMARK21X0[12]\)")

    def test_the_thumbnail_samples_carry_the_marker_bytes(self):
        data = self.specimens["mark_ch_thumbnail"][0].data
        self.assertIn(b"PDFKITMARK03X01", data)

    def test_named_destination_marker_is_a_string_key(self):
        data = self.specimens["mark_ch_named_dest"][0].data
        self.assertIn(b"(PDFKITMARK12X01)", data)
        self.assertNotIn(b"/PDFKITMARK12X01", data)


class LeakFixtureTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.specimens = leak.ch_page_owned()

    def test_two_specimens_with_both_cross_reference_forms(self):
        self.assertEqual(len(self.specimens), 2)
        self.assertIn(b"\nxref\n", self.specimens[0].data)
        self.assertIn(b"/Type /XRef", self.specimens[1].data)

    def test_every_page_owns_a_marker_in_every_channel(self):
        for specimen in self.specimens:
            found = kit.find_markers(kit.readable_blobs(specimen.data))
            for page in range(1, leak.PAGES + 1):
                for row in (1, 2, 3, 9, 13, 14, 15, 90, 91, 92, 93):
                    self.assertIn(kit.mark(row, page), found)
            self.assertEqual({int(m[10:12]) for m in found}, {1, 2, 3, 9, 13, 14, 15, 90, 91, 92, 93})

    def test_tags_name_the_planted_channels(self):
        for specimen in self.specimens:
            self.assertIn("ch_page_owned", specimen.tags)
            for tag in ("ch_revisions", "ch_unreferenced", "ch_thumbnail", "ch_annotation",
                        "ch_struct_tree", "ch_optional_content", "ch_page_labels"):
                self.assertIn(tag, specimen.tags)

    def test_drawn_markers_are_in_the_expected_text(self):
        for specimen in self.specimens:
            for page in range(1, leak.PAGES + 1):
                for row in (90, 91, 93):
                    self.assertIn(kit.mark(row, page), specimen.text)
                self.assertNotIn(kit.mark(92, page), specimen.text)

    def test_the_old_content_of_each_page_is_still_in_the_file(self):
        for specimen in self.specimens:
            self.assertEqual(len(re.findall(rb"%%EOF", specimen.data)), 2)


class MarkerTest(unittest.TestCase):
    def test_format_has_two_digit_row_and_counter(self):
        self.assertEqual(kit.mark(3), "PDFKITMARK03X01")
        self.assertEqual(kit.mark(90, 4), "PDFKITMARK90X04")

    def test_verify_rejects_an_unfindable_marker(self):
        with self.assertRaises(common.ToolError):
            kit.verify(b"%PDF-1.7\n", [kit.mark(1)])

    def test_verify_rejects_another_rows_marker(self):
        with self.assertRaises(common.ToolError):
            kit.verify(b"x " + kit.mark_bytes(2) + b" y", [], {1})


if __name__ == "__main__":
    unittest.main()
