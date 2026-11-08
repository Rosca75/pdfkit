# CHANGE-03: Damaged pages are reported, never refused, and the reference panel runs on Linux

| Field | Value |
|---|---|
| Status | in progress |
| Owner approval | 2026-11-07, `OPEN-QUESTIONS.md` §1.3 (damaged pages reported), §1.4 (word cuts judged by the panel), §1.5 (CJK stays a known gap) |
| Depends on | `docs/change-02.md` (ABI 3); `docs/change-01.md` (the reference panel order, run alongside) |
| Supersedes | nothing |
| Superseded by | nothing yet |
| Batches | W1 (damaged pages), P0 (the test tiers on Linux), the panel batches A, B and C of `docs/change-01.md` with W2 (word cuts), W3 (two gate counts) |
| `SessionVersion` | not applicable. The C ABI moves from 3 to 4 (`docs/ABI.md` §5): outputs only grow |
| Behaviour change | yes: every output a caller reads names the pages whose text could not be read in full |
| New dependency | none in the library; test-only readers on Linux (§0.4) |
| Minimum model | `opus` |

## 0. Context

pdfkit is the stand-alone C++20 library with a plain C ABI that
doc-anonymiser calls for every PDF. Read `CLAUDE.md` (it overrides
everything), then `docs/DESIGN.md` §3.1, §4, §6.1 and §19, `docs/ABI.md` §4
and §5, and `OPEN-QUESTIONS.md` §1.3 to §1.5.

### 0.1 What this order is

A report the owner ordered (§1.3): a page whose content stream is damaged
was read as far as it reads and named only by a `damaged_stream` warning of
`pdfkit_text`, which a caller can take for a page read whole. Every output a
caller reads now names each page whose text could not be read in full, with
the stream, the damage class and the bytes decoded, and the write report says
whether a text redaction ran over such a page and whether its damaged bytes
reach the output as they were. pdfkit decides nothing with it: no refusal, no
dropped page. The second half of the order (the reference panel's tiers on
Linux, the word cuts, the gate counts) is the panel's, below.

### 0.2 Ground rules

- `CLAUDE.md` holds: one interpreter, the layers, no global mutable state,
  determinism, every external fact sourced, comments in the present tense
  naming no phase or change order, warnings are errors.
- The ABI only grows: a new field at the end of each output, a raised
  `pdfkit_abi_version`.
- A test written first fails first.
- pdfkit reports facts and decides nothing (`CLAUDE.md` §5).

### 0.4 Where the work stands

This table is the programme's ledger. It is updated after every merge or
decision, and a session that resumes reads it before anything else.

