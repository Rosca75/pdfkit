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
| PdfPig's `UseActualText` option | https://github.com/UglyToad/PdfPig/blob/master/src/UglyToad.PdfPig/ParsingOptions.cs | read |
| Microsoft Print to PDF ligatures that trip extractors | https://github.com/py-pdf/pypdf/issues/2491 | read |
| Type 3 extraction | https://issues.apache.org/jira/browse/PDFBOX-3054 | read |
| The Adobe Glyph List (4,281 entries, BSD-3) | https://github.com/adobe-type-tools/agl-aglfn | read |
| Adobe CMap resources (189 files, 6.79 MB, BSD-3), not shipped | https://github.com/adobe-type-tools/cmap-resources | read |
| The Core 14 AFM files and their redistribution notice, as carried by PDFBox | https://raw.githubusercontent.com/apache/pdfbox/trunk/LICENSE.txt | read |
| Type 1 eexec and charstring decryption constants (55665, 4330) | Adobe Type 1 Font Format, https://adobe-type-tools.github.io/font-tech-notes/pdfs/T1_SPEC.pdf | recalled |
| CFF and Type 2 charstring layout | Adobe Technical Notes 5176 and 5177, https://adobe-type-tools.github.io/font-tech-notes/ | recalled |
| TrueType and OpenType tables | https://learn.microsoft.com/en-us/typography/opentype/spec/ | recalled |
| WinAnsi unassigned codes rendered as bullet by Acrobat | ISO 32000-1 Annex D footnotes | recalled |
| Generator behaviours: Chrome/Skia hex `Tj` and ActualText, LibreOffice ActualText, Quartz control characters, Ghostscript wrong ToUnicode, pdfTeX without ToUnicode, Distiller losing ToUnicode, Skia's Type 3 ToUnicode | https://skia.googlesource.com/skia/+/refs/heads/main/src/pdf/SkPDFDevice.cpp; https://bugs.libreoffice.org/show_bug.cgi?id=124191; https://gitlab.freedesktop.org/poppler/poppler/-/issues/1393; https://bugs.ghostscript.com/show_bug.cgi?id=704478; https://tug.org/pipermail/tex-live/2020-May/045763.html; https://community.adobe.com/t5/acrobat-sdk-discussions/missing-tounicode-after-distillation/m-p/9147079; https://codereview.chromium.org/2292303004 | read |
| Microsoft Print to PDF is XPS-based and drops tags and bookmarks | https://www.bullzip.com/kb/category/pdf-printer/fonts/; https://office-watch.com/2025/pdf-save-or-print-guide/ | read |

## 5. DESIGN.md §7 and §8: layout and hidden text

| Fact | Source | Status |
|---|---|---|
| PdfPig's recursive XY-cut and Docstrum (Apache 2.0), ported | https://github.com/UglyToad/PdfPig/wiki/Document-Layout-Analysis; https://github.com/UglyToad/PdfPig | read |
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
| pdf.js's JBIG2 decoder (Apache 2.0, 2,584 lines), ported | https://github.com/mozilla/pdf.js/blob/master/src/core/jbig2.js | read (size), recalled (structure) |
| pdf.js's CCITT decoder (Apache 2.0, 1,059 lines), ported | https://github.com/mozilla/pdf.js/blob/master/src/core/ccitt.js | read (size), recalled (structure) |
| PDFBox's CCITT decoder (BSD-3, TwelveMonkeys), alternative reference | https://raw.githubusercontent.com/apache/pdfbox/trunk/pdfbox/src/main/java/org/apache/pdfbox/filter/CCITTFaxDecoderStream.java | read |
| JBIG2 standard (ITU-T T.88) and CCITT (T.4, T.6) | https://www.itu.int/rec/T-REC-T.88; https://www.itu.int/rec/T-REC-T.4; https://www.itu.int/rec/T-REC-T.6 | recalled |
| The Adobe APP14 marker and inverted CMYK JPEGs | pdf.js `jpg.js`, https://github.com/mozilla/pdf.js/blob/master/src/core/jpg.js | recalled |
| `/SMaskInData` for JPX | ISO 32000-1 §8.9.5.4 | recalled |
| PNG file format | https://www.w3.org/TR/png/ | recalled |
| Drawing clustering, as a reference for figures | https://pymupdf.readthedocs.io/en/latest/page.html#Page.cluster_drawings; https://github.com/jsvine/pdfplumber/blob/stable/README.md | read |

## 7. DESIGN.md §10 to §13: metadata, matching, surgery, the placeholder font

| Fact | Source | Status |
|---|---|---|
| XMP specification (packet, `rdf:Alt`, `rdf:Seq`, `xmpMM:History`) | https://github.com/adobe/XMP-Toolkit-SDK/tree/main/docs | recalled |
| Unicode simple case folding and canonical decomposition | https://www.unicode.org/Public/UCD/latest/ucd/CaseFolding.txt; https://www.unicode.org/Public/UCD/latest/ucd/UnicodeData.txt | recalled |
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

## 9. CLAUDE.md §6: vendored components

| Component | Source | Status |
|---|---|---|
| zlib | https://zlib.net | recalled |
| libjpeg-turbo | https://github.com/libjpeg-turbo/libjpeg-turbo | recalled |
| OpenJPEG | https://github.com/uclouvain/openjpeg | recalled |
| doctest 2.5.3 (MIT; release list, archive and its SHA-256 in `third_party/inventory.tsv`) | https://github.com/doctest/doctest/releases/tag/v2.5.3 | read |
| Liberation fonts | https://github.com/liberationfonts/liberation-fonts | recalled |
| Unicode data | https://www.unicode.org/Public/ | recalled |
| Adobe Glyph List | https://github.com/adobe-type-tools/agl-aglfn | read |

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
| `ctypes.CDLL`, `Structure` and `string_at` | https://docs.python.org/3.12/library/ctypes.html | recalled; tested by `python tests/abi/run.py` |
