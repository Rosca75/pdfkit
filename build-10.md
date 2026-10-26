# build-10 — Metadata, the full dataset, release artefacts and the C ABI from .NET

| Field | Value |
|---|---|
| Phase | 10 |
| Branch | `phase/10-release` |
| Depends on | 1, 2, 3, 4, 5, 6, 7, 8, 9 |
| Parallel with | nothing |
| Model | `opus` |
| Owns | `src/syntax/metadata_reader.hpp`, `src/syntax/metadata_reader.cpp`, `src/syntax/xmp_scanner.hpp`, `src/syntax/xmp_scanner.cpp`, `src/writer/metadata_writer.hpp`, `src/writer/metadata_writer.cpp`, `src/redact/edit_plan.hpp`, `src/redact/edit_plan.cpp` (additive), `src/redact/self_check.cpp` (the metadata surface, additive), `src/api/metadata.cpp`, `src/api/set_metadata.cpp`, `cli/verbs/metadata.cpp`, `cli/verbs/redact.cpp` (the metadata section), `tests/unit/metadata/`, `tests/abi/`, `dataset/manifest.tsv`, `dataset/taxonomy.tsv`, `dataset/tools/`, `tests/dataset/gates/phase10.py`, `tests/pdfium/metadata.py`, `build/package.py`, `build/check_artefact.py`, `build/budgets.tsv`, `README.md`, `docs/archive/build/` |
| Size | about 2,500 lines of C++ including tests, about 600 lines of C# and Python for the ABI tier, about 300 lines of Python for packaging and the gate, and the dataset growth (manifest rows, synthesised files) |
| Price | estimate: 12 to 16 hours of session time and roughly $200 to $280 of model spend at list prices. Calibrated from the design record's stage 2 (channel table and XMP 600 lines of C#) and change-12d (the proof batch, about 1,400 lines, 6 to 8 h and $90 to $140), scaled by 1.3 for C++, plus the dataset growth, which is the least predictable term: scouting and synthesis for every under-filled tag can take a third of the session |

## 0. What this phase ships

The last two catalogue functions, a dataset of 500 to 1,000 files with every
tag covered and every earlier gate re-measured on it, the release artefact, the
C ABI exercised from .NET and Python, and the order-wide documentation sweep.

Catalogue rows of `CLAUDE.md` §2 delivered:

- **4 Extract metadata**: `pdfkit_metadata` answers one row per field of
  `docs/DESIGN.md` §10 with the Info value and the XMP value side by side and
  whether they agree.
- **7 Rewrite metadata**: `pdfkit_set_metadata` records reviewed values on an
  edit handle; the writer writes them into both the Info dictionary and the XMP
  packet at the metadata stage, and regenerates the document identifiers.

CLI: the `metadata` verb (`pdfkit metadata <in.pdf>`), and the `redact` job
file's `metadata` section (accepted; it was refused with an actionable message
until this phase).

Release: `dist/pdfkit-<version>-windows-x64.zip` per `CLAUDE.md` §10, and the
`v0.1.0` tag when, and only when, every phase's gate passed (§4).

## 1. What it reads from earlier phases

Read as landed, and read every earlier build file's §9 for deviations first:
this phase's documentation sweep is written from what landed, and each
deviation is a line it may have to correct.

