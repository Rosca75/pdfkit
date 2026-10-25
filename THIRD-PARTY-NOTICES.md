# Third-party notices

pdfkit is licensed under the Apache License 2.0. It includes the components
below, each under its own permissive licence. Every vendored component has a
row in `third_party/inventory.tsv`, with its version, upstream address and the
SHA-256 of the upstream source, and a section here; its full licence text is
kept in its folder under `third_party/` and reproduced in its section.

`build/check_third_party.py` checks that every inventory component is named in
this file.

## 1. zlib

- Use: Flate decoding and encoding.
- Licence: zlib License.
- Version: 1.3.2, the release of 2026-02-17, from `zlib-1.3.2.tar.gz`
  (SHA-256 on the release page, checked against the archive).
- Upstream: https://zlib.net
- Notice (the `LICENSE` file of the release):

```
Copyright notice:

 (C) 1995-2026 Jean-loup Gailly and Mark Adler

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.

  Jean-loup Gailly        Mark Adler
  jloup@gzip.org          madler@alumni.caltech.edu
```

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