| Item | Repository | Branch | Status | Commits | Numbers | Next step |
|---|---|---|---|---|---|---|
| Toolkit check | both | none | done 2026-11-07 | none | pdfkit unit 721/721 (Linux gcc Debug); `check.py --fast` 11/11 with the scratch venv; doc-anonymiser 7,612 unit (0 failed, 122 skipped) and 107 integration (95 passed, 12 skipped) against `pdfkit.so` 0.2.1 | see Toolkit below |
| W1 damaged pages | pdfkit | `w1-damaged-pages` | merged into `main` at fbb0b00 after three reviews (CHANGES REQUESTED twice, APPROVE the third, its small findings applied) | 94e4caa..3182029, merges a98df23, 4f96a1c, 831fe6b | ABI 4, 0.4.0-preview; unit 754/754 on Linux; run `w1-fix2` (961 files, two runs): `expect_damage` passes on 912 judged rows, 24 files damaged, every other column as on main; inspect, text and find agree on 912/912; ABI tier from .NET and Python passes | done; the consumer lands next |
| W1 consumer | doc-anonymiser | change order 19 | landed on doc-anonymiser's `main` at 85455e7 after review (APPROVE, six minor fixes applied) | 9333fe4..2b054d2 | unit 7,642 (7,520 passed, 122 skipped, 0 failed), integration 107 (95 passed, 12 skipped), page suite 2,046; end to end against pdfkit 5eb9fad: opf-0019 names 23 of 54 pages, Drop leaves none, Keep records 23, the copy names the pages a redaction passed over; decisions in its FEEDBACK-19 to confirm | the pin move after the owner's tag (its change-19 §14) |
| P0 Linux tiers | pdfkit | `p0-linux-tiers` | merged into `main` at 9a97e6f after review (APPROVE; follow-ups applied: preset host conditions, two delta words, a bounded read, the `test_channels` failure main already had) | 7af4f56, f15bb87, 821a7ac | dataset 961/968 present (7 `office` rows laptop-only), 0 hash mismatches; unit 721/721 linux-debug and linux-release; run `linux-base-09b8d78` (2 runs, 961 files, 752 s): 0 crashes, 0 non-deterministic, 1 hang (`dccc-0129`, 73 s alone under load; passes at `--timeout-scale 3`), 1 real miss (`mutate-0048` self-check refusal, as on the laptop); PDFium on Linux: F1 median 1.0, 55/898 below 0.90 (laptop 56), bitonal 495/498, Info 3,048/3,048; ABI tier .NET and Python pass (31 files, 717 calls); PDFium Linux SHA-256 `bdc60126…` computed, not read | done; departures in `docs/change-01.md` Appendix C, versions in `build/toolchain.md` §7 |
| Batch A (readers) | pdfkit | change/01-a-readers | merged into `main` at 7d62575 (`Change-Batch: 01a done`) after review (CHANGES REQUESTED, nine fixes applied) | 4293e8a, 2c9c88d, ef6a121, b3b0f00, 97917e3 | AC1: every member answers, refuses or holds a `repeated_failure` for every applicable file (961 text, pages and structure; 70 pixel files), warm runs start 0 processes; AC1a `fetch.py --verify` agrees; AC1b pixel facts in `tests/panel/members.tsv`; panel tests 50; unit 722/722; `check.py --fast` 10/10 | owner question for batch B's sweep: Poppler abstains on stencil masks (narrows D7) |
| Batch B (judge) | pdfkit | change/01-b-judge | merged into `main` at 9518fa0 (`Change-Batch: 01b done`) after review (CHANGES REQUESTED: the form-field rule let pdfkit drop words; fixed by members reading a QPDF copy without annotations) | 404c5b0, 1bb77be, da15079, c78e2d5, d806afc, 53ffb8b | reading gate 50/893 against pdfkit (5.60 %, fails 5 %; literal 93/893), median 1.0, 19 undecided; bitonal 489/489; R1 CR5 ordered (pdfjs-0047 and opf-0014), R2 owner question, R3 Letter; OPEN-QUESTIONS §3.1-§3.4 | batch C: CR5, the layout group, the §1.4 cuts |
| Batch C (+W2) | pdfkit | branch change/01-c-text-fixes (worktree `/home/user/wt/c`) | built; under review | b0b45ac, 3f8b442, c6ef55c, e33f9b8 | reading gate 44/890 against pdfkit (4.94 %, passes; literal 88/890), median 1.0; AC5a 0; old PDFium script 46/899 below 0.90 (55 before); unit 762/762; leaks: images 0, metadata 0, text 2 (opf-0019 "Large", mutate-0046 "Most", the damaged pages main has too); digit pairs of dccc-0124 left apart (OPEN-QUESTIONS §3.5) | merge after review |
| W3 gate counts | pdfkit | `w3-gate-counts` | merged into `main` at 523c10d after review (APPROVE, three minor fixes applied) | 82410b5, 752fddd, e6f813e, e4c588d | 8,075 − 7,681 = 394 pages, all `dccc-0129` (hung, no outcome); re-open 360/361 the same file; the numbers stand, the reports now name those pages with their cause; the selection rows keep a hung or failed selection in their population; unit tier 722/722, harness tests 94, `check.py --fast` 10/10 on Linux | done; `dccc-0129` (44.6 s alone of a 60 s limit) is a timing risk for the next laptop gate |

