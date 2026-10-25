# build-7 — The writer, page selection and the channel table

| Field | Value |
|---|---|
| Phase | 7 |
| Branch | `phase/7-writer` |
| Depends on | 3 |
| Parallel with | 4, 6 |
| Model | `opus` |
| Owns | `src/writer/`, `src/redact/edit_plan.hpp`, `src/redact/edit_plan.cpp`, `src/redact/write_pipeline.hpp`, `src/redact/write_pipeline.cpp`, `src/api/edit.cpp`, `src/api/write.cpp`, `src/api/select_pages.cpp`, `src/api/channels.cpp`, `cli/verbs/select_pages.cpp`, `cli/verbs/channels.cpp`, `tests/unit/writer/`, `tests/unit/support/pdf_builder_channels.cpp`, `tests/unit/support/pdf_builder_channels.hpp`, `dataset/tools/synth/channels/`, `tests/dataset/gates/phase7.py`, `tests/pdfium/render.py` |
| Size | about 5,000 lines of C++ including tests, plus about 400 lines of Python for the planted-marker synthesiser and the gate script |
| Price | estimate: 12 to 16 hours of session time and roughly $200 to $300 of model spend at list prices. Calibrated from the design record's stage 2 (writer and sweep 900 lines, channel table and XMP 600, tests about half of 2,200 lines of C#, the whole stage 25 to 35 h and $400 to $600) and change-12's per-batch figures (8 to 14 h, $120 to $260 per batch of 3,200 to 3,600 lines), scaled by 1.3 for C++ |

## 0. What this phase ships

The fresh-file writer every output of the library goes through, page selection
with the leak guarantee, and the channel table that strips every hidden channel
from every file the library writes.

Catalogue rows of `CLAUDE.md` §2 delivered:

- **8 Strip hidden channels**: `pdfkit_channels` reports, per row of the channel
  table, whether the channel is present and its count; every `pdfkit_write`
  applies the table.
- **9 Select pages**: `pdfkit_select_pages` records a page list on an edit
  handle; the writer applies it.
- **10.1 Write**: `pdfkit_write` serialises a fresh file holding only reachable
  objects, renumbered deterministically, with a cross-reference stream, and
  returns the PDF bytes and the write report. The self-check part of 10.1 is
  phase 8's; this phase writes the report field `self_check` as
  `{"status": "not_run", "reason": "no text redaction in the edit plan"}` when
  the plan holds no text match, which is always true until phase 8 lands.
- The lifecycle symbols `pdfkit_edit_new` and `pdfkit_edit_free`.

CLI verbs: `select-pages` (`pdfkit select-pages <in.pdf> --pages 1,3-5 --out <out.pdf>`)
and `channels` (`pdfkit channels <in.pdf>`), each with the exit codes of
`CLAUDE.md` §2 and JSON on standard output.

What this phase does not do: the placeholder font embedding (phase 8), text
surgery (phase 8), image treatments (phase 9), the metadata rewrite (phase 10).
The writer exposes the hooks those phases plug into (§2, `edit_plan` and
`write_pipeline`), and nothing more.

## 1. What it reads from earlier phases

Read these as landed, not as planned, and read `build-1.md` §9 and
`build-3.md` §9 for deviations first.

| From | What | Why |
|---|---|---|
| phase 1 | `include/pdfkit/pdfkit.h` (the stub declarations of `pdfkit_edit_new`, `pdfkit_edit_free`, `pdfkit_select_pages`, `pdfkit_write`, `pdfkit_channels`), `src/api/catalogue.cpp` (the registry rows marked not implemented), `src/api/guarded.*`, `cli/verbs/select_pages.cpp` and `cli/verbs/channels.cpp` stubs, `src/platform/json_*`, `src/platform/error.*`, `docs/ABI.md` | the surface this phase fills in; the registry row's status flips to implemented |
| phase 3 | `src/syntax/` object model (`PdfObject` variants, references, ordered dictionaries, strings with their literal-or-hex flag, streams with their raw byte range and decoded bytes), `Document` (resolve, catalog, trailer, the page tree with inherited attributes, `repaired` reasons, the security handler state), `ContentLexer` (operators with operand lists and byte ranges) | the writer serialises the object graph phase 3 reads; resource pruning reads which names a content stream uses through the content lexer |
| phase 3 | `src/platform/` budget, checked arithmetic, SHA-256, zlib deflate wrapper | the output's `/ID`, the Flate compression of rewritten streams, bounded allocation |
| phase 2 | `tests/dataset/run.py`, `tests/pdfium/` binding and `compare.py`, `dataset/tools/synth/`, `dataset/manifest.tsv` columns | the gate runs over the dataset; the planted-marker files join the synth set |

