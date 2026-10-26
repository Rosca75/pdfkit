# build-3 — The object layer: lexer, objects, cross-reference, reconstruction, encryption, filters, limits, page tree and content lexer

| Field | Value |
|---|---|
| Phase | 3 |
| Branch | `phase/3-syntax` |
| Depends on | 1 |
| Parallel with | 2 |
| Model | `opus` |
| Owns | `src/syntax/`, `src/platform/crypto/`, `src/api/open.cpp`, `src/api/inspect.cpp`, `cli/verbs/inspect.cpp`, `cli/verbs/dataset.cpp`, `tests/unit/syntax/`, `tests/unit/platform/crypto/`, `tests/unit/support/`, `third_party/zlib/`, `third_party/inventory.tsv` (its zlib row), `THIRD-PARTY-NOTICES.md` (its zlib section), `docs/ABI.md` (the `pdfkit_open` and `pdfkit_inspect` sections), `src/api/catalogue.cpp` (the status of rows 1.1 and 1.2) |
| Size | about 4,500 lines of C++, tests included |
| Price | estimate: 10 to 14 hours, $160 to $240 of model spend at list prices, calibrated from change-12's batch A (about 3,200 lines of C#, 8 to 10 h, $120 to $180) scaled by 1.3 for C++ and by the ciphers this repository writes itself |

**Phase graph.** 1 → {2, 3}; 3 → {4, 6, 7}; 4 → 5; 5 → 6's listing step;
{5, 7} → 8; {6, 7, 8} → 9; every phase → 10. This phase runs in parallel with
phase 2. Its gate is measured over phase 2's dataset: if phase 2 has not merged
when the code is done, the session measures over whatever manifest phase 2 has
pushed on its branch (read through the shared root) and names the row count, or
pushes `incomplete` at the gate step and the team lead re-runs the gate after
phase 2 merges.

---

## 0. What this phase ships

- **Catalogue row 1.1, Open** (`pdfkit_open`): bytes to a document handle,
  reading a damaged file by reconstruction, decrypting the standard security
  handler with an empty user password, refusing with the codes and messages of
  `docs/DESIGN.md` chapter 19 (`not_pdf`, `no_page`, `password_required`,
  `unsupported_security_handler`, `limit_exceeded`, `page_ceiling`).
- **Catalogue row 1.2, Inspect** (`pdfkit_inspect`): version, page count, per
  page the boxes and rotation, encryption, repairs and warnings.
- **CLI verbs** `inspect` and `dataset` (the latter running Open and Inspect per
  manifest row and writing the outcome files whose contract phase 2 fixed in
  `tests/dataset/outcome.md`; later phases add their function's key to the
  same verb).
- **The object layer** under `src/syntax/` that every later layer reads: the
  object model, resolution, the page tree with inherited attributes, resources,
  the content lexer with inline images, and the filters under the budget.