Decisions taken, with their reasons:

1. **A page is named when pdfkit cannot show that no text was lost**, and only
   then. A filter fault (bad Adler-32, corrupt or truncated Flate, an ASCIIHex,
   ASCII85, LZW, RunLength or predictor fault) names the page; bytes the content
   lexer skipped or an operator it dropped names the page; a wrong `/Length` or a
   missing `endstream` recovered to the next `endstream` stays the document-level
   `repaired` reason and names no page; a lexer recovery that skips no byte and
   drops no operator (trailing operands, an empty stray delimiter) names no page.
   Reason: the owner's rule (§1.3) is that a page repaired with no text lost is a
   normal page, and the product rule is that no output lets a caller take partial
   text for the whole page.
2. **One source of the facts**: the content interpreter's walk (page pieces and
   forms) records the damage; no second decoder decides it, because two readers
   of one stream disagree. A damaged form names every page that draws it.
3. **`pdfkit_inspect` reads every page's content to name damage**, and caches the
   result on the document, so later calls agree with it and inspect no longer
   depends on call order. A limit met during that scan names the page as not
   checked rather than failing, because inspect never fails on a valid handle.
   Engineering choice; the caller-visible part is in `OPEN-QUESTIONS.md` to
   confirm.
4. **The field is `damaged_pages`**, one shape in every output: per page its
   number, the glyphs read, and per damaged stream which stream, the class, the
   decoder's fault and the bytes decoded and left unread where known. The write
   report adds per page whether a text redaction searched or edited it and
   whether damaged bytes were copied as they were.
5. **ABI 4, version 0.4.0-preview.** Fields only grow, so no meaning changes.
6. **A stream pdfkit cannot decode at all, or a content stream or XObject that
   resolves to nothing, names the page**, because a missing form may have held
   text, and decision 1 names a page whenever pdfkit cannot show none was lost.
7. **A dropped operator names the page only when its operands could carry or
   decide text** (a string, an array, a name or a dictionary); an unknown keyword
   with no operands or only numbers, and an indirect reference written in content
   (`5 0 R`), name no page. Reason: the dataset showed these on pages read whole
   (qpdf-0060, safedocs-0005), and a false mention teaches a caller to ignore the
   report. The classification changes; the lexer's tokens and operator numbers do
   not, because the redaction surgery is addressed by them.

Toolkit (2026-11-07): gcc/g++ 13.3, clang 18.1.3; a scratch venv
(`/tmp/claude-0/-home-user/99d91c01-26b5-556f-b9dd-feff24862ffc/scratchpad/toolkit/venv/bin`)
holds the pinned cmake 4.3.2, ninja 1.13.2, clang-format and clang-tidy 23.1.3 and
fonttools 4.67.0; ccache 4.9.1 and mingw-w64 headers from apt; rustc/cargo 1.97.0;
dotnet SDK 10.0.114 from apt (doc-anonymiser's `global.json` pins 10.0.204, so its
build runs in a scratch clone with the pin relaxed, never by editing the file);
pdftotext 24.02.0, mutool 1.23.10, qpdf 11.9.0 from apt. gcc 13 at `-O2` fails
`-Werror` on a maybe-uninitialized in `syntax::Object`; a Release build uses clang.
GitHub release assets download; GitHub pages and API of unattached repositories do
not. Not runnable here: `check.py --full`, the mingw and arm64 artefacts, the
import-table check, the sanitizer presets of the laptop, the .NET abi tier against
the DLL, doc-anonymiser's fixture drift against the shipped DLL.

