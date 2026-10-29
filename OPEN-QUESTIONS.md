# Open questions for the owner

Working document. Each question is the owner's to answer and is written in
plain words, with the recommended answer first and the price of each option in
hours and model spend. Only a decision a pdfkit user or the owner would notice
in an output or a bill is asked; an engineering choice is decided by the
session, recorded with its reason and reported in one line. Work that does not
depend on the answer continues. An answered question stays here with its date
and the action it orders until that action lands; the rule it decides lives in
`CLAUDE.md`, `docs/DESIGN.md`, `docs/ABI.md` or a build plan.

## 1. Waiting on the owner

### 1.1 Word and Edge sample files (was 2.2)

`gen_word_pdfa` and `gen_edge` stay at 0 of their minimum of 1 because Word
and Edge cannot be driven unattended on the laptop. **Answered 2026-10-28: the
owner runs the script once at the laptop**:
`powershell -ExecutionPolicy Bypass -File dataset/tools/generate_office.ps1 -Only word,edge`,
answering whatever Word or Edge shows (about 10 minutes); a session then
checks and commits the four rows (about 0.5 hour, $5 to $10).

### 1.2 Removing every clip glyph of a text object shows what the letters hid

Asked by doc-anonymiser (2026-10-29), answered without a change in
`docs/DESIGN.md` §12 (Clip text): when a redaction removes every glyph a text
object shows in render mode 7 (or the clip half of modes 4 to 6), the text
object adds no outline to the clip, and PDFium and pdf.js then leave the clip
as it was before `BT`, so an image or shading painted through the letters
shows whole, more than the original page showed. Removing some of the glyphs
is faithful (the removed letters' shapes go blank).

**Recommended: close such a text object with an empty clip** (`0 0 m h W n`
after its `ET`, inside the same `q`), so nothing painted through the removed
letters shows, as the union of no outlines; a test per case, about 1 to 2
hours and $20 to $40. **Alternative: leave it** and keep doc-anonymiser's
rule of never removing a mode-7 glyph as hidden text; no cost, and a page
whose clip text is redacted may show an image in full.

## 2. Answered on 2026-10-28

The section numbers are those of the file before this date; its full text is
in git history (`git show 7cd2968:OPEN-QUESTIONS.md`).

### 2.1 Product principle: pdfkit is a literal tool

pdfkit is driven by doc-anonymiser-for-.NET, which owns every business rule:
which values and spelling variants to send, word boundaries, which short words
may be redacted and where. pdfkit applies what it is given and makes no such
judgement. Two kinds of text match follow:

- **A value** (a search string): every occurrence in the document is removed,
  inside longer words too ("Smith" in "Smithson"), under the folding the caller
  names, and the self-check searches the whole output for it.
- **An expression at a location** (a glyph range from `pdfkit_text`): that
  occurrence alone is removed, and the self-check verifies that location only
  (its glyphs, the operand bytes that drew them, the marked-content text over
  them), so redacting one "EU" leaves every other "eu" and is not refused.
- **A rewritten metadata field** is verified in the metadata surfaces (Info,
  XMP, every string of the file); a caller that wants the value gone from the
  page text sends it as a value.

The owner added on 2026-10-28: pdfkit offers two text operations, every
occurrence of a string or one single occurrence; one occurrence is that place
on that page only (an occurrence in a header drawn on 30 pages goes from the
picked page alone), and the single-occurrence call takes parameters that
narrow which text is meant. **Action**: `docs/archive/build/build-14.md`, done (shared forms copied per
located edit; the `occurrence` form with `index`, `near`, `before`, `after`).

`CLAUDE.md` §5 states the principle. **Action**: a branch changes the
self-check's glyph-range and metadata scope and drops the three-character
exemption (`docs/DESIGN.md` §18, `docs/ABI.md` §4.11, the dataset job and the
phase 8 and 10 gates), about 3 to 5 hours and $60 to $100. Questions this
settles: old 2.11 item 2 (inside words), 2.12 item 2 (short originals), 2.14
item 2 (phrases stay phrases; variants come from the caller).

### 2.2 The reference panel replaces PDFium as the only judge

**Answer**: a reading choice or a gate is judged by the majority of a panel of
readers, not by PDFium alone: PDFium, Poppler's `pdftotext` (GPL, C++), MuPDF's
`mutool` (AGPL, C++), pdf_oxide (Rust, MIT or Apache-2.0) and pdf-extract
(Rust, MIT) for text; QPDF and lopdf for structure. A file counts against
pdfkit only when pdfkit disagrees with the majority. `CLAUDE.md` §6 allows
GPL and AGPL readers as separate test processes, never linked or shipped.
**Action**: a build plan for the panel (fetch by hash, one adapter each,
answers cached per file), about 10 to 14 hours and $180 to $280; then the
phase 5 and 6 gates and the choices below re-judged by the panel. This
replaces old 2.10's suggestion to count only pdfkit-caused files.

Settled through the panel: old **2.5** (Type 3 glyph names that restate their
code: read or not as the panel majority reads them), old **2.7** (the CMYK
conversion: kept while the panel measures it), and the phase 6 bitonal
misses (PDFium's `FPDFImageObj_GetBitmap` "ignores" the image mask and matrix
by its own header; the panel replaces it as the judge).

### 2.3 Test cadence

**Answer**: the three-point rhythm, `CLAUDE.md` §8.2: while coding, the unit
tests of the component touched; per batch of commits, the routine check and
the unit tier; the dataset as a smoke set once mid-phase, the full gate once
at the phase end and again only after a fix to one of its failures; the
sanitizer tiers and `check.py --full` at the merge. **Action**: in force from
this commit; every brief carries it.

### 2.4 Release

**Answer** (old 2.15): tag `0.1.0-preview` with the known gaps in the release
notes. **Done 2026-10-29**: `v0.1.0-preview`, published as a GitHub pre-release
with the package and its notes.

### 2.5 Hidden layers are removed (old section 1, optional content)

**Answer**: flipped. Content in an optional-content group that is off is
removed from the output, and `/OCProperties` goes with it, instead of being
kept hidden. **Action**: `docs/DESIGN.md` §15 and the writer, with a
planted-marker test, about 2 to 3 hours and $40 to $60.

### 2.6 Kept as recommended

Each answer below keeps the rule as written; no action.

| Old § | Decision kept | How decided |
|---|---|---|
| 1 | digital signatures stripped; JPEG re-encoded at quality 90; 10,000-page default ceiling | owner |
| 1 | catalog and page keys by allow-list; a shared form rewritten once (a value goes from every page that draws it); earlier match wins an overlap; `/ActualText` rebuilt with the placeholder; blur refused for bitonal images under 128 px; no object streams; identifiers from the output hash; a missing `/MediaBox` read as US Letter until the panel says otherwise; right-to-left order kept | session: no visible product change |
| 2.1 | llvm-mingw for the ASan tier | already in `CLAUDE.md` §8.1 and §10 |
| 2.3 | corkami's proofs of concept stay refused | the repository still has no licence (checked 2026-10-28) |
| 2.4 | a revision 6 `/Perms` that fails refuses as `password_required` | already in `CLAUDE.md` §2, row 1.1 |
| 2.6 | every write prunes undrawn resources | session: a channel closed, 0 fidelity cost measured |
| 2.8 | no colour management | owner |
| 2.9 | one decoding budget per document handle | owner |
| 2.11 item 1 | no compatibility folding, and no flag for it | owner |
| 2.11 items 3, 4 | private XObject keys by allow-list; the raw scan reads string operands; a plan of values re-reads changed pages | session: safety and speed, no visible change |
| 2.11 item 5 | the dataset job rewrites metadata holding a redacted word | done by phase 10 (83 of 85 rows clean) |
| 2.12 item 1 | a custom metadata value repeating a redacted string is removed | owner |
| 2.12 items 3, 4 | XMP identifiers regenerated; dataset timeout 30 s per 200 pages | owner; session |
| 2.12 item 5 | no XMP packet created where none exists | owner |
| 2.12 item 6 | ABI version 1 until the first release | already in `CLAUDE.md` §3.3 |
| 2.12 item 7 | phrase search across reading orders | done by `fix/phrase-search` (62 PDFium hits to 0) |
| 2.13 items 1 to 4 | identical rewrite not a survivor; a broken inline image removed whole; box colour in RGB; treated inline images as `/AHx` | owner |
| 2.14 item 1 | four reading orders | session: needed to remove a value everywhere |
| 2.14 item 3 | Arabic presentation forms read as their letters | owner |
| 2.16, 2.17 | ccache; PDFium reset per request | session: build and test tools, measured, never shipped |
