# Third-party notices

pdfkit is licensed under the Apache License 2.0. It includes the components
below, each under its own permissive licence. Every component listed here has a
row in `third_party/inventory.tsv`, with its version, upstream address and the
SHA-256 of the upstream source. The full licence text of each component is kept
in its folder under `third_party/` and is reproduced in this file when the
component is vendored.

`build/check_third_party.py` checks that this file and the inventory name the
same components.

## 1. zlib

- Use: Flate decoding and encoding.
- Licence: zlib License.
- Upstream: https://zlib.net
- Notice: *(the zlib licence text is reproduced here when the component is vendored)*

## 2. libjpeg-turbo

- Use: DCT (JPEG) decoding, and JPEG re-encoding of treated images.
- Licence: BSD-3-Clause, the IJG License and the zlib License, as stated in
  its `LICENSE.md`.
- Upstream: https://github.com/libjpeg-turbo/libjpeg-turbo
- Notice: *(reproduced here when vendored, including the IJG acknowledgement
  "This software is based in part on the work of the Independent JPEG Group")*

## 3. OpenJPEG

- Use: JPEG 2000 decoding.
- Licence: BSD-2-Clause.
- Upstream: https://github.com/uclouvain/openjpeg
- Notice: *(reproduced here when vendored)*

## 4. Liberation Sans (Regular and Bold)

- Use: the source of the embedded placeholder font subset and of the bitmap
  caption font.
- Licence: SIL Open Font License 1.1, which permits embedding of a font or a
  subset in documents.
- Upstream: https://github.com/liberationfonts/liberation-fonts
- Notice: *(the OFL text and the copyright lines are reproduced here when
  vendored)*

## 5. Adobe Glyph List

- Use: glyph names to Unicode.
- Licence: BSD-3-Clause.
- Upstream: https://github.com/adobe-type-tools/agl-aglfn
- Notice: *(reproduced here when vendored)*

## 6. Core 14 font metrics

- Use: the widths of the 14 standard fonts when a file embeds none.
- Licence: Adobe's AFM notice, which permits redistribution with the notice.
- Upstream: Adobe's Core 14 AFM files.
- Notice: *(reproduced here when vendored)*

## 7. Unicode Character Database

- Use: case folding and canonical decomposition for search folding.
- Licence: Unicode License v3.
- Upstream: https://www.unicode.org/Public/
- Notice: *(reproduced here when vendored)*

## 8. Ported code

These source files were ported from permissively licensed code. Each file's
header names its source.

| pdfkit component | Ported from | Licence |
|---|---|---|
| JBIG2 decoder | pdf.js `src/core/jbig2.js` and `src/core/arithmetic_decoder.js` | Apache-2.0 |
| CCITT decoder | pdf.js `src/core/ccitt.js` | Apache-2.0 |
| Page segmentation (recursive XY-cut, Docstrum) | PdfPig's page segmenters | Apache-2.0 |

## 9. Test-only components (not shipped)

| Component | Use | Licence |
|---|---|---|
| doctest | the unit-test framework | MIT |
| PDFium (bblanchon's prebuilt binaries) | the independent reader of the test tier | BSD-3-Clause and Apache-2.0; packaging MIT |
