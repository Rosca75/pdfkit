# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# Shared pieces of the pattern modules: the sample texts every pattern draws
# (invented names and numbers, never a real person's), the Info dictionary the
# synthesised files carry, and the one-call helpers that turn a content stream
# and its resources into a finished file.

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from synth.run import Specimen  # noqa: E402,F401  (re-exported for the pattern modules)
from synth.writer import Name, Pdf, Stream, helvetica, ops, show  # noqa: E402,F401

TEXTS = (
    "Invoice 2026 issued to Jane Doe of Example Street",
    "Account IBAN LU28 0019 4006 4475 0000 held by Carl Sample",
    "The quick brown fox jumps over the lazy dog",
    "Redaction keeps the layout of every page intact",
    "Contract number 4711 signed by Maria Testing",
    "Quarterly report for the Fictional Holding company",
)

PRODUCER = "pdfkit dataset synthesiser"


def info(title: str) -> dict:
    return {"Producer": PRODUCER, "Title": title}


def page(pdf: Pdf, content: bytes, resources: dict | None = None, **extra) -> dict:
    """A page dictionary with its content stream added to `pdf`."""
    entry = {"Resources": resources or {}, "Contents": pdf.add(Stream(content))}
    entry.update(extra)
    return entry


def single(content: bytes, resources: dict | None = None, title: str = "synth",
           xref: str = "table", page_extra: dict | None = None,
           catalog_extra: dict | None = None, pdf: Pdf | None = None) -> bytes:
    """A one-page file from one content stream."""
    pdf = pdf or Pdf()
    pdf.document([page(pdf, content, resources, **(page_extra or {}))], catalog_extra, info(title))
    return pdf.build(xref)
