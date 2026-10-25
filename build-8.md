# build-8 — Text redaction, the placeholder font and the self-check

| Field | Value |
|---|---|
| Phase | 8 |
| Branch | `phase/8-redact-text` |
| Depends on | 5, 7 |
| Parallel with | 6 (when 6 is still running); 9 runs after it |
| Model | `opus` |
| Owns | `src/redact/` (all but `edit_plan.*`, which it extends additively), `src/writer/placeholder_font.hpp`, `src/writer/placeholder_font.cpp`, `src/writer/placeholder_font_data.inc`, `src/platform/unicode/`, `third_party/liberation-fonts/`, `third_party/unicode/`, `build/fonts/`, `build/unicode/`, `src/api/redact_text.cpp`, `src/api/check.cpp`, `cli/verbs/redact.cpp`, `cli/verbs/check.cpp`, `tests/unit/redact/`, `tests/unit/unicode/`, `dataset/tools/synth/redaction/`, `tests/dataset/gates/phase8.py`, `tests/pdfium/leak.py` |
| Size | about 5,500 lines of C++ including tests, plus about 400 lines of Python (the subset tool, the Unicode table generator, the redaction synthesiser, the gate) |
| Price | estimate: 14 to 18 hours of session time and roughly $250 to $350 of model spend at list prices. Calibrated from the design record's stage 2 (content-stream editor 1,200 lines, placeholder font and build tool 500, tests about half of 2,200 lines of C#; the stage 25 to 35 h and $400 to $600) and change-12's per-batch figures, scaled by 1.3 for C++ |

## 0. What this phase ships

Glyph-level text redaction: the matched glyphs leave the content stream, a
placeholder is drawn in the library's own embedded font with the width-fitting
rule, every other glyph stays where it was, and the output is re-read before it
is handed back.

Catalogue rows of `CLAUDE.md` §2 delivered:

- **5 Redact text**: `pdfkit_redact_text` records matches on an edit handle, as
  glyph ranges from a previous `pdfkit_text` or as search strings with the
  caller's folding rules, each with its placeholder; it answers per match the
  occurrences found, their device rectangles and how the placeholder will be
  fitted.
- **10.1 Write, the self-check half**: every `pdfkit_write` whose plan holds a
  text match runs the self-check of `docs/DESIGN.md` §18 and refuses the output
  when a redacted string survives, naming the page and the surface (exit code 3
  in the CLI).
- **10.2 Check**: `pdfkit_check` runs the same check over any PDF bytes and a
  list of strings with folding rules.

CLI verbs: `redact` (`pdfkit redact <job.json>`, the JSON job file of §2 whose
text-match and page-selection sections this phase fills; its image-treatment
section is phase 9's and its metadata section phase 10's, each refused with an
actionable message until its phase lands) and `check`
(`pdfkit check <out.pdf> --strings <strings.json>`).

## 1. What it reads from earlier phases

Read as landed, and read `build-3.md` to `build-7.md` §9 for deviations first.

| From | What | Why |
|---|---|---|
| phase 5 | `src/text/` glyph records (stream, operator index, operand index, byte range of the code, font, code, text, rung, advance, device box, render mode, fill colour, marked-content context with `/ActualText`, hidden classes), the layout's reading-order text with glyph index ranges, the interpreter's text state at each glyph (`Tm`, `Tc`, `Tw`, `Tz`, `TL`, `Ts`, `Tfs`, CTM), `ContentSerialiser` and the re-serialisation identity | the surgery edits exactly the bytes the records name; the match runs over the same reading-order text `pdfkit_text` returns |
| phase 4 | `src/fonts/` `Font::decode` (code byte lengths for multi-byte codes), widths, the descriptor flags (bold) and names | cutting a multi-byte code on its boundary; bold placeholder selection; advances |
| phase 7 | `src/redact/edit_plan.*`, `src/redact/write_pipeline.*` (the named no-op stage for text surgery and the self-check stage), `Serialiser`, `StreamCopier` (a rewritten stream is Flate-compressed), `WriteReport` (the rectangles field), `ChannelTable`, `tests/pdfium/render.py` | this phase fills the two stages and the report's rectangles |
| phase 2 | `tests/dataset/run.py`, `tests/pdfium/compare.py`, `dataset/tools/synth/` | the gate |