If phase 3 does not keep, for a string, whether it was read as literal or hex,
or for a dictionary, its key order, add it to the object model in a commit of
its own and record the change in §9 and in `build-3.md` §9: the writer must
re-emit what it read (`docs/DESIGN.md` §17), and an object model that forgot is
the one place this phase may touch `src/syntax/`.

## 2. Components

| File | What it holds | Lines | Reference |
|---|---|---|---|
| `src/writer/reachability.{hpp,cpp}` | `Reachability`: the set of objects reachable from the new trailer after the channel table and the edit plan have run, by an iterative walk (no recursion depth tied to file content) with a visited set; a reference to a missing object is written as `null` and counted in the report | 250 | `docs/DESIGN.md` §17; pypdf and pdfcpu writers measured at 3,564 and 2,261 lines (design record Appendix A, stream 5, read) |
| `src/writer/renumbering.{hpp,cpp}` | `Renumbering`: object numbers assigned in first-reach order of a fixed traversal (trailer `/Root`, then `/Info`, then catalog keys in their read order, depth first, arrays in index order), generation 0 throughout, so two runs give the same numbers | 150 | `docs/DESIGN.md` §17 |
| `src/writer/serialiser.{hpp,cpp}` | `Serialiser`: objects to bytes; strings in their read form (literal with the minimal escapes, or hex), names with `#xx` for delimiters and bytes outside 0x21 to 0x7E, numbers through `std::to_chars` (integers as integers, reals with the shortest round-trip form and no exponent), dictionaries in their read key order, one space between tokens, a newline after `obj`, `stream`, `endstream` and `endobj` | 450 | ISO 32000-1 §7.3 (read by the design record's stream 5) |
| `src/writer/stream_copier.{hpp,cpp}` | `StreamCopier`: the rules of `docs/DESIGN.md` §17 for a stream: untouched streams keep their `/Filter`, `/DecodeParms` and encoded bytes; a stream from an encrypted source is written decrypted with its filters kept; a rewritten stream is Flate-compressed at a fixed zlib level (the level and the zlib version make the bytes; both are pinned); `/Length` written as a direct integer; `/DL` dropped where the bytes changed | 250 | `docs/DESIGN.md` §17 |
| `src/writer/xref_stream_writer.{hpp,cpp}` | `XrefStreamWriter`: the cross-reference stream (`/Type /XRef`, `/W [1 4 2]` or the narrowest widths that hold the offsets, `/Index`, `/Size`, `/Root`, `/Info`, `/ID`), Flate-compressed with the PNG Up predictor off, written last with `startxref` and `%%EOF` | 200 | ISO 32000-1 §7.5.8 (read) |
| `src/writer/channel_table.{hpp,cpp}` | `ChannelTable`: one row per channel of `docs/DESIGN.md` §15, each row a struct of its id, its name, a `detect` function (count of occurrences in a document) and a `strip` function (the edit on the object graph); rows in the order of §15; this file is the single place a channel is named | 700 | `docs/DESIGN.md` §15; the NSA guidance, Acrobat's "Remove Hidden Information", PyMuPDF's `scrub` (design record Appendix A, stream 2, read) |
| `src/writer/page_selector.{hpp,cpp}` | `PageSelector`: `docs/DESIGN.md` §16: rebuild one flat `/Pages` node from the kept pages in the caller's order, copy inherited `/Resources`, `/MediaBox`, `/CropBox` and `/Rotate` onto each page, prune each page's resource dictionary to the names its content stream uses and each form's to the names the form uses, recursively, with a visited set; pages drawn from one content stream share it | 450 | `docs/DESIGN.md` §16 |
| `src/writer/resource_usage.{hpp,cpp}` | `ResourceUsage`: the names a content stream uses per resource category (`/Font` from `Tf`, `/XObject` from `Do`, `/ExtGState` from `gs`, `/ColorSpace` from `cs`/`CS` and inline images, `/Pattern` from `scn`/`SCN` and `cs`, `/Shading` from `sh`, `/Properties` from `BDC`), read through phase 3's `ContentLexer`; an unparsable stream keeps its whole resource dictionary and is counted, because pruning a name the stream uses breaks the page | 250 | ISO 32000-1 §7.8.3 (recalled; verify against the specification when writing, label in the comment) |
| `src/redact/write_pipeline.{hpp,cpp}` | `WritePipeline`: the fixed order of `CLAUDE.md` §3.2 (page selection, text surgery, image treatments, metadata, channel stripping, serialisation, self-check), with phases 8, 9 and 10's steps as named no-op stages this phase defines and the later phases fill, so no later phase reorders the pipeline | 200 | `CLAUDE.md` §3.2 |
| `src/writer/write_report.{hpp,cpp}` | `WriteReport`: the JSON of `pdfkit_write`: page count, the kept source pages, per channel the count stripped, the rectangles changed (empty here), `self_check` status, the warnings, the output's SHA-256 | 150 | `docs/ABI.md` |
| `src/writer/document_id.{hpp,cpp}` | `DocumentId`: the trailer `/ID` pair from the SHA-256 of the serialised body with the `/ID` placeholder bytes zeroed, so the identifier is a function of the content and two runs agree | 80 | `docs/DESIGN.md` §17 |
| `src/redact/edit_plan.{hpp,cpp}` | `EditPlan`: the edit handle's state; this phase holds the page selection (a validated, ordered, de-duplicated page list) and the document reference; it declares the members phases 8, 9 and 10 add (text matches, image treatments, metadata values) as comments naming the owning section of `docs/DESIGN.md`, never as stubs with behaviour | 120 | `CLAUDE.md` §4 (edit plan) |
| `src/api/edit.cpp`, `write.cpp`, `select_pages.cpp`, `channels.cpp` | the four catalogue entries and the edit lifecycle through `api::guarded`; the refusal messages of `docs/DESIGN.md` §19 for an empty, duplicated or out-of-range page list (what failed, what was expected, how to fix it) | 300 | `docs/ABI.md` |
| `cli/verbs/select_pages.cpp`, `channels.cpp` | the two verbs over the C ABI; `--pages` accepts `1,3-5` and refuses anything else with a usage message | 200 | `CLAUDE.md` §2 |
| `tests/unit/support/pdf_builder_channels.{hpp,cpp}` | the PDF builder extended with one knob per channel of `docs/DESIGN.md` §15 and per page-owned channel of §16, each planting a caller-given marker string (`PDFKITMARK<row>X<n>`) into that channel only | 400 | — |
| `tests/unit/writer/*.cpp` | §3 | 900 | — |
| `dataset/tools/synth/channels/` | a Python synthesiser writing one PDF per channel row with its planted marker (and one multi-page file with a marker in every page-owned channel per page), registered in the manifest with the tag of the row (`channel:<row-id>`) and the source `synth:channels` | 300 | — |
| `tests/dataset/gates/phase7.py` | the gate of §4 over a run report | 100 | — |
| `tests/pdfium/render.py` | page rendering through the pinned PDFium binary at 100 dpi, annotations off, into raw RGBA, and the pixel comparison (exact, and outside a rectangle set with a one-pixel dilation, reused by phases 8 and 9) | 150 | PDFium `fpdfview.h` (`FPDF_RenderPageBitmap`, flag `FPDF_ANNOT` not set; recalled, verify against the header in the pinned build) |

## 3. Tests

### 3.1 Unit tier (`tests/unit/writer/`), fixtures written in code

| File | Asserts |
|---|---|
| `serialiser_test.cpp` | each object type round-trips through the phase 3 parser to an equal object; a literal string read literal is written literal and a hex one hex; escapes of `(`, `)`, `\` and bytes outside printable ASCII; names with `#xx`; integers, reals (`0.1`, `-0.5`, `1e-7` written without exponent), large integers; dictionary key order kept |
| `reachability_test.cpp` | an unreferenced object is gone; an object referenced only from a stripped channel is gone; a reference cycle terminates; a missing object becomes `null` and is counted |
| `renumbering_test.cpp` | numbers are dense from 1 in first-reach order; two runs over one plan give identical numbers; a shuffled object table in the source gives identical output |
| `stream_copier_test.cpp` | an untouched Flate stream is copied byte for byte with its filters; an `ASCII85Decode` + `FlateDecode` chain kept; a stream of an RC4 and of an AESV3 source written decrypted with filters kept and readable; a rewritten stream compressed and `/Length` right |
| `xref_stream_writer_test.cpp` | offsets point at `N 0 obj`; widths minimal; the output opens through the phase 3 cross-reference reader without reconstruction (`repaired` empty) |
| `channel_table_test.cpp` | **one test per row of `docs/DESIGN.md` §15**: the builder plants a marker in that channel alone, `pdfkit_channels` reports it present with count 1, `pdfkit_write` produces bytes in which the marker does not occur in the raw file nor in any decompressed stream, and the output opens; a test asserts the table's row count equals the count of these tests, so a row added without its test fails |
| `optional_content_test.cpp` | the author decision of `docs/DESIGN.md` §15 on optional content: group names replaced by neutral names, the ON/OFF state kept, an OFF group's content still hidden (asserted on the configuration, the pixel check is the dataset's) |
| `page_selector_test.cpp` | every proper subset of a four-page planted fixture: page count, order, inherited attributes on each kept page, resource pruning (an XObject in the shared `/Resources` drawn only by a removed page is absent), a form drawing a form keeps both, a content stream shared by two kept pages written once; the leak assertions of `docs/DESIGN.md` §16 (no removed page's marker in the decompressed output, every kept marker present except those of stripped channels, every reference resolves, kept page content streams byte-identical) |
| `write_determinism_test.cpp` | the same plan written twice gives the same bytes and the same `/ID` |
| `api_edit_test.cpp` | the four symbols through the C ABI: status codes, the JSON error object's three fields for each refusal, buffers freed |

Every new test class is labelled `unit` in CTest.

### 3.2 Dataset

- Tags this phase adds to `dataset/taxonomy.tsv` with a minimum of one file
  each: one `channel:<row-id>` tag per row of `docs/DESIGN.md` §15, carried by
  the synthesised files; `pages:multi` (minimum 50) for files with two or more
  pages, set by `dataset/tools/report.py` from the `pages` column.
- Manifest expectation columns this phase fills (added to
  `dataset/manifest.tsv` and described in `dataset/README.md`):
  `write` (`identical`, `known_gap:<reason>` or `n/a` for a file the open
  column refuses) and `select` (`ok`, `known_gap:<reason>`, `n/a` for one page).
- `tests/dataset/run.py` gains the `write` and `select` checks: identity write
  of every file that opens, and a selection of a seeded random proper subset
  (seed `7` plus the file's id) of every file with two or more pages.

### 3.3 PDFium comparison

`tests/pdfium/compare.py --run <run-id> --check write` renders every page of
input and output at 100 dpi with annotations off and compares the pixels
exactly; `--check select` compares each kept page against its source page;
`--check text` compares PDFium's text of the output with PDFium's text of the
input (F1) and pdfkit's own text of both once phase 5 has landed (before phase
5 lands, the pdfkit half of the text check is skipped by name).

## 4. Gate

Measured over every manifest file whose `open` expectation is `opens` or
`repaired`:

| Measure | Threshold |
|---|---|
| identity write re-opens in pdfkit | 100% |
| identity write opens in PDFium, of the files PDFium opens as input | 100% |
| identity write: pages rendered by PDFium pixel-identical to the input rendered with annotations off | at least 99% of pages; every page below named in the report with its cause (optional-content state, a stripped structure, a renderer difference) |
| text agreement output against input (PDFium on both sides; pdfkit on both sides once phase 5 is merged) | F1 = 1.0 on at least 99.5% of files |
| planted-marker synth files: markers of stripped channels found in the output's raw or decompressed bytes | 0 (100% of files clean) |
| page selection, files with two or more pages: output page count right | 100% |
| page selection: kept pages pixel-identical to their source pages | at least 99% of kept pages |
| page selection leak test over the planted multi-page synth files | 0 leaks |
| two runs of the dataset: output bytes identical | 100% |
| crashes, hangs past 30 s, or anything but a status from the ABI | 0 |

Commands, in the phase's worktree after a release build:

```
cmake --preset mingw-release && cmake --build --preset mingw-release
python tests/dataset/run.py --checks open,write,select --runs 2
python tests/pdfium/compare.py --run <run-id> --check write,select,text
python tests/dataset/gates/phase7.py --run <run-id>
```

The report goes to `dataset/reports/<run-id>/` (ignored); the summary table of
`phase7.py` goes into the merge commit message.

## 5. Kill criterion and the question to the owner

**Quantity**: the share of pages whose identity write PDFium renders
pixel-identical to the input, annotations off. **Threshold**: under 97%.

**The question**: "The writer reproduces N of M pages pixel for pixel when it
writes a file back unchanged; the pages that differ fail on <the named causes,
with counts>. Do you want:

- (a) one more writer session on those causes (recommended): about 6 to 10
  hours and $100 to $160;
- (b) accept the writer as it is and record the causes as known gaps in the
  manifest: no extra cost now, and every later redaction gate inherits the
  gap;
- (c) a half-day examination of the differing pages only, then decide: about
  3 to 4 hours and $50 to $70."

**Unattended action**: write the question with the evidence (the failing pages
by file, the cause classification, the report path) into `OPEN-QUESTIONS.md`;
merge the branch if checks and the unit tier are green, with trailer
`Build-Phase: 7 gate-failed`; phases that depend on it (8, 9, 10) proceed and
report their gates with that caveat.

A planted marker found in an output is never a gate miss to record and pass
over: the channel row is fixed in this phase, because every later phase writes
through this table.

## 6. Rollback

Revert the merge. Nothing persists on disk in a new shape: the library returns
bytes, and the manifest columns this phase adds are additive and ignored by the
earlier phases' checks. The edit-plan file is new; phases 8 and 9 read it, so a
rollback after they land reverts them too.

## 7. Execution order inside the phase

1. On the branch, run `python build/check.py` and `ctest --preset unit` on the
   base; a red base is repaired in a commit of its own and recorded in §9.
2. `serialiser_test.cpp` red, then `Serialiser`; the same for `Reachability`,
   `Renumbering`, `StreamCopier`, `XrefStreamWriter`, `DocumentId`.
3. `EditPlan`, `WritePipeline`, `WriteReport`, `src/api/edit.cpp` and
   `write.cpp`: identity write through the C ABI green on the unit fixtures.
4. **Split point**: if the session is running out of room, push here with
   `Build-Phase: 7 incomplete` and the reason; the next session starts at 5.
5. `pdf_builder_channels`, then `channel_table_test.cpp` red row by row, then
   `ChannelTable` row by row in the order of `docs/DESIGN.md` §15;
   `src/api/channels.cpp` and the `channels` verb.
6. `ResourceUsage`, `PageSelector` with `page_selector_test.cpp`;
   `src/api/select_pages.cpp` and the `select-pages` verb.
7. The synthesiser under `dataset/tools/synth/channels/`, the manifest rows,
   the taxonomy tags, `tests/pdfium/render.py`, the dataset checks and
   `phase7.py`; run the gate; read every page below the threshold and classify
   its cause; a writer fault found here is fixed here.
8. The test sweep: `python build/check.py`, `ctest --preset unit`,
   `ctest --preset sanitize`, the gate commands of §4.
9. The documentation sweep, last, from what landed: `docs/ABI.md` (the four
   symbols, the write report's JSON), `docs/DESIGN.md` §15, §16 and §17 only
   where the landed code differs from the text (a difference is a deviation in
   §9), `dataset/README.md` (the two columns), `CLAUDE.md` §2 unchanged unless
   a failure mode differs.

## 8. Opening prompt

> Read `CLAUDE.md`, `docs/DESIGN.md` chapters 1, 3, 4, 15, 16, 17 and 19,
> `docs/ABI.md`, `dataset/README.md`, all of `build-7.md`, and the deviations
> sections (§9) of `build-1.md`, `build-2.md` and `build-3.md`, then implement
> `build-7.md` exactly as written. Before planning, read as landed
> `src/syntax/` (the object model, `Document`, `ContentLexer`),
> `src/platform/` (budget, checked arithmetic, hashes, the zlib wrapper, JSON),
> `src/api/catalogue.cpp`, `src/api/guarded.*`, `include/pdfkit/pdfkit.h` and
> `tests/unit/support/`. Work in a worktree of your own on branch
> `phase/7-writer` created from `main`. Run `python build/check.py` and
> `ctest --preset unit` before you start and before you finish; a red base is
> repaired in a commit of its own and recorded as a deviation. Every external
> fact you implement carries its source and whether you read it there or
> recalled it, in the comment and in `docs/SOURCES.md`. The dataset and the
> PDFium binary live in the main checkout (resolve it with
> `git rev-parse --git-common-dir`); never copy them into the worktree. No
> step waits for the owner: a question goes into `OPEN-QUESTIONS.md` with the
> recommended answer first and the price of each option. Write failing tests
> first. Finish with a last commit whose message carries `Build-Phase: 7 done`,
> or `Build-Phase: 7 incomplete` or `Build-Phase: 7 gate-failed` with the
> reason, and push the branch; never merge into `main`, because the team lead
> merges. Do not embed the placeholder font, do not edit content streams for
> text, and do not treat images: phases 8 and 9 own them. If you must deviate
> from this file, record each deviation in its §9 and in every downstream build
> file it affects (`build-8.md`, `build-9.md`, `build-10.md`), in the same
> change.

## 9. Deviations recorded by this phase

1. **`syntax::Stream` gains a created form** (`Stream(dictionary, data)`,
   `owns_data()`, `owned_data()`, `with_dictionary()`), its own commit, recorded
   in `build-3.md` §9 item 17; `Document::raw` and `Document::decode` read it
   and never decrypt it.

### Deviations of earlier phases that affect this one

From `build-1.md` §9:

- `pdfkit_write(edit, options_json, out_pdf, out)` has two output buffers: the PDF bytes in `out_pdf`, the write report or the error in `out`; `pdfkit_edit_new(doc, out_edit, out)` is implemented (item 4).
- `cli/verbs/select_pages.cpp` already runs open, edit, select, write and writes the output file only on success (item 7).
- Remove the `build/paths_pending.tsv` row `src/writer/channel_table.cpp` in the change that creates it (item 13).
- The sanitizer tier is two runs: `ctest --preset sanitize` (GCC, UBSan in trap mode) and `ctest --preset clang-sanitize` (llvm-mingw, ASan and UBSan) (`build-1.md` §9 item 8).

From `build-3.md` §9:

- The object model is `syntax::Object` (nested `Reference`, `Number`, `String` with its literal-or-hex flag, `Array` = `std::vector<Object>`), `syntax::Dictionary` (insertion-ordered) and `syntax::Stream` (dictionary, raw byte range, owner); all immutable and shared, built through the `make_*` factories (item 3).
- `syntax::Document::open(bytes, Limits)` returns the document; `object(reference)`, `resolve(object)` and `resolve_entry(dictionary, key)` resolve (strings come back decrypted), `decode(stream, page)` decrypts and runs the filters up to the first image codec (`Filters::Result::remaining` names the codecs left), `content_pieces(contents, page)` decodes a `/Contents` value, `pages()` holds `syntax::Page` (boxes, rotation, resolved resources, contents), `repair(reason)` and `warn(code, message, page)` record (items 3, 4).
- `syntax::Resources(document, page.resources, page.number)` looks a resource up by category and name, with the form fallback in `for_form`; `syntax::ContentLexer(pieces, document.limits(), "page N")` yields `Operation`s (operator, operands, byte range, `operator_start`; an inline image is one `BI` operation with its data range) and `locate(offset)` maps back to the piece (item 10).
- `Document::decode` charges the budget on every call, so a layer that decodes a shared stream more than once caches the decoded bytes (item 11).
- `Stream` holds no data of its own: a stream the writer creates needs an owned-data form added to `syntax::Stream`; a string keeps whether it was read literal or hex (`Object::String::hex`), so the writer can re-emit the form it read (item 3).
- The document's table is `Document::entries()` (object number to `CrossReference::Entry`) and its trailer `Document::trailer()`; `Document::raw(stream)` gives a stream's bytes as stored, for byte-for-byte copying (items 3, 5).