| From | What | Why |
|---|---|---|
| phase 1 | `include/pdfkit/pdfkit.h`, `src/api/catalogue.cpp`, `build/check.py` and its checks, `build/toolchain.md`, `CMakePresets.json`, `tests/abi/` (the hello loaded from .NET and Python) | the ABI tier grows from the hello; the release preset and the checks gate the artefact |
| phase 2 | `dataset/manifest.tsv`, `dataset/taxonomy.tsv`, `dataset/tools/` (fetch, register, report, synth, mutate, `generate_office.ps1`), `tests/dataset/run.py`, `tests/pdfium/` | the dataset grows to its final size with the same tools; every gate re-runs |
| phase 3 | `src/syntax/` (the Info dictionary through the trailer, the catalog's `/Metadata` stream, strings in PDFDocEncoding and UTF-16BE) | the metadata reader reads the object model |
| phase 7 | `src/redact/write_pipeline.*` (the metadata stage), `ChannelTable` (the XMP history row), `DocumentId` | the writer writes the metadata and the identifiers |
| phase 8 | `src/redact/self_check.*`, `Folding` | the self-check gains the metadata surface for rewritten fields |
| phases 4 to 9 | every gate script `tests/dataset/gates/phase<n>.py` | §4 re-runs them all on the final dataset |

## 2. Components

| File | What it holds | Lines | Reference |
|---|---|---|---|
| `src/syntax/metadata_reader.{hpp,cpp}` | `MetadataReader`: `docs/DESIGN.md` §10: the Info fields (`Title`, `Author`, `Subject`, `Keywords`, `Creator`, `Producer`, `CreationDate`, `ModDate`) decoded from PDFDocEncoding or UTF-16BE with a byte-order mark (UTF-16LE with a mark tolerated and counted), and the XMP fields (`dc:title`, `dc:creator`, `dc:description`, `dc:subject`, `xmp:CreatorTool`, `pdf:Producer`, `pdf:Keywords`, `xmp:CreateDate`, `xmp:ModifyDate`) paired by meaning into one row per field; dates normalised for the agreement test only, never rewritten on read | 350 | ISO 32000-1 §14.3 and Annex D (design record Appendix A, stream 5, read); XMP Specification Part 1 (recalled; verify the property names when writing) |
| `src/syntax/xmp_scanner.{hpp,cpp}` | `XmpScanner`: a byte-positioned scan of the packet for the named elements (both the element form and the attribute shorthand, `rdf:Alt` with `x-default`, `rdf:Seq` and `rdf:Bag` lists), recording each value's byte range so the writer can splice; a packet that will not scan is reported with the `xmp_unreadable` warning and is never guessed at | 350 | `docs/DESIGN.md` §10 |
| `src/writer/metadata_writer.{hpp,cpp}` | `MetadataWriter`: written values into both places: the Info dictionary rebuilt with the reviewed fields (UTF-16BE with a mark when a value is outside PDFDocEncoding, PDFDocEncoding otherwise), the XMP packet edited by splicing the scanned ranges and keeping every other byte, `xmpMM:History` dropped, `xmpMM:DocumentID` and `xmpMM:InstanceID` and the trailer `/ID` regenerated from the output hash; a field the caller did not set keeps its value in both places; an unreadable packet is dropped with the warning | 350 | `docs/DESIGN.md` §10 and §17 |
| `src/redact/edit_plan.{hpp,cpp}` (extended) | the metadata values; additive only | 40 | — |
| `src/redact/self_check.cpp` (extended) | every original value of a rewritten field is a redacted string for the self-check, in every surface of `docs/DESIGN.md` §18, Info and XMP included | 60 | `docs/DESIGN.md` §18 |
| `src/api/metadata.cpp`, `set_metadata.cpp` | the two entries through `api::guarded`, the JSON of `docs/ABI.md`, the refusal of a field outside the list (`docs/DESIGN.md` §19) | 200 | `docs/ABI.md` |
| `cli/verbs/metadata.cpp`, `cli/verbs/redact.cpp` (metadata section) | the verb and the job file's `metadata` object | 120 | — |
| `tests/unit/metadata/*.cpp` | §3 | 700 | — |
| `tests/abi/dotnet/` | a .NET 10 console test project (no NuGet package beyond the SDK, so nothing is fetched) declaring every exported symbol with `LibraryImport`, calling each catalogue function over a set of dataset files and one fixture written by the CLI, checking statuses, the JSON error object's three fields on a refusal, buffer frees, and that a redaction job's output opens again; it exits non-zero on the first failure with the file and the function | 400 | .NET `LibraryImport` source generator (recalled; the SDK on the laptop is 10.0.204) |
| `tests/abi/python/test_abi.py` | the same calls through `ctypes` | 200 | — |
| `tests/abi/run.py` | builds and runs both, against the release DLL | 60 | — |
| `build/package.py` | the release zip of `CLAUDE.md` §10 from the `mingw-release` build, the version read from the one place the CMake project declares it, the archive's entries in a fixed order with fixed timestamps so two packagings are byte-identical, the size checked against `build/budgets.tsv` | 150 | — |
| `build/check_artefact.py` | reads the DLL's and the EXE's import tables with `objdump -p` from the pinned toolchain and fails on any import that is not a Windows system DLL (the allowed list in the script with its reason), and on an export that is not in the header | 100 | PE import table via binutils `objdump` (recalled) |
| `build/budgets.tsv` | one row per artefact with its byte budget, set from the first release build plus 25% and stated as such | data | — |
| `tests/pdfium/metadata.py` | PDFium's `FPDF_GetMetaText` for each Info field of the input, compared with `pdfkit_metadata` after normalising encodings (recalled API name; verify against the pinned headers) | 80 | PDFium `fpdf_doc.h` (design record Appendix A, stream 1, read) |
| `tests/dataset/gates/phase10.py` | the gate of §4: the metadata measures, then each earlier `phase<n>.py` over the final run, one summary table | 150 | — |
| dataset growth | manifest rows and synthesised files until `report.py` is green (§3.2) | data | `CLAUDE.md` §7 |

## 3. Tests

### 3.1 Unit tier (`tests/unit/metadata/`), fixtures written in code

| File | Asserts |
|---|---|
| `metadata_reader_test.cpp` | each Info field in PDFDocEncoding and in UTF-16BE; a UTF-16LE value with a mark read and counted; each XMP field in element form and attribute shorthand; `rdf:Alt`, `rdf:Seq`, `rdf:Bag`; agree and disagree rows; an Info-only and an XMP-only file; an encrypted file's Info strings decrypted and `/EncryptMetadata false` honoured |
| `xmp_scanner_test.cpp` | byte ranges exact; a malformed packet reported with the warning; a packet with custom namespaces scanned without touching them |
| `metadata_writer_test.cpp` | a written field appears in both places; a field not set keeps both values; custom namespaces survive byte for byte; `xmpMM:History` gone; the identifiers regenerated and deterministic; a value outside PDFDocEncoding written as UTF-16BE with a mark |
| `self_check_metadata_test.cpp` | an original value planted in a second place (an object-level string, a custom XMP element) is found and the write refused naming the surface |
| `api_metadata_test.cpp` | both symbols through the C ABI; the refusal's three fields |

### 3.2 Dataset

- **Size and coverage.** The manifest holds at least 500 and at most 1,000
  files, and `python dataset/tools/report.py` passes: every tag of
  `dataset/taxonomy.tsv` is at or above its minimum. The session first runs the
  report, lists the under-filled tags, then fills them in this order of
  preference: a further public file from a set the manifest already uses, a
  further public set scouted on the web and admitted by the licence rule of
  `CLAUDE.md` §7.2 (its licence and URL recorded per file), a generated file
  through `dataset/tools/generate_office.ps1` (Word, Excel, PowerPoint and Edge
  automation with timeouts, run unattended; the owner allowed it), a
  synthesised file through `dataset/tools/synth/`, a mutated file through
  `dataset/tools/mutate.py`. No client document is ever added.
- **Metadata tags**: `meta:info_only`, `meta:xmp_only`, `meta:both_agree`,
  `meta:both_disagree`, `meta:utf16`, `meta:xmp_custom_namespace`,
  `meta:xmp_history`, `meta:xmp_unreadable`, each with a minimum of three
  files.
- **Manifest columns** this phase fills: `metadata` (`read`,
  `known_gap:<reason>`, `n/a` for a file with neither), `set_metadata`
  (`clean`, `known_gap:<reason>`, `n/a`).
- `tests/dataset/run.py` gains the `metadata` and `set_metadata` checks: read
  every file's metadata; rewrite every present field to `REDACTED <FIELD>` in a
  seeded job (seed `10` plus the file's id).

### 3.3 PDFium comparison

`tests/pdfium/metadata.py` compares the Info fields with PDFium's reading; the
leak scan of `tests/pdfium/leak.py` gains every original value of a rewritten
field.

## 4. Gate

| Measure | Threshold |
|---|---|
| dataset size | at least 500, at most 1,000 files; `report.py` green (every tag at its minimum) |
| files with an Info dictionary or an XMP packet whose metadata is read | 100% |
| Info fields agreeing with PDFium's `FPDF_GetMetaText` after normalising encodings | 100% (a disagreement is a reader fault to fix, or a PDFium difference named in the report) |
| rewritten outputs whose Info and XMP carry the written values | 100% |
| original values of rewritten fields found anywhere in the output bytes or by PDFium | 0 |
| every earlier phase's gate (phases 3 to 9) re-run on the final dataset | each at its threshold; reported in one table with the earlier run's number beside it |
| ABI tier from .NET and from Python | green |
| the release DLL and EXE import only operating-system DLLs; the zip within its budget; two packagings byte-identical | yes |
| two runs of the whole dataset: outputs byte-identical | 100% |

Commands:

```
cmake --preset mingw-release && cmake --build --preset mingw-release
python dataset/tools/report.py
python tests/dataset/run.py --checks all --runs 2
python tests/pdfium/compare.py --run <run-id> --check all
python tests/pdfium/leak.py --run <run-id> --images --metadata
python tests/pdfium/metadata.py --run <run-id>
python tests/dataset/gates/phase10.py --run <run-id>
python tests/abi/run.py
python build/package.py && python build/check_artefact.py
```

The report goes to `dataset/reports/<run-id>/`; the summary table (every
phase's gate on the final dataset) goes into the merge commit message and into
the release notes of the tag.

**The tag.** `v0.1.0` is tagged on the merged `main` and pushed when every
phase from 3 to 10 merged with `Build-Phase: <n> done` and every row of the
table above holds. Otherwise no tag is made, and `OPEN-QUESTIONS.md` says which
gate is missing and what the release would ship with.

## 5. Kill criterion and the question to the owner

This phase has no kill criterion of its own beyond the gates it re-runs.

**Quantity**: any earlier phase's gate that regresses below its threshold on
the final dataset, or that never passed. **The question**: "On the final
dataset of N files, <gate> reads <number> against its threshold of
<threshold>, failing on <named causes with counts>. Do you want:

- (a) a fix session on those causes, then the release (recommended): about 4
  to 10 hours and $70 to $180, depending on the gate;
- (b) release as `0.1.0-preview` with the known gaps listed in the release
  notes and the manifest: about 1 hour and $15;
- (c) no release until every gate holds: no extra cost now."

A regression of a leak measure (phase 8's or phase 9's survivors, or a
metadata original value found) is never offered as (b): it is a stop-the-line
defect handled as `docs/archive/build/build-8.md` §5 states.

**Unattended action**: write the question with the evidence into
`OPEN-QUESTIONS.md`; merge the branch if checks, the unit tier, the ABI tier and
every leak measure are green, with trailer `Build-Phase: 10 gate-failed`; make
no tag.

## 6. Rollback

Revert the merge: the metadata functions, the ABI tier, the packaging and the
dataset rows leave together; the dataset files on disk stay in the ignored
folder and are the owner's to delete. A pushed tag is deleted with
`git push origin :refs/tags/v0.1.0` only on the owner's instruction, because a
tag someone has fetched is a release someone may hold.

## 7. Execution order inside the phase

1. Run `python build/check.py`, `ctest --preset unit` and
   `python dataset/tools/report.py` on the base; a red base is repaired in a
   commit of its own and recorded in §9; the report's under-filled tags are
   the dataset work list.
2. `metadata_reader_test.cpp` and `xmp_scanner_test.cpp` red, then
   `MetadataReader` and `XmpScanner`; `src/api/metadata.cpp` and the
   `metadata` verb.
3. `metadata_writer_test.cpp` red, then `MetadataWriter`, the edit-plan
   extension, the pipeline stage, the self-check extension,
   `src/api/set_metadata.cpp`, the job file's `metadata` section.
4. **Split point**: if the session is running out of room, push here with
   `Build-Phase: 10 incomplete` and the reason; the next session starts at 5.
5. The dataset growth until `report.py` is green; the metadata tags and
   columns; `tests/pdfium/metadata.py`; the dataset checks.
6. `tests/abi/`: the .NET project and the Python test against the release DLL.
7. `build/package.py`, `build/check_artefact.py`, `build/budgets.tsv`.
8. The full gate of §4; read every miss and classify it.
9. The test sweep: `python build/check.py`, `ctest --preset unit`,
   `ctest --preset sanitize`, `python tests/abi/run.py`.
10. The documentation sweep, last, written from what landed: `README.md`
    (what the library does, the catalogue table, how to build with the pinned
    toolchain, how to call it from .NET and Python, the licence), `CLAUDE.md`
    (§2's failure modes where the landed code differs; §1's table and §2's
    "Build file" column pointing at `docs/archive/build/` for every phase that
    is done, moved in the same commit as the files; §7 and §10 numbers where
    the landed tools differ), `docs/ABI.md` (complete, every symbol and shape),
    `docs/DESIGN.md` (each chapter checked against the code; a difference is
    corrected in the text when the code is right and recorded in §9),
    `dataset/README.md`, `docs/SOURCES.md`, `THIRD-PARTY-NOTICES.md` against
    `third_party/inventory.tsv`. Each done phase's `build-<n>.md` moves to
    `docs/archive/build/` with `git mv`; a phase not done stays where it is.
11. The tag, when §4 allows it.

## 8. Opening prompt

> Read `CLAUDE.md`, `docs/DESIGN.md` (all of it: this phase sweeps it),
> `docs/ABI.md`, `docs/SOURCES.md`, `dataset/README.md`,
> `dataset/taxonomy.tsv`, all of `build-10.md`, and the deviations sections
> (§9) of `docs/archive/build/build-1.md` to `docs/archive/build/build-9.md`, then implement `build-10.md` exactly as
> written. Before planning, read as landed `src/syntax/` (the trailer, the
> catalog, strings), `src/redact/write_pipeline.*`,
> `src/writer/channel_table.cpp`, `src/redact/self_check.*`,
> `src/redact/edit_plan.*`, `dataset/tools/`, `tests/dataset/`,
> `tests/pdfium/`, `tests/abi/` and `build/`. Work in a worktree of your own on
> branch `phase/10-release` created from `main`. Run `python build/check.py`,
> `ctest --preset unit` and `python dataset/tools/report.py` before you start
> and before you finish; a red base is repaired in a commit of its own and
> recorded as a deviation. Every external fact you implement carries its
> source and whether you read it there or recalled it, in the comment and in
> `docs/SOURCES.md`. The dataset and the PDFium binary live in the main
> checkout (resolve it with `git rev-parse --git-common-dir`); grow the
> dataset there, never in the worktree, and never add a client document. You
> may run `dataset/tools/generate_office.ps1` unattended: the owner allowed it,
> with its timeouts. No step waits for the owner: a question goes into
> `OPEN-QUESTIONS.md` with the recommended answer first and the price of each
> option. Write failing tests first. The documentation sweep is last and is
> written from what landed. Finish with a last commit whose message carries
> `Build-Phase: 10 done`, or `Build-Phase: 10 incomplete` or
> `Build-Phase: 10 gate-failed` with the reason, and push the branch; never
> merge into `main` and never tag, because the team lead merges and tags when
> §4 allows it. If you must deviate from this file, record each deviation in
> its §9, in the same change.

## 9. Deviations recorded by this phase

1. **Two parts, by the team lead's instruction.** Part A (this push, run in
   parallel with phase 9): the metadata functions, the dataset at 953 rows
   with every metadata tag filled, the ABI tier, the packaging, and the
   metadata gate. Part B, after phase 9 merges: the rebase, every phase's gate
   re-run on the final dataset into one table, the documentation sweep, the
   archive of finished build files and the tag rule of §4. The base was green
   (`python build/check.py --fast` and the unit tier on `11508c3`); no repair
   commit.
2. **Files beyond §2**, each for one class or one concern:
   `src/syntax/pdf_doc_encoding.{hpp,cpp}` (`PdfDocEncoding`: the Annex D
   table the writer layer needs and cannot reach in the fonts layer, checked
   code by code against the text layer's reading),
   `tests/unit/metadata/support/metadata_fixture.*` (the one-page fixture
   with Info entries and a packet, and a write through the C ABI),
   `cli/verbs/dataset_metadata.*` (the dataset verb's `metadata` and
   `set_metadata` members), `dataset/tools/synth/metadata/` (the three
   unreadable packets), `tests/abi/python/test_abi.py`; the .NET project is
   `tests/abi/dotnet/PdfkitAbi.csproj` (renamed from `AbiHello.csproj`).
   Edits outside the owned paths, each additive: `src/writer/channel_table.cpp`
   (item 4), `src/writer/document_id.*` and `file_writer.cpp` (the XMP
   identifiers patched with the `/ID`), `src/redact/write_pipeline.cpp`
   (items 5 to 7), `src/redact/self_check.cpp` (item 8),
   `src/api/catalogue.cpp`, `cli/verbs/dataset.cpp` and `dataset_redact.*`
   (item 10), `tests/unit/writer/channel_table_test.cpp` (the identifiers
   now stay, regenerated), `tests/pdfium/pdfium.py` (a `meta` request with
   the eight Info fields), `tests/pdfium/leak.py` (`--metadata`, item 8),
   `dataset/tools/synth/run.py` (the `synth.metadata` package),
   `dataset/tools/common.py`, `add.py` and their tests, `tests/dataset/run.py`
   and its test (items 9 and 11), `CLAUDE.md` §7.3 (the `metadata` group).
3. **The tests were written beside the code, not seen red first**: each
   step's unit tests were written after its classes, in the same step, and
   passed on their first run; the dataset found the faults the tests had
   missed (items 6, 7 and 12), each fixed with a test that fails without the
   fix.
4. **The XMP identifiers stay, regenerated** (the first option of the
   `docs/archive/build/build-7.md` note, `OPEN-QUESTIONS.md` §2.12 item 3): the metadata stage
   writes `xmpMM:DocumentID` and `xmpMM:InstanceID` as placeholders, the
   channel strip keeps those two only when they hold the placeholder, and the
   file writer patches them with `xmp.did:` and `xmp.iid:` and the first and
   second 16 bytes of the output's SHA-256 (the `/ID` is the first 16). The
   metadata stage removes every other media-management property by
   namespace, so `xapMM:` properties, which the strip's literal prefix
   missed, go too.
5. **The metadata stage runs on every write**, whatever the plan, for the
   identifiers and the history; a write without metadata values leaves the
   Info dictionary as it is.
6. **A value outside the field list that repeats a redacted string goes**
   (`OPEN-QUESTIONS.md` §2.12 item 1): a custom Info entry, an XMP property no
   field pairs with and the packet's `x:xmptk` are removed when an original
   value of a rewritten field, or a string the text redaction removes, occurs
   in them under the default folding. The first dataset run refused 296 of
   the 802 metadata rewrites for such repeats (`xmp:MetadataDate`,
   `pdfx:Identifier`, `photoshop:headline`, `x:xmptk`), which no function
   could rewrite. `docs/DESIGN.md` §10 says so.
7. **An original of fewer than three letters or digits is not a self-check
   needle** (`OPEN-QUESTIONS.md` §2.12 item 2); every other original value of
   a rewritten field (the Info value, each paired XMP value and each array
   item) is, on every page, unless the plan also searches the page text for
   it, which keeps the changed-pages-only reading of `docs/archive/build/build-8.md` §9 item 8.
8. **The self-check's metadata surfaces, refined**: the `info` surface reads
   the strings of an Info entry's arrays and dictionaries (the string walk
   leaves the Info object to it), and the `xmp` surface leaves out the
   namespace declarations' URIs ("http", "adobe", "rights" refused 9 text
   redactions in the first run); `tests/pdfium/leak.py` reads XMP the same
   way.
9. **The manifest columns** are `expect_metadata` (`read`,
   `known_gap:<reason>`, `n/a`; it held `agree` or `n/a`, every row `n/a`)
   and `expect_set_metadata` (`clean`, `known_gap:<reason>`, `n/a`), placed
   before `notes`; whether Info and XMP agree is the file's tag. The tags are
   `meta_info_only`, `meta_xmp_only`, `meta_both_agree`,
   `meta_both_disagree`, `meta_utf16`, `meta_xmp_custom_namespace`,
   `meta_xmp_history` and `meta_xmp_unreadable` (the taxonomy spells tags
   with underscores), group `metadata`, minimum 3, detector `pdfkit`: a
   dataset run's outcome, written into the manifest by
   `tests/dataset/gates/phase10.py --write-manifest`.
10. **The dataset jobs**: the `set_metadata` member writes `[_n_]` (n the
    field's place in the field list), not `REDACTED <FIELD>`, because no
    searched original can occur in a value with no letter ("Word" occurred in
    "REDACTED KEYWORDS" and refused two files), and nothing is drawn, so no
    seed; it also searches the page text for every original it rewrites, as a
    caller must when the title is also the first heading. The `redact_text`
    member rewrites, to a fixed point, the fields that hold one of its words
    or an original of a field it rewrote, and searches their originals too
    (`tests/dataset/outcome.md`).
11. **The dataset tier's per-file timeout is 30 s per started block of 200
    pages** (`OPEN-QUESTIONS.md` §2.12 item 4): the verb now reads every page
    three times, and dccc-0135 (910 pages) takes 29 s alone.
12. **Writer details the dataset showed**: a rewritten element keeps its own
    start tag (pikepdf declares `xmlns:dc` on `dc:creator` itself, and the
    first writer dropped the binding); a packet that states nothing (an
    empty stream, an empty `rdf:RDF`) is dropped; a file without a packet
    gains none (`OPEN-QUESTIONS.md` §2.12 item 5); a field set to `null` is
    removed from both places; a date given in either form is written in each
    place's form, any other value as given.
13. **`pdfkit_metadata` answers more than §2 named**: `xmp_values` (every
    paired property present, with an array's `items`), `info_iso` and
    `xmp_iso` on the date rows, `utf16le`, `xmp_namespaces` and
    `xmp_history` (`docs/ABI.md` §4.13 and §4.14). The ABI version stays 1
    (`OPEN-QUESTIONS.md` §2.12 item 6).
14. **The dataset at 953 rows**: every tag at its minimum but `gen_word_pdfa`
    and `gen_edge` (Word and Edge cannot run unattended on the laptop,
    `OPEN-QUESTIONS.md` §2.2); the three `meta_xmp_unreadable` files are
    `synth-0901` to `synth-0903`, numbered clear of phase 9's synthesised
    rows, which share the dataset folder from `synth-0263` on. No public set
    was scouted: every metadata tag but the unreadable packet is far above
    its minimum in the existing files.
15. **Budgets** are the first release package plus 25 % (DLL 5,121,024,
    exe 616,960, zip 2,445,579 bytes on 2026-10-26, before phase 9's code);
    Part B measures them again on the merged tree. `build/package.py
    --verify` rebuilds from clean and packages twice; the first such run
    differed in the import library alone, whose member headers the linker
    dates with the build's time, so the package writes those dates as 0 (the
    DLL and the EXE were already identical). At the end of Part A: DLL
    5,131,264, exe 622,080, zip 2,450,719 bytes, two packagings identical
    (SHA-256 `e76f5f08...0f226f`), `check_artefact.py` green.
16. **The ABI tier's cases** are the smallest opening manifest file of 28
    tags, one file per refusal code the manifest expects, a repaired file and
    the CLI's fixture (a redact job with a text and a metadata section); with
    the metadata tags filled, both callers make the same 552 calls over 31
    files, every expectation held.
17. **The redact verb's refusal of a section this build lacks** is kept as
    it is (it reads `is_implemented`, so it now refuses only an `images`
    section); phase 9 removes it with its images section.
18. **The metadata gate as run in Part A** (run `p10-gate1`, release build
    of the tree committed as `0602043`, 953 files): `python
    tests/dataset/run.py --runs 2 --workers 4 --run-id p10-gate1` (1,042 s,
    every expectation column passing, no crash, no hang, both runs
    identical); `python tests/dataset/gates/phase10.py --run p10-gate1
    --write-manifest`; `python tests/pdfium/metadata.py --run p10-gate1`;
    `python tests/pdfium/leak.py --run p10-gate1 --metadata`. Dataset 953
    rows (500 to 1,000), tags 151 of 153 at their minimum (`gen_word_pdfa`
    and `gen_edge`, `OPEN-QUESTIONS.md` §2.2); metadata read on 807 of 807
    files holding any (4 of them report an unreadable packet, as they must);
    Info fields equal to PDFium's 3,042 of 3,042, one file's two dates a
    named PDFium difference (`tests/pdfium/metadata_causes.tsv`:
    pdfium-0033, a linearised file whose main trailer names no /Info), six
    files PDFium does not open; rewrites carrying the written values 805 of
    805; originals found in an output or by PDFium 0 outside 11 files the
    manifest now names (`known_gap:pdfium_reads_*`, `OPEN-QUESTIONS.md`
    §2.12 item 7); the text redaction job's leak check 0 of 902 files (16
    removed-code sequences in other fonts' strings, informational, as in
    phase 8). `expect_redact_text`: 83 of the 85
    `known_gap:metadata_holds_word` rows are `clean`; dccc-0044 (the word
    "ROSÉ", which the self-check's diacritic folding finds in the title
    "2019 Rose" and the job's byte comparison of fields does not) and
    pdfpig-0009 (the word in a stream) stay, renamed
    `known_gap:word_in_info_xmp` and `known_gap:word_in_stream`. The earlier gates were not re-run in Part A
    (Part B re-runs every one on the merged tree). The leak check's byte scan
    no longer counts a number original inside a longer number (`1994` in a
    path's `466.1994`), which the self-check never reads either. The checks
    before the push: `python build/check.py` (15 of 15 groups, clang-tidy
    included), `ctest --preset unit` (584 cases), `ctest --preset sanitize`,
    `ctest --preset clang-sanitize`, `python tests/abi/run.py`, `python
    build/package.py --verify` and `python build/check_artefact.py`, and the
    Python suites of `dataset/tools/tests`, `tests/dataset` and
    `tests/pdfium`.
19. **Skills**: the `metadata` group added to `CLAUDE.md` §7.3 is carried by
    no skill under the user's skills folder; none needed the change.
20. **The 11 `known_gap:pdfium_reads_*` rows of item 18 are `clean`** after
    the phrase-search fix (branch `fix/phrase-search`, `OPEN-QUESTIONS.md`
    §2.12 item 7 and §2.14): the search and the self-check read every page
    in the drawing order and along its rows too (`docs/DESIGN.md` §11 and
    §18), and an Arabic presentation form reads as its letters. Part B's
    re-run of every gate measures with them clean.
21. **Part B started from the merged `main`** (`bc7e794`: Part A, phase 9's
    image treatments and the phrase-search fix) on a fresh
    `phase/10-release`, not a rebase. The seven synthesised image rows
    (`synth-0263` to `synth-0269`) took their metadata tags and columns from
    the final run (`phase10.py --write-manifest`), and 23 synthesised rows
    their `expect_fonts` (`n/a` to `loads`, `text_agreement.py
    --write-manifest`); no other expectation moved.
22. **Every gate re-run on the final dataset** (960 files, the release build
    of `bc7e794`, at most four workers). `tests/dataset/run.py --runs 2
    --run-id p10-final` (927 s), then `text_agreement.py --hidden`,
    `image_agreement.py` (census and `--listing`), `compare.py --check
    write,select,text,redact`, `images.py`, `leak.py --images`, `leak.py
    --metadata`, `metadata.py`, and `tests/dataset/gates/phase7.py`,
    `phase8.py`, `phase9.py` and `phase10.py` over it. `phase10.py` now
    measures phases 3 to 6 itself, from the run's outcomes and the text and
    image reports (phase 4's character census, retired by phase 5, is not
    run; the unreadable share and the fonts loaded stand for it), and gathers
    phases 7 to 9 from their scripts' tables. The first run met two timeouts
    under the machine's other load (nicar25-0006, 147 pages, 12.5 s alone;
    opf-0022, 179 pages, 5.2 s alone); a second two-run pass of the same
    build, `p10-final-robust` (563 s), met none, with no crash, no failure
    and both runs identical. The table (the earlier phases' first numbers in
    their build files' §9):

    | Phase | Measure | Final dataset | Threshold | Result |
    |---|---|---|---|---|
    | 3 | of the files PDFium opens, pdfkit opens | 898 / 899 (99.89 %) | 99 % | pass |
    | 3 | page count equal to PDFium's | 897 / 898 (99.89 %) | 99 % | pass |
    | 3 | mutated files diverging in silence | 0 | 0 | pass |
    | 3 | expected refusals with their code | 49 / 49 | 100 % | pass |
    | 4 | fonts loaded (text answered on every opened file) | 911 / 911 | 100 % | pass |
    | 4 | codes no rung reads | 0.54 % (88,787 of 16,565,200) | 1 % | pass |
    | 5 | median word F1 with PDFium | 1.0 | 0.97 | pass |
    | 5 | files below 0.90 | 56 / 898 (6.24 %; 21 pdfkit, 27 PDFium, 8 legitimate) | 5 % | **fail** (as at phase 5: 6.4 %) |
    | 5 | text determinism, re-serialisation, hidden markers | 0 differing, 12,122 of 12,122 walks, 0 missed | exact | pass |
    | 6 | occurrences decoded (resources; listing) | 6,132 / 6,138 (99.90 %); 36,505 / 36,516 (99.97 %) | 98 % | pass |
    | 6 | undecodable images without their warning | 0 | 0 | pass |
    | 6 | bitonal images equal to PDFium's bitmap | 495 / 498 (99.40 %) | 100 % | **fail** (as at phase 6: 491 / 492); all three PDFium's (`OPEN-QUESTIONS.md` §2.15) |
    | 6 | continuous-tone within MAD 2 | 3,687 / 3,723 (99.03 %) | 99 % | pass |
    | 7 | identity write re-opens; opens in PDFium | 911 / 911; 898 / 898 | 100 % | pass |
    | 7 | identity-write pages pixel-identical | 8,061 / 8,066 (99.94 %) | 99 % | pass |
    | 7 | text F1 = 1.0 output against input | 898 / 898 | 99.5 % | pass |
    | 7 | planted markers left; drawn markers lost | 0; 0 | 0 | pass |
    | 7 | selections right; kept pages identical; leaks | 283 / 283; 3,736 / 3,737 (99.97 %); 0 | 100 %; 99 %; 0 | pass |
    | 8 | self-check passes (known gaps apart) | 909 / 909 | 100 % | pass |
    | 8 | redacted words in PDFium's text; in decoded bytes | 0; 0 (19 removed-code sequences in other fonts' strings, informational) | 0 | pass |
    | 8 | pages stable outside the rectangles | 8,024 / 8,061 (99.54 %) | 99 % | pass |
    | 8 | placeholders read back | 9,289 / 9,341 (99.44 %) | 99 % | pass |
    | 9 | treated rectangles carrying an original sample | 0 of 2,633 | 0 | pass |
    | 9 | treated or removed identities drawn or found in the output | 0; 0 of 3,681 | 0 | pass |
    | 9 | pages stable outside the treated draws | 5,513 / 5,513 | 99 % | pass |
    | 9 | outputs re-open; PDFium text unchanged | 359 / 359; 356 / 356 | 100 % | pass |
    | 10 | dataset size; tags at their minimum | 960; 153 / 155 | 500 to 1,000; every tag | **fail** (`gen_word_pdfa`, `gen_edge`, `OPEN-QUESTIONS.md` §2.2) |
    | 10 | metadata read | 814 / 814 | 100 % | pass |
    | 10 | Info fields equal to PDFium's | 3,056 / 3,056 (pdfium-0033 named) | 100 % | pass |
    | 10 | rewrites carrying the written values | 812 / 812 | 100 % | pass |
    | 10 | originals found in an output or by PDFium | 0 of 812 files | 0 | pass |
    | all | two runs identical; crashes; hangs | 960 / 960; 0; 0 (`p10-final-robust`) | 100 %; 0; 0 | pass |
    | all | ABI tier (.NET, Python); packaging | 31 files, 552 calls each; two packagings identical, imports clean | green | pass |

23. **The release package of the whole catalogue**: the image treatments
    took the executable to 926,208 bytes, past Part A's budget of 771,200,
    so the budgets are set again from this first package of every function
    plus 25 % (DLL 5,793,792, exe 926,208, zip 2,815,699 bytes); `package.py
    --verify` now fails when a budget does (it compared two failures as
    identical), and two packagings around a clean rebuild are byte-identical
    (SHA-256 `e29f573c...2419df`), the imports clean. The ABI tier passes from
    .NET and Python against the final DLL (31 files, 552 calls each), and the
    ASan build's dataset verb ran over every file: the `clang-sanitize`
    build (llvm-mingw, ASan and UBSan, debug) through `run.py --runs 1
    --run-id p10-asan` (2,263 s) finished 803 of the 960 files with no
    sanitizer report and no crash; the other 157 passed the page-scaled limit
    in that debug build (phase 8's pass, with no limit and one job fewer, saw
    25) and were not read to the end there.
24. **No tag** (`OPEN-QUESTIONS.md` §2.15): phases 5 and 6 merged
    `gate-failed` and their gates still read failed on the final dataset,
    and two generator tags wait on the owner, so this phase closes
    `Build-Phase: 10 gate-failed` with the release question, and its plan,
    like those of phases 5 and 6, stays at the root.
25. **The documentation sweep**, written from what landed: `CLAUDE.md` §1
    (the build plans' row, the archive's one inbound link), §2 (every
    function's failures as the code refuses them, the plans' paths, 1.1 for
    every document verb, the reserved status), §3 (platform and images rows,
    the layout), §4 (the region treatment), §7 (the columns, 960 rows, the
    sets admitted and refused, Word and Edge), §8.1 (both sanitizer tiers, the
    page-scaled timeout, the PDFium tier's commands, the check groups), §10
    (the `clang-sanitize` preset, the census target, the budgets and
    `--verify`, the runners), §11 and §12 (`gate-failed` as a closing status,
    where a phase's plan lives), and the ABI version rule of §3.3
    (`OPEN-QUESTIONS.md` §2.12 item 6); `docs/ABI.md` (every function
    implemented, the real version object, the plans' column, 24 channel rows,
    the version rule, `LibraryImport`); `docs/DESIGN.md` §2 and §19 (the
    `/Perms` refusal and the handler's other refusals as the code has them),
    §5.3, §9.4, §9.5 (the PNG forms and `decode_jpeg`), §12, §15, §17;
    `dataset/README.md` (the columns' order, the unused `expect_redact`, the
    synthesised count); `README.md` (the catalogue as `CLAUDE.md` §2 numbers
    it, the CLI and a job file, calling from .NET and Python, the pinned
    toolchain and the build, packaging and tests). The plans of phases 1 to
    4 and 7 to 9 moved to `docs/archive/build/` with `git mv`, every
    reference to them pointed there and `build/paths_pending.tsv` emptied.
    `docs/SOURCES.md` and `THIRD-PARTY-NOTICES.md` needed nothing: the
    inventory's eight components are the notices' sections 1 to 7 and 9.

### Deviations of earlier phases that affect this one

From `build-5.md` §9:

- The manifest's `expect_text` (`agree`, `known_gap:<pdfkit|pdfium|legitimate>`, `n/a`) and `expect_fonts` (`loads`, `unreadable:<count>`, `n/a`) are filled by `python tests/pdfium/text_agreement.py --run <id> --write-manifest` from a dataset run; a row added later starts `n/a` until that runs, and a file below 0.90 needs its cause line in `tests/pdfium/text_causes.tsv` (item 13).
- The reading gate's question is `OPEN-QUESTIONS.md` §2.10 (item 16).

From `docs/archive/build/build-1.md` §9:

- `pdfkit_version` returns `version`, `abi`, `build` and a `catalogue` array (item 6).
- Release links strip symbols, drop unused sections and omit the PE time stamp; at the first package the DLL was 634 KB, the exe 333 KB and the zip 432 KB against budgets of 8 MB, 1 MB and 6 MB (item 9).
- `CMakePresets.json` uses schema version 11, the newest CMake 4.3.2 accepts (item 10).
- The sanitizer tier is two runs: `ctest --preset sanitize` (GCC, UBSan in trap mode) and `ctest --preset clang-sanitize` (llvm-mingw, ASan and UBSan) (`docs/archive/build/build-1.md` §9 item 8).

From `docs/archive/build/build-2.md` §9:

- `dataset/tools/generate_office.ps1` could not drive Word or Edge unattended on the laptop, so `gen_word_pdfa` and `gen_edge` wait on `OPEN-QUESTIONS.md` §2.2 (item 6).
- The dataset tools have no `cryptography` and no Pillow, because the proxy cuts compiled wheels; a synthesised JPEG or JPEG 2000 needs another route (item 4).
- The manifest holds 910 rows, which leaves room for 90 more under the ceiling of 1,000 (item 11).

From `docs/archive/build/build-7.md` §9:

- The write surface is `writer::ObjectGraph` (`src/writer/object_graph.hpp`): `get(number)` reads an object with every earlier stage's edits applied, `set(number, object)` replaces one, `add(object)` adds one under a fresh number; the source `syntax::Document` is never changed. Read through the graph, never through `Document::object`, because page selection runs first and replaces page dictionaries, the catalog and every pruned form (a form with resources of its own gets a pruned copy under its own number) (items 2, 5).
- A created stream is `syntax::Stream(dictionary, encoded bytes)`; `writer::StreamCopier::compress(dictionary, decoded)` makes a Flate one, `StreamCopier::plain` an uncompressed one; the file writer copies untouched streams byte for byte (decrypted, `/Crypt` dropped) (`docs/archive/build/build-3.md` §9 item 17).
- `redact::WritePipeline` (`src/redact/write_pipeline.cpp`) has the named stages page selection, text surgery, image treatments, metadata, channel stripping, serialisation, self-check; fill your stage, never reorder. The write report (`writer::WriteReport`) already carries `rectangles` (empty) and `self_check` (`not_run` with its reason) (items 2, 10).
- `redact::EditPlan` holds the page list only; add your member beside it (its header names where).
- Every write prunes resources to the names the content uses (`writer::ResourceUsage` reads `Tf`, `Do`, `gs`, `cs`/`CS`, `scn`/`SCN`, `sh`, `BDC`/`DP` and inline-image colour spaces), so an object your stage stops drawing leaves the output without further work, and a resource your stage starts drawing must be named in the resources it edits (item 5).
- The channel table strips after your stage: `/Metadata` off every object but the catalog, the catalog's XMP loses its whole `xmpMM` namespace, `/PieceInfo`, `/LastModified`, `/AA`, `/JS`, `/AF`, `/EF`, `/StructParent(s)` go from every dictionary, and the catalog and pages keep only their allow-lists (items 7, 11, 12).
- Test helpers: `tests/unit/writer/output_scan.hpp` (`identity_write`, `select_write`, `write_report`, `everything` = raw bytes plus every stream decoded, `contains_marker`, `reopen` without repair) and `tests/unit/support/pdf_builder_channels.hpp` (planted markers per channel, the four-page leak fixture); `tests/pdfium/render.py` compares pages exactly or outside a rectangle set with a one-pixel dilation (`compare_bitmaps`).
- The dataset outcome gains `channels`, `write` and `select`; outputs go to `<run>/outputs/`, named relative to the run folder (`tests/dataset/outcome.md`).
- The metadata stage runs before the channel strip, and the strip removes the whole `xmpMM` namespace of the catalog's packet afterwards: regenerate `xmpMM:DocumentID` and `xmpMM:InstanceID` after the strip (for instance as a step of the serialisation stage, once the output's hash is known), or narrow the strip to `xmpMM:History` in the same change. The Info dictionary is copied as it is until this stage rewrites it.

From `docs/archive/build/build-9.md` §9:

- `redact::EditPlan` holds `images()` (`redact::ImageTreatmentPlan`) beside `text()`; the image-treatment stage (`redact::ImageTreatmentApplier`) runs after the text surgery and before your metadata stage, and edits image objects, their masks and the content streams that draw removed or inline images through the object graph (items 2, 8, 9).
- The write report gained an `images` key between `rectangles` and `self_check` (`docs/ABI.md` §4.7); `pdfkit_redact_images` is `docs/ABI.md` §4.15, after your §4.13 and §4.14 (items 7, 17).
- `cli/verbs/redact.cpp`'s section refusal names the missing function; with rows 6 and 7 implemented it no longer fires (item 14).
- The manifest's last expectation column is `expect_redact_images`, before `notes` (960 rows, synth-0263 to synth-0269 this phase's, their `expect_set_metadata` left `n/a` for your fill); the dataset outcome's last member is `redact_images`, after `set_metadata`; `tests/dataset/run.py`, `dataset/tools/common.py` and their tests know it. Phase 2's `expect_redact` is still unused (item 12).
- The image-treatment specimens are rows synth-0263 to synth-0269, and `dataset/tools/synth/run.py` lets a specimen carry its own `licence` (item 13).
- `tests/pdfium/leak.py` has `--images`; `compare.py --check redact` writes `pdfium-redact.json` (text) and `pdfium-redact-images.json` (images) (item 16).
- The clang-sanitize preset compiles the vendored OpenJPEG with `-fno-sanitize=function` (item 15).

From `docs/archive/build/build-8.md` §9:

- The self-check already searches the Info dictionary's strings (`info`) and the XMP packet's text (`xmp`, entities read) for every redacted string, and refuses the output on a survivor; 85 manifest rows are `known_gap:metadata_holds_word` because the seeded job's words are also in their metadata. When `pdfkit_set_metadata` lands, either make the dataset job rewrite the fields that hold a redacted word and re-derive those rows to `clean`, or keep them as gaps (`OPEN-QUESTIONS.md` §2.11 item 5); `tests/dataset/outcome.md` describes the job (items 8, 10).
- Search folding is `platform::unicode::Folding` (`src/platform/unicode/folding.hpp`, Unicode 18.0.0 tables); `redact::MatchFinder::ranges_in` finds a search string in any UTF-8 value and `MatchFinder::byte_forms` gives its byte forms, for a metadata surface of your own (items 5, 11).
- An indirect string referenced from the Info dictionary is reported as a `string` surface, not `info` (pdfminer-0038); the metadata surface of the self-check is yours to refine.
- `tests/pdfium/leak.py`'s `--metadata` flag is yours to add; it reads `redact_text.strings` today.
- The channel table has 24 rows (`ch_xobject_private` added); `docs/ABI.md` §4.5 lists it (item 9).
