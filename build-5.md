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

1. (none yet)
