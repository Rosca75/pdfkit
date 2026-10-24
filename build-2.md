# build-2 — The dataset: tools, manifest with the first 300 files, taxonomy and the PDFium harness

| Field | Value |
|---|---|
| Phase | 2 |
| Branch | `phase/2-dataset` |
| Depends on | 1 |
| Parallel with | 3 |
| Model | `opus` |
| Owns | `dataset/`, `tests/dataset/`, `tests/pdfium/`, `build/paths_pending.tsv` (removing its own rows only), `build/check_network.py` (its exemption list only) |
| Size | about 3,500 lines of Python and PowerShell, tests included |
| Price | estimate: 9 to 13 hours, $130 to $220 of model spend at list prices, calibrated from change-12's batch A (about 3,200 lines, 8 to 10 h, $120 to $180) with the downloads, the scouting and the Office generation added |

**Phase graph.** 1 → {2, 3}; 3 → {4, 6, 7}; 4 → 5; 5 → 6's listing step;
{5, 7} → 8; {6, 7, 8} → 9; every phase → 10. This phase runs in parallel with
phase 3 and touches none of its files: phase 3 owns `src/`, `tests/unit/` and
`cli/verbs/inspect.cpp` and `cli/verbs/dataset.cpp`; this phase owns the
dataset and the two Python harnesses. The two meet at the `pdfkit dataset` verb's
output format, which §2.6 fixes so both sides can be written at once.

---

## 0. What this phase ships

The dataset exists before the parser does, so that phase 3's gate is a number
over real files on the day the parser lands (`CLAUDE.md` §7.4).

- **`dataset/manifest.tsv`** with at least 300 rows, every row's file fetched,
  hashed and tagged, every source's licence recorded.
- **`dataset/taxonomy.tsv`** with every tag of `CLAUDE.md` §7.3 and its minimum.
- **The tools** under `dataset/tools/`: fetch, add, autotag, report, mutate,
  synth, generate_office.
- **The PDFium harness** under `tests/pdfium/`: fetch (by hash), the `ctypes`
  binding, text agreement, page rendering and the pixel comparison, the leak
  scan.
- **The dataset harness** `tests/dataset/run.py`, which runs `pdfkit dataset`
  over the manifest and checks every expectation column it knows.
- **`dataset/README.md`**: the columns, the licence rule, the sets, how to add a
  file, the commands.

Catalogue rows delivered: none. CLI verbs: none (the `dataset` verb is phase 3's;
this phase fixes its output contract and consumes it).

## 1. What it reads from earlier phases

- Phase 1: `build/check.py` and its scripts (this phase's Python must pass
  `check_headers.py`, `check_comments.py` and `check_network.py`, whose
  exemption list gains `dataset/tools/fetch.py` and `tests/pdfium/fetch.py` by
  name with their reasons), `build/paths_pending.tsv` (remove the rows for the
  paths this phase creates), `.gitignore` (`dataset/files/`, `dataset/cache/`,
  `dataset/reports/`, `build/cache/` ignored), `docs/ABI.md` (the CLI exit
  codes the harness interprets).
- `build/toolchain.md`: the proxy behaviour the spike recorded for `git`,
  `pip` and plain HTTPS; a source the proxy refuses is skipped and named.
- Read build-1.md §9 for deviations before planning.

## 2. Components

### 2.1 Where the files live

Every worktree reads one copy: the tools resolve the dataset root as
`$PDFKIT_DATASET` if set, else `<main checkout>/dataset/`, where the main
checkout is the parent of `git rev-parse --git-common-dir` (`CLAUDE.md` §11).
Downloads land in `dataset/cache/<set>/`, selected files in
`dataset/files/<set>/<id>.pdf`, reports in `dataset/reports/<run-id>/`. PDFium
lands in `build/cache/pdfium/<tag>/` under the same main checkout. All four are
ignored by git.

### 2.2 The manifest and the taxonomy

