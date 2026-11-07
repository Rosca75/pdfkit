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
| W1 damaged pages | pdfkit | `w1-damaged-pages` (worktree `/home/user/wt/w1`) | implementing | none | none | review, merge |
| W1 consumer | doc-anonymiser | change order 19, `claude/change-19-<letter>-<slug>` | waiting on W1's ABI | none | none | brief after W1 lands. The app pins pdfkit 0.2.1 (ABI 2); its fixtures are recorded from a tagged release and its Linux build script builds only a tag, so the pin move stays the owner's after the tag |
| P0 Linux tiers | pdfkit | `p0-linux-tiers` | merged into `main` at 9a97e6f after review (APPROVE; follow-ups applied: preset host conditions, two delta words, a bounded read, the `test_channels` failure main already had) | 7af4f56, f15bb87, 821a7ac | dataset 961/968 present (7 `office` rows laptop-only), 0 hash mismatches; unit 721/721 linux-debug and linux-release; run `linux-base-09b8d78` (2 runs, 961 files, 752 s): 0 crashes, 0 non-deterministic, 1 hang (`dccc-0129`, 73 s alone under load; passes at `--timeout-scale 3`), 1 real miss (`mutate-0048` self-check refusal, as on the laptop); PDFium on Linux: F1 median 1.0, 55/898 below 0.90 (laptop 56), bitonal 495/498, Info 3,048/3,048; ABI tier .NET and Python pass (31 files, 717 calls); PDFium Linux SHA-256 `bdc60126…` computed, not read | done; departures in `docs/change-01.md` Appendix C, versions in `build/toolchain.md` §7 |
| Batch A, B, C (+W2) | pdfkit | branch change/01-a-readers (worktree `/home/user/wt/a`) | batch A starting | none | none | in order |
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

The unit tier on Linux (gcc 13 Debug): 721 cases at the base, 739 after W1,
none failing. The dataset tier's rules in `tests/dataset/test_run.py` and the
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
