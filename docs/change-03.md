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
| W1 damaged pages | pdfkit | `w1-damaged-pages` (worktree `/home/user/wt/w1`) | implementing | none | none | review, merge |
| W1 consumer | doc-anonymiser | change order 19, `claude/change-19-<letter>-<slug>` | waiting on W1's ABI | none | none | brief after W1 lands. The app pins pdfkit 0.2.1 (ABI 2); its fixtures are recorded from a tagged release and its Linux build script builds only a tag, so the pin move stays the owner's after the tag |
| P0 Linux tiers | pdfkit | `p0-linux-tiers` (worktree `/home/user/wt/p0`) | implementing: presets, Linux paths, dataset fetch, base run `linux-base-09b8d78`, PDFium Linux | none | none | review, merge |
| Batch A, B, C (+W2) | pdfkit | one per batch | waiting on P0 | none | none | in order |
| W3 gate counts | pdfkit | `w3-gate-counts` (worktree `/home/user/wt/w3`) | explained; reviewed APPROVE with three minor fixes being applied | 82410b5, 752fddd | 8,075 − 7,681 = 394 pages, all `dccc-0129` (hung, no outcome); re-open 360/361 the same file; scripts right on the numbers, reports now name the pages; selection rows now keep a hung file | merge after the fixes |

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