| File | What it holds | Lines | Reference and status |
|---|---|---|---|
| `dataset/README.md` | numbered chapters: what the dataset is; the column definitions of `manifest.tsv` (below); the licence rule (no client document ever; public files admitted only when their licence or terms allow local testing; no PDF committed; the licence recorded per row); the sets with their source, licence and count; the taxonomy file; how to add a file (`add.py`), a set (`sources.tsv` row and `fetch.py`), a synthesised pattern (`synth/`), a mutation (`mutate.py`); the commands; the shared-root rule | 250 | `CLAUDE.md` §7 |
| `dataset/manifest.tsv` | one row per file, tab-separated, sorted by `id`, UTF-8, LF. Columns: `id` (`<set>-<nnnn>`), `set`, `source` (URL with commit, or `synth:<pattern>`, `mutate:<source-id>:<mutation>:<seed>`, `office:<generator>`), `licence` (SPDX id, or `terms:<short name>` with the terms URL in `sources.tsv`), `sha256`, `bytes`, `pages` (PDFium's count, empty when PDFium refuses), `producer` (the Info `/Producer` or XMP `pdf:Producer`, PDFium's reading, empty when absent), `tags` (comma list from `taxonomy.tsv`), `expect_open` (`opens`, `repaired`, `refused:<code>` with the codes of `docs/DESIGN.md` chapter 19), `expect_text` (`agree`, `known_gap:<reason>`, `n/a`), `expect_images` (`decoded`, `partial:<reason>`, `n/a`), `expect_redact` (`stable`, `n/a`), `expect_metadata` (`agree`, `n/a`), `notes` | data | `CLAUDE.md` §7.1 |
| `dataset/taxonomy.tsv` | columns `tag`, `group`, `minimum`, `description`, `detector` (`autotag`, `synth`, `mutate`, `office`, `manual`). Every tag of `CLAUDE.md` §7.3; minimum 5 for the font-path tags and the structural-damage tags, 3 by default, 1 for rare ones (`vertical`, `right_to_left`, `encryption_r5`, `jbig2`, `jpx`, `type3`, `deniable_removal`) | data | `CLAUDE.md` §7.3 |
| `dataset/sources.tsv` | one row per public set: `set`, `url`, `fetch` (`git` with commit, `archive` with URL), `pin` (commit hash or archive SHA-256, recorded on the first fetch and pinned after), `licence`, `licence_url`, `optional` (`yes` for the unsafe sets), `select` (how many files are taken and by which rule), `status` (`admitted`, `refused_by_proxy`, `licence_unclear`), `notes` | data | below |

The tag groups and tags, at minimum (the session adds a tag when a taxonomy
gap appears, never removes one):

- **damage**: `xref_missing`, `xref_wrong_offsets`, `startxref_wrong`,
  `eof_missing`, `junk_before_header`, `length_wrong`, `endstream_missing`,
  `endobj_missing`, `two_trailers`, `incremental_update`, `object_streams`,
  `xref_stream`, `hybrid_xref`, `linearised`, `linearised_stale_hints`,
  `truncated`, `damaged_flate`.
- **encryption**: `rc4_40`, `rc4_128`, `aesv2`, `aesv3_r5`, `aesv3_r6`,
  `encrypt_metadata_false`, `user_password` (refused).
- **fonts**: `tounicode_correct`, `tounicode_missing`, `tounicode_wrong`,
  `differences_aglnames`, `differences_uni`, `differences_gnn`,
  `differences_cdd`, `symbolic_truetype`, `type1_builtin`, `cff_charset`,
  `identity_cid`, `predefined_cmap`, `type3`, `text_as_outlines`,
  `font_not_embedded`.
- **layout**: `no_space_glyphs`, `kerning_split`, `ligatures`,
  `hyphenation_line`, `hyphenation_page`, `multi_column`, `rotated`, `vertical`,
  `right_to_left`, `table`, `header_footer`, `actual_text`.
- **hidden**: `render_invisible`, `white_fill`, `clipped`, `off_media`,
  `off_crop`, `optional_content_off`, `duplicate`, `covered`.
- **images**: `img_flate`, `img_dct`, `img_dct_cmyk`, `img_jpx`, `img_jbig2`,
  `img_jbig2_globals`, `img_ccitt_g3`, `img_ccitt_g4`, `img_lzw`,
  `img_runlength`, `cs_gray`, `cs_rgb`, `cs_cmyk`, `cs_indexed`, `cs_icc`,
  `cs_separation`, `cs_devicen`, `cs_lab`, `smask`, `stencil`, `colour_key`,
  `inline_image`, `shared_image`, `image_in_form`, `bpc_1`, `bpc_16`.
