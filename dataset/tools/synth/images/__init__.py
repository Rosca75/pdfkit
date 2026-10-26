# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# The image treatment synthesiser: files that hold together what public files
# rarely do, so the seeded image treatment job of tests/dataset/outcome.md
# meets every case of docs/DESIGN.md section 14 in one file: one image drawn
# on three pages and inside a form, an inline image, a soft mask, a
# colour-key mask, a stencil, an Indexed and a 16-bit image, and the codec
# sources (a CMYK JPEG, JPEG 2000, JBIG2 with globals, CCITT Group 4) taken
# from permissively licensed dataset files. The module holding the PATTERNS
# table is treatments.py.
