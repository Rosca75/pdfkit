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

### 0.4 Where the work stands

This table is the programme's ledger. It is updated after every merge or
decision, and a session that resumes reads it before anything else.

| Item | Repository | Branch | Status | Commits | Numbers | Next step |
|---|---|---|---|---|---|---|
| Toolkit check | both | none | done 2026-11-07 | none | pdfkit unit 721/721 (Linux gcc Debug); `check.py --fast` 11/11 with the scratch venv; doc-anonymiser 7,612 unit (0 failed, 122 skipped) and 107 integration (95 passed, 12 skipped) against `pdfkit.so` 0.2.1 | see Toolkit below |
| W1 damaged pages | pdfkit | `w1-damaged-pages` (worktree `/home/user/wt/w1`) | reviewed CHANGES REQUESTED (undecodable or missing streams not named, an unbounded loss list, false mentions); main merged in; dataset measured; fixes in progress | 94e4caa..3186958, merges a98df23 4f96a1c, manifest 86ea593 | unit 740/740; run `w1-damage`: no column lost a pass, 0 crashes, 0 non-deterministic; 24 files named damaged (19 the manifest held clean); inspect, text and find agree on 912/912 readable files; inspect over the dataset 4.84 s → 10.72 s; false mentions qpdf-0060 (two pages) and safedocs-0005 | rerun `w1-fix`, re-review, merge |
| W1 consumer | doc-anonymiser | branch claude/change-19-damaged-pages (worktree `/home/user/wt/19`) | change order 19 built (4 commits); under review | 9333fe4, 2424294, f0331d4, efbb408 | unit 7,638 (0 failed, 122 skipped), integration 107 (95 passed, 12 skipped, fixture drift green on 0.2.1), node 2,046; end to end against a local ABI 4 build: opf-0019's row names 23 of 54 pages, Drop leaves an excerpt with none; decisions D1-D5 in its FEEDBACK-19 to confirm | land on its main after review; the pin move waits on the owner's tag |
| P0 Linux tiers | pdfkit | `p0-linux-tiers` | merged into `main` at 9a97e6f after review (APPROVE; follow-ups applied: preset host conditions, two delta words, a bounded read, the `test_channels` failure main already had) | 7af4f56, f15bb87, 821a7ac | dataset 961/968 present (7 `office` rows laptop-only), 0 hash mismatches; unit 721/721 linux-debug and linux-release; run `linux-base-09b8d78` (2 runs, 961 files, 752 s): 0 crashes, 0 non-deterministic, 1 hang (`dccc-0129`, 73 s alone under load; passes at `--timeout-scale 3`), 1 real miss (`mutate-0048` self-check refusal, as on the laptop); PDFium on Linux: F1 median 1.0, 55/898 below 0.90 (laptop 56), bitonal 495/498, Info 3,048/3,048; ABI tier .NET and Python pass (31 files, 717 calls); PDFium Linux SHA-256 `bdc60126…` computed, not read | done; departures in `docs/change-01.md` Appendix C, versions in `build/toolchain.md` §7 |
| Batch A (readers) | pdfkit | change/01-a-readers | merged into `main` at 7d62575 (`Change-Batch: 01a done`) after review (CHANGES REQUESTED, nine fixes applied) | 4293e8a, 2c9c88d, ef6a121, b3b0f00, 97917e3 | AC1: every member answers, refuses or holds a `repeated_failure` for every applicable file (961 text, pages and structure; 70 pixel files), warm runs start 0 processes; AC1a `fetch.py --verify` agrees; AC1b pixel facts in `tests/panel/members.tsv`; panel tests 50; unit 722/722; `check.py --fast` 10/10 | owner question for batch B's sweep: Poppler abstains on stencil masks (narrows D7) |
| Batch B (judge) | pdfkit | branch change/01-b-judge (worktree `/home/user/wt/b`) | starting | none | none | review, merge |
| Batch C (+W2) | pdfkit | one branch | waiting on batch B | none | none | after B |
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
