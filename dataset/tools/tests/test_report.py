# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# Tests of the coverage report: counting per tag against the minimum, tags
# missing from the taxonomy, rows outside an admitted set, and the committed
# taxonomy holding every tag group the charter names.

from __future__ import annotations

import unittest

import support  # noqa: F401

import common
import report


def row(row_id: str, tags: str, set_name: str = "synth", licence: str = "Apache-2.0") -> dict:
    return {"id": row_id, "tags": tags, "set": set_name, "licence": licence}


class CoverageTest(unittest.TestCase):
    taxonomy = [{"tag": "type3", "group": "fonts", "minimum": "1", "detector": "autotag"},
                {"tag": "table", "group": "layout", "minimum": "3", "detector": "synth"}]

    def test_counts_against_minimum(self):
        cov = {c["tag"]: c for c in report.coverage([row("a", "type3,table"), row("b", "table")], self.taxonomy)}
        self.assertTrue(cov["type3"]["ok"])
        self.assertFalse(cov["table"]["ok"])
        self.assertEqual(cov["table"]["count"], 2)

    def test_unknown_tags_are_named(self):
        self.assertEqual(report.unknown_tags([row("a", "type3,bogus")], self.taxonomy), {"bogus": ["a"]})

    def test_licence_problems(self):
        sources = [{"set": "synth", "status": "admitted"}, {"set": "corkami", "status": "licence_unclear"}]
        problems = report.licence_problems([row("a", "", "corkami"), row("b", "", "synth", "")], sources)
        self.assertEqual(len(problems), 2)


class CommittedTablesTest(unittest.TestCase):
    def test_taxonomy_holds_every_group(self):
        groups = {t["group"] for t in common.read_taxonomy()}
        self.assertEqual(groups, {"damage", "encryption", "fonts", "layout", "hidden", "images", "channels",
                                  "generators", "traps"})

    def test_rare_tags_have_minimum_one(self):
        minimum = {t["tag"]: t["minimum"] for t in common.read_taxonomy()}
        for tag in ("vertical", "right_to_left", "aesv3_r5", "img_jbig2", "img_jpx", "type3", "deniable_removal"):
            self.assertEqual(minimum[tag], "1", tag)


if __name__ == "__main__":
    unittest.main()
