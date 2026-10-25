# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# The text-agreement metric of the reading gate: a text becomes a multiset of
# words (NFKC, case-folded, split on every character that is neither a letter
# nor a digit, words of two or more characters kept) and two texts agree by the
# F1 of their multisets, the harmonic mean of multiset precision and recall. The
# metric ignores order on purpose: reading order is judged elsewhere, and two
# readers that see the same words in another order still read the same text.

from __future__ import annotations

import collections
import unicodedata


def words(text: str) -> collections.Counter:
    """The multiset of words of `text`."""
    folded = unicodedata.normalize("NFKC", text or "").casefold()
    out: collections.Counter = collections.Counter()
    current: list[str] = []
    for char in folded:
        if char.isalnum():
            current.append(char)
            continue
        if len(current) >= 2:
            out["".join(current)] += 1
        current = []
    if len(current) >= 2:
        out["".join(current)] += 1
    return out


def f1(candidate: str, reference: str) -> float:
    """F1 of the candidate's words against the reference's; 1.0 when both are empty."""
    a, b = words(candidate), words(reference)
    total_a, total_b = sum(a.values()), sum(b.values())
    if total_a == 0 and total_b == 0:
        return 1.0
    if total_a == 0 or total_b == 0:
        return 0.0
    common = sum((a & b).values())
    precision = common / total_a
    recall = common / total_b
    if common == 0:
        return 0.0
    return 2 * precision * recall / (precision + recall)
