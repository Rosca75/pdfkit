# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# Tests of the mutator: the same source and seed give the same bytes, and each
# mutation does to a clean file written in code what its name says, checked by
# a small reader of its own rather than by the scanner the mutation feeds.

from __future__ import annotations

import random
import re
import unittest
import zlib

import support

import autotag
import mutate
import pdfcrypt


def rng(seed: int = 7) -> random.Random:
    return random.Random(seed)


class DeterminismTest(unittest.TestCase):
    def test_every_mutation_is_deterministic_by_seed(self):
        source = support.clean_pdf(pages=2)
        for name, (fn, _, _, need) in mutate.MUTATIONS.items():
            if need == "linearised":
                continue
            with self.subTest(name):
                self.assertEqual(fn(source, rng(11)), fn(source, rng(11)))

    def test_seed_depends_on_source_and_mutation(self):
        self.assertNotEqual(mutate.seed_for("a-0001", "truncated"), mutate.seed_for("a-0002", "truncated"))
        self.assertEqual(mutate.seed_for("a-0001", "aesv2"), mutate.seed_for("a-0001", "aesv2"))


class EffectTest(unittest.TestCase):
    source = support.clean_pdf(pages=2)

    def startxref(self, data: bytes) -> int:
        return int(re.findall(rb"startxref\s+(\d+)", data)[-1])

    def test_xref_missing_removes_the_table(self):
        out = mutate.m_xref_missing(self.source, rng())
        self.assertIsNone(re.search(rb"(?<!start)xref\s+\d+\s+\d+", out))
        self.assertIn(b"trailer", out)

    def test_startxref_wrong_misses_the_table(self):
        out = mutate.m_startxref_wrong(self.source, rng())
        self.assertNotEqual(out[self.startxref(out):self.startxref(out) + 4], b"xref")

    def test_xref_wrong_offsets_moves_entries(self):
        out = mutate.m_xref_wrong_offsets(self.source, rng())
        self.assertEqual(len(out), len(self.source))
        self.assertTrue(autotag.wrong_offsets(out, self.startxref(out), 0))
        self.assertFalse(autotag.wrong_offsets(self.source, self.startxref(self.source), 0))

    def test_eof_missing(self):
        self.assertNotIn(b"%%EOF", mutate.m_eof_missing(self.source, rng()))

    def test_junk_before_header_under_and_over_1024(self):
        short, long = mutate.m_junk_short(self.source, rng()), mutate.m_junk_long(self.source, rng())
        self.assertTrue(0 < short.find(b"%PDF-") < 1024)
        self.assertGreater(long.find(b"%PDF-"), 1024)

    def test_length_wrong_keeps_every_offset(self):
        for fn in (mutate.m_length_short, mutate.m_length_long):
            out = fn(self.source, rng())
            self.assertEqual(len(out), len(self.source))
            lengths = lambda d: re.findall(rb"/Length (\d+)", d)
            self.assertNotEqual(lengths(out), lengths(self.source))

    def test_keywords_blanked(self):
        self.assertEqual(mutate.m_endstream_missing(self.source, rng()).count(b"endstream"),
                         self.source.count(b"endstream") - 1)
        count = lambda d: len(re.findall(rb"(?<!end)endobj", d))
        self.assertEqual(count(mutate.m_endobj_missing(self.source, rng())), count(self.source) - 1)

    def test_two_trailers(self):
        self.assertEqual(mutate.m_two_trailers(self.source, rng()).count(b"trailer"), 2)

    def test_truncated_is_shorter(self):
        out = mutate.m_truncated(self.source, rng())
        self.assertLess(len(out), len(self.source))
        self.assertTrue(self.source.startswith(out))

    def test_damaged_flate_breaks_one_stream(self):
        out = mutate.m_damaged_flate(support.clean_pdf("x" * 400), rng())
        streams = re.findall(rb"stream\n(.*?)\nendstream", out, re.S)
        broken = 0
        for data in streams:
            try:
                zlib.decompress(data)
            except zlib.error:
                broken += 1
        self.assertEqual(broken, 1)

    def test_incremental_update_appends_a_revision(self):
        out = mutate.m_incremental_update(self.source, rng())
        self.assertTrue(out.startswith(self.source))
        self.assertEqual(out.count(b"%%EOF"), 2)
        self.assertIn(b"/Prev", out[len(self.source):])

    def test_object_streams_rewrite(self):
        out = mutate.m_object_streams(self.source, rng())
        tags, _ = autotag.autotag(out)
        self.assertIn("object_streams", tags)
        self.assertIn("xref_stream", tags)
        self.assertNotIn("startxref_wrong", tags)

    def test_encryptions_name_their_revision(self):
        for name, revision in (("rc4_40", 2), ("rc4_128", 3), ("aesv2", 4), ("aesv3_r5", 5), ("aesv3_r6", 6)):
            with self.subTest(name):
                out = mutate.MUTATIONS[name][0](self.source, rng())
                self.assertRegex(out, rb"/R %d\b" % revision)
                self.assertNotIn(b"Invoice for Jane Doe", out)
                tags, _ = autotag.autotag(out)
                self.assertIn(name, tags)

    def test_encrypt_metadata_false_is_flagged(self):
        out = mutate.MUTATIONS["encrypt_metadata_false"][0](self.source, rng())
        self.assertIn(b"/EncryptMetadata false", out)

    def test_user_password_r6_does_not_open_with_the_empty_password(self):
        out = mutate.MUTATIONS["user_password_r6"][0](self.source, rng())
        u = bytes.fromhex(re.search(rb"/U <([0-9a-f]+)>", out).group(1).decode())
        self.assertNotEqual(pdfcrypt.hash_r6(b"", u[32:40], b""), u[:32])
        opened = mutate.MUTATIONS["aesv3_r6"][0](self.source, rng())
        u = bytes.fromhex(re.search(rb"/U <([0-9a-f]+)>", opened).group(1).decode())
        self.assertEqual(pdfcrypt.hash_r6(b"", u[32:40], b""), u[:32])


if __name__ == "__main__":
    unittest.main()
