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
- Version: 3.2.0, the release of 2026-06-30, from `libjpeg-turbo-3.2.0.tar.gz`
  (SHA-256 equal to the digest GitHub lists for the release asset).
- Upstream: https://github.com/libjpeg-turbo/libjpeg-turbo
- Changes: the vendored copy is the libjpeg API library's sources and headers,
  the three configuration templates, `LICENSE.md`, `README.ijg` and
  `README.md` of the release; the SIMD code, the TurboJPEG API, the programs,
  the documentation and the tests are left out. No file is altered.
- This software is based in part on the work of the Independent JPEG Group.
- Notice (the IJG License, the LEGAL ISSUES section of `README.ijg`):

```
LEGAL ISSUES
============

In plain English:

1. We don't promise that this software works.  (But if you find any bugs,
   please let us know!)
2. You can use this software for whatever you want.  You don't have to pay us.
3. You may not pretend that you wrote this software.  If you use it in a
   program, you must acknowledge somewhere in your documentation that
   you've used the IJG code.

In legalese:

The authors make NO WARRANTY or representation, either express or implied,
with respect to this software, its quality, accuracy, merchantability, or
fitness for a particular purpose.  This software is provided "AS IS", and you,
its user, assume the entire risk as to its quality and accuracy.

This software is copyright (C) 1991-2020, Thomas G. Lane, Guido Vollbeding.
All Rights Reserved except as specified below.

Permission is hereby granted to use, copy, modify, and distribute this
software (or portions thereof) for any purpose, without fee, subject to these
conditions:
(1) If any part of the source code for this software is distributed, then this
README file must be included, with this copyright and no-warranty notice
unaltered; and any additions, deletions, or changes to the original files
must be clearly indicated in accompanying documentation.
(2) If only executable code is distributed, then the accompanying
documentation must state that "this software is based in part on the work of
the Independent JPEG Group".
(3) Permission for use of this software is granted only if the user accepts
full responsibility for any undesirable consequences; the authors accept
NO LIABILITY for damages of any kind.

These conditions apply to any software derived from or based on the IJG code,
not just to the unmodified library.  If you use our work, you ought to
acknowledge us.

Permission is NOT granted for the use of any IJG author's name or company name
in advertising or publicity relating to this software or products derived from
it.  This software may be referred to only as "the Independent JPEG Group's
software".

We specifically permit and encourage the use of this software as the basis of
commercial products, provided that all warranty or liability claims are
assumed by the product vendor.
```

- Notice (the Modified BSD License, from `LICENSE.md`):

```
The Modified (3-clause) BSD License
===================================

Copyright (C) 2009-2026 D. R. Commander<br>
Copyright (C) 2018-2023 Randy <randy408@protonmail.com>

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

- Redistributions of source code must retain the above copyright notice,
  this list of conditions and the following disclaimer.
- Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.
- Neither the name of the libjpeg-turbo Project nor the names of its
  contributors may be used to endorse or promote products derived from this
  software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS",
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
```

## 3. OpenJPEG

- Use: JPEG 2000 decoding.
- Licence: BSD-2-Clause.
- Version: 2.5.4, the release of 2025-09-20, from the tag archive
  `v2.5.4.tar.gz` (the release lists no digest for it; its SHA-256 is pinned in
  `third_party/inventory.tsv`).
- Upstream: https://github.com/uclouvain/openjpeg
- Changes: the vendored copy is the `openjp2` library's sources and headers
  with `LICENSE`, `README.md` and `AUTHORS.md` of the release; the JPIP index
  files, the codec tools, the executables and the tests are left out. No file
  is altered.
- Notice (the `LICENSE` file of the release):

```
/*
 * The copyright in this software is being made available under the 2-clauses 
 * BSD License, included below. This software may be subject to other third 
 * party and contributor rights, including patent rights, and no such rights
 * are granted under this license.
 *
 * Copyright (c) 2002-2014, Universite catholique de Louvain (UCL), Belgium
 * Copyright (c) 2002-2014, Professor Benoit Macq
 * Copyright (c) 2003-2014, Antonin Descampe
 * Copyright (c) 2003-2009, Francois-Olivier Devaux
 * Copyright (c) 2005, Herve Drolon, FreeImage Team
 * Copyright (c) 2002-2003, Yannick Verschueren
 * Copyright (c) 2001-2003, David Janssens
 * Copyright (c) 2011-2012, Centre National d'Etudes Spatiales (CNES), France 
 * Copyright (c) 2012, CS Systemes d'Information, France
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS `AS IS'
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
```

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
| JBIG2 decoder | pdf.js `src/core/jbig2.js` and `src/core/arithmetic_decoder.js` at commit fe48736846e2043696738bd54435ebb5ab24ecff (Copyright 2012 Mozilla Foundation) | Apache-2.0 |
| The simple-font encoding tables (`src/fonts/encodings.cpp`) | pdf.js `src/core/encodings.js` | Apache-2.0 |
| The CFF standard strings and predefined charsets (`src/fonts/cff_font.cpp`) | pdf.js `src/core/cff_parser.js` and `src/core/charsets.js` | Apache-2.0 |
| The 258 standard Macintosh glyph names (`src/fonts/true_type_tables.cpp`) | pdf.js `src/core/fonts_utils.js` | Apache-2.0 |
| CCITT decoder | pdf.js `src/core/ccitt.js` at commit fe48736846e2043696738bd54435ebb5ab24ecff (Copyright 2012 Mozilla Foundation), itself a JavaScript port of xpdf's CCITTFaxStream (Copyright 1996-2003 Glyph & Cog, LLC, made available under the Apache 2.0 licence) | Apache-2.0 |
| The Adobe CMYK to sRGB table and its interpolation (`src/images/adobe_cmyk.cpp`) | PDFium `core/fxge/dib/cfx_cmyk_to_srgb.cpp` at commit 7b3f6f93625e6df97a7d9e677137a558df8648c4 (Copyright 2014 The PDFium Authors; original code copyright 2014 Foxit Software Inc.) | BSD-3-Clause |
| Page segmentation (recursive XY-cut, Docstrum: `src/text/xy_cut.cpp`, `src/text/docstrum.cpp`) | PdfPig's page segmenters `RecursiveXYCut.cs` and `DocstrumBoundingBoxes.cs` at commit e17fc2d577a24db986d8642ec7190188d0f6a0a9 (the algorithm ported, no code copied) | Apache-2.0 |

PDFium's licence, which covers the Adobe CMYK table ported above (the
BSD-3-Clause part of PDFium's `LICENSE` file):

```
Copyright 2014 The PDFium Authors

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

   * Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.
   * Redistributions in binary form must reproduce the above
copyright notice, this list of conditions and the following disclaimer
in the documentation and/or other materials provided with the
distribution.
   * Neither the name of Google Inc. nor the names of its
contributors may be used to endorse or promote products derived from
this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

## 9. Test-only components (not shipped)

| Component | Use | Licence |
|---|---|---|
| doctest | the unit-test framework | MIT |
| PDFium (bblanchon's prebuilt binaries) | the independent reader of the test tier | BSD-3-Clause and Apache-2.0; packaging MIT |
