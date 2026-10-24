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
defect handled as `build-8.md` §5 states.

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
> (§9) of `build-1.md` to `build-9.md`, then implement `build-10.md` exactly as
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

1. (none yet)

### Deviations of earlier phases that affect this one

From `build-1.md` §9:

- `pdfkit_version` returns `version`, `abi`, `build` and a `catalogue` array (item 6).
- Release links strip symbols, drop unused sections and omit the PE time stamp; at the first package the DLL was 634 KB, the exe 333 KB and the zip 432 KB against budgets of 8 MB, 1 MB and 6 MB (item 9).
- `CMakePresets.json` uses schema version 11, the newest CMake 4.3.2 accepts (item 10).
- The sanitizer tier is two runs: `ctest --preset sanitize` (GCC, UBSan in trap mode) and `ctest --preset clang-sanitize` (llvm-mingw, ASan and UBSan) (`build-1.md` §9 item 8).