W3 record. Phase 7's identity-write pixel row reads 7,676 / 8,075 (95.06 %)
while `compare.py` compares 7,681 pages. The 394 pages between them are
`dccc-0129`'s, which hung in both runs and so has no outcome, no write and no
rendering. The denominator is the gate's own (every page of the files PDFium
opens, a page with no output not reproduced), so the number stands and the gap
counts against the share; the report now names each file's pages not compared
with the cause. Phase 9's re-open 360 / 361 is the same file. The selection rows
now judge a file with no outcome on the manifest's page count and count the
pages its outcome names (or the verb's seeded draw when it names none), so a
hang fails both selection rows rather than leaving them; the unit tier pins the
draw to the values the gate's Python copy is pinned to.

### 0.5 What the library did before this order

| Fact | Where (at `09b8d78`) |
|---|---|
| The Flate filter keeps every byte when the Adler-32 disagrees, and stops at corrupt or truncated data, each a fault string with no class | `src/syntax/filters.cpp:114`, `:123`, `:125` |
| A wrong `/Length` or a missing `endstream` is recovered to the next `endstream` and recorded as a repair of the document | `src/syntax/parser.cpp:312`, `:319` |
| A walk is marked damaged when its stream met a filter fault or its lexer counted any recovery, a lossless one included | `src/text/content_interpreter.cpp:205` |
| A form is decoded once per interpreter; its damage rides on its bytes, and its `damaged_stream` warning names only the first page that decoded it | `src/text/content_interpreter.cpp:728`, `:734`; `src/syntax/document.cpp:688` |
| The lexer counts every recovery in one number, a stray delimiter and an inline image without `EI` alike, and an unknown operator in none | `src/syntax/content_lexer.cpp:103` to `:314` |
| `pdfkit_text` names a damaged page only by a `damaged_stream` warning of that page | `src/text/text_reader.cpp:111` to `:119` |
| `pdfkit_inspect` reads only what open read, so a damaged page is named only after a call that read it | `src/api/inspect.cpp:6` |
| The self-check's raw scan reads a content stream's string operands, its unknown operators' names and the bytes between operations, and no keyword read into an operand's value | `src/redact/self_check.cpp:789`, `:804` |
| A rewritten stream keeps the bytes between operations that spell no needle | `src/redact/text_redactor.cpp:649` |

## 4. Change requests

| `CRx` | The request | Why it does not exist before this order | In this order? |
|---|---|---|---|
| CR1 | Every output a caller reads (inspect, text, find, check, the write report) names each page whose text could not be read in full, in one shape | the only sign was a warning of the text call | yes (W1) |
| CR2 | A page repaired with nothing lost is not named | the walk marked every lexer recovery as damage | yes (W1) |
| CR3 | Inspect names damage for every page whatever was called first | inspect read no page content | yes (W1) |
| CR4 | The write report says per damaged page whether a text redaction ran over it and whether damaged bytes were copied as they were | the self-check passes over text it cannot read | yes (W1) |
| CR5 | The reference panel's tiers run on Linux (P0) | the panel waits on its launch | panel batches, below |
| CR6 | Word cuts judged by the panel (W2), two gate counts (W3) | `OPEN-QUESTIONS.md` §1.4 | panel batches, below |

## 5. Design rules

| `DRx` | The rule | Why | Enforced by |
|---|---|---|---|
| DR1 | A page is named when a stream it draws met a filter fault or a lexer loss that can take text, and only then; a borderline case names it | a false silence costs more than a false mention | `tests/unit/api/damaged_pages_test.cpp` |
| DR2 | The content interpreter's walk is the one source of the damage; a form's fault names every page drawing it | two readers of one stream disagree | `damaged_pages_test.cpp` (a form on two pages), `tests/unit/text/content_interpreter_test.cpp` (the damage reading) |
| DR3 | Inspect never fails on a valid handle: a limit names the page `not_checked` | the catalogue row of inspect | `damaged_pages_test.cpp` |
| DR4 | Outputs only grow: `damaged_pages` is the last key of each output, and every `damaged_stream` warning is kept as it was | a caller binds to the shapes once | the key-order tests of `open_inspect_test.cpp`, `api_edit_test.cpp`, `text_json_test.cpp` |
| DR5 | One shape in every output, written by one writer | five copies drift | `src/syntax/page_damage.cpp` |

## 9. The work

1. **Failing tests first**: the builder's Flate damage knob (`FlateDamage`
   in `tests/unit/support/pdf_builder.hpp`); `damaged_pages_test.cpp` (every
   class in every output, the recoveries that name no page, a form on two
   pages, inspect before and after text, `not_checked`, the write report's
   flags and source pages), seen red (14 cases); the classes in
   `filters_test.cpp` and the losses in `content_lexer_test.cpp`.
