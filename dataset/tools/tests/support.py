# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# Shared fixtures of the tool tests: the tools folder on the import path and a
# small clean PDF written in code by the synth writer (Helvetica text, a Flate
# content stream, an Info dictionary and an /ID), so no test reads a PDF file.

from __future__ import annotations

import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1]
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

from synth.writer import Name, Pdf, Stream, helvetica, ops, show  # noqa: E402


def clean_pdf(text: str = "Invoice for Jane Doe", xref: str = "table", pages: int = 1,
              extra_page: dict | None = None, catalog: dict | None = None) -> bytes:
    pdf = Pdf()
    font = pdf.add(helvetica())
    page_dicts = []
    for n in range(pages):
        content = pdf.add(Stream(ops(b"BT /F1 12 Tf 72 700 Td " + show(f"{text} page {n + 1}") + b" Tj ET")))
        entry = {"Resources": {"Font": {"F1": font}}, "Contents": content}
        entry.update(extra_page or {})
        page_dicts.append(entry)
    pdf.document(page_dicts, catalog, {"Producer": "pdfkit test fixture", "Title": text})
    return pdf.build(xref)


__all__ = ["clean_pdf", "Name", "Pdf", "Stream", "helvetica", "ops", "show"]