## 2. Components

| File | What it holds | Lines | Reference |
|---|---|---|---|
| `third_party/liberation-fonts/` | Liberation Sans Regular and Bold (the TTF files only) from the pinned upstream release, with `LICENSE` (SIL OFL 1.1) and the inventory row with the release archive's SHA-256 | data | https://github.com/liberationfonts/liberation-fonts (recalled; verify the release and the licence file when vendoring, record in `docs/SOURCES.md`) |
| `build/fonts/subset.py` | cuts each face to the 40 glyphs of the alphabet (`[`, `]`, `_`, `0`-`9`, `A`-`Z`, `.notdef`) with fontTools (MIT, pinned in `build/fonts/requirements.txt` by version and hash, installed into a virtual environment under the shared `build/cache/`), keeps `glyf`, `loca`, `head`, `hhea`, `hmtx`, `maxp`, `cmap`, `post` (format 3) and `name` (the OFL requires the copyright and licence entries to stay), drops hinting, writes the glyph-id order deterministically, and emits `src/writer/placeholder_font_data.inc` (the two subsets as byte arrays, their `hmtx` widths in 1/1000 em, the code-to-GID map, the SHA-256 of each) | 200 | fontTools subsetter (recalled); OFL FAQ on embedding and subsetting (design record Appendix A, stream 3, read) |
| `build/fonts/check_regenerated.py` | regenerates the data into the cache and compares it with the committed file; run by `build/check.py`, because a committed generated file that its tool no longer produces is a file nobody can update | 60 | — |
| `src/writer/placeholder_font.{hpp,cpp}` | `PlaceholderFont`: `docs/DESIGN.md` §13: one `Type0` font per face used, over `CIDFontType2`, `Identity-H`, `/CIDToGIDMap /Identity`, `/W` from the subset's widths, a `FontDescriptor` with `/FontFile2`, a `ToUnicode` CMap mapping each code to its character, a subset tag derived from the hash (so it is stable), added to a page's `/Resources /Font` under a name that does not collide with an existing one; embedded once per output file and only when a placeholder is drawn | 350 | ISO 32000-1 §9.7 and §9.10 (design record Appendix A, stream 3, read); PdfPig's `CompositeTrueTypeWritingFont` (Apache 2.0, read as reference only) |
| `build/unicode/generate.py` and `third_party/unicode/` | the Unicode `CaseFolding.txt` (simple and common foldings) and `UnicodeData.txt` (canonical decompositions, combining classes) at the pinned version with the Unicode License v3 and the inventory row; the generator writes `src/platform/unicode/tables.inc` (compact range tables) and is held by a regeneration check like the font's | 150 + data | https://www.unicode.org/Public/ (recalled; verify the version and the licence when vendoring) |
| `src/platform/unicode/folding.{hpp,cpp}` | `Folding`: UTF-8 iteration, simple case folding, canonical decomposition and stripping of combining marks, whitespace collapse, each a flag of the caller's folding options (`docs/DESIGN.md` §11) | 300 | Unicode Standard §3.13 and UAX #15 (recalled) |
| `src/redact/match_finder.{hpp,cpp}` | `MatchFinder`: `docs/DESIGN.md` §11: glyph ranges validated against the document fingerprint and page bounds; search strings folded and found in each page's reading-order text (visible then hidden blocks, as `pdfkit_text` returns it) with a map from folded offsets back to glyph indices; inferred word gaps count as spaces; a hyphenation join counts as no character; overlapping matches merged; a match crossing operators, lines and a hyphen is one match | 450 | `docs/DESIGN.md` §11 |
| `src/redact/text_surgery.{hpp,cpp}` | `TextSurgery`: `docs/DESIGN.md` §12: per content stream, the operators holding matched glyphs are rewritten: in a `TJ` array the removed codes become a number `-advance * 1000 / Tfs` (the horizontal scale applied as §12 states) merged with neighbouring numbers; a `Tj` operand is cut on code boundaries and the remaining pieces re-anchored with a `Td` computed by the state machine; `'` and `"` keep their line advance and their `Tw`/`Tc` effect when their string is cut; a match spanning operators edits each; the rest of the stream's bytes are copied as they were through `ContentSerialiser` | 900 | MuPDF's `pdf-op-filter.c` (AGPL, technique read, no code taken; design record Appendix A, stream 3, read); UniPDF's width-adjustment note (read) |
| `src/redact/marked_content_rewriter.{hpp,cpp}` | `MarkedContentRewriter`: an `/ActualText`, `/Alt` or `/E` on a marked-content span covering a removed glyph is replaced by the placeholder (or removed when the span holds nothing else), in the inline property list or in the `/Properties` resource, so the text does not survive as a property | 200 | `docs/DESIGN.md` §12 |
| `src/redact/form_rewriter.{hpp,cpp}` | `FormRewriter`: text inside a form XObject rewritten inside the form, once per distinct rewrite; the form object keeps its number in the plan so every page that draws it draws the rewritten form | 200 | `docs/DESIGN.md` §12 |
| `src/redact/placeholder_fitting.{hpp,cpp}` | `PlaceholderFitting`: `docs/DESIGN.md` §13: the hole is the removed advance plus the gap to the next glyph on the line; natural width when it fits, else `Tz` down to 60%, else overflow at 60%; the outcome `natural`, `scaled` or `overflow` reported per match; the placeholder drawn at the removed run's size, baseline, fill colour, `Ts` and text rise, in bold when the removed font is bold; a placeholder outside the alphabet refused | 300 | Acrobat, Apryse and PyMuPDF overlay sizing (design record Appendix A, stream 3, read) |
| `src/redact/placeholder_drawing.{hpp,cpp}` | `PlaceholderDrawing`: the operators that draw a placeholder (`q`, `BT`, `Tf`, `Tm`, `Tz`, `Tr 0`, fill colour, `Tj` of the CID codes, `ET`, `Q`) inserted after the edited operator in the same stream, so it is drawn under the same CTM and clip; a match across a line break draws once at the first line; a placeholder for a match whose glyphs were all hidden is drawn with the same render mode, so an invisible layer stays invisible | 250 | `docs/DESIGN.md` §12 and §13 |
| `src/redact/self_check.{hpp,cpp}` | `SelfCheck`: `docs/DESIGN.md` §18: re-open the output with the library's own reader; search the reading-order text of every page (hidden included), every `/ActualText`, `/Alt` and `/E`, the Info dictionary and the XMP packet, every string object left in the file, and the decompressed bytes of every stream outside font programs, for each redacted string under its folding; a survivor is a refusal naming the page and the surface | 350 | `docs/DESIGN.md` §18 |
| `src/redact/edit_plan.{hpp,cpp}` (extended) | the text matches and their placeholders; additive only | 80 | — |
| `src/api/redact_text.cpp`, `check.cpp` | the two entries through `api::guarded`, the JSON of `docs/ABI.md`, the refusals of `docs/DESIGN.md` §19 | 250 | `docs/ABI.md` |
| `cli/verbs/redact.cpp`, `check.cpp` | the job file reader (input path, output path, `text` matches, `pages`, and the `images` and `metadata` sections refused with "this build does not apply image treatments or metadata yet: remove the section from the job or use a build that does" until phases 9 and 10 fill them), the output written only when `pdfkit_write` succeeds | 300 | `CLAUDE.md` §2 |
| `tests/unit/redact/*.cpp`, `tests/unit/unicode/*.cpp` | §3 | 1,300 | — |
| `dataset/tools/synth/redaction/` | synthesised files that set the traps: a name split by a `TJ` kerning number, by a font change, across a line with a hyphen, across a page's two `/Contents` pieces, inside a form drawn on three pages, inside `/ActualText`, in render mode 3 over an image (a hybrid scan layer), white on white, duplicated for fake bold, overlapped text, a Type 3 font, an Identity-H font with two-byte codes; each carries a known marker the gate redacts | 250 | — |
| `tests/pdfium/leak.py` | PDFium's text of every output page searched for each redacted word under the default folding, and the output's bytes decompressed stream by stream (font programs excluded by their dictionary type) scanned for the same words in Latin-1, UTF-16BE and the font's own codes where pdfkit reports them | 120 | — |
| `tests/dataset/gates/phase8.py` | the gate of §4 | 120 | — |