2. **The syntax layer**: each filter fault's class and unread bytes
   (`Filters::Result`); the content lexer's `losses()` beside its count; the
   lexer's flag for a string run to the end; `PageDamage` and its JSON.
3. **The text layer**: each walk's damage (`ContentStream::damage`), a
   form's fault on every draw, `PageContent::damaged_page()`, the damage
   reading of the interpreter, and `DamageScan`.
4. **The api and redact layers**: the scan on the document handle, shared
   with the edit plan; `damaged_pages` in inspect, text, find, check (from
   `SelfCheck::run`) and the write report (`WritePipeline::damaged_pages`).
5. **ABI 4, version 0.4.0-preview**; `docs/ABI.md` §4.4.1 and the rows of each
   output; `docs/DESIGN.md` §3.1, §4, §6.1, §19 and §19.1; `CLAUDE.md` §2 and
   §7.1; `docs/SOURCES.md`.
6. **The dataset**: the verb's inspect and text members carry the page
   numbers; the manifest's `expect_damage` with its rule in
   `tests/dataset/run.py`, `--write-damage`, `dataset/README.md` and
   `tests/dataset/outcome.md`.
7. **The panel batches** (P0, A, B and C with W2, W3): to be written by their
   sessions under `docs/change-01.md`.

## 12. Tests

The unit tier on Linux (gcc 13 Debug, and clang 18 Release): 721 cases at
the base, 739 after W1, 750 after the first review's changes on the merged
branch (740 there before them), 753 after the second review's, none failing. The dataset tier's rules in `tests/dataset/test_run.py` and the
manifest vocabulary in `dataset/tools/tests/test_common.py`. The dataset,
sanitizer and PDFium tiers need the owner's laptop; `opf-0019` and
`mutate-0046` were run through the dataset verb on Linux (§14).

## 13. Acceptance criteria

| `ACx` | Criterion | Gate |
|---|---|---|
| AC1 | The tests of §9 pass and every base case still passes | pre-merge |
| AC2 | `check_layout`, `check_headers`, `check_comments`, `check_catalogue`, `check_network`, `check_repo`, `check_paths` and `check_format --no-tidy` pass on Linux | pre-merge |
| AC3 | `pdfkit_abi_version()` answers 4 and `pdfkit_version` names 0.4.0-preview | pre-merge |
| AC4 | `opf-0019` and `mutate-0046` name page 13 `flate_checksum` in inspect, text and the write report | measured on Linux |
| AC5 | The dataset tier's `expect_damage` column passes on the laptop after `--write-damage` has recorded the files the manifest does not hold | laptop |

## 14. Manual verification on the laptop

`python build/check.py --full` (clang-tidy ran on Linux over the changed
sources only), the unit and sanitizer tiers, then
`python tests/dataset/run.py --run-id <id>`. Its `expect_damage` failures are
the readable files whose damaged pages the manifest does not hold (every row
but `opf-0019`, `mutate-0046` and the five `damaged_flate` mutations expects
`none`); `python tests/dataset/run.py --id <ids> --write-damage --runs 1`
records them, and a second run passes. Measured on Linux: `opf-0019` and
`mutate-0046` name 23 pages each (3, 4, 9, 11, 12, 13, 17 to 19, 21, 23 to 25,
30, 32, 36, 37, 47 to 49, 51 to 53); page 13 reads 700 glyphs of a stream of
48,388 decoded bytes, `flate_checksum`, with `unknown_operator` (3,167 bytes
from byte 38,206) and `unlexable_content` (a string from byte 45,328 to the
end, 3,060 bytes). A search for `budget` writes an output the self-check
passes whose source page 30 keeps `4budget` inside a `TJ` array (§C.9); the
write report names page 30 with `text_redaction` and `copied_as_is` true.