- **channels**: one per row of the channel table of `docs/DESIGN.md`
  chapter 15 (`ch_thumbnail`, `ch_pieceinfo`, `ch_javascript`,
  `ch_embedded_file`, `ch_annotation`, `ch_redact_annot`, `ch_acroform`,
  `ch_xfa`, `ch_outline`, `ch_named_dest`, `ch_struct_tree`,
  `ch_optional_content`, `ch_page_labels`, `ch_xmp_history`,
  `ch_object_metadata`, `ch_unreferenced`, `ch_threads`, `ch_portfolio`).
- **generators**: `gen_word`, `gen_word_pdfa`, `gen_print_to_pdf`,
  `gen_excel`, `gen_powerpoint`, `gen_edge`, `gen_chrome`, `gen_libreoffice`,
  `gen_quartz`, `gen_ghostscript`, `gen_pdftex`, `gen_acrobat`,
  `gen_distiller`, `gen_itext`, `gen_reportlab`, `gen_other` (from the producer
  string by `autotag.py`).
- **traps**: `overlapped_text`, `incremental_redaction`, `deniable_removal`,
  `box_over_live_text`.

### 2.3 The public sets

The named sets of `CLAUDE.md` §7.2, with what the design record counted on
2026-10-24 (read there; recount on fetch):

| Set | URL | Licence (status) | Take |
|---|---|---|---|
| qpdf test files | `https://github.com/qpdf/qpdf` (`qpdf/qtest/qpdf/`) | Apache-2.0 (read in the design record) | 60, weighted to the damage, encryption, xref-stream and linearised cases |
| PDFium test resources | `https://pdfium.googlesource.com/pdfium/+/refs/heads/main/testing/resources` (fetch as the `+archive/…/testing/resources.tar.gz` tarball; the archive URL form is recalled, verify) | BSD-3 (read) | 50, weighted to fonts, JBIG2 and damage |
| PDF Association safedocs | `https://github.com/pdf-association/safedocs` | Apache-2.0 (read) | 20 lexer edge cases |
| PDF Association pdf20examples | `https://github.com/pdf-association/pdf20examples` | CC BY-SA 4.0 (unverified in the design record; verify) | all (about 7) |
| OPF format corpus | `https://github.com/openpreserve/format-corpus` | CC0 unless stated (read) | 30 from the PDF folders and the cabinet of horrors |
| pdf.js test files | `https://github.com/mozilla/pdf.js` (`test/pdfs/`, committed files only, linked files not fetched) | repository Apache-2.0, files mixed (read) | 60, weighted to fonts and generators |
| PdfPig test files | `https://github.com/UglyToad/PdfPig` | repository Apache-2.0, files unverified | 15 layout cases |
| PDFBox test files | `https://github.com/apache/pdfbox` | repository Apache-2.0, files unverified | 15 |
| py-pdf sample files | `https://github.com/py-pdf/sample-files` | CC BY-SA 4.0 (read) | all (about 35), each labelled by generator |
| veraPDF corpus | `https://github.com/veraPDF/veraPDF-corpus` | CC BY 4.0 (read) | 20 tagged and PDF/A cases |
| corkami proofs of concept | `https://github.com/corkami/pocs/tree/master/pdf` | no licence file (read): `terms:none`, local reading only | 15 traps |
| Digital Corpora CC-MAIN-2021-31-PDF-UNTRUNCATED | `https://digitalcorpora.org/corpora/file-corpora/cc-main-2021-31-pdf-untruncated/`; one 1,000-file zip (the zip URL pattern under `downloads.digitalcorpora.org` is recalled; read it from the corpus page) | Common Crawl terms, no per-file licence (read) | 150 sampled with seed 20261024 from one zip |
| PDF Association stressful corpus | `https://labs.pdfa.org/stressful-corpus/` | none; may contain malicious files | optional, not fetched by default |
| Digital Corpora UNSAFE-DOCS | `https://downloads.digitalcorpora.org/corpora/files/CC-MAIN-2021-31-UNSAFE` | none; malicious files | optional, not fetched by default |

**Scouting.** The session searches the web for further public PDF test corpora
built to expose technical variety and records each candidate in `sources.tsv`
with `status` and a licence note, admitting it only when the licence or terms
allow local testing. Candidates to check, each "to verify": the PDF
Association's `pdf-differences` and `pdf-issues` repositories, Apache Tika's
test documents, pdfminer.six's samples, pdfplumber's test files, pikepdf's
test resources, GovDocs1 (`https://digitalcorpora.org/corpora/file-corpora/files/`),
the Isartor test suite, the PDF/UA reference suite. A candidate whose licence
cannot be established is recorded `licence_unclear` and not fetched.

