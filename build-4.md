# build-4 — Fonts and the decoding ladder

| Field | Value |
|---|---|
| Phase | 4 |
| Branch | `phase/4-fonts` |
| Depends on | 3 |
| Parallel with | 6, 7 |
| Model | `opus` |
| Owns | `src/fonts/`, `tests/unit/fonts/`, `third_party/agl/`, `third_party/core14/`, `build/fonts/core14.py`, `build/fonts/core14.lock`, `tests/dataset/font_census.py`, `cli/verbs/dataset_fonts.cpp` |
| Size | about 5,000 lines of C++ including tests, plus two data tables |
| Price | estimate: 13 to 18 hours, about $230 to $340 of model spend at list prices (change-12b's fonts batch, about 3,600 lines of C#, was estimated at 10 to 14 hours and $180 to $260; scaled by 1.3 for C++) |

Shared files touched additively: `third_party/inventory.tsv`,
`THIRD-PARTY-NOTICES.md`, `docs/SOURCES.md`, the `fonts` column of
`dataset/manifest.tsv`, and `src/CMakeLists.txt` only if phase 1 did not leave a
`src/fonts/CMakeLists.txt` stub. The team lead resolves conflicts in these with
phases 6 and 7.

## 0. What this phase ships

The fonts layer of CLAUDE.md §3.1: given a font dictionary resolved through
phase 3's object layer, a `pdfkit::fonts::PdfFont` that splits a string
operand into codes, decodes each code to Unicode text with the rung that
answered, and answers the code's width in text space. It is the layer the
content interpreter of phase 5 asks for every glyph, and the layer the
`unreadable_text` warning (`docs/DESIGN.md` §19) counts in.

No catalogue row of CLAUDE.md §2 is delivered by this phase alone: the fonts
reach the caller through `pdfkit_text` in phase 5. The phase adds one
measurement mode to the CLI's `dataset` verb (`pdfkit dataset --fonts`, the font
census of §4), labelled in its help text as a census approximation that phase 5
replaces with the interpreter.

## 1. What it reads from earlier phases

Before planning, read what landed, not what phase 3 planned:

- `src/syntax/` as merged: the object model (the dictionary, array, name,
  string, number, stream and reference types), the document's resolver, the
  decoded bytes of a stream under the inflate limits, the page list with
  inherited resources, the resource lookup with the form-to-page fallback, and
  the content lexer (operators with their operands and byte ranges). A font
  dictionary is resolved through the resolver; a font program (`/FontFile`,
  `/FontFile2`, `/FontFile3`) and a CMap or `/ToUnicode` stream are streams read
  through the filters.
- `src/platform/`: the budget (every table count read from a font program is
  checked against it before anything is reserved), the checked arithmetic, the
  error type, the JSON writer.
- `tests/unit/support/pdf_builder`: the fixture writer; this phase adds a font
  helper beside it in `tests/unit/fonts/support/` (it owns that folder) rather
  than editing the builder, unless a builder change is one line, recorded in §9.
- `build-3.md` §9 and `build-1.md` §9: what those phases left behind.
- `dataset/manifest.tsv` and `dataset/taxonomy.tsv` as phase 2 left them: the
  font tags (ToUnicode correct, missing, wrong; Differences with glyph-list names
  and with `uniXXXX`, `gNN`, `Cdd`; symbolic TrueType; Type 1 built-in encoding;
  CFF charset; identity CID; predefined CMap; Type 3; text as outlines) and how
  many files carry each.
- `docs/DESIGN.md` §5 (fonts and the decoding ladder), §4 (resources), §3
  (filters and limits), §19 (warnings).

## 2. Components

Namespace `pdfkit::fonts`, one class per file, files in `src/fonts/` (CLAUDE.md
§3.5). Line counts are rough and include the header.

