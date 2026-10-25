# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# Unit tests of the text-agreement metric on pairs whose score is known by hand,
# including the perturbation the self-test relies on (one word in ten dropped
# scores 2 * 0.9 / 1.9 = 0.947).

from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import compare  # noqa: E402
import words  # noqa: E402


class WordsTest(unittest.TestCase):
    def test_split_folds_and_drops_short_words(self):
        bag = words.words("The IBAN: LU28-0019, a b CD Ｆｕｌｌ")
        self.assertEqual(bag, {"the": 1, "iban": 1, "lu28": 1, "0019": 1, "cd": 1, "full": 1})

    def test_ligature_folds_through_nfkc(self):
        self.assertEqual(words.f1("ﬁnance oﬃce", "finance office"), 1.0)

    def test_identical_and_reordered_texts_agree(self):
        self.assertEqual(words.f1("alpha beta gamma", "gamma alpha beta"), 1.0)

    def test_disjoint_texts_score_zero(self):
        self.assertEqual(words.f1("alpha beta", "gamma delta"), 0.0)

    def test_both_empty_agree_one_empty_does_not(self):
        self.assertEqual(words.f1("", " a "), 1.0)
        self.assertEqual(words.f1("", "alpha"), 0.0)

    def test_multiset_counts_repeats(self):
        # precision 2/3, recall 1 -> F1 0.8
        self.assertAlmostEqual(words.f1("alpha alpha alpha", "alpha alpha"), 0.8)

    def test_one_word_in_ten_dropped(self):
        text = " ".join(f"word{i:03d}" for i in range(100))
        self.assertAlmostEqual(words.f1(compare.drop_every_tenth(text), text), 2 * 0.9 / 1.9, places=6)


if __name__ == "__main__":
    unittest.main()