### 2.4 The tools (`dataset/tools/`)

Every tool is Python 3.12 with the standard library unless named, reads the
shared root of §2.1, prints JSON or a table, and fails with what failed, what
was expected and how to fix it.

| File | What it does | Lines | Reference and status |
|---|---|---|---|
| `requirements.txt` | the tools' pinned dependencies with `--hash`: `fontTools` (MIT) for building font programs in synth; `cryptography` (Apache-2.0 or BSD-3) for AES in the mutator's encryption | 10 | licences recalled; verify on PyPI and record in `dataset/README.md` |
| `common.py` | the shared root resolution, manifest read and write (stable sort by `id`, LF, UTF-8), SHA-256, the run-id format (`YYYYMMDD-HHMMSS-<git short hash>`) | 150 | |
| `fetch.py` | reads `sources.tsv`; clones (`git clone --filter=blob:none` then sparse checkout of the needed folders, pinned commit) or downloads (streamed, size-limited) each non-optional admitted set into `dataset/cache/<set>/`; records the pin on a first fetch and refuses a later fetch whose pin differs; copies the files the manifest names into `dataset/files/<set>/<id>.pdf`; `--verify` re-hashes every manifest file and fails naming each mismatch; `--offline` registers a set placed by hand; a source the proxy refuses is named and skipped, never worked around. A named exemption of `build/check_network.py` | 400 | |
| `autotag.py` | infers tags from bytes and PDFium: filters present (`/FlateDecode`, `/DCTDecode`, `/JPXDecode`, `/JBIG2Decode`, `/CCITTFaxDecode` with `/K`), encryption (`/V`, `/R`, `/StmF`, `/EncryptMetadata`), xref kind, `/Linearized`, object streams, `%%EOF` count (incremental updates), font subtypes and encodings (`/Type0`, `/Identity-H`, predefined CMap names, `/Type3`, `/Differences` with the name classes, `/ToUnicode` present or absent, `/FontFile`, `/FontFile2`, `/FontFile3`), images (`/ImageMask`, `/SMask`, `/Mask` arrays, colour-space names, `BI` operators found in decoded content when PDFium can give it, else skipped), channels (`/Thumb`, `/PieceInfo`, `/JavaScript`, `/JS`, `/EmbeddedFiles`, `/Annots`, `/Subtype /Redact`, `/AcroForm`, `/XFA`, `/Outlines`, `/Dests`, `/StructTreeRoot`, `/OCProperties`, `/PageLabels`, `xmpMM:History`, `/Threads`, `/Collection`), generator from the producer string. Raw-byte detection of compressed objects is incomplete by nature, so it decompresses Flate object streams with `zlib` to look inside them; tags it cannot infer come from provenance | 450 | |
| `add.py` | registers one file or a folder: copies it under its id, hashes it, asks PDFium for pages and producer, runs `autotag.py`, writes the row with expectations defaulted (`opens`, `agree`, `decoded`, `n/a`, `n/a`) unless the provenance says otherwise (a mutation of a refusing class, a `user_password` file); refuses a duplicate hash naming the existing id | 200 | |
| `report.py` | counts per tag against `taxonomy.tsv`, per set, per licence; prints the table and the tags under their minimum; exit non-zero when a tag is under minimum, `--allow-under` lists them without failing (this phase's gate uses the share, §4) | 150 | |
| `mutate.py` | the seeded mutator over clean files: `xref_missing`, `startxref_wrong`, `eof_missing`, `junk_before_header` (random bytes, under and over 1,024), `length_wrong` (short and long), `endstream_missing`, `endobj_missing`, `two_trailers`, `incremental_update` (appends a revision changing one page's text), `linearised_stale_hints` (when the source is linearised), `object_streams` (rewrites objects into object streams), and the encryptions `rc4_40`, `rc4_128`, `aesv2`, `aesv3_r5`, `aesv3_r6` with an empty user password and `encrypt_metadata_false`, plus one `user_password` file per revision for the refusal; each output's `source` names the source id, the mutation and the seed; its `expect_open` is `repaired` or `opens` (same text as the source) or `refused:<code>`. RC4 and the key algorithms are written here; AES comes from `cryptography` | 600 | ISO 32000-1 §7.6.3 and ISO 32000-2 §7.6.4 algorithms (read in the design record's sources), change-12a §9.2's mutation list |
| `synth/writer.py` | a minimal PDF writer: objects, streams (Flate or raw), a classic xref table, content streams written from a small operator list, font dictionaries with or without embedded programs, image XObjects and inline images | 400 | |
| `synth/patterns/*.py` | one module per pattern, each writing one or more files with a known expected text into `dataset/files/synth/` and registering them with their tags and expectations: `no_space_glyphs`, `kerning_split`, `ligatures_tounicode` and `ligatures_no_tounicode`, `tounicode_wrong`, `differences_uni`, `differences_gnn`, `differences_cdd`, `symbolic_truetype` (a Liberation Sans subset built with fontTools, a (3,0) cmap at `0xF000`), `type3`, `identity_cid`, `predefined_cmap`, every hidden-text class, `rotated`, `vertical`, `right_to_left`, `multi_column`, `table`, `hyphenation_line`, `hyphenation_page`, `inline_image`, `stencil`, `colour_key`, `shared_image`, `image_in_form`, one per channel-table row with a unique marker string, `incremental_redaction` (a revision that deletes a name the earlier revision drew), `overlapped_text`, `box_over_live_text` | 800 | Liberation fonts are SIL OFL 1.1 (read in the design record, stream 3); the font files come from `https://github.com/liberationfonts/liberation-fonts` releases, pinned in `sources.tsv` |
| `generate_office.ps1` | writes one reference document through Word COM from a text spec held in the script (prose with a name and an IBAN, a two-column section, a ruled table, a header and footer, bold and italic, the words "office", "affine", "flight" for ligatures, a hyphenated line, one image), then saves it through Word Save as PDF, Save as PDF/A, and Print to PDF ("Microsoft Print to PDF"); builds a workbook through Excel COM and a deck through PowerPoint COM from the same text and prints and saves each; renders an HTML version with Edge headless (`msedge --headless --print-to-pdf`); every application is started, given 120 s, then killed with its process tree whatever state it is in; outputs land in `dataset/files/office/` and are registered by `add.py` with `office:<generator>` and the generator tag. The night session runs it | 250 | the Word `ExportAsFixedFormat` and `SaveAs2` COM methods and Edge's `--print-to-pdf` switch (recalled; the script records each application's version and the outcome) |

### 2.5 The PDFium harness (`tests/pdfium/`)

| File | What it does | Lines | Reference and status |
|---|---|---|---|
| `fetch.py` | downloads bblanchon's `pdfium-win-x64.tgz` for a pinned release tag from `https://github.com/bblanchon/pdfium-binaries/releases` into `build/cache/pdfium/<tag>/` of the main checkout, records the SHA-256 on the first fetch and pins it, extracts `bin/pdfium.dll` and the licence files; refuses a hash mismatch. A named exemption of `build/check_network.py` | 150 | the release asset name and URL pattern are recalled; read them on the releases page and record them. The design record read chromium/8111 on 2026-10-19 with `pdfium.dll` at 7,586,816 bytes x64 |
| `pdfium.py` | the `ctypes` binding: `FPDF_InitLibraryWithConfig` or `FPDF_InitLibrary`, `FPDF_DestroyLibrary`, `FPDF_LoadMemDocument`, `FPDF_GetLastError`, `FPDF_GetPageCount`, `FPDF_LoadPage`, `FPDF_ClosePage`, `FPDF_CloseDocument`, `FPDF_GetPageWidthF`, `FPDF_GetPageHeightF`, `FPDFText_LoadPage`, `FPDFText_ClosePage`, `FPDFText_CountChars`, `FPDFText_GetText`, `FPDF_GetMetaText`, `FPDFBitmap_Create`, `FPDFBitmap_FillRect`, `FPDFBitmap_GetBuffer`, `FPDFBitmap_GetStride`, `FPDFBitmap_Destroy`, `FPDF_RenderPageBitmap` (with and without `FPDF_ANNOT`), and for phase 6: `FPDFPage_CountObjects`, `FPDFPage_GetObject`, `FPDFPageObj_GetType`, `FPDFImageObj_GetBitmap`, `FPDFImageObj_GetImageDataDecoded`. Each file is opened in a child process with a 30 s limit, because PDFium on an unsafe file may hang | 350 | the signatures in PDFium's public headers `fpdfview.h`, `fpdf_text.h`, `fpdf_edit.h`, `fpdf_doc.h` (read in the design record, stream 1) |
| `words.py` | the metric of `CLAUDE.md` §7.5: text to a multiset of words (NFKC, lower-cased with `str.casefold`, split on characters that are neither letter nor digit, words of two or more characters kept), F1 as the harmonic mean of multiset precision and recall | 80 | |
| `compare.py` | `--run <run-id>`: for each file of a dataset run, reads pdfkit's text output (the `text` field of the outcome, §2.6) and PDFium's text, writes `dataset/reports/<run-id>/pdfium-text.tsv` (id, set, pages, F1, pdfkit unreadable count, notes), the median per set and overall, the files under 0.90; `--render` renders pages before and after for a redaction run and compares outside the reported rectangles with a tolerance of one pixel's anti-aliasing at rectangle edges; `--leak` searches PDFium's text and the output's decompressed bytes for every redacted string. In this phase the pdfkit side does not exist, so `--self-test` runs PDFium against PDFium (F1 must be 1.0 on every file it opens) and against a perturbed copy of its own text (one word in ten dropped must give F1 ≈ 0.947) | 450 | |
| `test_words.py` | the metric on known pairs | 60 | |

### 2.6 The dataset harness (`tests/dataset/`)

| File | What it does | Lines |
|---|---|---|
| `run.py` | runs `pdfkit dataset --manifest dataset/manifest.tsv --out dataset/reports/<run-id>/` (or, per file, `pdfkit dataset --file <path> --id <id>`) with a 30 s limit per file in a child process, twice, and compares the two outputs byte for byte; reads each outcome and checks every expectation column whose function is implemented (an unimplemented function, `PDFKIT_E_NOT_IMPLEMENTED`, is counted as `skipped`, never as a pass); writes `summary.json` and `summary.md` (per expectation column: pass, fail, skipped; per set; the failing ids with the reason; crashes, hangs, non-determinism) | 400 |
| `outcome.md` | the contract of one outcome file, which phase 3 implements in `cli/verbs/dataset.cpp`: `dataset/reports/<run-id>/outcomes/<id>.json` holding `{"id", "sha256", "open": {"status", "code", "pages", "repairs": [...]}, "text": {...} or null, "images": {...} or null, ...}`, one key per catalogue function the verb ran, each with its status and the fields that function's expectation reads; keys in a fixed order so the bytes are comparable | 80 |

## 3. Tests

- **Unit (Python)**: `tests/pdfium/test_words.py`, `dataset/tools/tests/`
  (manifest round trip, `autotag` on synthesised files whose tags are known,
  the mutator's determinism by seed, each mutation's effect checked by a small
  reader of its own). Run by `python -m unittest discover dataset/tools/tests`
  and `python -m unittest discover tests/pdfium`.
- **Dataset**: the whole manifest is the test data of later phases; this phase
  fills `id` to `notes` and the expectation columns from provenance.
- **PDFium**: `compare.py --self-test` over the manifest.
- **Checks**: `python build/check.py` over the new files.

## 4. Gate

| Measure | Required | Command |
|---|---|---|
| manifest size | at least 300 rows | `python dataset/tools/report.py` |
| hashes | every row's file present and its SHA-256 verified | `python dataset/tools/fetch.py --verify` |
| licences | every row's licence recorded and every set's row in `sources.tsv` `admitted` | `python dataset/tools/report.py --licences` |
| PDFium opens | at least 95% of the rows not tagged with a damage, encryption-refusal or unsafe tag | `python tests/pdfium/compare.py --self-test` |
| PDFium self-agreement | F1 = 1.0 on every file it opens; the perturbation case at 0.947 ± 0.01 | the same |
| tag coverage | at least 80% of the tags in `taxonomy.tsv` at or above their minimum; every tag under minimum is either filled by `synth` or listed in `OPEN-QUESTIONS.md` with why no file could be made | `python dataset/tools/report.py --allow-under` |
| harness time | `tests/dataset/run.py` over the whole manifest (with the stub `dataset` verb, which phase 3 replaces) finishes in under 30 minutes | `python tests/dataset/run.py` |

The report goes to `dataset/reports/<run-id>/` and the summary (counts per set,
per licence, tag coverage, the under-minimum list) into the last commit message.

## 5. Kill criterion and the question to the owner

**Quantity:** admissible files actually fetched and registered (rows with a
verified hash and an admitted licence). **Threshold:** fewer than 300 after the
named sets, the scouted sets, synthesis, mutation and the Office generation.

**Question, word for word:** "The dataset holds N admissible files (by set:
<counts>); <sets> were refused by the proxy or had unclear licences. Which do you
want? (a) Synthesise and mutate more files to reach 300 (about 2 to 3 hours and
$30 to $50; the extra files are variations of the same sources, so coverage of
real producers stays at the current level); (b) fetch <the refused sets> by hand
on a network that allows them and place them under `dataset/cache/` for
`fetch.py --offline` (about 30 minutes of your time and 1 hour and $15 of
session time to register them); (c) accept N for now and reach 500 in phase 10
(no cost now; phase 3's gate is then measured on N files and says so)."

**The unattended action:** write the question with the evidence (the
`report.py` output and the refused sources with their error lines) into
`OPEN-QUESTIONS.md`; take option (a) up to 300 files if synthesis and mutation
can reach it in this session, otherwise stop at what exists; merge the branch
if the checks and the unit tier are green, with trailer
`Build-Phase: 2 gate-failed`; phases that depend on it proceed and report their
gates with that caveat.

## 6. Rollback

Reverting the merge removes the tools and the manifest; the downloaded files in
the ignored folders stay until deleted by hand (`dataset/cache/`,
`dataset/files/`, `build/cache/pdfium/`). Nothing is installed outside the
repository except the `--user` pip packages of `requirements.txt`.

## 7. Execution order inside the phase

1. `common.py`, the manifest and taxonomy formats, `dataset/README.md`'s
   column chapter; `report.py`; their tests first.
2. `tests/pdfium/fetch.py` and `pdfium.py`; open one file to prove the binding.
3. `fetch.py`, `sources.tsv` with the named sets; fetch them; `autotag.py` and
   `add.py`; register the selected files.
4. Scouting: search, record each candidate in `sources.tsv`, fetch the admitted
   ones.
5. `mutate.py` over a clean subset (one file per mutation per source producer,
   about 60 files).
6. `synth/` for every tag still under minimum.
7. `generate_office.ps1`; run it; register the outputs.
8. `words.py`, `compare.py --self-test`; `tests/dataset/run.py` and
   `outcome.md`.
9. The gate of §4; the summary into the last commit message.

**Split point:** after step 3 with at least 200 rows registered and verified,
pushed with `Build-Phase: 2 incomplete`; a second session continues from step 4.
Phase 3 may start on the incomplete manifest.

## 8. Opening prompt

> Read `CLAUDE.md` (chapters 6, 7, 8 and 11 above all), `docs/DESIGN.md`
> chapters 15 (the channel table, one tag per row) and 19 (the refusal codes
> the expectations name), `build-1.md` §9 for what phase 1 left behind, and all
> of `build-2.md`. Work in a worktree on branch `phase/2-dataset` created from
> `main`. Phase 3 runs at the same time in another worktree and owns `src/`,
> `tests/unit/` and `cli/`; touch none of them. The dataset files, the download
> cache and PDFium live in the main checkout's ignored folders, resolved
> through `git rev-parse --git-common-dir`, so every worktree reads one copy.
> No client document ever enters the dataset; admit a public set only when its
> licence or terms allow local testing, record the licence per row, and commit
> no PDF. Scout the web for further public PDF test corpora and record every
> candidate. Run `generate_office.ps1` yourself: Word, Excel, PowerPoint and Edge
> are on the laptop, and the script kills any application after 120 s. A source
> the proxy refuses is named and skipped, never worked around. Every external
> fact (a URL pattern, a licence, an API signature) carries its source and
> whether you read it or recalled it, in the comment and in `docs/SOURCES.md`.
> Run `python build/check.py` and `ctest --preset unit` before starting and
> before finishing. When fewer than 300 admissible files exist, follow §5's
> unattended action and do not wait for the owner. Finish with a last commit
> whose message carries the gate numbers of §4 and the trailer
> `Build-Phase: 2 done` (or `incomplete` or `gate-failed` with the reason), push
> the branch, and never merge to `main`: the team lead merges. Record any
> deviation in §9 and in every later build file it affects.

## 9. Deviations recorded by this phase

1. (none yet)
