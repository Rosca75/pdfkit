# SOURCES.md

This file owns the external sources pdfkit relies on: every URL behind a rule of
`CLAUDE.md` or `docs/DESIGN.md`, grouped by the chapter that relies on it, with
the status the fact was taken at. **read** means the source was opened and read
on 2026-10-24 by the research behind the design; **recalled** means it is cited
from memory or a secondary account and is verified when the code that relies on
it is written, which then changes the status here in the same change. A code
comment that implements an external fact names the source; this file says where
to find it.

## 1. The reasons (CLAUDE.md §5)

| Fact | Source | Status |
|---|---|---|
| PDFium's page regeneration writes RGB and Gray colours only, no shading operators, ExtGState `ca`/`CA`/`BM` only, an early return for Type 3 | https://pdfium.googlesource.com/pdfium/+/refs/heads/main/core/fpdfapi/edit/cpdf_pagecontentgenerator.cpp | read |
| PDFium has no setter for metadata beside `FPDF_GetMetaText`; the structure tree is read-only | https://raw.githubusercontent.com/chromium/pdfium/main/public/fpdf_edit.h, `fpdf_text.h`, `fpdf_annot.h`, `fpdf_save.h`, `fpdf_doc.h` | read |
| PDFium editing details and losses | https://raw.githubusercontent.com/chromium/pdfium/main/fpdfsdk/fpdf_edittext.cpp; https://github.com/embedpdf/runtime/issues/59; https://github.com/embedpdf/runtime/pull/62; https://github.com/SlyWombat/MegaPDF/issues/119; https://github.com/SlyWombat/MegaPDF/issues/141; https://groups.google.com/g/pdfium-bugs/c/RBwhmdbejRk | read |
| PdfPig reads well and makes "very limited changes" to existing files | https://github.com/UglyToad/PdfPig | read |
| PDFsharp 6.2.4 is the last MIT release; later releases are AGPL-3.0 or commercial | https://api.nuget.org/v3/registration5-gz-semver2/pdfsharp/index.json; https://docs.pdfsharp.net/General/License/License-FAQ.html | read |
| QPDF "knows nothing about the semantics of PDF content streams" | https://qpdf.readthedocs.io/en/stable/overview.html; https://github.com/qpdf/qpdf | read |
| pdf.js `saveDocument` is an incremental update; `getTextContent` yields runs, not glyphs | https://registry.npmjs.org/pdfjs-dist/latest; https://mozilla.github.io/pdf.js/api/draft/module-pdfjsLib-PDFDocumentProxy.html | read |
| Disqualified by licence: iText, MuPDF and PyMuPDF, Ghostscript, Poppler, PdfClown | https://github.com/itext/itext-dotnet; https://artifex.com/licensing; https://sourceforge.net/p/clown | read |
| MuPDF's redaction filter keeps the advance and closes the hole with a `TJ` adjustment or a fresh text matrix (technique only, AGPL) | https://raw.githubusercontent.com/ArtifexSoftware/mupdf/master/source/pdf/pdf-op-filter.c; https://raw.githubusercontent.com/ArtifexSoftware/mupdf/master/source/pdf/pdf-clean.c | read |
| UniPDF closes the hole with a width adjustment | https://docs.unidoc.io/unipdf/guides/redaction/ | read |
| Vendors draw overlay text in their own font sized to the hole | https://helpx.adobe.com/acrobat/desktop/protect-documents/redact-pdfs/text-redaction-properties.html; https://docs.apryse.com/api/PDFTronSDK/cpp/classpdftron_1_1_p_d_f_1_1_appearance.html; https://pymupdf.readthedocs.io/en/latest/page.html | read |
| The NSA redaction guidance | https://www.nextgov.com/digital-government/2009/12/in-wake-of-tsa-breach-a-refresher-on-redacting-pdfs/203197/; https://www.cs.columbia.edu/~Smb/doc/Redaction-of-PDF-Files-Using-Adobe-Acrobat-Professional-X.pdf | read |
| The Manafort filing: boxes over live text, produced with PDFium | https://eclecticlight.co/2019/03/11/pdf-without-adobe-17-unredacting-manaforts-documents-and-recovering-pdf-versions/ | read |
| The 2025 to 2026 DOJ releases (a vendor's account, secondary) | https://www.redactable.com/blog/how-epstein-files-redactions-were-uncovered-timeline-2025-26 | read |
| The incremental-update trap | https://blog.didierstevens.com/2008/05/07/; https://www.nutrient.io/blog/incremental-and-full-save-in-pdfs/ | read |
| The width side channel: redacted names recovered from glyph positions in eleven tools | https://petsymposium.org/popets/2023/popets-2023-0069.php | read |
| Re-anchoring the rest of the line as an implementation reference | https://docs.rs/crate/pdf_oxide/0.3.76/source/src/redaction/text_prune.rs | read |
| TrueType encoding heuristics are "not described here" | ISO 32000-1 §9.6.6.4, https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/PDF32000_2008.pdf | read |
| PdfPig's nearest-neighbour word extractor has a non-deterministic tie order | https://github.com/UglyToad/PdfPig/blob/master/NearestNeighbourWordExtractor-review.md | read |
| PDFBox on text replacement: "the incorrect illusion that text can be replaced easily" | https://pdfbox.apache.org/2.0/migration.html | read |
| Other in-place replacement attempts and their limits | https://github.com/JoshData/pdf-redactor; https://github.com/pdfcpu/pdfcpu/issues/4; https://github.com/Hopding/pdf-lib/issues/1247 | read |

## 2. The specifications

| Fact | Source | Status |
|---|---|---|
| PDF 1.7: §7.2 lexical conventions, §7.3 objects, §7.4 filters, §7.5 file structure (§7.5.4 table, §7.5.5 trailer, §7.5.6 incremental updates, §7.5.7 object streams, §7.5.8 cross-reference streams), §7.6 encryption, §7.7.3 page tree, §8.4 graphics state, §8.6 colour spaces, §8.9 images (§8.9.7 inline), §8.10 forms, §9.2 to §9.4 text (§9.4.4 the advance), §9.6 simple fonts, §9.7 composite fonts, §9.8 descriptors, §9.10 ToUnicode, §14.3 metadata, §14.6 marked content, §14.7 tagged PDF, §14.9.4 ActualText, Annex D encodings | https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/PDF32000_2008.pdf | read |
| PDF 2.0: §7.6.4 the revision 6 handler, algorithms 2.A and 2.B; UTF-8 text strings | https://pdfa.org/sponsored-standards | read |

## 3. DESIGN.md §1 to §4: reading, encryption, filters, pages

| Fact | Source | Status |
|---|---|---|
| The 1,024-byte header window and PDFium's rebuild | https://github.com/chromium/pdfium/blob/main/core/fpdfapi/parser/cpdf_parser.cpp | read |
| pdf.js's object index scan, stream-length recovery, last object wins | https://github.com/mozilla/pdf.js/blob/master/src/core/xref.js; https://github.com/mozilla/pdf.js (parser.js) | read |
| MuPDF's repair: trailer rebuilt from the catalog, the info object and the encrypt object | https://github.com/ArtifexSoftware/mupdf/blob/master/source/pdf/pdf-repair.c | read |
| QPDF's recovery behaviour | https://qpdf.readthedocs.io/en/stable/cli.html | read |
| A failure both PdfPig parsing modes share | https://github.com/UglyToad/PdfPig/issues/823 | read |
| The standard security handler revisions 2 to 6 as pdf.js implements them | https://github.com/mozilla/pdf.js/blob/master/src/core/crypto.js | read |
| The table's off-by-one first subsection correction, the doubled-sign number, the lone `#` in a name | pdf.js `parser.js` and `xref.js` | recalled |
| The inline image `EI` pitfall and the check that the following bytes lex | https://github.com/UglyToad/PdfPig/pull/1476 | read |
| The zlib stream format (two-byte header, FDICT, Adler-32 trailer) and the deflate format, read raw after the header | RFC 1950, https://www.rfc-editor.org/rfc/rfc1950; RFC 1951, https://www.rfc-editor.org/rfc/rfc1951 | recalled; tested in `tests/unit/syntax/filters_test.cpp` against zlib's own output |
| LZW's code widths, clear and end codes and `/EarlyChange`, and the example "-----A---B" | ISO 32000-1 §7.4.4.2 | recalled; tested in `tests/unit/syntax/filters_test.cpp` |
| The PNG filter types None, Sub, Up, Average and Paeth used by the predictors | https://www.w3.org/TR/png/ §9 | recalled; tested against hand-encoded rows in `tests/unit/syntax/filters_test.cpp` |
| The linearization dictionary's `/L` (file length) and `/O` (first page object) | ISO 32000-1 Annex F.2.2 | recalled; tested in `tests/unit/syntax/repair_test.cpp` |
| A `/Rotate` that is not a multiple of 90 reads as 0 | https://github.com/mozilla/pdf.js/blob/master/src/core/page.js | recalled; tested in `tests/unit/syntax/document_test.cpp` |
| An object-stream lookup by index, falling back to the object number | https://github.com/mozilla/pdf.js/blob/master/src/core/xref.js | recalled; tested in `tests/unit/syntax/object_streams_test.cpp` |
| The standard security handler's algorithms 1 to 5 and the padding string; algorithms 2.A, 2.B, 8 to 10 and 13 for revisions 5 and 6 | ISO 32000-1 §7.6.3; ISO 32000-2 §7.6.4 | read in the design record; tested in `tests/unit/syntax/security_handler_test.cpp` through the test tree's own encryptor |
| The content operators of Annex A, table A.1 | ISO 32000-1 Annex A | read; tested in `tests/unit/syntax/content_lexer_test.cpp` |
| Measured sizes of open implementations (for estimates) | https://github.com/UglyToad/PdfPig; https://github.com/mozilla/pdf.js; https://github.com/pdfcpu/pdfcpu; https://github.com/py-pdf/pypdf; https://github.com/pdfminer/pdfminer.six | read |

## 4. DESIGN.md §5 and §6: fonts, the ladder, the interpreter

| Fact | Source | Status |
|---|---|---|
| `(cid:N)` output for unmapped codes in pdfminer | https://pdfminersix.readthedocs.io/en/latest/faq.html | read |
| PDFBox's legacy stream engine mapping | PDFBox `LegacyPDFStreamEngine.java`, https://github.com/apache/pdfbox | read |
| PyMuPDF's handling of unknown Unicode | https://pymupdf.readthedocs.io/en/latest/vars.html | read |
| pdf.js glyph-name guessing (`uniXXXX`, `uXXXX`, `gNN`, `glyphNN`, `cidNN`, `Cdd`, `Cxx`) and private-use remapping | https://github.com/mozilla/pdf.js/blob/master/src/core/fonts.js; https://github.com/mozilla/pdf.js/pull/9062 | read |
| Symbolic TrueType `cmap` order (3,0), (3,0)+0xF000, (1,0), (3,1) | pdf.js `fonts.js`; PDFBox `PDTrueTypeFont.java` | read (pdf.js), recalled (PDFBox) |
| Wrong ToUnicode and ignored ActualText cost | https://github.com/mozilla/pdf.js/issues/12237 | read |
| The graphics and text state machine: the operators of Annex A, the rendering matrix `[Tfs*Th 0 0 Tfs 0 Trise] x Tm x CTM`, the advance `tx = ((w0 - Tj/1000) * Tfs + Tc + Tw) * Th`, word spacing on the single-byte code 32, vertical writing by `w1` and the position vector, the initial colours of each colour space, the text matrices outside the graphics state | ISO 32000-1 §8.4, §8.6.8 table 74, §9.3, §9.4.2, §9.4.4, §9.7.4.3 | read |
| Literal and hexadecimal string escapes, the CR LF rule and line continuations, an odd final hexadecimal digit | ISO 32000-1 §7.3.4.2 and §7.3.4.3 | read |
| Text strings: UTF-16BE after FE FF, PDFDocEncoding otherwise; UTF-8 after EF BB BF | ISO 32000-1 §7.9.2.2; ISO 32000-2 §7.9.2.2 | read (32000-1), recalled (32000-2) |
| A UTF-16LE byte-order mark read as little-endian | pdf.js `stringToPDFString`, https://github.com/mozilla/pdf.js/blob/master/src/shared/util.js | recalled |
| Optional content: `/OCProperties /D` with `/BaseState`, `/ON`, `/OFF`; membership dictionaries with `/OCGs`, `/P` and `/VE` | ISO 32000-1 §8.11.2 and §8.11.4.3 | read |
| Content in an optional-content group that is off is not drawn but the graphics-state operators inside it still apply; a catalog without `/OCProperties` shows every group (`src/redact/optional_content_remover.cpp`) | ISO 32000-1 §8.11 | recalled; the off-layer fixture of `tests/unit/redact/optional_content_remover_test.cpp` checks the text after an off sequence keeps its place |
| The structure tree's marked-content references: `/K` as an MCID, an `/MCR` with `/Pg` and `/Stm`, an `/OBJR` | ISO 32000-1 §14.7.2 to §14.7.4 | read |
| PdfPig's `UseActualText` option | https://github.com/UglyToad/PdfPig/blob/master/src/UglyToad.PdfPig/ParsingOptions.cs | read |
| Microsoft Print to PDF ligatures that trip extractors | https://github.com/py-pdf/pypdf/issues/2491 | read |
| Type 3 extraction | https://issues.apache.org/jira/browse/PDFBOX-3054 | read |
| The Adobe Glyph List (4,281 entries, BSD-3) | https://github.com/adobe-type-tools/agl-aglfn | read |
| Adobe CMap resources (189 files, 6.79 MB, BSD-3), not shipped | https://github.com/adobe-type-tools/cmap-resources | read |
| The Core 14 AFM files and their redistribution notice, as carried by PDFBox | https://raw.githubusercontent.com/apache/pdfbox/trunk/LICENSE.txt | read |
| Type 1 eexec and charstring decryption constants (55665, 4330, c1 52845, c2 22719, four lead bytes, `lenIV`), PFB segment headers, hsbw (13) and sbw (12 7) | Adobe Type 1 Font Format, https://adobe-type-tools.github.io/font-tech-notes/pdfs/T1_SPEC.pdf; pdf.js src/core/type1_parser.js (hexadecimal eexec test, RD spellings), https://github.com/mozilla/pdf.js/blob/e2a5e1d34517b7e544f234d54eec3aa37df0b846/src/core/type1_parser.js | recalled (the specification), read (pdf.js); verified on the dataset's embedded Type 1 programs (`type1_builtin`, median census F1 0.9996) |
| CFF and Type 2 charstring layout: INDEX, DICT operands and operators, charsets 0 to 2, encodings 0 and 1 with supplements, FDSelect 0 and 3, the width of the first stack-clearing operator, the subroutine bias (107, 1131, 32768) | Adobe Technical Notes 5176 and 5177, https://adobe-type-tools.github.io/font-tech-notes/; pdf.js src/core/cff_parser.js at commit e2a5e1d34517b7e544f234d54eec3aa37df0b846 | recalled (the notes), read (pdf.js); verified on the dataset's CFF programs (`cff_charset`, median census F1 0.9992) |
| The 391 CFF standard strings and the ISOAdobe, Expert and ExpertSubset charsets (ported) | pdf.js src/core/cff_parser.js and src/core/charsets.js, https://github.com/mozilla/pdf.js/tree/e2a5e1d34517b7e544f234d54eec3aa37df0b846/src/core | read |
| TrueType and OpenType tables: the table directory, `cmap` formats 0, 4, 6 and 12 (format 4's idRangeOffset addressing), `head` (unitsPerEm at 18, indexToLocFormat at 50), `hhea` (numberOfHMetrics at 34), `hmtx`, `maxp`, `post` format 2, `loca` | https://learn.microsoft.com/en-us/typography/opentype/spec/ (`otff`, `cmap`, `head`, `hhea`, `hmtx`, `post`, `loca` pages) | read (2026-10-25) |
| The 258 standard Macintosh glyph names of `post` formats 1 and 2 (ported) | pdf.js src/core/fonts_utils.js MacStandardGlyphOrdering, commit e2a5e1d34517b7e544f234d54eec3aa37df0b846 | read |
| The simple-font encoding tables (Standard, WinAnsi with its unassigned codes as bullet, MacRoman, MacExpert, Symbol, ZapfDingbats, the CFF Expert encoding) (ported) | pdf.js src/core/encodings.js, commit e2a5e1d34517b7e544f234d54eec3aa37df0b846; checked code by code against the Core 14 AFM files in the unit tier | read |
| WinAnsi unassigned codes rendered as bullet by Acrobat | ISO 32000-1 Annex D footnotes | recalled |
| The ITC Zapf Dingbats glyph list (201 names, BSD-3) | https://github.com/adobe-type-tools/agl-aglfn (`zapfdingbats.txt`) | read |
| The glyph-name conventions as pdf.js applies them (`uniXXXX` groups, `uXXXX` to `uXXXXXX` in upper case, `Gxx`, `g00xx`, `Cdd` decimal before hexadecimal) | pdf.js src/core/unicode.js getUnicodeForGlyph and src/core/evaluator.js _simpleFontToUnicode, commit e2a5e1d34517b7e544f234d54eec3aa37df0b846 | read |
| CMap code reading: codespaces matched shortest first by numeric range; a bfrange destination incremented in its last byte with a carry; a numeric source range; an odd-length destination gaining a leading zero; `/ToUnicode /Identity-H` read as the code's own UTF-16 value | pdf.js src/core/cmap.js (readCharCode, mapBfRange) and src/core/evaluator.js (readToUnicode), commit e2a5e1d34517b7e544f234d54eec3aa37df0b846 | read |
| The aliases viewers apply to the Core 14 names (Arial, ArialMT, TimesNewRoman, CourierNew and their style forms) | pdf.js src/core/standard_fonts.js getStdFontMap, commit e2a5e1d34517b7e544f234d54eec3aa37df0b846 | read |
| The codespaces predefined CMaps imply (Shift-JIS lead bytes 0x81 to 0x9F and 0xE0 to 0xFC; EUC, GBK, Big5 and UHC lead bytes from 0x81; the Uni CMaps' UCS-2, UTF-16, UTF-8 and UTF-32 codes) | Adobe CMap resources, https://github.com/adobe-type-tools/cmap-resources | recalled |
| The AFM file syntax (FontName, Ascender, Descender, FontBBox, C, WX, N) | Adobe Font Metrics File Format Specification 4.1 (Technical Note 5004), https://adobe-type-tools.github.io/font-tech-notes/pdfs/5004.AFM_Spec.pdf | recalled; the fourteen files are the specimen |
| Generator behaviours: Chrome/Skia hex `Tj` and ActualText, LibreOffice ActualText, Quartz control characters, Ghostscript wrong ToUnicode, pdfTeX without ToUnicode, Distiller losing ToUnicode, Skia's Type 3 ToUnicode | https://skia.googlesource.com/skia/+/refs/heads/main/src/pdf/SkPDFDevice.cpp; https://bugs.libreoffice.org/show_bug.cgi?id=124191; https://gitlab.freedesktop.org/poppler/poppler/-/issues/1393; https://bugs.ghostscript.com/show_bug.cgi?id=704478; https://tug.org/pipermail/tex-live/2020-May/045763.html; https://community.adobe.com/t5/acrobat-sdk-discussions/missing-tounicode-after-distillation/m-p/9147079; https://codereview.chromium.org/2292303004 | read |
| Microsoft Print to PDF is XPS-based and drops tags and bookmarks | https://www.bullzip.com/kb/category/pdf-printer/fonts/; https://office-watch.com/2025/pdf-save-or-print-guide/ | read |

## 5. DESIGN.md §7 and §8: layout and hidden text

| Fact | Source | Status |
|---|---|---|
| PdfPig's recursive XY-cut and Docstrum (Apache 2.0), ported | https://github.com/UglyToad/PdfPig/wiki/Document-Layout-Analysis; https://github.com/UglyToad/PdfPig | read |
| The two segmenters as ported: PdfPig's PageSegmenter RecursiveXYCut.cs (vertical cut before horizontal, projection gaps) and DocstrumBoundingBoxes.cs (the between-line distance as the mean of the fullest 10 pt histogram bin, the 1.3 multiplier, the ±30° angular bound, the overlap test of `PerpendicularOverlappingDistance`) | https://github.com/UglyToad/PdfPig/tree/e17fc2d577a24db986d8642ec7190188d0f6a0a9/src/UglyToad.PdfPig.DocumentLayoutAnalysis/PageSegmenter | read (2026-10-25, the algorithm; no code copied) |
| The word gap measured on the dataset: inside words within 0.07 of the font size, between positioned words from 0.15 up | `dataset/reports/phase5-try2` (pypdf-0004, pdfjs-0013, dccc-0120), the main checkout's report folder | read (2026-10-25) |
| PDFium folds no duplicated (fake bold) text in some files and interleaves it, and joins words across positional gaps in others | the PDFium text of pdfminer-0007, dccc-0066 in the dataset | read (2026-10-25) |
| The Unicode blocks of the scripts written without spaces (CJK punctuation, kana, ideographs and extensions, hangul syllables, full-width forms) and of the right-to-left scripts (Hebrew to Arabic Extended-A, the presentation forms) | https://www.unicode.org/charts/ | recalled |
| Word-gap tolerances: PDFBox | PDFBox `PDFTextStripper.java`, https://github.com/apache/pdfbox | read |
| Word-gap tolerances: pdfminer `word_margin`, `char_margin` | https://pdfminersix.readthedocs.io/en/latest/topic/converting_pdf_to_text.html | read |
| PdfPig narrow gaps | https://github.com/UglyToad/PdfPig/issues/319 | read |
| The 14-tool extraction benchmark | https://ad-publications.informatik.uni-freiburg.de/benchmark.pdf | read |
| pdftotext hyphen handling | https://man.archlinux.org/man/pdftotext.1 | read |
| pdf.js drops format characters | https://github.com/mozilla/pdf.js/blob/master/src/core/unicode.js | read |
| PyMuPDF sorting | https://pymupdf.readthedocs.io/en/latest/recipes-text.html | read |
| Extractors divided on invisible and off-page text: poppler drops off-page glyphs, flags mode 3, de-duplicates fake bold, skips OFF optional content | poppler `TextOutputDev.cc` and `Gfx.cc`, https://gitlab.freedesktop.org/poppler/poppler | read |
| pdf.js keeps "ghost text" because filtering would break OCR layers | https://github.com/mozilla/pdf.js/issues/9666 | read |
| PyMuPDF clips to the media box and hides hidden text by default | https://github.com/pymupdf/PyMuPDF/discussions/2263 | read |
| A non-zero media-box origin | https://github.com/jsvine/pdfplumber/issues/1181 | read |
| A glyph straddling the page edge | https://bugs.freedesktop.org/show_bug.cgi?id=71160 | read |
| The invisible OCR text layer | https://leanpub.com/read/pdfkungfoo/make-ocr-visible | read |

## 6. DESIGN.md §9 and §14: images and treatments

| Fact | Source | Status |
|---|---|---|
| pdf.js's JBIG2 decoder (Apache 2.0, 2,436 lines at the commit read), ported | https://github.com/mozilla/pdf.js/blob/fe48736846e2043696738bd54435ebb5ab24ecff/src/core/jbig2.js | read at commit fe48736846e2043696738bd54435ebb5ab24ecff, the last commit before pdf.js replaced its JavaScript decoders with a build of PDFium's (commit a6988582d22488021734027cd05358e04a3cda05) |
| pdf.js's CCITT decoder (Apache 2.0, 1,024 lines at the commit read), ported; the filter parameters it is fed come from `ccitt_stream.js` at the same commit | https://github.com/mozilla/pdf.js/blob/fe48736846e2043696738bd54435ebb5ab24ecff/src/core/ccitt.js | read at commit fe48736846e2043696738bd54435ebb5ab24ecff |
| CCITT byte alignment beside end-of-line codes: xpdf's `CCITTFaxStream` aligns after looking for the code and only when none was found, because producers write the code ending on the boundary (T.4, TIFF) or starting on it (the two formats its comment describes); PDFium's fax decoder skips the code, then aligns | https://www.xpdfreader.com/ (the CCITTFaxStream class of Stream.cc); https://pdfium.googlesource.com/pdfium/+/refs/heads/main/core/fxcodec/fax/faxmodule.cpp | recalled; tested in `tests/unit/images/ccitt_decoder_test.cpp` with both conventions and, over synthetic streams, against PDFium |
| pdf.js's MQ arithmetic decoder (Apache 2.0, 170 lines at the commit read), ported; `jbig2_stream.js` at the same commit parses the globals stream's bytes ahead of the image's and inverts the decoded bits | https://github.com/mozilla/pdf.js/blob/fe48736846e2043696738bd54435ebb5ab24ecff/src/core/arithmetic_decoder.js | read at commit fe48736846e2043696738bd54435ebb5ab24ecff |
| pdf.js's JBIG2 test files `bitmap-*.pdf` (28 of their image streams are unit-test vectors; each stands for one 399 by 400 bitmap, and PDFium decodes them to the same pixels) | https://github.com/mozilla/pdf.js/tree/e2a5e1d34517b7e544f234d54eec3aa37df0b846/test/pdfs | read |
| JBIG2 beyond pdf.js: the text-region placement by reference corner when transposed (6.4.5 step 3c; pdf.js applies the corner offsets to the wrong axes), typical prediction in refinement (6.3.5.6), Huffman coding with refinement (6.4.11, 6.5.8.2), the five combination operators (7.4.1.5), the halftone skip bitmap (6.6.5.1), retained bitmap coding contexts (7.4.2.2), a generic region of unknown length that ends with its own row count (7.4.6.4) and the file organisations (annex D) | https://www.itu.int/rec/T-REC-T.88 | recalled; each tested in `tests/unit/images/jbig2_decoder_test.cpp` and agrees with PDFium over pdf.js's test files |
| PDFBox's CCITT decoder (BSD-3, TwelveMonkeys), alternative reference | https://raw.githubusercontent.com/apache/pdfbox/trunk/pdfbox/src/main/java/org/apache/pdfbox/filter/CCITTFaxDecoderStream.java | read |
| JBIG2 standard (ITU-T T.88) and CCITT (T.4, T.6) | https://www.itu.int/rec/T-REC-T.88; https://www.itu.int/rec/T-REC-T.4; https://www.itu.int/rec/T-REC-T.6 | recalled |
| The Adobe APP14 marker does not invert a CMYK JPEG's samples in PDFium (the belief that viewers invert them came from pdf.js `jpg.js`, recalled) | `tests/pdfium/image_agreement.py` over the dataset's CMYK JPEGs; https://github.com/mozilla/pdf.js/blob/master/src/core/jpg.js | measured: uninverted, the YCCK JPEG of `dccc-0003` agrees with `FPDFImageObj_GetBitmap` within a mean difference of 0.65; inverted it differs by 248 |
| PDFium's Adobe CMYK to sRGB table (`kCMYK`, 9 x 9 x 9 x 9 samples) and its fixed-point interpolation (`AdobeCmykToStandardRgb`), ported into `src/images/adobe_cmyk.cpp` (BSD-3-Clause) | https://pdfium.googlesource.com/pdfium/+/7b3f6f93625e6df97a7d9e677137a558df8648c4/core/fxge/dib/cfx_cmyk_to_srgb.cpp | read at commit 7b3f6f93625e6df97a7d9e677137a558df8648c4; tested in `tests/unit/images/colour_space_test.cpp` and agrees with PDFium on every DeviceCMYK image of the dataset |
| PDFium's Lab conversion reads an 8-bit Lab sample's a* and b* as the byte minus 128 whatever `/Range` and `/Decode` say | https://pdfium.googlesource.com/pdfium/+/7b3f6f93625e6df97a7d9e677137a558df8648c4/core/fpdfapi/page/cpdf_colorspace.cpp | read; pdfkit applies `/Range` as ISO 32000-1 §8.6.5.4 states, so the dataset's three Lab images differ from PDFium |
| `FPDFImageObj_GetBitmap` returns an image without its soft mask, the colours as stored (a `/Matte` premultiplication kept), an image mask as 255 where it paints, and a 1-bit Indexed image as 8-bit palette indices whose palette the API does not expose; `FPDFImageObj_GetImageDataRaw` returns the stream's decrypted, still encoded bytes | https://raw.githubusercontent.com/chromium/pdfium/main/public/fpdf_edit.h | read (the signatures); the behaviours measured over the dataset by `tests/pdfium/image_agreement.py` |
| The libjpeg API: `jpeg_mem_src`, `saw_Adobe_marker`, `jpeg_color_space` and `out_color_space` (YCCK converted to CMYK), `jpeg12_read_scanlines`, and an `error_exit` that must not return | https://github.com/libjpeg-turbo/libjpeg-turbo/blob/3.2.0/doc/libjpeg.txt | recalled; tested in `tests/unit/images/dct_decoder_test.cpp` against libjpeg-turbo's own compressor |
| The OpenJPEG decoding API: memory stream callbacks, `opj_read_header`, `opj_decode`, `opj_end_decompress`, component `dx`/`dy`/`prec`/`sgnd`, `OPJ_CLRSPC_SYCC`, and `OPJ_DPARAMETERS_IGNORE_PCLR_CMAP_CDEF_FLAG` (leaves a JP2 palette unapplied, as PDFium's `CJPX_Decoder` does for an Indexed space) | `third_party/openjpeg/src/lib/openjp2/openjpeg.h` | read in the vendored header; tested in `tests/unit/images/jpx_decoder_test.cpp` against OpenJPEG's own encoder, and the palette flag agrees with PDFium on the dataset's Indexed JPX images |
| PDF functions of types 0, 2, 3 and 4 and the calculator's operators | ISO 32000-1 §7.10, https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/PDF32000_2008.pdf; https://github.com/mozilla/pdf.js/blob/master/src/core/function.js | recalled; tested in `tests/unit/images/pdf_function_test.cpp` |
| The image colour spaces, `/Decode` defaults, sample packing, colour-key and stencil masks and `/Matte` | ISO 32000-1 §8.6, §8.9.5, §8.9.6, §11.6.5.3 | recalled; tested in `tests/unit/images/colour_space_test.cpp`, `sample_unpacker_test.cpp` and `masks_test.cpp`, and against PDFium over the dataset |
| `/SMaskInData` for JPX | ISO 32000-1 §8.9.5.4 | recalled |
| PNG file format: signature, IHDR, tRNS, IDAT, IEND, CRC-32, the None and Paeth filters | https://www.w3.org/TR/png/ | recalled; tested in `tests/unit/images/png_writer_test.cpp` by a reader written in the test over zlib |
| An image's pixel grid on the unit square: sample row 0 is the top of the image, column x spans u from x / width, and the image space maps the unit square through the CTM at the draw | ISO 32000-1 §8.9.4 and §8.9.5.1, https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/PDF32000_2008.pdf | recalled; tested in `tests/unit/treatments/region_mapper_test.cpp` on an identity, a scaled, a turned and a flipped matrix, and by the region boxes of the dataset run against PDFium's rendering |
| A JPX image reads no `/Decode` unless it is a stencil (table 89) | ISO 32000-1 §8.9.5.1 table 89 | recalled; the re-encoder drops it when a JPX image becomes Flate |
| An inline image's data ends at the `EI` after white space, and an ASCIIHex filter ends its data at `>` | ISO 32000-1 §8.9.7 and §7.4.2 | recalled; tested in `tests/unit/redact/image_treatment_applier_test.cpp` (the rewritten inline image lists and decodes) |
| The libjpeg compression API: `jpeg_mem_dest`, `jpeg_set_defaults`, `jpeg_set_colorspace(JCS_CMYK)` (which writes the Adobe marker), `jpeg_set_quality`, per-component `h_samp_factor`/`v_samp_factor`, and an `error_exit` that must not return | https://github.com/libjpeg-turbo/libjpeg-turbo/blob/3.2.0/doc/libjpeg.txt | recalled; tested in `tests/unit/treatments/reencoder_test.cpp` through the library's own decoder |
| zlib's `compress` defaults: level 6, window bits 15, memory level 8 | https://zlib.net/manual.html | recalled (the writer's `StreamCopier`, which compresses every treated Flate image) |
| fontTools' pen protocol (`BasePen`: `_moveTo`, `_lineTo`, `_qCurveToOne`, `_curveToOne`, `_closePath`) and `getGlyphSet` | https://fonttools.readthedocs.io/en/latest/pens/basePen.html | recalled; the caption font's hash is checked in `tests/unit/treatments/caption_drawer_test.cpp` and the glyphs inspected by eye when generated |
| The non-zero winding fill rule of TrueType outlines | https://developer.apple.com/fonts/TrueType-Reference-Manual/RM02/Chap2.html | recalled |
| The OpenJPEG function-pointer table of `openjpeg.c` (`opj_jp2_destroy`, `opj_j2k_end_decompress` and their kin called through `void *` parameters), which clang's `-fsanitize=function` reports | `third_party/openjpeg/src/lib/openjp2/openjpeg.c`; https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html | read in the vendored source; the clang-sanitize preset turns that one check off for the openjpeg target |
| Drawing clustering, as a reference for figures | https://pymupdf.readthedocs.io/en/latest/page.html#Page.cluster_drawings; https://github.com/jsvine/pdfplumber/blob/stable/README.md | read |

## 7. DESIGN.md §10 to §13: metadata, matching, surgery, the placeholder font

| Fact | Source | Status |
|---|---|---|
| XMP specification (packet, `rdf:Alt`, `rdf:Seq`, `xmpMM:History`) | https://github.com/adobe/XMP-Toolkit-SDK/tree/main/docs | recalled |
| The XMP property forms a reader meets: a property as an attribute of `rdf:Description` (the shorthand) or as an element holding text or one `rdf:Alt`, `rdf:Seq` or `rdf:Bag` of `rdf:li` items, `xml:lang="x-default"` naming an `rdf:Alt`'s default; the namespaces of `dc:` (`http://purl.org/dc/elements/1.1/`), `xmp:` (`http://ns.adobe.com/xap/1.0/`, written `xap:` by older producers), `pdf:` (`http://ns.adobe.com/pdf/1.3/`) and `xmpMM:` (`http://ns.adobe.com/xap/1.0/mm/`); `dc:title` and `dc:description` are language alternatives, `dc:creator` an ordered and `dc:subject` an unordered array | XMP Specification Part 1 §7 (RDF serialisation) and Part 2 §8 (the Dublin Core, XMP basic and Adobe PDF schemas), https://github.com/adobe/XMP-Toolkit-SDK/tree/main/docs | recalled; `xmp_scanner_test.cpp` reads each form, and the dataset's 343 readable packets (Acrobat, Distiller, pikepdf, DynaPDF, Word and LibreOffice among their producers) read with them |
| The Info dictionary's eight text entries and their pairing with the XMP properties | ISO 32000-1 §14.3.3 table 317 and §14.3.2; XMP Specification Part 2 §8 | read (32000-1), recalled (the pairing); `metadata_reader_test.cpp` and `tests/pdfium/metadata.py` |
| A PDF date is `D:YYYYMMDDHHmmSSOHH'mm'` with every part after the year optional and `Z`, `+` or `-` as O | ISO 32000-1 §7.9.4 | read; `metadata_reader_test.cpp` normalises the dataset's forms, the `Z00'00'` of Quartz among them |
| An XMP date is ISO 8601 as `YYYY[-MM[-DD[Thh:mm[:ss[.s]]TZD]]]` | XMP Specification Part 1 §8.2.1.1, after the W3C note https://www.w3.org/TR/NOTE-datetime | recalled; `metadata_reader_test.cpp` |
| The PDFDocEncoding table (0x18 to 0x1F spacing accents, 0x80 to 0xA0 marks and letters, Latin-1 from 0xA1, 0x7F, 0x9F and 0xAD undefined) | ISO 32000-1 Annex D table D.2 | read; `metadata_reader_test.cpp` checks all 256 codes against the fonts layer's Annex D names |
| A UTF-16LE text string with the mark FF FE, which producers write, is read as little-endian | pdf.js `stringToPDFString` | recalled; `metadata_reader_test.cpp` reads one and the reader counts it |
| PDFium's `FPDF_GetMetaText(document, tag, buffer, length)` answers an Info entry as UTF-16LE with a terminating NUL, its size in bytes when the buffer is short | `fpdf_doc.h` (https://pdfium.googlesource.com/pdfium/+/refs/heads/main/public/fpdf_doc.h) | recalled; the binding in `tests/pdfium/pdfium.py` reads every dataset file's eight fields for `tests/pdfium/metadata.py` |
| Unicode simple case folding (CaseFolding.txt status C and S), canonical decompositions (UnicodeData.txt field 5 without a tag), General_Category Mn, Me, Zs, Zl and Zp, as compiled by `build/unicode/generate.py` | https://www.unicode.org/Public/18.0.0/ucd/CaseFolding.txt; https://www.unicode.org/Public/18.0.0/ucd/UnicodeData.txt (each pinned in `build/unicode/unicode.lock`) | read; `tests/unit/unicode/folding_test.cpp` |
| The White_Space property adds the controls U+0009 to U+000D and U+0085 to Zs, Zl and Zp | PropList.txt, https://www.unicode.org/Public/18.0.0/ucd/PropList.txt | recalled; `tests/unit/unicode/folding_test.cpp` checks U+0085 and U+3000 |
| The simple case folding leaves U+00DF and U+0130 unfolded and folds U+03C2 to U+03C3 | The Unicode Standard §3.13 and CaseFolding.txt | read in the data; `folding_test.cpp` |
| fontTools' subsetter: `Options` (`layout_features`, `hinting`, `notdef_outline`, `glyph_names`, `name_IDs`, `name_languages`, `recalc_timestamp`, `drop_tables`) and `Subsetter.populate(unicodes=...)` | https://fonttools.readthedocs.io/en/latest/subset/index.html | recalled; `build/fonts/subset.py` writes the same bytes twice and `placeholder_font_test.cpp` reads the result back through pdfkit's font loader |
| A subset font's `/BaseFont` carries a tag of six capital letters and a plus sign | ISO 32000-1 §9.6.4 | recalled; the tag is derived from the program's hash |
| A Type0 font over a CIDFontType2 with `/Identity-H`, `/CIDToGIDMap /Identity`, `/W`, `/DW`, a descriptor with `/FontFile2`, and a ToUnicode CMap of `bfchar` entries | ISO 32000-1 §9.7.4, §9.7.6, §9.8, §9.10.3 | read in the design record; `placeholder_font_test.cpp` decodes every character back to itself |
| A stem width estimated from the OS/2 weight class (50 + weight² / 65²) when a writer has none | common PDF writer practice | recalled; viewers of an embedded TrueType program do not read `/StemV` |
| A text string is PDFDocEncoding or UTF-16BE with the byte order mark FE FF | ISO 32000-1 §7.9.2.2 | recalled; `marked_content_rewriter_test.cpp` writes and reads one back |
| The TJ number moves the text position by -n/1000 × Tfs × Th, so a removed glyph's displacement tx is the number -tx/(Tfs × Th) × 1000 | ISO 32000-1 §9.4.3, §9.4.4 | read; the position test of `tests/unit/redact/text_surgery_test.cpp` holds every kept glyph within 0.01 pt |
| Text state parameters (Tc, Tw, Tz, TL, Tf, Tr, Ts) are part of the graphics state that q and Q save, and q, Q and cm are not allowed inside a text object | ISO 32000-1 §8.4.1, §9.3.1, figure 9 | read in the design record; the placeholder is drawn in `q BT ... ET Q` after the text object |
| A comment runs from `%` outside a string to the end of its line; the white-space characters of table 1 | ISO 32000-1 §7.2.2, §7.2.3 | read; `redact_text_test.cpp` and `self_check_test.cpp` plant a comment |
| The standard keys of a form XObject (table 95), an image XObject (table 89), a soft mask image (`/Matte`, table 146) and a stream (table 5) | ISO 32000-1 §8.10.2, §8.9.5, §11.6.5.3, §7.3.8 | read in the design record; the `ch_xobject_private` row's unit test plants a private key on a form |
| A page is displayed as its crop box turned clockwise by `/Rotate` (a multiple of 90) | ISO 32000-1 §7.7.3.3 table 30 | read; `compare.py --check redact` places the rectangles so and the gate's rotated and cropped pages compare |
| XML character references and the five predefined entities | XML 1.0 §4.1 and §4.6, https://www.w3.org/TR/xml/ | recalled; `self_check_test.cpp` finds a string written with `&amp;` |
| The OFL FAQ permits embedding a font subset in a PDF | https://openfontlicense.org/ofl-faq/ | read |
| Liberation 2.x is SIL OFL 1.1 | https://metadata.ftp-master.debian.org/changelogs//main/f/fonts-liberation2/fonts-liberation2_2.00.5-1_copyright | read |
| PdfPig's TrueType subsetter and CID font writer, as references for the build-time subset | https://raw.githubusercontent.com/UglyToad/PdfPig/master/src/UglyToad.PdfPig.Fonts/TrueType/Subsetting/TrueTypeSubsetter.cs; https://raw.githubusercontent.com/UglyToad/PdfPig/master/src/UglyToad.PdfPig/Writer/Fonts/CompositeTrueTypeWritingFont.cs | read |
| `Tz` horizontal scaling | ISO 32000-1 §9.3.4 | read |

## 8. DESIGN.md §15 to §18: channels, page selection, the writer, the self-check

| Fact | Source | Status |
|---|---|---|
| PyMuPDF's `scrub` list of hidden channels | https://pymupdf.readthedocs.io/en/latest/document.html | read |
| Acrobat's "Examine Document" / "Remove Hidden Information" items | https://www.michbar.org/file/pmrc/articles/0000149.pdf | read |
| Text outside content streams: XFA placeholders, AcroForm `/V` against `/AP` | the design research's secondary accounts | recalled |
| The corkami proofs of concept: overlapped and hidden text, deniable removal | https://github.com/corkami/pocs/tree/master/pdf | read |
| The keys each planted-marker specimen writes (`dataset/tools/synth/channels/rows.py`): catalog entries (`/Names`, `/Dests`, `/PageLabels`, `/Threads`, `/SpiderInfo`, `/Collection`, `/Perms`, `/Legal`, `/URI`, `/OCProperties`, `/StructTreeRoot`, `/MarkInfo`, `/AF`), page entries (`/Thumb`, `/PieceInfo`, `/LastModified`, `/Annots`, `/AA`, `/B`, `/ID`, `/StructParents`, `/Metadata`), annotation, form-field, outline, structure-element, optional-content, signature-reference and file-specification keys | ISO 32000-1 (PDF 1.7) §7.7.2, §7.7.3, §12.3, §12.4, §12.5, §12.6, §12.7, §12.8, §14.6 to §14.10, §8.11, §7.11 and Adobe's PDF Reference 1.7 table of catalog entries, https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/PDF32000_2008.pdf | recalled; every specimen is opened and rendered by PDFium and its markers are found by `verify` in `dataset/tools/synth/channels/kit.py` |
| Strings and streams inside the /Encrypt dictionary are not encrypted, so a vendor key's string there stays in clear | ISO 32000-1 §7.6.1 (encryption dictionary) | recalled; `dataset/tools/synth/channels/rows.py` writes a vendor key and the marker scan finds it in the raw bytes of the encrypted file |
| `/SearchIndex` and a non-standard `/PageMode` value as a private index channel: an author decision, not a specification rule; the key and the value are inventions of the specimen | `docs/DESIGN.md` §15 (search indexes row) | author decision |
| Object syntax the serialiser writes: literal-string escapes (three-digit octal), name `#xx` escapes and the delimiter set, numbers without exponent, dictionaries, streams with the end-of-line before `endstream` not counted in `/Length` | ISO 32000-1 §7.2.2, §7.3.3 to §7.3.8, https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/PDF32000_2008.pdf | read in the design record; every form is round-tripped through pdfkit's own parser in `tests/unit/writer/serialiser_test.cpp` |
| The cross-reference stream: `/W`, `/Index`, `/Size`, big-endian fields, type 0 for the free head with generation 65535, type 1 offsets; the header's binary comment of four bytes above 127 | ISO 32000-1 §7.5.2, §7.5.8 | read in the design record; outputs re-open in pdfkit without a repair (unit tier) and in PDFium (dataset tier) |
| A `/Crypt` filter names its crypt filter in `/DecodeParms /Name`, defaulting to `/Identity` | ISO 32000-1 §7.4.10 | read in the design record |
| zlib's defaults: level 6, window bits 15, memory level 8; the zlib stream format | zlib manual (https://zlib.net/manual.html), RFC 1950 | recalled; the deflated streams inflate through pdfkit's own Flate filter in the unit tier |
| The resource categories and the operators naming them (`Tf`, `Do`, `gs`, `cs`/`CS`, `scn`/`SCN`, `sh`, `BDC`/`DP`, the inline image's `/ColorSpace`) | ISO 32000-1 §7.8.3, Annex A | read |
| `/DefaultGray`, `/DefaultRGB` and `/DefaultCMYK` replace the device spaces without being named by an operator, so pruning keeps them | ISO 32000-1 §8.6.5.6 | read; found by the dataset (pdfdiff-0009, pdfdiff-0010, dccc-0003 rendered differently until kept) |
| The catalog keys (table 28), the page keys (table 30), the standard viewer preferences (table 150, `/Enforce` from ISO 32000-2) and the `/PageMode` values | ISO 32000-1 §7.7.2, §7.7.3.3, §12.2; ISO 32000-2 table 147 | read in the design record |
| A linearized file carries a first-page cross-reference section of its own | ISO 32000-1 Annex F | recalled; the revisions row discounts it |
| The XMP media-management properties (`xmpMM:History`, `DocumentID`, `InstanceID`, `DerivedFrom` and the rest of the namespace `http://ns.adobe.com/xap/1.0/mm/`) | XMP Specification Part 2, §3.3 (https://github.com/adobe/XMP-Toolkit-SDK/tree/main/docs) | recalled; the unit tier plants a history and attribute identifiers and asserts both gone |
| SplitMix64, the generator of the dataset's page subsets | Steele, Lea and Flood, "Fast splittable pseudorandom number generators", OOPSLA 2014; the public-domain reference code at https://prng.di.unimi.it/splitmix64.c | recalled; the constants are fixed in `tests/dataset/outcome.md` |

## 9. CLAUDE.md §6: vendored components

| Component | Source | Status |
|---|---|---|
| zlib 1.3.2 (zlib licence; released 2026-02-17; the archive's SHA-256 as the release page lists it, in `third_party/inventory.tsv`) | https://zlib.net | read |
| libjpeg-turbo 3.2.0 (IJG, BSD-3-Clause and zlib licences in `third_party/libjpeg-turbo/LICENSE.md`; released 2026-06-30, the newest stable release on 2026-10-25 by the GitHub releases API; the archive SHA-256 equals the digest GitHub lists, in `third_party/inventory.tsv`; the library source list of its `CMakeLists.txt`) | https://github.com/libjpeg-turbo/libjpeg-turbo/releases/tag/3.2.0 | read |
| OpenJPEG 2.5.4 (BSD-2-Clause in its `LICENSE`; released 2025-09-20, the newest release on 2026-10-25 by the GitHub releases API; the tag archive pinned by SHA-256 in `third_party/inventory.tsv`; the `openjp2` source list of the release's own openjp2 CMake list) | https://github.com/uclouvain/openjpeg/releases/tag/v2.5.4 | read |
| doctest 2.5.3 (MIT; release list, archive and its SHA-256 in `third_party/inventory.tsv`) | https://github.com/doctest/doctest/releases/tag/v2.5.3 | read |
| Liberation Sans 2.1.5 Regular and Bold (SIL OFL 1.1 in the release's `LICENSE`, with the Reserved Font Names Arimo, Tinos, Cousine and Liberation; the release archive downloaded again on 2026-10-25 has the SHA-256 `third_party/inventory.tsv` records) | https://github.com/liberationfonts/liberation-fonts/files/7261482/liberation-fonts-ttf-2.1.5.tar.gz | read |
| The Unicode Character Database 18.0.0 (`CaseFolding.txt`, `UnicodeData.txt`; Unicode License v3 at https://www.unicode.org/license.txt; 18.0.0 is the version the UCD's latest folder names on 2026-10-25) | https://www.unicode.org/Public/18.0.0/ucd/ | read |
| Adobe Glyph List | https://github.com/adobe-type-tools/agl-aglfn | read |
| Adobe Glyph List 2.0 and the Zapf Dingbats list at commit 4036a9ca80a62f64f9de4f7321a9a045ad0ecfd6 (BSD-3-Clause, LICENSE.md; tree hash in `third_party/inventory.tsv`) | https://github.com/adobe-type-tools/agl-aglfn/tree/4036a9ca80a62f64f9de4f7321a9a045ad0ecfd6 | read |
| The Core 14 AFM files and `MustRead.html` as Apache PDFBox carries them at commit 19d3a6799f829051e8efb27197af59935fd8179b (each file's SHA-256 in `build/fonts/core14.lock`) | https://github.com/apache/pdfbox/tree/19d3a6799f829051e8efb27197af59935fd8179b/pdfbox/src/main/resources/org/apache/pdfbox/resources/afm | read |

## 10. CLAUDE.md §7: the dataset and the independent reader

| Set or tool | Source | Licence as recorded | Status |
|---|---|---|---|
| PDFium prebuilt binaries (bblanchon) | https://github.com/bblanchon/pdfium-binaries | PDFium BSD-3 and Apache 2.0, packaging MIT | read |
| qpdf test files (628 PDFs) | https://github.com/qpdf/qpdf/tree/main/qpdf/qtest/qpdf | Apache 2.0 | read |
| PDFium test resources and corpus | https://pdfium.googlesource.com/pdfium/+/refs/heads/main/testing/resources; https://pdfium.googlesource.com/pdfium_tests | BSD-style, Apache 2.0 | read |
| PDF Association safedocs and pdf20examples | https://github.com/pdf-association/safedocs; https://github.com/pdf-association/pdf20examples | Apache 2.0; examples CC BY-SA 4.0 (LICENSE.md read) | read |
| Open Preservation Foundation format corpus | https://github.com/openpreserve/format-corpus | CC0 unless stated | read |
| veraPDF corpus | https://github.com/veraPDF/veraPDF-corpus | CC BY 4.0 | read |
| pdf.js test files and Debian's per-file licence log | https://github.com/mozilla/pdf.js; https://metadata.ftp-master.debian.org/changelogs//main/p/pdf.js/pdf.js_2.6.347+dfsg-3_copyright | mixed | read |
| PdfPig and PDFBox test files | https://github.com/UglyToad/PdfPig; https://github.com/apache/pdfbox | repositories Apache 2.0, files unverified | read |
| py-pdf sample files | https://github.com/py-pdf/sample-files | CC BY-SA 4.0 | read |
| Digital Corpora CC-MAIN-2021-31-PDF-UNTRUNCATED | https://digitalcorpora.org/corpora/file-corpora/cc-main-2021-31-pdf-untruncated/ | Common Crawl terms | read |
| Digital Corpora UNSAFE-DOCS (optional, sandboxed) | https://downloads.digitalcorpora.org/corpora/files/CC-MAIN-2021-31-UNSAFE | none; may be malicious | read |
| PDF Association stressful corpus (optional, sandboxed) | https://labs.pdfa.org/stressful-corpus/ | none; may be malicious | read |
| GovDocs1 | https://digitalcorpora.org/corpora/file-corpora/files/ | public domain (US government) | recalled |
| corkami PDF proofs of concept | https://github.com/corkami/pocs/tree/master/pdf | no licence file | read |
| pdf.js per-file licences: eleven test files excluded by Debian as non-free, four CC0, one CC BY-SA 3.0, the rest Apache 2.0 | https://metadata.ftp-master.debian.org/changelogs//main/p/pdf.js/pdf.js_2.6.347+dfsg-3_copyright | as listed | read |
| PDF Association pdf-differences | https://github.com/pdf-association/pdf-differences | Apache 2.0 | read |
| PDF Association pdf-issues (one PDF, no test files) | https://github.com/pdf-association/pdf-issues | CC BY 4.0 | read |
| pdfminer.six samples (without samples/nonfree) | https://github.com/pdfminer/pdfminer.six/tree/master/samples | MIT (repository) | read |
| pdfplumber test files | https://github.com/jsvine/pdfplumber/tree/stable/tests/pdfs | MIT (repository) | read |
| pikepdf test resources (no notice of their own) | https://github.com/pikepdf/pikepdf/tree/main/tests/resources | MPL 2.0 (repository) | read |
| Apache Tika PDF test documents | https://github.com/apache/tika (tika-parser-pdf-module test resources) | Apache 2.0 | read |
| NICAR 2025 hard PDFs | https://github.com/dwillis/nicar25-pdfs | MIT (repository) | read |
| BFO PDF/A test suite | https://github.com/bfosupport/pdfa-testsuite | no licence file or statement | read |
| PDF/UA reference suite | https://pdfa.org/resource/pdfua-reference-suite/ | not readable (HTTP 403) | read |
| Ghent Output Suite | https://gwg.org/workflow-tools-downloads/test-suites/ghent-output-suite/ | no terms found | read |
| IUST PDF corpus | https://zenodo.org/records/3484013 | not readable (the API answered 406) | read |
| olmOCR-bench | https://huggingface.co/datasets/allenai/olmOCR-bench | ODC-BY | read |
| Evil PDF library (malicious, optional) | https://github.com/GerHobbelt/Evil-PDF-Library-for-Qiqqa | GPL 3.0 | read |
| The index of PDF corpora | https://github.com/pdf-association/pdf-corpora | CC BY 4.0 | read |
| Digital Corpora's corpus README: zips of 1,000 PDFs under zipfiles/0000-0999/, the request to note Common Crawl's terms | https://digitalcorpora.s3.amazonaws.com/corpora/files/CC-MAIN-2021-31-PDF-UNTRUNCATED/README.md | | read |
| Common Crawl terms of use: a limited licence to access and use the crawled content | https://commoncrawl.org/terms-of-use | | read |
| Digital Corpora terms of use (CC0 for its own material) | https://digitalcorpora.org/terms-of-use/ | | read |
| bblanchon release chromium/8111: asset `pdfium-win-x64.tgz`, its SHA-256 digest, the `releases/download/<tag>/<asset>` URL | https://api.github.com/repos/bblanchon/pdfium-binaries/releases | | read |
| Liberation fonts 2.1.5 TTF archive URL (in the release notes; the release has no asset) | https://github.com/liberationfonts/liberation-fonts/releases/tag/2.1.5 | SIL OFL 1.1 | read |
| fontTools 4.67.0 wheel and its hash | https://pypi.org/project/fonttools/4.67.0/ | MIT | read |
| PDFium's C API signatures used by `tests/pdfium/pdfium.py` | public/fpdfview.h, fpdf_text.h and fpdf_edit.h under https://pdfium.googlesource.com/pdfium/+/refs/heads/main/public/ | | recalled; exercised by `tests/pdfium/compare.py` with `--self-test` |
| `FPDF_RenderPageBitmap(bitmap, page, start_x, start_y, size_x, size_y, rotate, flags)` and its flags (`FPDF_ANNOT` 0x01 draws annotations, so the pixel comparison passes flags 0) | https://raw.githubusercontent.com/chromium/pdfium/main/public/fpdfview.h | | read; exercised by `tests/pdfium/render.py` |
| AES (FIPS-197) and its appendix C test vectors | https://csrc.nist.gov/pubs/fips/197/final | | recalled; the vectors pass in `dataset/tools/tests/test_pdfcrypt.py` |
| The security handler algorithms the mutator writes (1 to 5, 2.A, 2.B, 8 to 10) | ISO 32000-1 §7.6.3.3; ISO 32000-2 §7.6.4 | | read in the design record; PDFium opens every mutated file encrypted with the empty user password and refuses the others |
| Word `ExportAsFixedFormat` and `PrintOut`, Excel `ExportAsFixedFormat` and `PrintOut`, PowerPoint `SaveAs` (32) and `PrintOut`, Edge `--headless --print-to-pdf` | https://learn.microsoft.com/en-us/office/vba/api/overview/ | | recalled; each outcome is recorded in the generation log the script writes |

## 11. CLAUDE.md §9 and §10: the toolchain, the build and the C ABI

Facts measured on the laptop are recorded with their commands in
`build/toolchain.md`; this table holds the external ones.

| Fact | Source | Status |
|---|---|---|
| The clang-format and clang-tidy wheels bundle the LLVM binaries, version 23.1.3, Apache-2.0 metadata | https://pypi.org/project/clang-format/23.1.3/; https://pypi.org/project/clang-tidy/23.1.3/ (wheel metadata read from the downloaded wheels) | read |
| llvm-mingw 20261020, its asset name and SHA-256 digest | https://github.com/mstorsjo/llvm-mingw/releases/tag/20261020 | read |
| WinLibs GCC 16.1.0 UCRT r2, its asset name and SHA-256 digest (the CI install) | https://github.com/brechtsanders/winlibs_mingw/releases/tag/16.1.0posix-14.0.0-ucrt-r2 | read |
| `CMAKE_CXX_EXTENSIONS OFF` selects `-std=c++20` rather than `-std=gnu++20` | https://cmake.org/cmake/help/latest/variable/CMAKE_CXX_EXTENSIONS.html | recalled; checked in `out/mingw-debug/compile_commands.json` |
| The presets schema versions CMake 4.3.2 accepts (at most 11) | the error CMake 4.3.2 prints for version 12 | read |
| The GCC warning options of `build/warnings.cmake` | https://gcc.gnu.org/onlinedocs/gcc/Warning-Options.html | recalled; every option is accepted by GCC 16.1.0 |
| JSON grammar, string escapes and the UTF-8 requirement | RFC 8259, https://www.rfc-editor.org/rfc/rfc8259 | recalled; tested in `tests/unit/platform/json_*_test.cpp` |
| Well-formed UTF-8 byte sequences (table 3-7) and the maximal-subpart replacement | The Unicode Standard, chapter 3, https://www.unicode.org/versions/latest/ch03.pdf | recalled; tested in `tests/unit/platform/json_writer_test.cpp` |
| `std::to_chars` without a precision gives the shortest form that reads back to the same double | ISO C++ [charconv.to.chars], https://eel.is/c++draft/charconv.to.chars | recalled; tested in `tests/unit/platform/json_writer_test.cpp` |
| The `activeCodePage` manifest element sets the process code page to UTF-8; resource type 24 identifier 1 is an executable's manifest | https://learn.microsoft.com/en-us/windows/apps/design/globalizing/use-utf8-code-page | recalled; tested by `pdfkit text` on a Cyrillic file name |
| P/Invoke with `DllImport`, `NativeLibrary.SetDllImportResolver` and `Marshal.PtrToStringUTF8` | https://learn.microsoft.com/en-us/dotnet/standard/native-interop/pinvoke | recalled; tested by `python tests/abi/run.py` |
| The `LibraryImport` source generator: `static partial` declarations, `StringMarshalling.Utf8`, `out` handles, pointers under `AllowUnsafeBlocks` | https://learn.microsoft.com/en-us/dotnet/standard/native-interop/pinvoke-source-generation | recalled; `tests/abi/dotnet` declares every symbol so and builds with warnings as errors under the .NET 10.0.204 SDK |
| `ctypes.CDLL`, `Structure` and `string_at` | https://docs.python.org/3.12/library/ctypes.html | recalled; tested by `python tests/abi/run.py` |
| MD5 algorithm, constants T[1..64], rotation amounts, padding, and the seven test vectors of appendix A.5 | https://www.rfc-editor.org/rfc/rfc1321.txt | read; tested in `tests/unit/platform/crypto/md5_test.cpp` |
| SHA-256, SHA-384 and SHA-512: algorithm, constants K and initial hash values | https://nvlpubs.nist.gov/nistpubs/FIPS/NIST.FIPS.180-4.pdf | read; tested in `tests/unit/platform/crypto/sha2_test.cpp` |
| SHA-2 digests of "abc", the 448-bit and 896-bit messages | https://csrc.nist.gov/CSRC/media/Projects/Cryptographic-Standards-and-Guidelines/documents/examples/SHA256.pdf (and SHA384.pdf, SHA512.pdf) | read; tested in `tests/unit/platform/crypto/sha2_test.cpp` |
| SHA-2 digests of one million "a" | https://csrc.nist.gov/files/pubs/fips/180-2/final/docs/fips180-2.pdf; https://www.rfc-editor.org/rfc/rfc6234.txt §8.5 | read; tested in `tests/unit/platform/crypto/sha2_test.cpp` |
| SHA-384 and SHA-512 of the 448-bit message, the empty digests, and digests at the padding boundaries | Python `hashlib` | computed; tested in `tests/unit/platform/crypto/sha2_test.cpp` and `md5_test.cpp` |
| AES: cipher, inverse cipher, key expansion, S-boxes, Rcon, and the block vectors of appendix C.1 and C.3 | https://nvlpubs.nist.gov/nistpubs/FIPS/NIST.FIPS.197.pdf | read; tested in `tests/unit/platform/crypto/aes_test.cpp` |
| CBC mode and the vectors F.2.1, F.2.2, F.2.5 and F.2.6 | https://nvlpubs.nist.gov/nistpubs/Legacy/SP/nistspecialpublication800-38a.pdf | read; tested in `tests/unit/platform/crypto/aes_test.cpp` |
| PKCS#7 padding (n bytes of value n) | https://www.rfc-editor.org/rfc/rfc5652.txt §6.3 | recalled; tested in `tests/unit/platform/crypto/aes_test.cpp` |
| RC4 (key scheduling and generation) | Schneier, Applied Cryptography, 2nd ed., §17.1 | recalled; confirmed by the RFC 6229 vectors |
| RC4 keystream vectors for seven key sizes | https://www.rfc-editor.org/rfc/rfc6229.txt | read; tested in `tests/unit/platform/crypto/rc4_test.cpp` |
| The CLI's SHA-256 (FIPS 180-4 §4.1.2, §4.2.2, §5.1.1, §6.2) | https://nvlpubs.nist.gov/nistpubs/FIPS/NIST.FIPS.180-4.pdf | recalled; tested against the library's SHA-2 in `tests/unit/cli/sha256_test.cpp` and against every manifest hash by the dataset tier |