## 3. Tests

### 3.1 Unit tier, fixtures written in code

| File | Asserts |
|---|---|
| `tests/unit/redact/text_surgery_test.cpp` | a word removed from a `TJ` array: the remaining glyphs' device positions, recomputed by the interpreter over the output, equal the input's to 0.01 pt; the same for `Tj`, `'` and `"` (with `Tw` and `Tc` set by `"`), for a two-byte Identity-H font, for a match spanning a `TJ` number, two operators, a font change, two `/Contents` pieces, a line break with a hyphen; `Tz` other than 100; a rotated text matrix; render mode 3 |
| `tests/unit/redact/form_rewriter_test.cpp` | a form drawn on three pages rewritten once; the output holds one form object; each page's drawn text lacks the match |
| `tests/unit/redact/marked_content_rewriter_test.cpp` | `/ActualText` inline and through `/Properties`, `/Alt`, `/E`; a span covering only the match removed |
| `tests/unit/redact/placeholder_fitting_test.cpp` | the three outcomes with their numbers; bold chosen from the descriptor flag and from a name containing `Bold`; a placeholder outside the alphabet refused with the three-part message |
| `tests/unit/redact/placeholder_font_test.cpp` | the embedded font opens through the phase 4 font loader; every alphabet character decodes through `ToUnicode` to itself; the widths match the subset's `hmtx`; one embedding per output whatever the number of placeholders; the data's hash equals the generator's |
| `tests/unit/redact/match_finder_test.cpp` | each folding flag; a stale glyph range refused; overlapping matches merged; a match found in hidden text; a match across a hyphenation join; the offsets map back to the right glyphs |
| `tests/unit/redact/self_check_test.cpp` | a planted survivor in each surface of `docs/DESIGN.md` §18 is found and the refusal names the page and the surface; a clean output passes; `pdfkit_check` over bytes |
| `tests/unit/redact/determinism_test.cpp` | one job written twice gives the same bytes |
| `tests/unit/unicode/folding_test.cpp` | case folding (`ß`, `İ`, Greek final sigma under the simple folding), decomposition and mark stripping (`Élise`, `Łódź` where `Ł` has no decomposition and stays), whitespace collapse |