## 16. Rollback

Revert the merge on `main`. Nothing is released by this order.

## Appendix C. Deviations recorded by this order

1. `damaged_pages` is the last key of each output, after the members the call
   wrote before, so no existing key moves.
2. The `damaged_stream` warnings stay as they were, a lossless lexer recovery
   included, because dropping one would remove a warning a caller saw; an
   unknown operator names its page in `damaged_pages` and adds no warning.
3. A stream is named by `piece` (the `/Contents` piece, `null` for a form),
   `form` and `form_path`; the glyph record's `walk` index is left out,
   because one form drawn twice is one damaged stream and its walk indices
   differ.
4. `unread_bytes` is written `null` when not known, so every entry has the
   same keys; for a lexer class it is the bytes its losses took, summed.
5. The write report's entries carry `source_page` (as `rectangles` do), and
   `copied_as_is` is decided by reading the output page again with the same
   walk, so it holds for a stream copied byte for byte and for bytes a
   rewrite kept alike, and is false where a rewrite wrote only what decoded.
6. `text_redaction` is true on every damaged page of a plan holding a search
   string (a value is searched on every page), and otherwise on the pages of
   its glyph-range, occurrence and string matches.
7. Inspect reads each page for its damage alone and a damaged page again in
   full (the damage reading of the interpreter), measured on a generated
   2,000-page file of 60 text lines a page (release, clang 18, best of
   three): `pdfkit channels`, which reads about what inspect read before,
   0.025 s; inspect with a full reading 4.7 s, with the damage reading
   0.14 s; text 7.4 s. On `opf-0019`: 0.008 s, 0.087 s and 0.083 s (its 23
   damaged pages read in full). The reading is charged to the document's
   budget.
8. Inspect's `warnings` hold every page's warnings once it has read them; a
   form's `damaged_stream` warning names the first page that decoded it, so
   that list still depends on what was read first, while `damaged_pages`
   does not.
9. The word `budget` the laptop's gate saw on "page 13" is in the stream of
   source page 30 of `mutate-0046` (page 13 decodes no `budget`): garbage of
   a stream whose checksum fails, `4budget` inside a `TJ` array, which the
   parser reads as a keyword and so as null. The self-check's raw scan reads
   string operands, unknown operators and the bytes between operations, and
   never a keyword read into an operand's value, so it does not see it; the
   rewrite keeps the array's bytes between codes. The verdict is unchanged;
   page 30 is named with `unlexable_content`, `text_redaction` and
   `copied_as_is`.
10. The dataset gains a column, `expect_damage`, because no existing column
    is inspect's; `CLAUDE.md` §7.1 names it.
11. The interpreter's damage-reading test was written with the code, after
    the measurement that asked for it, not seen red first.
12. clang-tidy 23.1.3 from PyPI ran on Linux over every changed source and
    test with the Linux compile database (the gcc-only warning flags aside, which
    clang does not know); its findings in the new code are fixed.
13. A content stream that cannot be decoded at all names its page:
    `unsupported_filter` for a page piece or form left encoded with an image
    codec or a filter no reader defines (it is lexed as it is),
    `external_stream` for data in an external file (`/F`), which decodes to
    nothing, and `encrypted_stream` for data under a crypt filter whose key
    needs a user password.
14. A missing object names its page: `missing_stream` for a `/Contents`
    entry that resolves to no stream, `missing_object` for a `Do` whose
    XObject resolves to nothing (beside its `resource_missing` warning),
    because it may have been a form with text. A page piece is named by its
    place in `/Contents`, so a missing entry keeps the places of the others;
    a page without `/Contents` is an empty page.
15. The content lexer keeps one loss per class and piece (the first fault,
    the count, the bytes summed with a saturating add), found from the back
    because losses arrive in byte order, so its memory is bounded by the
    classes and not by `operators_per_page`; each `damaged_pages` stream
    entry carries `count`, merged from the back the same way.
