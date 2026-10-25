# build-5 — The text model, the layout, the `text` command and the agreement gate

| Field | Value |
|---|---|
| Phase | 5 |
| Branch | `phase/5-text` |
| Depends on | 4 |
| Parallel with | 6 (decoders only), 7 |
| Model | `opus` |
| Owns | `src/text/`, `tests/unit/text/`, `src/api/text.cpp`, `cli/verbs/text.cpp`, `tests/pdfium/text_agreement.py`, `dataset/tools/synth/hidden_text.py`, `src/fonts/font_census.hpp`, `src/fonts/font_census.cpp`, `cli/verbs/dataset_fonts.cpp`, `tests/dataset/font_census.py` |
| Size | about 5,000 lines of C++ including tests |
| Price | estimate: 11 to 14 hours, about $160 to $240 of model spend at list prices (change-12c's text batch, about 3,400 lines of C#, was estimated at 8 to 10 hours and $120 to $180; scaled by 1.3 for C++) |

Shared files touched additively: `docs/ABI.md` (the `pdfkit_text` section),
`docs/SOURCES.md`, the `text` column of `dataset/manifest.tsv`, and
`tests/dataset/run.py` only to register the `text` expectation if phase 2 left
no registration point (recorded in §9). This phase removes phase 4's census
approximation (it owns those files for that purpose).

## 0. What this phase ships

- Catalogue row **2, Extract text** (`pdfkit_text`): for one page or all, the
  blocks, lines and words in reading order, and beneath them the glyph list with
  every field of the glyph record (CLAUDE.md §4).
- The **content interpreter** (`docs/DESIGN.md` §6), the one walker of a page's
  content that extraction, image occurrences (phase 6) and redaction (phase 8)
  all read. It also records figures and image occurrences.
- The **layout** (`docs/DESIGN.md` §7) and the **hidden-text classes**
  (`docs/DESIGN.md` §8).
- The CLI verb **`text`**, and the `text` expectation of the `dataset` verb.
- The **reading gate**: agreement with PDFium over the dataset, the kill
  criterion of the whole approach.

## 1. What it reads from earlier phases

- `src/fonts/` as merged by phase 4: `PdfFont::decode`, `width`, the vertical
  metrics, `is_bold`, `DecodingRung`, `FontLoader` and its cache. Read
  `build-4.md` §9 first: if phase 4 merged `gate-failed`, its census numbers are
  the baseline this phase must beat and the open question stands.
- `src/syntax/`: the content lexer (operators with operands and byte ranges, the
  `/Contents` concatenation, inline images), the page list with boxes and
  rotation, resources with the form fallback, the damaged-stream flag.
- `src/platform/`: budget, checked arithmetic, the JSON writer (number
  formatting through `std::to_chars`).
- `src/api/` and `cli/` from phase 1: the catalogue registry row for
  `pdfkit_text` (a stub returning not-implemented), `api::guarded`, the verb
  stub `cli/verbs/text.cpp`, `docs/ABI.md`'s conventions.
- `tests/pdfium/` and `tests/dataset/run.py` from phase 2: how PDFium's text is
  read per page, how the dataset verb's per-file outcomes are compared to the
  manifest.
- `build-3.md` §9 and `build-4.md` §9.

## 2. Components

Namespace `pdfkit::text`, one class per file in `src/text/`.

| File | What it holds | Lines | Reference and status |
|---|---|---|---|
| `glyph_record.hpp` | One shown glyph: page index; stream identity (page contents, or a form by object number with the form path it was reached through); operator index; operand index; byte range of the code within the string operand; font (object number); code; decoded text and rung; advance in text space; device-space box (origin and advance under the full transformation, ascent and descent from the font); render mode; fill colour (colour-space name and components); marked-content and optional-content context; the `/ActualText` span id; clip rectangle; outside-media and outside-crop flags; hidden classes (filled by `HiddenText`); the duplicate partner's operator when folded | 150 | change-12c §9.1 `GlyphRecord.cs` (read in the hand-over) |
| `figure_record.hpp` | A group of path-painting operators between one `q` and its `Q`, with device box and operator range | 60 | design record §4.5 (read) |
| `image_occurrence.hpp` | One `Do` of an image XObject or one inline image: page, ordinal in draw order through forms, the image's object number (or the inline image's operator range), the CTM, the device box, the form path. Phase 6 reads it | 70 | design record §4.5 (read) |
| `graphics_state.hpp/.cpp` | The graphics and text state: CTM, clip rectangle, fill and stroke colour and colour space, text matrix and line matrix, `Tc`, `Tw`, `Tz`, `TL`, `Ts`, `Tf` font and size, `Tr`; the `q`/`Q` stack with a depth limit (the budget refuses a stack past 256, an author decision recorded in `docs/DESIGN.md` §6 if it is not already there) | 300 | ISO 32000-1 §8.4, §9.3 (read by the design record) |
| `content_interpreter.hpp/.cpp` | The state machine of `docs/DESIGN.md` §6 over a page: `cm`; the text object operators; `Td`, `TD`, `T*`, `Tm`; `Tj`, `TJ` (numbers adjust by `-adj/1000 * Tfs * Th`), `'`, `"`; the advance `tx = ((w0 - adj/1000) * Tfs + Tc + Tw) * Th` with `Tw` on the single-byte code 32 alone (and on no code of a composite font, whose codes are multi-byte unless the CMap defines a one-byte 32); vertical writing through `/W2` for `Identity-V`; colour operators as values; `W`/`W*` with the current path's bounding rectangle as the clip; `BDC`/`BMC`/`EMC` with `/ActualText`, `/Alt`, `/E` and the optional-content group noted; `Do` of a form walked with `/Matrix` and `/BBox`, depth 32, a visited set per path refusing a form that draws itself, a form drawn twice walked twice; `Do` of an image and `BI` recorded as occurrences; path operators recorded as figures; patterns and soft masks not recursed; Type 3 glyphs as records; a damaged stream interpreted as far as it reads, the page noted | 1,100 | ISO 32000-1 §8, §9.4.4 (read by the design record); MuPDF's filter model (technique only, AGPL, no code) |
| `actual_text.hpp/.cpp` | A span's `/ActualText` replaces the decoded text of the span's glyphs when the two differ; the rung `actual_text` recorded; the text attached to the span's first glyph and the others marked as covered by the span, so a match still maps to every glyph | 150 | design record §4.2 rung 4 (read); pdf.js issue 12237 (read by the design record) |
| `content_serialiser.hpp/.cpp` | The inverse: a page's operators (text-showing ones represented by their glyph records) written back to bytes. Writes what it is given and formats nothing; held to the identity over every fixture page and every dataset page (§4). Phase 8's surgery writes through it | 350 | design record §4.4 and change-12 DR1 (read) |
| `text_layout.hpp/.cpp` | Glyphs to words (gap under 0.3 of the font size one word, up to 2.0 two words, beyond a block boundary), lines (baseline within a fifth of the font size; rotated text by its own baseline direction; vertical text by column), blocks and order (recursive XY-cut over line boxes with the Docstrum refinement), the tagged-PDF structure order where `/StructTreeRoot` is present and every content item resolves to the page's glyphs, the hyphenation rule (a line-ending hyphen before a lower-case start joins; before a capital it stays), right-to-left lines ordered by their direction, the duplicate fold (same code, same font, box within a tenth of the font size; one record carrying both operators). Every sort `std::stable_sort`; every key rounded to 0.01 pt | 900 | the thresholds are the design record's §4.2 (read); within PDFBox's (0.5, 0.3) and pdfminer's ranges (read by the design record) |
| `xy_cut.hpp/.cpp`, `docstrum.hpp/.cpp` | The two segmenters, ported from PdfPig (Apache-2.0): `src/UglyToad.PdfPig.DocumentLayoutAnalysis/PageSegmenter/RecursiveXYCut.cs` (620 lines measured) and `DocstrumBoundingBoxes.cs` (761 lines measured); each header names the source file, the commit read and the licence, and states the port's own changes (stable sorts, rounded keys). PdfPig's nearest-neighbour word extractor is **not** ported (its committed review records a non-deterministic order) | 900 | https://github.com/UglyToad/PdfPig (read by the design record; file paths recalled, verified when cloned and recorded in `docs/SOURCES.md`) |
| `hidden_text.hpp/.cpp` | The classes of CLAUDE.md §4 and `docs/DESIGN.md` §8 per glyph: `render_invisible` (mode 3 or 7), `white_fill` (DeviceGray 1, DeviceRGB 1 1 1, DeviceCMYK 0 0 0 0, with no filled path or image under it that is not white), `clipped`, `off_media`, `off_crop`, `optional_content_off` (default configuration OFF, including `/OCMD` evaluation of `/OCGs` and `/P`), `duplicate`, `covered` (an image or an opaque filled path drawn later over the glyph's box); a page's records split into visible and hidden sets, the hidden set laid out by the same rules after the visible blocks | 350 | design record §4.2 (read); the `covered` class is CLAUDE.md §4's |
| `page_text.hpp/.cpp` | The per-page result: records, words, lines, blocks, hidden blocks, unreadable count per font name, warnings (`unreadable_text`, `hidden_text`, `font_not_embedded`, `damaged_stream`, `resource_missing` of `docs/DESIGN.md` §19) | 200 | `docs/DESIGN.md` §19 |
| `text_json.hpp/.cpp` | The JSON of `pdfkit_text` in the shape this phase writes into `docs/ABI.md`: pages, each with blocks (lines, words with glyph index ranges and boxes), hidden blocks, glyphs (when glyph level is on: every record field), warnings; keys in fixed order, numbers rounded to 0.01 pt | 300 | `docs/ABI.md` |
| `src/api/text.cpp`, `cli/verbs/text.cpp` | The catalogue function and the verb (`pdfkit text <file> [--pages 1-3,7] [--glyphs]`), exit codes of CLAUDE.md §2 | 200 | CLAUDE.md §2, §3.6 |

Phase 4's census files are deleted, and the `dataset --fonts` mode is replaced by
reading the rung counts out of the `text` outcome; recorded in this file's §9
and in `build-4.md`'s header is left as written.

## 3. Tests

Unit tier, `tests/unit/text/`, fixtures written in code through
`tests/unit/support/pdf_builder` and a content-stream helper this phase adds in
`tests/unit/text/support/`.

| File | Holds |
|---|---|
| `content_interpreter_test.cpp` | Each operator's effect on the next glyph's box and advance; `Tw` on code 32 alone and never on a two-byte code; `TJ` adjustments; `Tz` and `Ts`; `'` and `"`; forms with matrices; the depth limit and the self-drawing form; inline images and `Do` recorded as occurrences with ordinals through forms; figures between `q` and `Q` |
| `content_serialiser_test.cpp` | `records_reserialise_to_the_stream` over every fixture page of the unit tier (the identity of change-12's DR1) |
| `text_layout_test.cpp` | Words by gap; lines by baseline; two and three columns in reading order; a rotated block; a vertical (`Identity-V`) column; a right-to-left line; a ruled table's cells as blocks in order; hyphenation both ways; a duplicate folded; `order_is_stable` (glyph list shuffled with ten seeds, the output identical bytes) |
| `hidden_text_test.cpp` | One fixture per class, each glyph extracted, flagged with its class, placed after the visible blocks, counted in the page warning |
| `actual_text_test.cpp` | A ligature under `/ActualText`; a span whose `/ActualText` agrees (no replacement); a nested span |
| `text_json_test.cpp` | The JSON of a two-page fixture against an expected string written in the test; twice in one process, identical bytes |

**Dataset.** The phase fills the `text` column for every row: `agree` (F1 at or
above 0.90), `known_gap:<cause>` (below, with the cause class of §4), or `n/a`.
It adds `dataset/tools/synth/hidden_text.py`, which writes one file per hidden
class with planted marker words, tagged with that class; and it checks every
text-layout tag of `dataset/taxonomy.tsv` is at its minimum, synthesising files
for any that is not (no space glyphs, kerning-split words, ligatures,
hyphenation across lines and pages, multi-column, rotated, vertical,
right-to-left, tables, headers and footers).

**PDFium.** `tests/pdfium/text_agreement.py` computes, per file, the bag-of-words
F1 of CLAUDE.md §7.5 between pdfkit's text (all pages, hidden blocks included,
because PDFium reads them too) and PDFium's pages joined.

## 4. Gate

Over every dataset file both readers open:

1. **Agreement**: the per-file bag-of-words F1 has a **median of at least
   0.97**, and **at most one file in twenty is below 0.90**. Every file below
   0.90 is named in the report with a cause class: `pdfkit` (a defect here),
   `pdfium` (PDFium reads it wrongly, shown by the PDF's own text), or
   `legitimate` (hidden text PDFium drops, a hyphen join, an `/ActualText`
   PDFium ignores, a duplicate fold), with one line of evidence.
2. **Determinism**: two runs of the `dataset` verb over the whole dataset
   produce byte-identical `text` outcomes on 100% of files.
3. **Re-serialisation identity**: on 100% of pages that interpret without a
   damaged-stream flag, the serialiser reproduces the page's content bytes (and
   every form's) exactly.
4. **Hidden text**: on the synthesised hidden-text files, 100% of planted
   hidden marker words are extracted and flagged with their class.
5. **No crash, no hang**: every file returns within 30 s; the sanitizer tier is
   green; the unit tier and `python build/check.py` are green.

Commands:

```
cmake --preset mingw-release && cmake --build --preset mingw-release
python tests/dataset/run.py --functions open,text --out dataset/reports/phase5-<date>/
python tests/dataset/run.py --functions open,text --out dataset/reports/phase5-<date>-b/ --compare-to dataset/reports/phase5-<date>/
python tests/pdfium/compare.py --run phase5-<date> --text
ctest --preset unit && ctest --preset sanitize
python build/check.py
```

The report (`agreement.tsv`: set, id, pages, F1, unreadable codes by rung,
hidden counts, warnings, cause class; and `summary.md` with the median, the
count below 0.90 and the per-tag medians) goes into the merge commit message.

## 5. Kill criterion and the question to the owner

**Quantity:** the median per-file F1 of gate item 1, and the share of files
below 0.90. **Threshold:** a median below 0.97, or more than one file in twenty
below 0.90.

**Question, word for word with the numbers filled in:** "pdfkit agrees with
PDFium on N of M files at the gate; the files below it fail on <causes>; do you
want (a) one more fonts/layout session on those causes (~8-12 h, ~$150-250),
(b) accept the reader as is and continue to redaction, recording the named files
as known gaps, or (c) stop and re-plan?"

**Unattended action:** write the question with the evidence (the summary, the
twenty worst files with their cause classes) into `OPEN-QUESTIONS.md`; merge the
branch if checks and the unit tier are green, with trailer
`Build-Phase: 5 gate-failed`; phases that depend on it proceed and report their
gates with that caveat. Phases 8 and 9 rely on the text model existing, not on
the agreement number, so they proceed; phase 6's listing step proceeds too.

## 6. Rollback

Reverting the merge removes the text layer, `pdfkit_text` returns to the phase-1
stub, and the `text` verb to its stub; phase 4's census is not restored (revert
its removal commit too if the census is wanted back). No persisted shape changes:
the `text` column of the manifest is reverted with the merge.

## 7. Execution order inside the phase

1. Run `python build/check.py` and `ctest --preset unit` on the base.
2. `glyph_record`, `figure_record`, `image_occurrence`, `graphics_state`,
   `content_interpreter`, `content_serialiser` and the re-serialisation test
   over every fixture page; failing tests first. This is green before anything
   else is written, because phase 8 edits the bytes these records point at and
   an off-by-one here is invisible to every text assertion.
3. Run the re-serialisation identity over the whole dataset (a test mode of the
   dataset verb); fix every page that fails before going on.
4. `actual_text`, `hidden_text` and their tests; `dataset/tools/synth/hidden_text.py`.
5. `text_layout`, `xy_cut`, `docstrum` and their tests.
6. **Split point.** If the session is running out of room, push here with
   `Build-Phase: 5 incomplete` naming what remains (JSON, ABI, verb, gate).
7. `page_text`, `text_json`, `src/api/text.cpp`, `cli/verbs/text.cpp`, the
   `docs/ABI.md` section; remove phase 4's census.
8. The gate run of §4; read every file below 0.90 and classify its cause; fix
   pdfkit causes that are rules of `docs/DESIGN.md` §5 to §8 implemented wrong;
   re-run.
9. Documentation lines this phase invalidates: `docs/ABI.md` (the
   `pdfkit_text` shape), `docs/SOURCES.md` (PdfPig files and commit),
   `docs/DESIGN.md` §6 to §8 only where the code proves a rule wrong, recorded
   in §9.

## 8. Opening prompt

> Read `CLAUDE.md`, `docs/DESIGN.md` §4 to §8 and §19, `docs/ABI.md`,
> `docs/SOURCES.md`, all of `build-5.md`, and the deviations sections (§9) of
> `build-1.md` to `build-4.md`. Before planning, read what landed in
> `src/fonts/` (`PdfFont`, `DecodingRung`, `FontLoader`), `src/syntax/` (the
> content lexer, pages, resources), `src/api/` (the registry, `guarded`),
> `cli/verbs/text.cpp`, `tests/dataset/run.py` and `tests/pdfium/`. Work in a
> worktree of your own on branch `phase/5-text` created from `main`; other
> phases may run at the same time in other worktrees, so touch only the paths
> this file's header owns and add, never rewrite, rows in the shared files it
> names. Run `python build/check.py` and `ctest --preset unit` before you start
> and before you finish; if the base is red, repair it in a commit of its own and
> record the repair in §9. Write the re-serialisation test first and keep it
> green throughout. Every rule copied from a reader rather than the
> specification names the reader in its comment, and every external fact
> carries its source and whether you read it or recalled it, in the comment and
> in `docs/SOURCES.md`. The dataset is in the main checkout's `dataset/files/`
> and PDFium in its `build/cache/pdfium/`, reached through
> `git rev-parse --git-common-dir`. No step waits for the owner: when the kill
> criterion of §5 trips, write its question with the evidence into
> `OPEN-QUESTIONS.md` and finish. Finish by pushing the branch with a last
> commit whose message carries `Build-Phase: 5 done`, or
> `Build-Phase: 5 incomplete` (split point of §7) or
> `Build-Phase: 5 gate-failed`, with the gate's numbers in the message. Never
> merge into `main`; the team lead merges. Record every deviation from this file
> in its §9 and in every downstream build file it affects (`build-6.md`,
> `build-8.md`, `build-9.md`, `build-10.md`) in the same change.

## 9. Deviations recorded by this phase

1. **Rebased on phase 7.** Phase 7 merged while this phase ran; the branch
   was rebased onto it (`dataset/manifest.tsv` with `expect_write` and
   `expect_select`, `cli/verbs/dataset_write.cpp`, the writer). The `text`
   member of the dataset outcome is written beside phase 7's members.
2. **Files added beyond §2**, each to keep one public class per header or one
   concern per file: `src/text/matrix.*` (`Matrix`), `rect.*` (`Rect`),
   `colour.*` (`Colour` and the white test), `hidden_class.hpp` (the class
   bits and names), `content_stream.hpp` (`ContentStream`, one walk of a
   stream: bytes, operations, form path), `marked_span.hpp` (`MarkedSpan`),
   `page_content.*` (`PageContent`, the interpreter's result),
   `string_operands.*` (the map from a string operand's bytes to the bytes
   that wrote them), `text_string.*` (PDF text strings to UTF-8),
   `optional_content.*` (the default configuration and `/OCMD`),
   `structure_order.*` (the tagged reading order), `text_reader.*`
   (`TextReader`, the passes in their order, shared per document);
   `tests/unit/text/support/text_document.*` (the fixture: three fonts, every
   interpretation checked for the re-serialisation identity);
   `cli/verbs/dataset_text.*` (the dataset verb's `text` member);
   `tests/pdfium/text_causes.tsv` (the cause of every file below 0.90).
3. **The glyph list is in reading order**, not draw order: the visible blocks'
   glyphs, then the hidden blocks', each record naming its `draw` index, so
   every word, line and block is a half-open range of that list
   (`docs/DESIGN.md` §7 says so now).
4. **Hyphenation** keeps the two halves as words of their own lines, the
   first marked `hyphenated`, so ranges stay inside their lines; the join
   across a page break is made when both pages are asked together.
5. **Duplicates**: the copy stays a record of its own, in the hidden set with
   the class `duplicate` and `duplicate_of` naming its partner, which lists
   it among its `duplicates`; the page's `text` reads a word whose only class
   is `duplicate` once. Fonts compare by name, because a stamp that adds its
   own Helvetica object on every draw (tika-0012) is one face; a glyph of no
   size is never a duplicate.
6. **The rules the dataset proved wrong**, changed in `docs/DESIGN.md` §7:
   the word gap is 0.1 of the font size on a line with no space glyph
   (pdfminer's `word_margin`; measured: gaps inside words within 0.07, between
   positioned words from 0.15; 0.3 merged words on 95 files), 0.3 on a line
   with space glyphs (its other gaps are letter spacing), 1.0 between two
   ideographic glyphs; a baseline's glyphs keep the content's runs whole
   (an overprinted name was read with its letters interleaved); the
   direction pass reverses runs from visual order (a drawing-order rule read
   Arabic backwards when a line mixed scripts); text at a negative size or
   scaling reads along its advance.
7. **Blocks and order**: Docstrum groups lines into blocks and XY-cut orders
   the blocks in the page's dominant direction (the plan cut over lines with
   Docstrum refining); a ruling drawn between two lines keeps them apart, so a
   ruled table's rows are blocks; the structure order reorders the geometric
   blocks by their lowest structure rank rather than re-segmenting. PdfPig's
   two files could not be cloned (the session's sandbox refused git and
   scripted downloads); they were read through the web reader at commit
   e17fc2d577a24db986d8642ec7190188d0f6a0a9 and the algorithm ported, no
   code copied (`docs/SOURCES.md`, `THIRD-PARTY-NOTICES.md`).
8. **Bounds chosen here**: a `q` past 256 is counted and saves nothing (not a
   refusal), recorded in `docs/DESIGN.md` §6.1; forms nest 32 deep; the
   structure tree is walked 256 deep and 1,000,000 nodes at most; `covered`
   is not decided on a page past 50,000,000 glyph-shape comparisons; more
   than 5,000 thin shapes are not used as rulings.
9. **Hidden classes as built**: `covered` needs an image without a mask or an
   opaque filled rectangle (one `re` under an axis-aligned CTM), because the
   bound of any other path is not what it paints; `white_fill` tests the
   colour the render mode paints with (the stroke for modes 1 and 5). The
   glyph record keeps the fill colour and only whether the stroke is white.
10. **A vertical glyph's box** is its cell: across, its width about the
    position vector's `vx`; down the column, the vertical advance.
11. **`pdfkit_text` options**: `pages`, `blocks`, `glyphs`, `reserialisation`;
    the shape is the `pdfkit_text` section of `docs/ABI.md` (numbered 4.8
    after phase 7's sections; phase 6 adds its own, the team lead
    renumbers). The CLI verb takes `--pages 1-3,7` and `--glyphs`.
12. **The re-serialisation identity** is a field of `pdfkit_text`
    (`reserialisation`) that the dataset verb asks for, rather than a test
    mode of the verb; `ContentSerialiser::write(stream, index, records,
    edits)` decodes each record's bytes again, so an off-by-one range is a
    named mismatch.
13. **The census is removed** and `expect_fonts` is driven by the `text`
    outcome: `tests/dataset/run.py` judges it from the pages' `unreadable`
    counts, `tests/pdfium/text_agreement.py --write-manifest` fills it with
    `expect_text`; `dataset/tools/common.py`, `add.py`, `dataset/README.md`,
    `tests/dataset/outcome.md` and `tests/dataset/test_run.py` say so.
    `expect_text` is `n/a` for a file where neither reader finds a word.
14. **`dataset/tools/synth/hidden_text.py` was not written**: phase 2's
    `dataset/tools/synth/patterns/hidden.py` already writes three files per
    hidden class with planted markers (24 files); the check of gate item 4 is
    `python tests/pdfium/text_agreement.py --run <id> --hidden`. Every
    text-layout tag of the taxonomy is at its minimum, so nothing was
    synthesised.
15. **The gate commands as run**: `python tests/dataset/run.py --checks
    open,text --runs 2 --run-id phase5-final` (run.py has no `--out` or
    `--compare-to`; its second run is the determinism check) and `python
    tests/pdfium/text_agreement.py --run phase5-final --hidden` (compare.py's
    `--check text` is phase 7's writer check).
16. **The gate failed on its share below 0.90** (6.4 % against 5 %, median
    1.0); every other item passed. The question with the evidence is
    `OPEN-QUESTIONS.md` §2.10; its recommended answer (b) stands.
17. **Speed**: the largest file (dccc-0135, 910 pages, 3.6 million glyphs)
    reads in 6 to 10 s alone in the release build, most of it the
    interpreter. Other sessions loaded the laptop during the runs: with six
    workers a run saw up to four files pass 30 s (dccc-0074 among them,
    0.4 s alone); the final run, four workers and two passes, saw none.
18. **Known reader gaps for a later fonts session**: Latin codes of a
    non-embedded CJK font with no `/W` take `/DW` 1000 and run off the page
    (dccc-0012, synth-0151); CJK predefined CMaps have no data
    (`docs/DESIGN.md` §5.3); a few subset fonts read no rung (pdfdiff-0025,
    pdfdiff-0026, pdfjs-0042).
19. **What the images listing reads** (`build-6.md` §9's request): an inline
    occurrence carries `colour_spaces`, the drawing content's `/ColorSpace`
    resources; `PageContent::unwalked` names every tiling pattern and
    soft-mask group a page sets, which the interpreter does not walk, so the
    listing reports the images inside as `image_in_pattern` without
    numbering them.
20. **AddressSanitizer over the dataset** (llvm-mingw, the `dataset` verb on
    all 937 files) found a pointer kept into a temporary (a colour space's
    resolved name, 115 files); fixed, the rerun found no error (25 files pass
    30 s in that debug build only).

### Deviations of earlier phases that affect this one

From `build-1.md` §9:

- The JSON tree type is `platform::JsonValue` in `src/platform/json_value.hpp`; options arrive through `api::parse_json_object` (item 2).
- `cli/verbs/text.cpp` takes `--options <json>` and `--open-options <json>` and is complete once the functions exist (item 7).
- The sanitizer tier is two runs: `ctest --preset sanitize` (GCC, UBSan in trap mode) and `ctest --preset clang-sanitize` (llvm-mingw, ASan and UBSan) (`build-1.md` §9 item 8).

From `build-3.md` §9:

- The object model is `syntax::Object` (nested `Reference`, `Number`, `String` with its literal-or-hex flag, `Array` = `std::vector<Object>`), `syntax::Dictionary` (insertion-ordered) and `syntax::Stream` (dictionary, raw byte range, owner); all immutable and shared, built through the `make_*` factories (item 3).
- `syntax::Document::open(bytes, Limits)` returns the document; `object(reference)`, `resolve(object)` and `resolve_entry(dictionary, key)` resolve (strings come back decrypted), `decode(stream, page)` decrypts and runs the filters up to the first image codec (`Filters::Result::remaining` names the codecs left), `content_pieces(contents, page)` decodes a `/Contents` value, `pages()` holds `syntax::Page` (boxes, rotation, resolved resources, contents), `repair(reason)` and `warn(code, message, page)` record (items 3, 4).
- `syntax::Resources(document, page.resources, page.number)` looks a resource up by category and name, with the form fallback in `for_form`; `syntax::ContentLexer(pieces, document.limits(), "page N")` yields `Operation`s (operator, operands, byte range, `operator_start`; an inline image is one `BI` operation with its data range) and `locate(offset)` maps back to the piece (item 10).
- `Document::decode` charges the budget on every call, so a layer that decodes a shared stream more than once caches the decoded bytes (item 11).
- The fixture writer is `test::PdfBuilder`, `test::ContentBuilder` and `test::open_document` in `tests/unit/support/`; the `dataset` verb writes `text: null` until this phase fills it (item 12).

From `build-4.md` §9:

- The fonts API as built: `fonts::FontLoader loader(document)` and `loader.load(entry)`, where `entry` is the `/Font` resource value as written (pass the reference, not the resolved dictionary, so the font is cached by object number; `syntax::Resources::lookup` returns resolved objects, so read the raw entry from the resource dictionary as `fonts::FontCensus` does); `PdfFont::decode(bytes)` returns `std::vector<PdfFont::DecodedCode>` with `code`, `byte_length`, `text` (UTF-8, empty when unreadable), `rung` and `decomposed`; `width(code)` is in thousandths of text space (a Type 3 width already through `/FontMatrix[0]`, so the interpreter applies `Tfs` only); `vertical_advance(code)` gives `w1`, `vx`, `vy` for `is_vertical()` fonts; `vertical_metrics()` gives ascent and descent in thousandths with a `fallback` flag (0.8 and -0.2 em); `widths_estimated()` is the `font_not_embedded` condition; `unreadable_count()` counts over the whole document, so the per-page `unreadable_text` count is the interpreter's own tally of `DecodingRung::unreadable` records (items 2, 8).
- `Tw` applies to a code whose `byte_length` is 1 and whose `code` is 32 only; a composite font's two-byte code 32 takes no word spacing (`docs/DESIGN.md` §6.1).
- The census approximation this phase removes: `src/fonts/font_census.{hpp,cpp}`, `tests/unit/fonts/font_census_test.cpp`, `tests/dataset/font_census.cpp` (the `pdfkit_font_census` target), `tests/dataset/CMakeLists.txt` and its `add_subdirectory(tests/dataset)` line in the root `CMakeLists.txt`, and `tests/dataset/font_census.py`. `cli/verbs/dataset_fonts.cpp` was never created (item 2). The manifest's `expect_fonts` column (`loads`, `unreadable:<n>`, `n/a`) is filled by the census (item 6): when the census goes, either drive the column from the `dataset` verb's per-page `unreadable` counts or drop it from `dataset/tools/common.py`, `add.py`, `tests/test_common.py` and `dataset/README.md` in the same change.
- The census baseline this phase's interpreter must beat or match: median character F1 1.0 over 736 files, 0.54% unreadable codes. Its known gaps, none a reader fault: CJK fonts with a predefined non-Unicode CMap or a non-embedded CID font and no `/ToUnicode` (no CMap data ships); Type 3 fonts whose glyph names restate their codes (`OPEN-QUESTIONS.md` §2.5); private-use `/ToUnicode` entries PDFium emits and pdfkit never does; Arabic presentation forms PDFium normalises; duplicated (fake bold) text PDFium removes; forms drawn several times, which the census reads once (item 9).
- `fonts::DecodingRung::actual_text` exists for the interpreter to set on every glyph of an `/ActualText` span; the fonts layer never sets it.

From `build-6.md` §9:

- `tests/dataset/CMakeLists.txt` also holds `pdfkit_image_census` (the images layer's gate tool, `tests/dataset/image_census.cpp`): when the font census goes, keep the file and its `add_subdirectory(tests/dataset)` line in the root `CMakeLists.txt` and remove only the font census's target (item 3).
- The images listing step resumes after this phase and reads `text::ImageOccurrence`: per draw, the page, the ordinal in that page's draw order (forms included, from 0), the device box (the unit square under the CTM), the form path, and what was drawn: the image XObject's stream (as resolved, for `images::ImageIdentity::of_stream`) or, for `BI`, the operation's dictionary, its data range in `ContentLexer::bytes()` and the drawing content's `/ColorSpace` resource dictionary (for `images::ImageIdentity::of_inline` and `images::ImageDecoder::decode_inline`); an image met inside a pattern or a soft-mask group is reported as such and not numbered (item 1).