- **The ciphers and hashes** under `src/platform/crypto/`: SHA-256, SHA-384,
  SHA-512, MD5, AES-128 and AES-256 in CBC (decryption, plus encryption for the
  test builder and the writer's later needs), RC4.
- **zlib vendored** under `third_party/zlib/` with its licence, inventory row
  and notice.
- **The test PDF builder** under `tests/unit/support/`, which every later phase's
  unit tests use.

The Info dictionary is resolved through the trailer and nothing more; reading
its fields and the XMP packet is phase 10's.

## 1. What it reads from earlier phases

- Phase 1: `src/platform/` (`Error`, `Budget`, `Buffer`, `ByteSpan`,
  `checked.hpp`, the JSON writer and reader), `src/api/guarded.hpp`,
  `src/api/handles.hpp`, `src/api/catalogue.cpp`, `include/pdfkit/pdfkit.h`,
  `cli/main.cpp`, `build/warnings.cmake` and its `pdfkit_third_party_target`,
  the presets, `build/toolchain.md` (which sanitizers run).
- Phase 2, when present: `dataset/manifest.tsv`, `tests/dataset/run.py`,
  `tests/dataset/outcome.md` (the outcome contract the `dataset` verb writes),
  `tests/pdfium/` for the page-count comparison.
- Read `build-1.md` §9 and, if it has landed, `build-2.md` §9 before planning.

## 2. Components

The specification of every rule below is `docs/DESIGN.md` chapters 1 to 4 and
19. Sizes are translated from change-12a §9.1 (C#) to C++ and include tests
only where stated.

### 2.1 `src/syntax/` (namespace `pdfkit::syntax`)

| File | What it holds | Lines | Reference and status |
|---|---|---|---|
| `object.hpp`, `object.cpp` | the object model as a closed variant: `Null`, `Boolean`, `Number` (integer or real with the integer flag), `String` (bytes with the literal-or-hex flag, so the writer re-emits the form it read), `Name`, `Array`, `Dictionary` (insertion-ordered, because the writer reproduces it), `Stream` (dictionary, raw byte range in the file, decoded bytes on demand), `Reference`; the model holds the file's vocabulary and no semantics | 350 | ISO 32000-1 §7.3 (read in the design record's sources) |
| `lexer.hpp`, `lexer.cpp` | the byte walk of `docs/DESIGN.md` chapter 1: white space (the six characters), comments, delimiters, numbers (`+`, `-`, leading or trailing dot, `--5` read as `-5`), literal strings with every escape (octal, line continuation, both line endings as one newline, nested parentheses by count), hex strings (odd final digit padded, white space skipped), names with `#xx` escapes (a lone `#` kept), keywords, `<<`, `>>`, `[`, `]`, `{`, `}`; tokens are slices with byte positions | 450 | §7.2, §7.3.4.2 (read); the `--5` behaviour copied from viewers (pdf.js, recalled) |
| `parser.hpp`, `parser.cpp` | direct objects, `n g R` by two-token lookahead, dictionaries, arrays, indirect objects at an offset; `/Length` trusted only when `endstream` follows after optional white space, else the next `endstream` by scan less the line ending, the object marked repaired; a missing `endobj` ends at the next `obj` or the end; never follows a reference | 400 | pdf.js `parser.js` and QPDF's recovery for the length rule (read in the design record) |
| `cross_reference.hpp`, `.cpp` | the table reader (entries read as white-space-separated fields, tolerating 19-, 20- and 21-byte entries), the stream reader (`/W`, `/Index`, `/Size`, types 0, 1, 2), hybrid `/XRefStm`, the `/Prev` chain with a visited set, first entry wins across the chain; `startxref` searched in the last 2,048 bytes and checked to land on `xref` or on `n g obj` of an xref stream | 450 | §7.5.4, §7.5.8 (read) |
| `object_streams.hpp`, `.cpp` | §7.5.7: the `/N` pairs, objects parsed at their offsets, cached per stream, decoded through the filters under the budget | 180 | §7.5.7 (read) |
| `repair.hpp`, `.cpp` | reconstruction run on every trigger of `docs/DESIGN.md` chapter 1 (each trigger a named reason the document records): one forward scan for `n g obj` at a token boundary, `endobj`, `trailer`, `startxref`, `%%EOF`, the last occurrence winning; object streams opened and indexed, an object inside a stream yielding to a later one outside only when that one parses; the trailer rebuilt from every `trailer` found (later over earlier), `/Root` from the `/Type /Catalog` object, `/Info` from the object with `/Producer` or `/Title`, `/Encrypt` from the object with `/Filter /Standard`; refusal `no_page` when the scan finds no `/Type /Page` | 450 | pdf.js `xref.js` `indexObjects`, PDFium `cpdf_parser.cpp` rebuild, MuPDF `pdf-repair.c` for the trailer rules (each read in the design record; MuPDF technique only, AGPL, no code taken) |
| `security_handler.hpp`, `.cpp` | the standard security handler for reading, `docs/DESIGN.md` chapter 2: revisions 2 to 4 (RC4 40-bit and 128-bit, AESV2; algorithms 2, 4, 5, 6 with the padding string), revisions 5 and 6 (AESV3; algorithm 2.A and the revision-6 hash loop of algorithm 2.B, with the empty password only), crypt filters `/StdCF`, `/Identity`, `/EncryptMetadata false`, per-object keys with the object-number salt, the xref stream left unencrypted; refusal `password_required` for a non-empty user password and `unsupported_security_handler` for a `/Filter` other than `/Standard` | 500 | ISO 32000-1 §7.6.3, ISO 32000-2 §7.6.4 (read in the design record's sources); pdf.js `crypto.js` R2 to R6 as the reference behaviour (read) |
| `filters.hpp`, `.cpp` | `FlateDecode` through zlib into the budget (a damaged stream yields what inflated before the damage, the object recorded `damaged_flate`), `LZWDecode` with `/EarlyChange`, `ASCII85Decode` (`z`, white space, `~>`), `ASCIIHexDecode`, `RunLengthDecode`, the PNG and TIFF predictors (`/Predictor`, `/Colors`, `/BitsPerComponent`, `/Columns`), `/Crypt` as identity; a chain applied in order; a stream whose next filter is an image codec (`DCTDecode`, `CCITTFaxDecode`, `JBIG2Decode`, `JPXDecode`) stops and hands back the encoded bytes with the remaining filter names | 450 | §7.4 (read); pdfminer `lzw.py` as a size reference (read) |
| `limits.hpp`, `.cpp` | the open options (per-stream and per-document inflate limits, object-count limit, page ceiling default 10,000) bound into a `platform::Budget` per document; the refusals `limit_exceeded` and `page_ceiling` with the object, the limit and the way out ("re-save the file in its application, split it, or raise the limit in the open options") | 120 | `docs/DESIGN.md` chapter 3 |
| `document.hpp`, `.cpp` | `Document::open(ByteSpan, OpenOptions)`: header within the first 1,024 bytes (offset recorded and every later offset relative to it), the xref chain or reconstruction, the trailer, the security handler; `resolve(const Object&)` cached, a missing object as `Null`, a cycle refused; `catalog()`, `info()`, `pages()`, `repairs()` (reasons), `damaged()` (objects), `warnings()`; reads nothing outside the bytes, follows no action, opens no embedded file and no external stream | 400 | §7.5.2 header and the 1,024-byte window copied from PDFium, QPDF and pdf.js (read) |
| `page_tree.hpp`, `.cpp` | the page tree walked depth first, `/Kids` cycles refused, `/Resources`, `/MediaBox`, `/CropBox`, `/Rotate` inherited and recorded on each `Page`, a kid that is not a page skipped with a warning; `Page` exposes its boxes (normalised rectangles), rotation (normalised to 0, 90, 180, 270), contents and resources | 250 | §7.7.3 (read) |
| `resources.hpp`, `.cpp` | resource lookup by category and name, a form's resources falling back to the page's when absent, an unresolved name a `resource_missing` warning and never a refusal | 120 | the fallback is what every viewer does (design record chapter 4.1, read) |
| `content_lexer.hpp`, `.cpp` | the content-stream tokeniser: operands (object syntax without references) and operators with their byte ranges, over a page's `/Contents` concatenated with one newline between pieces, and over a form's stream; inline images `BI … ID <data> EI` with the data ending at the length computed from the dictionary when width, height, bits and components give it, else at the first `EI` preceded by white space and followed by white space or the end; each operator's operand list and byte range reported, so the interpreter of phase 5 can point back at the bytes | 350 | §8.9.7 (read); the `EI` pitfall recorded in PdfPig PR 1476 (read in the design record) |

`src/syntax/syntax_placeholder.cpp` from phase 1 is deleted.

### 2.2 `src/platform/crypto/` (namespace `pdfkit::platform::crypto`)

| File | What it holds | Lines | Reference and status |
|---|---|---|---|
| `md5.hpp`, `.cpp` | MD5 | 150 | RFC 1321 (recalled; test vectors from its appendix A.5, verify against the RFC text) |
| `sha2.hpp`, `.cpp` | SHA-256, SHA-384, SHA-512 | 250 | FIPS 180-4 (recalled; test vectors from NIST's published examples, verify) |
| `aes.hpp`, `.cpp` | AES-128 and AES-256 block cipher, CBC decryption and encryption, PKCS#7 padding handling as the PDF handler needs it (a wrong padding byte read leniently, recorded), constant tables in code | 350 | FIPS 197 (recalled; test vectors from its appendix C, verify) |
| `rc4.hpp`, `.cpp` | RC4, about twenty lines, with its reason (no standard library carries it) | 40 | the RC4 algorithm (recalled; test vectors from RFC 6229, verify) |

### 2.3 `third_party/zlib/`

The zlib source at a pinned release (1.3.1 or the newest release on
`https://zlib.net` on the day, recalled; read the release page), only the files
the library needs (inflate, deflate, adler32, crc32, trees, zutil and their
headers), its `LICENSE`, a `CMakeLists.txt` building a static target through
`pdfkit_third_party_target`, a row in `third_party/inventory.tsv` with the
archive's SHA-256 from the release page and the tree hash, and the zlib section
of `THIRD-PARTY-NOTICES.md`.

### 2.4 `tests/unit/support/` (namespace `pdfkit::test`)

| File | What it holds | Lines |
|---|---|---|
| `pdf_builder.hpp`, `.cpp` | the fixture writer: objects, streams (Flate or raw, with a `/Length` that may lie by a given amount), a classic table (with 19-, 20- or 21-byte entries) or an xref stream (any `/W`), object streams, `/Prev` chains, incremental updates that change an object, garbage before the header, a missing `%%EOF`, a wrong `startxref`, a missing `endobj` or `endstream`, two trailers, linearisation dictionaries with wrong hints, and encryption under each revision from a clear fixture; every damage is a named knob, so a test reads as the file it makes | 700 |
| `pdf_encryptor.hpp`, `.cpp` | the builder's encryptor, written in the test tree from the same algorithms, the one place a second copy is allowed, because a reader tested only against itself proves nothing | 300 |
| `content_builder.hpp`, `.cpp` | a small helper writing content streams from operator calls (`BT`, `Tf`, `Td`, `Tj`, `TJ`, `cm`, `q`, `Q`, `Do`, `BI`), which phases 4 to 9 extend | 150 |

### 2.5 The API and the CLI

| File | What it holds | Lines |
|---|---|---|
| `src/api/open.cpp` | options JSON to `OpenOptions`, `Document::open`, the handle, the open report (`repairs`, `decrypted` with the revision, `warnings`) | 120 |
| `src/api/inspect.cpp` | the inspect JSON: `version`, `pages`, per page `media_box`, `crop_box`, `rotate`, `encryption`, `repairs`, `warnings`, keys in fixed order | 120 |
| `cli/verbs/inspect.cpp` | reads the file, calls open and inspect, prints the JSON, exit codes of `docs/ABI.md` | 60 |
| `cli/verbs/dataset.cpp` | `--manifest <tsv> --out <dir>` or `--file <path> --id <id>`: per row, open and inspect, writing the outcome file of `tests/dataset/outcome.md` with the `open` key (status, refusal code, pages, repairs); later phases add their keys; files are read from the shared dataset root | 200 |
| `docs/ABI.md` | the `pdfkit_open` and `pdfkit_inspect` sections written from what landed: the options and report shapes, the refusal codes | 80 |
| `src/api/catalogue.cpp` | rows 1.1 and 1.2 marked implemented | 2 |

## 3. Tests

### 3.1 Unit (`tests/unit/syntax/`, `tests/unit/platform/crypto/`, label `unit`)

| File | Holds |
|---|---|
| `lexer_test.cpp` | every token class with its edge cases: each string escape, nested parentheses, `#` in names, odd hex, every number form, keywords, positions |
| `parser_test.cpp` | direct and indirect objects, references by lookahead, streams with a right, short and long `/Length`, a missing `endobj` |
| `cross_reference_test.cpp` | the table with 19-, 20- and 21-byte entries, the stream with each `/W` shape, the hybrid file, the `/Prev` chain, a chain loop, first entry wins |
| `object_streams_test.cpp` | objects inside a stream, an object both inside and outside one, the cache |
| `repair_test.cpp` | every trigger over a builder-mutated fixture, and the invariant: the repaired graph equals the clean one, or the refusal names the damage; the `no_page` refusal |
| `security_handler_test.cpp` | revisions 2, 3, 4 (RC4 40, RC4 128, AESV2), 5 and 6 (AESV3) from one clear fixture round-tripped by the test encryptor; `/EncryptMetadata false`; `/Identity`; the `password_required` and `unsupported_security_handler` refusals with their three message parts |
| `filters_test.cpp` | each filter over known bytes, the predictors over a PNG-predicted and a TIFF-predicted buffer, a chain, a damaged Flate stream yielding its prefix and the `damaged_flate` warning, a budget of 4,096 bytes refused with the object, the limit and the way out, an image-codec stream handed back encoded |
| `document_test.cpp` | the header window (junk under and over 1,024 bytes), the page tree with inheritance and a cycle, resource fallback, the page ceiling refusal, and a fixture holding an external stream (`/F`), a `/URI` action, a launch action, `/JS` and `/EmbeddedFiles` whose embedded bytes never appear in any decoded output, proving none is read |
| `content_lexer_test.cpp` | operators and operands with byte ranges over hand-written streams, the `/Contents` array concatenation, inline images with `EI` inside the data, with and without a computable length |
| `crypto/*_test.cpp` | MD5, SHA-2, AES, RC4 against the published vectors |
| `api/open_inspect_test.cpp` | the ABI functions over builder fixtures: report and inspect JSON shapes, each refusal code through the JSON error object |

Fixtures are written in code by `tests/unit/support/`; no PDF is committed.

### 3.2 Dataset

The `dataset` verb fills the `open` key of every outcome; `tests/dataset/run.py`
checks the `expect_open` column of every manifest row and the page count against
the `pages` column (PDFium's). This phase corrects an `expect_open` value only
where the file proves the expectation wrong (a mutation the reader legitimately
repairs where the mutator expected a refusal), each correction named in the
last commit message.

### 3.3 PDFium

The page-count comparison runs through the manifest's `pages` column, which
phase 2 filled from PDFium; no new PDFium call is needed.

## 4. Gate

Measured over the whole manifest present at the gate (the row count is stated
with every number):

| Measure | Required | Command |
|---|---|---|
| open agreement | of the files PDFium opens, pdfkit opens at least 99% | `python tests/dataset/run.py` (the `open` section of `summary.md`) |
| page counts | on at least 99% of the files both open, the page count equals PDFium's | the same |
| mutated files | every `mutate:` row either opens with its source's page count or is refused with a named code: 0 silent divergences | the same |
| expected refusals | every `refused:<code>` row refused with that code | the same |
| robustness | 0 crashes, 0 hangs past 30 s, over the whole manifest including the optional unsafe sets when present | the same |
| sanitizer | 0 sanitizer reports over the whole manifest in the `mingw-sanitize` build | `python tests/dataset/run.py --preset mingw-sanitize` |
| determinism | two runs byte-identical on every outcome | `tests/dataset/run.py` (it runs twice) |
| floor | `python build/check.py` and `ctest --preset unit` green | |

The report lands in `dataset/reports/<run-id>/`; the numbers go into the last
commit message.

## 5. Kill criterion and the question to the owner

**Quantity:** the open agreement of §4 (files pdfkit opens over files PDFium
opens). **Threshold:** under 95%.

**Question, word for word:** "pdfkit opens N of the M files PDFium opens
(P%); the failures group as <causes with counts and example ids>. Which do you
want? (a) One more object-layer session on those causes before the fonts (about
6 to 10 hours and $90 to $150; phases 4, 6 and 7 wait for it); (b) accept the
agreement and continue, recording each failing file as `known_gap` in the
manifest (no cost now; every later gate is measured on fewer files and says
so); (c) examine the failures set by set first and decide per set (about 2 hours
and $30 to $45 for the analysis, then (a) or (b) per set)."

**The unattended action:** write the question with the evidence (the failing
ids grouped by cause, from `summary.md`) into `OPEN-QUESTIONS.md`; merge the
branch if the checks and the unit tier are green, with trailer
`Build-Phase: 3 gate-failed`; phases that depend on it (4, 6, 7) proceed and
report their gates with that caveat.

## 6. Rollback

Reverting the merge removes the object layer, the ciphers, zlib and the two
functions; `pdfkit_open` and `pdfkit_inspect` return to `not_implemented`, and
the `dataset` verb to its stub. Phases 4, 6 and 7 depend on it, so a rollback
after they merge reverts them too.

## 7. Execution order inside the phase

1. Run `python build/check.py` and `ctest --preset unit` on the base.
2. zlib vendored with its inventory row and notice; the check passes.
3. `pdf_builder` and `content_builder`, then the lexer and parser with their
   tests first.
4. The cross-reference reader and object streams with their tests.
5. **Split point**: push with `Build-Phase: 3 incomplete` here if the session
   is running out of room; a second session continues from step 6.
6. Reconstruction and its tests, measured against the clean graphs of step 4.
7. The ciphers with their vectors, the security handler and the test encryptor.
8. The filters and the limits with their tests.
9. `Document`, the page tree, resources and their tests; the content lexer and
   its tests.
10. `open.cpp`, `inspect.cpp`, the two verbs, `docs/ABI.md` sections, the
    catalogue status.
11. The dataset run of §4 (normal, then sanitize); read every failure and fix
    the reader where the fault is the reader's; record a manifest correction
    where the expectation was wrong.
12. `python build/check.py`, `ctest --preset unit`, `ctest --preset sanitize`;
    remove this phase's rows from `build/paths_pending.tsv`.

## 8. Opening prompt

> Read `CLAUDE.md` (chapters 3, 6, 8 and 9 above all), `docs/DESIGN.md`
> chapters 1, 2, 3, 4 and 19, `build-1.md` §9 and, if it has landed,
> `build-2.md` §9 for what the earlier phases left behind, and all of
> `build-3.md`. Work in a worktree on branch `phase/3-syntax` created from
> `main`. Phase 2 may be running at the same time and owns `dataset/` and the
> two Python harnesses; the dataset files live in the main checkout's ignored
> `dataset/files/`, resolved through `git rev-parse --git-common-dir`. Run
> `python build/check.py` and `ctest --preset unit` before you start and before
> you finish. Write the failing test before the code, component by component,
> in the order of §7. Every rule copied from a reader rather than the
> specification names the reader in its comment, and every external fact
> (a clause, a test vector, a release hash) carries its source and whether you
> read it or recalled it, in the comment and in `docs/SOURCES.md`. Measure the
> gate of §4 over the manifest present and state its row count. When the open
> agreement is under 95%, follow §5's unattended action and do not wait for the
> owner. Finish with a last commit whose message carries the gate numbers and
> the trailer `Build-Phase: 3 done` (or `incomplete` or `gate-failed` with the
> reason), push the branch, and never merge to `main`: the team lead merges.
> Record any deviation in §9 and in every later build file it affects
> (`build-4.md`, `build-6.md`, `build-7.md` read this phase's types).

## 9. Deviations recorded by this phase

1. **zlib 1.3.2**, the newest release on the day (2026-02-17; the archive's
   SHA-256 matches the release page), rather than 1.3.1. Vendored: the deflate
   and inflate sources, `gzguts.h` (which `zutil.c` includes) and the shipped
   `zconf.h`; `compress.c` and the gz file functions are left out, so the
   fixture writer deflates through `deflateInit`/`deflate`.
2. **Files added beyond §2**, each to keep one public class per header or one
   concern per file: `syntax/dictionary.{hpp,cpp}` (`Dictionary`),
   `syntax/stream.{hpp,cpp}` (`Stream`), `syntax/page.hpp` (`Page`),
   `src/api/document_report.{hpp,cpp}` (the encryption, repairs and warnings
   members open and inspect share), `cli/sha256.{hpp,cpp}` (see item 12),
   `tests/unit/support/require_value.hpp` (checked access to an optional,
   which clang-tidy cannot see through doctest's `REQUIRE`),
   `tests/unit/support/document_summary.{hpp,cpp}` (open a fixture; the
   summary a reconstruction test compares), `tests/unit/cli/sha256_test.cpp`.
   The crypto files were written by a helper session on `phase/3-crypto` and
   cherry-picked; the object model of `object.hpp` is `Object` with nested
   `Reference`, `Number`, `String` and `Array` (a `std::vector<Object>`), not
   a `PdfObject` family.
3. **The object model is immutable and shared**: arrays, dictionaries and
   streams live behind `std::shared_ptr<const ...>`, so an `Object` copies in
   constant time; a writer builds new objects through the `make_*` factories.
   A `Stream` holds its dictionary, the raw data's range in the document's
   bytes and its owner reference; it holds no data of its own, so a stream the
   writer creates needs an owned-data form added to `Stream` (phase 7).
   Lexer tokens carry their decoded text as a `std::string`, not a slice.
4. **Open parses every object once** (the eager parse): a recovery anywhere in
   the file (a wrong `/Length`, a missing `endobj` or `endstream`) is a repair
   reason at open, even for an object no page uses, because the dataset's
   `length_wrong`, `endobj_missing` and `endstream_missing` mutations damage
   content and font streams that open would otherwise never read. Repair
   reasons are deduplicated and capped at 100 lines, the last counting the
   rest.
5. **What triggers reconstruction** beyond DESIGN.md §1.6: an offset entry
   that does not land on its own `n g obj` (checked for every entry at open),
   a compressed entry whose object stream has no offset entry, a page tree
   that yields no page, and a linearization `/O` that names another first
   page while `/L` still equals the file's length (an incremental update makes
   `/L` stale, and then the hint is not checked). When reconstruction still
   finds no tree, the `/Type /Page` objects found are read in file order with
   a repair note, and only then is the file refused `no_page` or `not_pdf`.
6. **Leniencies recorded as repairs without reconstruction**: the off-by-one
   first subsection, a subsection shorter than its count, a `/Prev` loop, a
   page-tree cycle (skipped with a note, as DESIGN.md chapter 4 says, rather
   than refused as §2.1 said), a `/Kids` entry naming no dictionary, a
   `stream` keyword followed by a stray byte before its end of line, and a
   revision 5 or 6 `/Perms` that is missing or names other permissions than
   `/P` (DESIGN.md chapter 2 calls it a warning, but chapter 19 has no code
   for it, so it rides under `repaired`). A `/Perms` that does not decrypt to
   "adb" means the file key is wrong and is refused `password_required`, as
   PDFium refuses it (`OPEN-QUESTIONS.md` §2.4 asks the owner to confirm).
7. **Flate verifies a present Adler-32**: a mismatch marks the stream damaged
   (warning `damaged_stream`) with every byte kept, because a flipped byte
   inside a literal inflates without any other sign (the `damaged_flate`
   mutation does exactly that). A missing checksum is not a fault. The plan's
   "`damaged_flate`" warning is DESIGN.md §19's `damaged_stream`.
8. **Bounds chosen here**: containers nest at most 64 deep (PDFium's
   parser recursion limit; 256 overflowed the 1 MiB stack under ASan); object streams
   open at most 8 deep; a reference chain resolves at most 32 hops; the page
   walk visits at most 4 × the page ceiling + 100,000 nodes; a content stream
   keeps at most 65,536 operands before an operator; an inline image wider or
   taller than 1,048,576 has no computable length. Resolution matches by
   object number and ignores a generation mismatch, as viewers do.
9. **Open options are flat keys**: `stream_bytes`, `document_bytes`,
   `objects`, `page_ceiling`, `operators_per_page`, each a positive integer;
   an unknown key is `invalid_argument`. The page count is `page_count` in the
   open report and in inspect (inspect's `pages` is the per-page array); each
   warning is `{code, message, page}` and the `repaired` warning also carries
   `reasons`. `docs/ABI.md` §4.3 and §4.4 hold the shapes.
10. **The inline image** is one `ContentLexer::Operation` named `BI` whose
    first operand is its dictionary and whose `data_start`/`data_end` delimit
    its data; the PdfPig check reads up to three tokens after a candidate `EI`
    and accepts it when each is an operand or an operator of Annex A.
11. **Decoding is charged on every call**: `Document::decode` charges the
    budget each time, so a stream decoded twice counts twice against
    `document_bytes`; a later layer that decodes shared fonts or images per
    page caches the decoded bytes. An external stream (`/F`) decodes to
    nothing, silently.
12. **The dataset verb** follows `tests/dataset/outcome.md`
    (`--file/--id/--out`, `--manifest/--files/--out`); it does not create
    `<dir>/outcomes/` (the CLI has neither `std::filesystem`, for its size,
    nor an OS header, by the layout rule), so the caller does, as
    `tests/dataset/run.py` does. The outcome's `sha256` comes from the CLI's
    own SHA-256 (`cli/sha256.cpp`), because the CLI includes only the C header
    and the ABI has no hash; the unit tier checks it against the library's
    SHA-2. `text`, `images`, `metadata`, `channels` and `redact` are `null`
    until their phases add them; `open.warnings` lists the warning codes.
13. **`pdfkit_document`** holds `std::unique_ptr<syntax::Document>`;
    `api::new_document` takes it, and `require_document` also refuses a handle
    without a document. `tests/unit/api/abi_test.cpp`'s fixture opens a
    one-page built document through `pdfkit_open`.
14. **The security handler**: when `/StmF` and `/StrF` are both `/Identity`
    (version 4 or 5), nothing the document draws is encrypted and a user
    password guards only the embedded files `/EFF` names, so the password is
    not checked and the document opens, as PDFium opens it (qpdf's
    `encrypted-attachments` files in the dataset). A `/CF` given by reference,
    as a whole or per filter, is resolved before the handler reads it.
15. **Page counts follow the tree, not `/Count`**: PDFium returns the root
    `/Count` when it is positive, so on `pdfium-0048` (`/Count 3` over one
    kid) it says 3 where the walk finds 1, and on `pikepdf-0012` (the one page
    object lies in a corrupt object stream) it says 1 where pdfkit refuses
    `no_page`. Both stay as they are: a page that has no object cannot be read.
16. **Manifest corrections, proposed and not applied**: the team lead's rule
    keeps this branch out of `dataset/`, so the `expect_open` values the run
    of §4 proves wrong are listed here for phase 2's owner to apply. `opens`
    (read as written; the `xref_wrong_offsets` tag is a false positive, every
    offset lands): dccc-0006, dccc-0021, dccc-0134, nicar25-0007, pdfium-0047,
    pdfjs-0008, pdfjs-0039, pdfminer-0038. `repaired` (PDFium opens them in
    silence, but each needs a named recovery: a wrong `/Length`, a missing
    `endobj`, an off-by-one table, offsets or a `/Prev` that do not land, an
    unknown filter on a cross-reference stream, a malformed object stream):
    dccc-0019, dccc-0039, pdfdiff-0020, pdfdiff-0021, pdfdiff-0031,
    pdfdiff-0033, pdfdiff-0034, pdfdiff-0035, pdfium-0013, pdfium-0020,
    pdfium-0036, pdfium-0037, pdfjs-0029, pdfminer-0046, qpdf-0030,
    qpdf-0031, qpdf-0032, qpdf-0054, qpdf-0060, safedocs-0002. Refused, as
    PDFium refuses them: `refused:no_page` for pdfium-0008, pdfium-0010,
    pdfium-0011, pdfminer-0014, qpdf-0035 and pikepdf-0012 (PDFium's one page
    there comes from `/Count`, item 15); `refused:not_pdf` for qpdf-0029 and
    qpdf-0039; `refused:unsupported_security_handler` for qpdf-0002 (a
    `/CFM /AESV1`, which no revision defines).

17. **Added by phase 7** (the one change `build-7.md` §1 allows it in
    `src/syntax/`): `Stream` gains its created form, `Stream(dictionary,
    data)`, holding its encoded bytes behind a shared pointer (`owns_data()`,
    `owned_data()`, `with_dictionary()`); `Document::raw` hands a created
    stream's bytes back and `Document::decode` never decrypts them.

### Deviations of earlier phases that affect this one

From `build-1.md` §9:

- `pdfkit_open(bytes, size, options_json, out_doc, out)` accepts `bytes == NULL` with `size == 0`, so an empty input reaches the parser and is refused as `not_pdf` (item 4).
- Flipping rows 1.1 and 1.2 to `implemented` in `src/api/catalogue.cpp` also updates `pdfkit_version`'s `catalogue`, which the CLI reads before opening (item 6).
- `cli/verbs/inspect.cpp` is complete once the functions exist (open, inspect, print, close through `run_document_verb`); `cli/verbs/dataset.cpp` parses `--manifest/--out` and `--file/--id`, checks the catalogue and ends in a `not_implemented` error that this phase replaces with the outcome writer (item 7).
- The JSON tree type is `platform::JsonValue` in `src/platform/json_value.hpp` (item 2).
- `third_party/inventory.tsv` has the columns `path, name, version, upstream, archive, archive_sha256, tree_sha256, licence, test_only`; compute a tree hash with `python build/check_third_party.py --print-tree-hash third_party/<name>` (our `CMakeLists.txt` in the folder is excluded) and cache the upstream archive in the main checkout's `build/cache/third_party/` (`build-1.md` §9 item 11).
- Remove the `build/paths_pending.tsv` rows `syntax/cross_reference.hpp`, `tests/unit/support/`, `tests/unit/support/pdf_builder` and `tests/unit/support/pdf_builder.hpp` in the change that creates them (item 13).
- The sanitizer tier is two runs: `ctest --preset sanitize` (GCC, UBSan in trap mode) and `ctest --preset clang-sanitize` (llvm-mingw, ASan and UBSan) (`build-1.md` §9 item 8).

From `build-2.md` §9:

- The `dataset` verb's forms are `--file <pdf> --id <id> --out <dir>` and `--manifest <tsv> --files <root> --out <dir>`: the verb reads only the paths it is given, so the shared dataset root is passed in, and every outcome goes to `<dir>/outcomes/<id>.json` as `tests/dataset/outcome.md` fixes; the harness uses the per-file form (item 10).
- The dataset tools' shared-root override is `PDFKIT_DATASET_ROOT` (item 2).