### 3.2 Dataset

- Tags with their minimum in `dataset/taxonomy.tsv`: the redaction traps of
  `CLAUDE.md` §7.3 (`trap:overlapped_text`, `trap:incremental_update`,
  `trap:deniable_removal`), and one `redact:<case>` tag per synthesised case of
  §2, minimum one file each.
- The manifest column `redact_text` (`clean`, `known_gap:<reason>`, `n/a`).
- `tests/dataset/run.py` gains the `redact_text` check: per file, a seeded job
  (seed `8` plus the file's id) that redacts five random words of four or more
  letters taken from pdfkit's own extraction of the file (hidden text
  included), plus the known marker of every synthesised file, each with the
  placeholder `[REDACTED]`, written with the default folding.

### 3.3 PDFium comparison

`tests/pdfium/compare.py --check redact` renders input and output pages and
compares them outside the rectangles of the write report dilated by one pixel;
`tests/pdfium/leak.py` runs the second extractor's leak check; the placeholder
check counts `[REDACTED]` in PDFium's text of each output page against the
matches drawn there.

## 4. Gate

Over every manifest file whose `open` expectation is `opens` or `repaired` and
whose `redact_text` expectation is not `n/a`:

| Measure | Threshold |
|---|---|
| outputs that pass the self-check | 100% (a refusal is a failure to investigate unless the manifest names the file a known gap) |
| redacted words found by PDFium in the text of any output | 0 |
| redacted words found in the decompressed bytes of any output outside font programs | 0 |
| pages whose PDFium rendering differs from the input only inside the reported rectangles (one-pixel dilation) | at least 99% |
| placeholders found in PDFium's text of the output, of matches drawn | at least 99% |
| two runs: output bytes identical | 100% |
| crashes, hangs past 30 s, anything but a status from the ABI | 0 |

