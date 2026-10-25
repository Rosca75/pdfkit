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

- Use: glyph names to Unicode (`glyphlist.txt`, 4,281 names, and the ITC Zapf
  Dingbats list `zapfdingbats.txt`, 201 names), compiled into
  `src/fonts/generated/glyph_list_table.cpp`.
- Licence: BSD-3-Clause.
- Version: Adobe Glyph List 2.0, at commit
  4036a9ca80a62f64f9de4f7321a9a045ad0ecfd6 of the repository.
- Upstream: https://github.com/adobe-type-tools/agl-aglfn
- Notice (the `LICENSE.md` of the repository; each list carries the copyright
  line "Copyright 2002-2019 Adobe (http://www.adobe.com/)" in its header):

```
Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.

Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in the
documentation and/or other materials provided with the distribution.

Neither the name of Adobe nor the names of its contributors may be
used to endorse or promote products derived from this software without
specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

## 6. Core 14 font metrics

- Use: the widths, built-in encodings and vertical metrics of the 14 standard
  fonts when a file embeds none, reduced to font, glyph name, code and width in
  `src/fonts/generated/core14_table.cpp` (the AFM files are reduced, not
  modified; the fourteen files and `MustRead.html` are kept unmodified in
  `third_party/core14/`).
- Licence: Adobe's AFM notice (`LicenseRef-Adobe-AFM`), which permits use,
  copying and distribution for any purpose with the notice.
- Version: the AFM files as Apache PDFBox carries them, at commit
  19d3a6799f829051e8efb27197af59935fd8179b; each file's SHA-256 is pinned in
  `build/fonts/core14.lock`.
- Upstream: https://github.com/apache/pdfbox (`pdfbox/src/main/resources/org/apache/pdfbox/resources/afm/`)
- Notice (`MustRead.html`, which accompanies the AFM files; each AFM file
  carries "Copyright (c) 1985, 1987, 1989, 1990, 1997 Adobe Systems
  Incorporated. All Rights Reserved." or the like in its header):

```
This file and the 14 PostScript(R) AFM files it accompanies may be used,
copied, and distributed for any purpose and without charge, with or without
modification, provided that all copyright notices are retained; that the AFM
files are not distributed without this file; that all modifications to this
file or any of the AFM files are prominently noted in the modified file(s);
and that this paragraph is not modified. Adobe Systems has no responsibility
or obligation to support the use of the AFM files.
```

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
| The simple-font encoding tables (`src/fonts/encodings.cpp`) | pdf.js `src/core/encodings.js` | Apache-2.0 |
| The CFF standard strings and predefined charsets (`src/fonts/cff_font.cpp`) | pdf.js `src/core/cff_parser.js` and `src/core/charsets.js` | Apache-2.0 |
| The 258 standard Macintosh glyph names (`src/fonts/true_type_tables.cpp`) | pdf.js `src/core/fonts_utils.js` | Apache-2.0 |
| CCITT decoder | pdf.js `src/core/ccitt.js` | Apache-2.0 |
| Page segmentation (recursive XY-cut, Docstrum) | PdfPig's page segmenters | Apache-2.0 |

## 9. Test-only components (not shipped)

| Component | Use | Licence |
|---|---|---|
| doctest | the unit-test framework | MIT |
| PDFium (bblanchon's prebuilt binaries) | the independent reader of the test tier | BSD-3-Clause and Apache-2.0; packaging MIT |
