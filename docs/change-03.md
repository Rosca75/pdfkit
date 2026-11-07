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
| Toolkit check | both | none | running | none | none | record results here |
| W1 damaged pages | pdfkit | `w1-damaged-pages` | design decided (below), implementation starting | none | none | implement, review, merge |
| W1 consumer | doc-anonymiser | its own workflow | waiting on W1's ABI | none | none | brief after W1's ABI shape is fixed |
| P0 Linux tiers | pdfkit | `p0-linux-tiers` | waiting on the toolkit check | none | none | brief |
| Batch A, B, C (+W2) | pdfkit | one per batch | waiting on P0 | none | none | in order |
| W3 gate counts | pdfkit | `w3-gate-counts` | waiting | none | none | last |

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
