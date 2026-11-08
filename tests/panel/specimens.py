# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# The specimens the reference panel reads to re-decide how a page with no
# /MediaBox is sized. They are written at run time into build/cache/panel/
# specimens/ of the main checkout and never committed, because no PDF is
# committed (build/check_repo.py). Three one-page files with one line of text
# in Helvetica: `no_media_box` (no /MediaBox on the page nor on any ancestor),
# `inherited_media_box` (the control: /MediaBox [0 0 300 400] on the /Pages
# root only, which every reader inherits, ISO 32000-1 table 30) and
# `crop_box_only` (no /MediaBox, a /CropBox [0 0 300 400]). The control's size
# is not a common paper size, so a reader that answers it reads the box and
# does not guess. Each file's cross-reference table is computed from the
# objects' offsets, so the files are well formed apart from the box left out.

from __future__ import annotations

import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import readers  # noqa: E402

CONTENT = b"BT /F1 24 Tf 40 40 Td (Specimen page) Tj ET"
SPECIMENS = {
    "no_media_box": (b"", b""),
    "inherited_media_box": (b"", b" /MediaBox [0 0 300 400]"),
    "crop_box_only": (b" /CropBox [0 0 300 400]", b""),
}


def build(page_extra: bytes, pages_extra: bytes) -> bytes:
    """A one-page PDF whose page and /Pages root carry the given extra entries."""
    objects = [
        b"<< /Type /Catalog /Pages 2 0 R >>",
        b"<< /Type /Pages /Kids [3 0 R] /Count 1" + pages_extra + b" >>",
        b"<< /Type /Page /Parent 2 0 R /Resources << /Font << /F1 5 0 R >> >> /Contents 4 0 R"
        + page_extra + b" >>",
        b"<< /Length " + str(len(CONTENT)).encode("ascii") + b" >>\nstream\n" + CONTENT + b"\nendstream",
        b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
    ]
    out = bytearray(b"%PDF-1.7\n%\xe2\xe3\xcf\xd3\n")
    offsets = []
    for number, body in enumerate(objects, start=1):
        offsets.append(len(out))
        out += f"{number} 0 obj\n".encode("ascii") + body + b"\nendobj\n"
    start = len(out)
    out += f"xref\n0 {len(objects) + 1}\n0000000000 65535 f \n".encode("ascii")
    for offset in offsets:
        out += f"{offset:010d} 00000 n \n".encode("ascii")
    out += f"trailer\n<< /Size {len(objects) + 1} /Root 1 0 R >>\nstartxref\n{start}\n%%EOF\n".encode("ascii")
    return bytes(out)


def folder() -> Path:
    return readers.cache_dir() / "specimens"


def write_all(target: Path | None = None) -> dict[str, Path]:
    """Writes the three specimens and returns their paths by name."""
    target = target or folder()
    target.mkdir(parents=True, exist_ok=True)
    paths = {}
    for name, (page_extra, pages_extra) in SPECIMENS.items():
        path = target / f"{name}.pdf"
        data = build(page_extra, pages_extra)
        if not path.exists() or path.read_bytes() != data:
            path.write_bytes(data)
        paths[name] = path
    return paths


if __name__ == "__main__":
    for specimen, written in write_all().items():
        print(f"{specimen}: {written}")