Commands:

```
cmake --preset mingw-release && cmake --build --preset mingw-release
python tests/dataset/run.py --checks open,redact_text --runs 2
python tests/pdfium/compare.py --run <run-id> --check redact
python tests/pdfium/leak.py --run <run-id>
python tests/dataset/gates/phase8.py --run <run-id>
```

The report goes to `dataset/reports/<run-id>/`; the summary goes into the merge
commit message.

## 5. Kill criterion and the question to the owner

There are two triggers, and they are handled differently.

**Leak (stop the line).** **Quantity**: redacted words that survive in any
output, by PDFium's text or by the byte scan, not explained by a file the
manifest names as a known gap. **Threshold**: one. **The question**:
"Redacted text survived in N files via <surface, with counts>. Do you want:

- (a) a fix session on that surface before anything else (recommended): about
  4 to 8 hours and $70 to $140;
- (b) continue the build with the defect recorded and no release until it is
  fixed: no extra cost now."

**Unattended action**: the session fixes the surface itself when it can within
this phase, and re-runs the gate. When it cannot, it writes the question with
the evidence (files, surfaces, the surviving bytes' location) into
`OPEN-QUESTIONS.md`, pushes the branch with trailer
`Build-Phase: 8 incomplete` and the reason, and **does not ask for a merge**:
`main` must never hold a redactor that leaks. Phase 9 then waits for this
phase.

**Fidelity.** **Quantity**: pages pixel-stable outside the reported
rectangles. **Threshold**: under 97%. **The question**: "Redaction keeps N of
M pages pixel-stable outside the redacted rectangles; the others differ
because of <named causes>. Do you want (a) one more surgery session on those
causes (recommended): about 6 to 10 hours and $100 to $160; (b) accept and
record the causes as known gaps: no extra cost; (c) a half-day examination,
then decide: about 3 to 4 hours and $50 to $70?" **Unattended action**: write
the question with the evidence into `OPEN-QUESTIONS.md`; merge the branch if
checks, the unit tier and the leak measures (zero survivors) are green, with
trailer `Build-Phase: 8 gate-failed`; phases that depend on it proceed and
report their gates with that caveat.

## 6. Rollback

Revert the merge: the redact layer's text parts, the placeholder font data, the
Unicode tables and the two verbs leave together. The writer of phase 7 keeps
working, because the text-surgery and self-check stages return to their no-op
form. The vendored font and Unicode data leave `third_party/` with their
inventory rows and their notices.

## 7. Execution order inside the phase

1. Run `python build/check.py` and `ctest --preset unit` on the base; a red
   base is repaired in a commit of its own and recorded in §9.
2. Vendor Liberation Sans and the Unicode data with their licences and
   inventory rows; `build/fonts/subset.py`, `build/unicode/generate.py`, their
   regeneration checks; `THIRD-PARTY-NOTICES.md` gains both notices.
3. `folding_test.cpp` red, then `Folding`; `match_finder_test.cpp` red, then
   `MatchFinder`.
4. `text_surgery_test.cpp` red case by case, then `TextSurgery`; this is the
   step the phase rests on, so every case of §3.1 is green before step 5.
5. **Split point**: if the session is running out of room, push here with
   `Build-Phase: 8 incomplete` and the reason; the next session starts at 6.
6. `PlaceholderFont`, `PlaceholderFitting`, `PlaceholderDrawing`,
   `MarkedContentRewriter`, `FormRewriter`, each with its tests first.
7. `SelfCheck` and its tests; the edit-plan extension; the write pipeline's
   two stages filled; `src/api/redact_text.cpp`, `check.cpp`, the `redact`
   and `check` verbs.
8. The synthesised trap files and their manifest rows and tags,
   `tests/pdfium/leak.py`, the dataset check, `phase8.py`; run the gate; read
   every failure and classify it; a survivor is fixed here (§5).
9. The test sweep: `python build/check.py`, `ctest --preset unit`,
   `ctest --preset sanitize`, the gate commands of §4.
10. The documentation sweep, last, from what landed: `docs/ABI.md` (the two
    symbols, the job file's text section), `docs/DESIGN.md` §11, §12, §13 and
    §18 only where the landed code differs (each difference a deviation in §9),
    `docs/SOURCES.md` (the font, the Unicode data, fontTools), `dataset/README.md`
    (the column).

## 8. Opening prompt

> Read `CLAUDE.md`, `docs/DESIGN.md` chapters 6, 7, 8, 11, 12, 13, 17, 18 and
> 19, `docs/ABI.md`, `dataset/README.md`, all of `build-8.md`, and the
> deviations sections (§9) of `build-3.md` to `build-7.md`, then implement
> `build-8.md` exactly as written. Before planning, read as landed
> `src/text/` (the glyph record, the interpreter's text state, the layout's
> reading-order text, `ContentSerialiser`), `src/fonts/` (`Font::decode`, the
> widths, the descriptor), `src/redact/edit_plan.*`,
> `src/redact/write_pipeline.*`, `src/writer/write_report.*` and
> `tests/pdfium/render.py`. Work in a worktree of your own on branch
> `phase/8-redact-text` created from `main`. Run `python build/check.py` and
> `ctest --preset unit` before you start and before you finish; a red base is
> repaired in a commit of its own and recorded as a deviation. Every external
> fact you implement carries its source and whether you read it there or
> recalled it, in the comment and in `docs/SOURCES.md`; MuPDF is AGPL, so its
> technique is described from the design and no line of it is taken. The
> dataset and the PDFium binary live in the main checkout (resolve it with
> `git rev-parse --git-common-dir`). No step waits for the owner: a question
> goes into `OPEN-QUESTIONS.md` with the recommended answer first and the price
> of each option. Write failing tests first and keep the surgery's position
> test green throughout. A redacted word that survives in any output is fixed
> in this phase or the branch is pushed `incomplete` and not offered for
> merge. Finish with a last commit whose message carries
> `Build-Phase: 8 done`, or `Build-Phase: 8 incomplete` or
> `Build-Phase: 8 gate-failed` with the reason, and push the branch; never
> merge into `main`, because the team lead merges. Do not treat images and do
> not write metadata: phases 9 and 10 own them. If you must deviate from this
> file, record each deviation in its §9 and in every downstream build file it
> affects (`build-9.md`, `build-10.md`), in the same change.

## 9. Deviations recorded by this phase

1. (none yet)

### Deviations of earlier phases that affect this one

From `build-1.md` §9:

- `platform::ErrorCode::self_check` maps to `PDFKIT_E_SELF_CHECK` and CLI exit 3 (item 5).
- The redact job format is fixed in `docs/ABI.md` §6: `input`, `output`, optional `open_options`, and the sections `text`, `images`, `metadata`, `pages`, each forwarded byte for byte to its function; `cli/verbs/redact.cpp` already reads it (item 7).
- `pdfkit_check(bytes, size, strings_json, out)` requires `strings_json`; the `check` verb takes it as `--strings <json>` (item 4).
- `third_party/inventory.tsv` has the columns `path, name, version, upstream, archive, archive_sha256, tree_sha256, licence, test_only`; compute a tree hash with `python build/check_third_party.py --print-tree-hash third_party/<name>` (our `CMakeLists.txt` in the folder is excluded) and cache the upstream archive in the main checkout's `build/cache/third_party/` (`build-1.md` §9 item 11).
- The sanitizer tier is two runs: `ctest --preset sanitize` (GCC, UBSan in trap mode) and `ctest --preset clang-sanitize` (llvm-mingw, ASan and UBSan) (`build-1.md` §9 item 8).

From `build-4.md` §9:

- The font a placeholder is read back with is `fonts::FontLoader(document).load(entry)`; `PdfFont::decode` splits a composite font's operand by its CMap and reports each code's `byte_length`, which is where a multi-byte code is cut; `PdfFont::is_bold()` reads `/FontWeight` 600 and above, the ForceBold flag, a Core 14 bold face or "Bold", "Black" or "Heavy" in the name; `width(code)` is in thousandths of text space (item 8).
- `build/fonts/` holds the font-table generators (`glyph_list.py`, `core14.py`, the shared `cpp_table.py`), and `build/check_generated.py` regenerates every table its `GENERATORS` list names and fails on a difference: the placeholder subset generator adds its row there (item 1).

From `build-7.md` §9:

- The write surface is `writer::ObjectGraph` (`src/writer/object_graph.hpp`): `get(number)` reads an object with every earlier stage's edits applied, `set(number, object)` replaces one, `add(object)` adds one under a fresh number; the source `syntax::Document` is never changed. Read through the graph, never through `Document::object`, because page selection runs first and replaces page dictionaries, the catalog and every pruned form (a form with resources of its own gets a pruned copy under its own number) (items 2, 5).
- A created stream is `syntax::Stream(dictionary, encoded bytes)`; `writer::StreamCopier::compress(dictionary, decoded)` makes a Flate one, `StreamCopier::plain` an uncompressed one; the file writer copies untouched streams byte for byte (decrypted, `/Crypt` dropped) (`build-3.md` §9 item 17).
- `redact::WritePipeline` (`src/redact/write_pipeline.cpp`) has the named stages page selection, text surgery, image treatments, metadata, channel stripping, serialisation, self-check; fill your stage, never reorder. The write report (`writer::WriteReport`) already carries `rectangles` (empty) and `self_check` (`not_run` with its reason) (items 2, 10).
- `redact::EditPlan` holds the page list only; add your member beside it (its header names where).
- Every write prunes resources to the names the content uses (`writer::ResourceUsage` reads `Tf`, `Do`, `gs`, `cs`/`CS`, `scn`/`SCN`, `sh`, `BDC`/`DP` and inline-image colour spaces), so an object your stage stops drawing leaves the output without further work, and a resource your stage starts drawing must be named in the resources it edits (item 5).
- The channel table strips after your stage: `/Metadata` off every object but the catalog, the catalog's XMP loses its whole `xmpMM` namespace, `/PieceInfo`, `/LastModified`, `/AA`, `/JS`, `/AF`, `/EF`, `/StructParent(s)` go from every dictionary, and the catalog and pages keep only their allow-lists (items 7, 11, 12).
- Test helpers: `tests/unit/writer/output_scan.hpp` (`identity_write`, `select_write`, `write_report`, `everything` = raw bytes plus every stream decoded, `contains_marker`, `reopen` without repair) and `tests/unit/support/pdf_builder_channels.hpp` (planted markers per channel, the four-page leak fixture); `tests/pdfium/render.py` compares pages exactly or outside a rectangle set with a one-pixel dilation (`compare_bitmaps`).
- The dataset outcome gains `channels`, `write` and `select`; outputs go to `<run>/outputs/`, named relative to the run folder (`tests/dataset/outcome.md`).
- The self-check stage is `WritePipeline::self_check`; it sets `report.self_check_status` and `self_check_reason`, and a survivor is `platform::ErrorCode::self_check`.