| File | What it holds | Lines | Reference and status |
|---|---|---|---|
| `pdf_font.hpp/.cpp` | The interface the interpreter asks: `decode(span<const byte>)` yielding one `DecodedCode {code, byte_length, text, rung}` per code (one byte per code for a simple font, codespace ranges for a composite font), `width(code)` in thousandths of text space, `is_symbolic`, `name`, `is_embedded`, `kind`, `is_bold` (for the placeholder of phase 8), `unreadable_count`, and the vertical metrics the glyph box needs (ascent and descent from the descriptor, or the font program's, or a stated fallback of 0.8 and -0.2 em) | 250 | change-12b §9.1 `PdfFont.cs`, read in the hand-over |
| `decoding_rung.hpp` | The `DecodingRung` enum: `to_unicode`, `glyph_name`, `font_program`, `actual_text`, `unreadable`; `actual_text` is set by the interpreter of phase 5 and exists here so the enum is complete | 40 | CLAUDE.md §4 |
| `font_loader.hpp/.cpp` | A font dictionary to a `PdfFont`, cached per object number inside one document; dispatch on `/Subtype` (`Type1`, `MMType1`, `TrueType`, `Type3`, `Type0`), a missing or unknown subtype read as Type 1, a font with no `/BaseFont` named `unnamed`; never throws for a malformed font program (the program is dropped and the other rungs answer) | 250 | change-12b §9.1; "unknown subtype read as Type 1" is viewer behaviour recalled from change-12b, labelled recalled |
| `simple_font.hpp/.cpp` | Type 1, TrueType and Type 3: `/FirstChar`, `/Widths`, `/MissingWidth` from the descriptor, Type 3 `/FontMatrix` (widths in glyph space scaled through it), the encoding rule of `docs/DESIGN.md` §5 (base encoding by name, or the program's built-in, or Standard for a non-symbolic font; `/Differences` over it), the symbolic TrueType `cmap` order | 600 | ISO 32000-1 §9.6.6 (read by the design record); the symbolic TrueType order (3,0) with the code and with code plus `0xF000`, then (1,0), then (3,1) through the glyph name is copied from pdf.js and PDFBox (design record Appendix A, stream 2, read) |
| `type0_font.hpp/.cpp` | Composite fonts: `Identity-H` and `Identity-V`, an embedded CMap stream, the descendant `CIDFontType0` or `CIDFontType2`, `/W` and `/DW` in both array forms (and `/W2`, `/DW2` read and kept for vertical metrics), `/CIDToGIDMap` (identity or stream), text through `/ToUnicode` first, then through the CFF charset's glyph names; a predefined non-identity CMap without `/ToUnicode` decodes every code as unreadable and counts them | 450 | ISO 32000-1 §9.7 (read); the predefined-CMap rule is the owner-settled decision of the hand-over (no CMap data shipped) |
| `cmap_parser.hpp/.cpp` | The CMap syntax: `begincodespacerange`, `begincidrange`, `begincidchar`, `beginbfrange` (string and array destinations), `beginbfchar`, `usecmap` for embedded CMaps resolved against the identities, UTF-16BE destinations with surrogate pairs kept whole, a malformed entry skipped and counted | 450 | ISO 32000-1 §9.7.5 and §9.10.3 (read by the design record) |
| `encodings.hpp/.cpp` | Annex D as `constexpr` arrays of 256 glyph names: Standard, WinAnsi (unassigned codes read as `bullet`), MacRoman, MacExpert, PDFDoc, Symbol's and ZapfDingbats' built-in encodings | 700 | ISO 32000-1 Annex D (read); "WinAnsi unassigned codes to bullet as Acrobat does" is recalled from change-12b, labelled recalled |
| `glyph_list.hpp/.cpp` | The Adobe Glyph List lookup over the generated table, plus the naming conventions: `uniXXXX` (and sequences of four-digit groups), `uXXXX` to `uXXXXXX`, `gNN`, `glyphNN`, `cidNN`, `GNN`, `Cdd` (decimal), `Cxx` (hexadecimal), a suffix after a period dropped (`a.sc` reads `a`), `a_b` read as the ligature of its parts; a name nothing answers is unreadable | 300 | the AGL (BSD-3, read); the conventions are pdf.js's (`src/core/fonts.js`, design record Appendix A stream 2, read) |
| `true_type_tables.hpp/.cpp` | The table directory, `cmap` formats 0, 4, 6 and 12 with subtables by platform and encoding id, `hmtx` with `hhea`'s metric count, `head` units per em, `post` format 2 names, `loca` and `glyf` only to say whether a glyph is empty, the `CFF ` table handed on; every offset and count bounds-checked against the table length | 650 | OpenType specification (recalled; the batch verifies against Microsoft's published spec when the rails reach it and names the result in `docs/SOURCES.md`) |
| `cff_font.hpp/.cpp` | A CFF program, bare (`/FontFile3 /Type1C`, `/CIDFontType0C`) or inside OpenType: header, INDEX, top DICT, string INDEX, charset formats 0, 1, 2 and the predefined ISOAdobe, Expert and ExpertSubset, encodings (standard, expert, custom with supplements), private DICT `nominalWidthX` and `defaultWidthX`, each Type 2 charstring read only to its width (the first stack-clearing operator), CID-keyed `FDArray` and `FDSelect` | 750 | Adobe Technical Notes 5176 and 5177 (recalled; PdfPig's CFF reader, Apache-2.0, 5,585 lines measured by the design record, is the reference to read beside it) |
| `type1_font.hpp/.cpp` | A Type 1 program: PFB segment headers skipped, the clear-text `/Encoding` (`StandardEncoding` or `dup <code> /<name> put`), eexec decryption (r = 55665, four lead bytes), `lenIV`, charstring names, each charstring decrypted (r = 4330) and read to `hsbw` or `sbw` for its width | 400 | Adobe Type 1 Font Format (recalled; constants as written in change-12b §9.1, read in the hand-over) |
| `standard_fonts.hpp/.cpp` | The Core 14 widths from the generated table and the alias table a viewer applies (`Arial` and `ArialMT` to Helvetica, `Arial,Bold` and `Arial-BoldMT` to Helvetica-Bold, `TimesNewRoman` and `TimesNewRomanPSMT` to Times-Roman, `CourierNew` to Courier, the `Bold`, `Italic`, `BoldItalic` suffix forms), the subset prefix `ABCDEF+` stripped before lookup; the fallback when a font is not embedded and has no `/Widths` | 250 | change-12b §9.1 (read in the hand-over); the alias list is viewer practice, recalled |
| `ligatures.hpp/.cpp` | The decomposition table: `fi`, `fl`, `ff`, `ffi`, `ffl`, `st`, `ſt`, U+FB00 to U+FB06; applied to decoded text, the record keeping one code mapped to two or three characters | 80 | Unicode compatibility decompositions (recalled; verified against UnicodeData.txt when phase 8 vendors it) |
| `decoding_ladder.hpp/.cpp` | The ladder of `docs/DESIGN.md` §5 as one function per font kind, in this order and no other: `/ToUnicode`; the glyph name through the encoding and the glyph list; the font program's own encoding, charset or `cmap`; the predefined-CMap case as unreadable; unreadable last. Records the rung on every code | 400 | design record §4.2 (read) |
| `font_census.hpp/.cpp` | The census of §4: for one document, every font reachable from every page's resources and its forms' resources loaded, and every string operand of `Tj`, `TJ`, `'` and `"` decoded in the font named by the preceding `Tf` **in lexical order of the content lexer**, which is an approximation of the interpreter, not the interpreter. Its header says so and that phase 5 replaces it | 250 | this file |
| `third_party/agl/glyphlist.txt`, `LICENSE`, generated `src/fonts/generated/glyph_list_table.cpp` | The AGL as published (`adobe-type-tools/agl-aglfn`, `glyphlist.txt`, 4,281 entries, BSD-3), unmodified with its header; a generator script compiles it to a sorted `constexpr` table | data + 120 | https://github.com/adobe-type-tools/agl-aglfn (read by the design record: 4,281 entries, 82 KB, BSD-3) |
| `third_party/core14/` (the AFM files or the reduced TSV with the notice), `build/fonts/core14.py`, `build/fonts/core14.lock`, generated `src/fonts/generated/core14_table.cpp` | The fourteen AFM files reduced to `font, glyph name, width`; the generator reads the AFM files from the source the lock pins by SHA-256 (PDFBox's `org/apache/pdfbox/resources/afm/`, Apache-2.0 repository carrying Adobe's AFM notice) and writes the table | data + 200 | the AFM notice as carried by PDFBox, https://raw.githubusercontent.com/apache/pdfbox/trunk/LICENSE.txt (read by the design record: 689,634 bytes of AFM files) |
| `cli/verbs/dataset_fonts.cpp` | The `--fonts` mode of the `dataset` verb, calling a test-only C ABI entry or, if phase 1's ABI has no room for it, a census executable built from `tests/dataset/` (record which in §9; the C ABI never gains a symbol for the census) | 120 | this file |
| `tests/dataset/font_census.py` | Runs the census over the manifest, reads PDFium's text per file through `tests/pdfium/`, writes the report of §4 | 250 | this file |

**Generated sources rule.** A table generated from data is committed as a
`.cpp` beside the generator, and `build/check.py` (through a line this phase
adds to `build/check_generated.py`, or a new check if phase 1 left none)
regenerates it into a temporary folder and fails when the bytes differ, because
a generated file edited by hand is data nobody can reproduce.

## 3. Tests

Unit tier, `tests/unit/fonts/`, doctest, every fixture written in code.

| File | Holds |
|---|---|
| `decoding_ladder_test.cpp` | One case per decoding path, each asserting the decoded string and the rung: a correct, a missing and a wrong `/ToUnicode` (the wrong one answers a string different from the glyph names; the test asserts the `/ToUnicode` rung wins and why); `/Differences` with glyph-list names and with each naming convention; a symbolic TrueType with the (3,0) subtable at `0xF000`; a Type 1 program's built-in encoding; a CFF charset; an identity-encoded composite font with `/W`; a Type 3 font with and without `/ToUnicode`; a predefined CMap font reading unreadable with the count; a ligature decomposed; an unreadable code never emitted as U+FFFD or a private-use character |
| `font_widths_test.cpp` | `/Widths` with `/FirstChar`, `/MissingWidth`, `/W` in both array forms with `/DW`, a TrueType `hmtx` (with fewer metrics than glyphs, the last advance repeating), a CFF width (nominal and default), a Type 1 `hsbw`, a Core 14 width through an alias and through a subset prefix, a Type 3 width through its matrix; each matches the font program's own number to the unit |
| `cmap_parser_test.cpp` | Every operator, multi-byte codespaces (one, two, three and four bytes mixed), surrogate pairs, the bfrange array form, a range crossing a byte boundary as the specification forbids (clamped), a malformed entry skipped and counted |
| `true_type_tables_test.cpp`, `cff_font_test.cpp`, `type1_font_test.cpp` | Each reader over a small font program built in code by `tests/unit/fonts/support/font_builder` (a minimal TrueType with `cmap`, `hmtx`, `hhea`, `head`, `post`; a minimal CFF; a minimal Type 1 with eexec) and over truncated variants: a truncated `cmap`, a CFF with a bad INDEX count, a Type 1 whose eexec section ends early each yield a font whose codes decode through the other rungs or as unreadable, never an exception out of `FontLoader` |
| `encodings_test.cpp` | Annex D spot checks: the codes the annex's footnotes call out, WinAnsi `0x80` (Euro), `0xA0`, the unassigned codes |
| `glyph_list_test.cpp` | Every naming convention, the suffix rule, the ligature underscore rule, a name nothing answers |
| `font_data_test.cpp` | The two generated tables' entry counts and a hash of their content against the value the generator records, so a hand edit fails here as well as in the check |
| `font_census_test.cpp` | The census over a fixture with two fonts switched by `Tf` |

**Dataset.** This phase fills the manifest column `fonts` for every row:
`loads` (every font dictionary loads), `unreadable:<count>` (codes the ladder
cannot read, with the font names in the report), or `n/a` (no text). It checks
the font tags of `dataset/taxonomy.tsv`; where a tag is under its minimum the
phase adds synthesised files through `dataset/tools/synth/` (owned by phase 2:
a synth recipe is added as a new file in that folder, which is additive and
recorded in §9).

**PDFium.** The census compares the multiset of non-white-space characters per
file with PDFium's text of the same file (`tests/pdfium/`), because a character
bag needs no layout.

## 4. Gate

Measured over every dataset file whose manifest `open` expectation is `opens`
or `repaired`:

1. **Font dictionaries loaded without an exception: 100%.** A font program that
   does not parse is a warning and the other rungs answer; an exception
   reaching the census is a defect.
2. **Character agreement with PDFium**: the per-file character-multiset F1
   (characters excluding white space, after ligature decomposition on both
   sides) has a median of at least 0.95 over the files PDFium extracts any text
   from. The threshold is below phase 5's 0.97 because the census decodes in
   lexical order, not through the interpreter (forms drawn twice are counted
   once, `Tf` inside a form is approximate).
3. **Unreadable share**: codes decoded as unreadable are at most 1% of all codes
   on files PDFium extracts text from, reported per font kind and per rung.
4. **Per-tag table**: for every font tag, the number of files, the median F1 and
   the unreadable share, so a tag that fails is visible by name.
5. Sanitizer tier green; the unit tier green; `python build/check.py` green.

Commands, in the merged tip's worktree:

```
cmake --preset mingw-release && cmake --build --preset mingw-release
python tests/dataset/font_census.py --out dataset/reports/phase4-<date>/
ctest --preset unit && ctest --preset sanitize
python build/check.py
```

The report folder (ignored) holds `census.tsv` (one row per file: set, id,
fonts, kinds, codes, unreadable, F1) and `summary.md`; the summary goes into the
merge commit message.

## 5. Kill criterion and the question to the owner

**Quantity:** the median per-file character F1 of gate item 2. **Threshold:**
below 0.90.

**Question, word for word with the numbers filled in:** "The fonts layer agrees
with PDFium on characters at a median of F over M files; the files below 0.90
fail mostly on <the named rungs and font kinds>. Do you want (a) one more fonts
session on those causes (about 8 to 12 hours, about $150 to $250), (b) to
proceed to the layout phase and measure again with the real interpreter, which
removes the census's own approximation (no extra cost now), or (c) predefined
CMap data for the CJK files that account for the gap (Adobe's CMap resources,
BSD-3, about 2.3 MB compressed; about 4 to 6 hours, about $70 to $120)?"

**Unattended action:** write the question with the evidence (the census summary,
the ten worst files with their causes) into `OPEN-QUESTIONS.md`; merge the branch
if checks and the unit tier are green, with trailer `Build-Phase: 4 gate-failed`;
phases that depend on it (5) proceed and report their gates with that caveat.

## 6. Rollback

Fully reversible: a new layer nothing outside `src/fonts/` and the census calls
until phase 5. Reverting the merge removes the layer, the two data tables and
their notices together; `THIRD-PARTY-NOTICES.md` must not keep a notice for data
that no longer ships, nor lose one while it does.

## 7. Execution order inside the phase

1. Run `python build/check.py` and `ctest --preset unit` on the base.
2. The data: fetch the AGL and the AFM files into the scratchpad (`git clone`
   over HTTPS or `python` with the lock), write `build/fonts/core14.py` and its
   lock, generate the two tables, add the inventory rows, the notices and the
   `docs/SOURCES.md` rows; run the checks.
3. `pdf_font`, `decoding_rung`, `encodings`, `glyph_list`, `ligatures`,
   `simple_font`, `font_loader` and the ladder over simple fonts, failing tests
   first.
4. **Split point.** If the session is running out of room, push here with
   `Build-Phase: 4 incomplete` naming what remains (composite fonts, font
   programs, standard fonts, census).
5. `cmap_parser` and `type0_font`, with their tests.
6. `true_type_tables`, `cff_font`, `type1_font` and `standard_fonts`, with the
   widths tests and the truncated-program tests.
7. `font_census`, the CLI mode, `tests/dataset/font_census.py`; run the census;
   read the ten worst files; fix reader faults; add synth recipes for any font tag
   under its minimum.
8. The gate run of §4; the sanitizer tier.
9. Documentation lines this phase invalidates: `docs/SOURCES.md` rows for every
   fact used, `third_party/inventory.tsv`, `THIRD-PARTY-NOTICES.md`; `docs/DESIGN.md`
   §5 is corrected only where the code proves a rule wrong, recorded in §9.

## 8. Opening prompt

> Read `CLAUDE.md`, `docs/DESIGN.md` §3, §4, §5 and §19, `docs/SOURCES.md`, all
> of `build-4.md`, and the deviations sections (§9) of `build-1.md`,
> `build-2.md` and `build-3.md`. Before planning, read what landed in
> `src/syntax/` (the object model, the resolver, the stream decoding, the page
> list and resources, the content lexer), `src/platform/` (budget, checked
> arithmetic, error type) and `tests/unit/support/pdf_builder`. Work in a
> worktree of your own on branch `phase/4-fonts` created from `main`; phases 6
> and 7 run at the same time in other worktrees, so touch only the paths this
> file's header owns and add rows (never rewrite rows) in the shared files it
> names. Run `python build/check.py` and `ctest --preset unit` before you start
> and before you finish; if the base is red, repair it in a commit of its own and
> record the repair in §9. Write failing tests first. Every TrueType or CFF
> heuristic you implement names in its comment the reader it is copied from,
> and every external fact carries its source and whether you read it or
> recalled it, in the comment and in `docs/SOURCES.md`. The dataset is in the
> main checkout's `dataset/files/` and PDFium in its `build/cache/pdfium/`,
> reached through `git rev-parse --git-common-dir`; never fetch a second copy.
> No step waits for the owner: when the kill criterion of §5 trips, write the
> question with its evidence into `OPEN-QUESTIONS.md` and finish. Finish by
> pushing the branch with a last commit whose message carries
> `Build-Phase: 4 done`, or `Build-Phase: 4 incomplete` (split point of §7) or
> `Build-Phase: 4 gate-failed`, with the gate's numbers in the message. Never
> merge into `main`; the team lead merges. Record every deviation from this file
> in its §9 and in `build-5.md` (and any other downstream build file it affects)
> in the same change.

## 9. Deviations recorded by this phase

1. (none yet)

### Deviations of earlier phases that affect this one

From `build-1.md` §9:

- `third_party/inventory.tsv` has the columns `path, name, version, upstream, archive, archive_sha256, tree_sha256, licence, test_only`; compute a tree hash with `python build/check_third_party.py --print-tree-hash third_party/<name>` (our `CMakeLists.txt` in the folder is excluded) and cache the upstream archive in the main checkout's `build/cache/third_party/` (`build-1.md` §9 item 11).
- `build/check_catalogue.py` accepts `cli/verbs/dataset_fonts.cpp` as a helper of the `dataset` verb (files named `<verb>_<part>.cpp`) (item 12).
- Remove the `build/paths_pending.tsv` row `build/fonts/` in the change that creates it (item 13).
- The sanitizer tier is two runs: `ctest --preset sanitize` (GCC, UBSan in trap mode) and `ctest --preset clang-sanitize` (llvm-mingw, ASan and UBSan) (`build-1.md` §9 item 8).

From `build-3.md` §9:

- The object model is `syntax::Object` (nested `Reference`, `Number`, `String` with its literal-or-hex flag, `Array` = `std::vector<Object>`), `syntax::Dictionary` (insertion-ordered) and `syntax::Stream` (dictionary, raw byte range, owner); all immutable and shared, built through the `make_*` factories (item 3).
- `syntax::Document::open(bytes, Limits)` returns the document; `object(reference)`, `resolve(object)` and `resolve_entry(dictionary, key)` resolve (strings come back decrypted), `decode(stream, page)` decrypts and runs the filters up to the first image codec (`Filters::Result::remaining` names the codecs left), `content_pieces(contents, page)` decodes a `/Contents` value, `pages()` holds `syntax::Page` (boxes, rotation, resolved resources, contents), `repair(reason)` and `warn(code, message, page)` record (items 3, 4).
- `syntax::Resources(document, page.resources, page.number)` looks a resource up by category and name, with the form fallback in `for_form`; `syntax::ContentLexer(pieces, document.limits(), "page N")` yields `Operation`s (operator, operands, byte range, `operator_start`; an inline image is one `BI` operation with its data range) and `locate(offset)` maps back to the piece (item 10).
- `Document::decode` charges the budget on every call, so a layer that decodes a shared stream more than once caches the decoded bytes (item 11).
- The fixture writer is `test::PdfBuilder` (with `test::Layout`, `test::StreamOptions` and `test::ObjectOptions` at namespace scope), `test::ContentBuilder`, `test::PdfEncryptor`, `test::require_value`, and `test::open_document` and `test::summarise` in `tests/unit/support/` (item 2).