16. The parser names each recovery's kind (`Parser::Recovery`), and the
    lexer classifies by kind, never by a message's prefix; the string
    overload keeps every message as the repair report has it.
17. Measured on the dataset: an unknown operator with no operand or numbers
    alone names no page (`qpdf-0060` page 14's `endstreM-'m`, page 5's
    `CT`), and an indirect reference written in content is a lossless
    recovery: `n g R` alone, a name then `n g R` before `BDC` or `DP`
    (`/OC 5 0 R BDC`), `[5 0 R]`, `safedocs-0005` page 1's `7 0 R` in an
    inline image's dictionary. An `R` with other operands (`(Secret) 5 0 R
    Tj`, `/Im1 5 0 R Do`, `/F1 5 0 R 12 Tf`) names the page, because the
    operator after it lost operands that drew or decided text. Both are
    classification alone; the tokens and the operators' numbering are those
    of any reading. An unknown operator with a string, an array, a name or a
    dictionary names the page; `pdfminer-0048` (a bad Adler-32, every byte
    decoded) is named.
18. A limit met by the full reading of a page the damage reading found
    damaged keeps those classes beside `not_checked` (its `glyphs` 0).
19. The write report's re-read never fails a write: an output it cannot
    open, or a page it reads only as `not_checked`, answers `copied_as_is`
    `null` (unknown is neither copied nor not copied);
    `WritePipeline::copied_as_is` is the seam its test reads.
20. `pdfkit_check` reads every file open reads. On `main` it failed with
    `invalid_argument` ("an AES key of 0 bytes") on `qpdf-0017`, `qpdf-0051`
    and `qpdf-0059`: their handler's `/StmF` and `/StrF` are `/Identity`,
    their embedded files name the AESV3 filter `/StdCF` in their own
    `/Crypt` `/DecodeParms`, and no file key was derived. The key the empty
    user password gives is derived and kept when `/U` accepts it; when `/U`
    refuses it (a password guards the embedded files), such a stream is
    locked: it decodes to nothing, its page is named `encrypted_stream`, and
    check lists it in `not_searched`.
21. `not_checked` in `pdfkit_check` is tested through `SelfCheck::run` with a
    lowered operator limit, because the call opens with the default limits,
    which a unit fixture cannot reach in reasonable time.
22. The run over the 961 present dataset files with the rules of entries 13
    to 17 (release CLI, two runs, run id `w1-fix`) has no hang, crash or
    non-deterministic file. Its `expect_damage` values, written by
    `--write-damage` and confirmed: `qpdf-0060` none (its pages 5 and 14
    hold an unknown operator with no operand or numbers alone),
    `safedocs-0005` none (a reference in an inline image's dictionary),
    `pdfplumber-0010` pages 1 and 3 (page 3 `missing_stream`, object 86
    absent), `pdfplumber-0012` pages 1 and 2 (`missing_stream`: object 6's
    dictionary is commented out, object 12 does not read as a stream after
    reconstruction, and both pages read no text), `qpdf-0037` pages 1, 3, 4
    and 5 (pages 1 and 5 `missing_stream`: object 32 absent, and a direct
    non-stream `/Contents`). The one other failure, `mutate-0048`'s
    self-check refusal, reproduces on 0.2.1-preview.
23. The run over the 961 present files with the rules of entries 13 to 20 in
    full (run id `w1-fix2`, release CLI, two runs) has no hang, crash or
    non-deterministic file, and every `expect_damage` row passes (912 judged,
    49 refused files skipped): no file of the dataset holds a stream under a
    locked crypt filter or an `R` that takes another operator's operands.
    Its one failure is `mutate-0048`'s self-check refusal of entry 22.
24. A stream whose own `/Crypt` filter names a filter `/CF` does not define is
    locked under any handler (never read as clear), the key length reported
    for an `/Identity` handler is the key held when one is derived, and the
    tests cover an RC4 (`/V2`) crypt filter, revision 5, and a `/Perms` that
    fails under the empty password (locked, never refused).
