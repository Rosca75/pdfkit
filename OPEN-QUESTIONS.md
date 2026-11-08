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

### 1.3 Text on a damaged page that PDFium reads and pdfkit does not is not redacted

Found on 2026-11-07 while measuring 0.3.0-preview against 0.2.1-preview; both
versions behave alike. The content stream of page 13 of `opf-0019` (and of its
mutation `mutate-0046`) is damaged in the corpus file itself: its Flate data
fails its checksum and part of it decodes to garbage. pdfkit reads 699
characters of that page (a `damaged_stream` warning); PDFium reads the page
almost whole. A word only PDFium reads is neither found nor removed by pdfkit,
and the self-check, which reads as pdfkit does, passes: a search for "Large"
left one "Large" on that page that PDFium reads in the output, and "Most"
likewise in `mutate-0046`.

**Answered 2026-11-07: pdfkit reports, it does not refuse.** What to do with a
damaged page is a business rule of the caller (§2.1), so pdfkit decides
nothing about it: every output a caller reads (inspect, text, find, check and
the write report) names each page whose text could not be read in full, so a
caller never takes partial text for the whole page or "nothing found" for
"nothing there". A page pdfkit repaired with no text lost is a normal page.
The same report covers the bytes such a stream copies as they were (a word of
the damaged data that no reader shows but a byte search finds, `budget` in
`mutate-0046`). doc-anonymiser-for-.NET shows a warning and lets its user
ignore the page or drop it; dropping is that application's workflow. Action:
a session adds the report to pdfkit with its tests, its `docs/ABI.md` and
`docs/DESIGN.md` rows and the dataset expectations, and the warning and the
choice to doc-anonymiser-for-.NET.

### 1.4 Word cuts that differ from the reference readers

Answered 2026-11-07 by the owner: for an anonymiser, a word cut where the
document has none (`Re cue i l`, `06 51`) or two words glued (`Lordfromheav'n`)
can hide a name or a number from the caller's detection, so a drop of text
agreement against the reference readers is never accepted as small. Action:
a session examines each file whose agreement is below 0.2.1-preview
(`dccc-0124`, `dccc-0139`, `dccc-0097`, `pdfpig-0005`) and the Latin digits
glued to ideographs of `dccc-0073`, judges each cut by the reader panel
(§2.2), fixes what the panel calls wrong with a test seen red first. Where
the panel splits, the session judges the cut from the words around it and
pdfkit's purpose, and changes pdfkit only when the gain in reading is clear
and costs no quality elsewhere.

### 1.5 Chinese, Japanese and Korean fonts

Answered 2026-11-07 by the owner: pdfkit stays a general library, but its
first caller reads mostly European languages, so CJK reading is a
nice-to-have that must not add complexity. The Adobe CMap resources are not
embedded and the CJK misses stay known gaps; the panel's verdicts on CJK
files are not pursued.

### 1.6 Damaged pages: choices decided by the session, to confirm

The session that built the report of §1.3 (`docs/change-03.md`, W1) made
these choices a caller sees. Each is in force; confirming costs nothing, and
the price given is that of changing it.

1. **Inspect names a page it could not read as `not_checked`.** When a limit
   of the open options is met while inspect reads a page (the operator count,
   a stream or the whole document past its bytes), inspect still answers and
   names that page with the class `not_checked` and the limit's message,
   instead of failing. Example: a file opened with
   `{"operators_per_page": 12}` whose page 2 holds 20 operators answers
   `{"page": 2, "glyphs": 0, "streams": [{"stream": null, "class":
   "not_checked", "fault": "page 2 holds more than 12 content operators",
   ...}]}`. Text and find still fail with `limit_exceeded` on such a page.
   **Recommended: keep.** Alternative: inspect fails with `limit_exceeded`,
   which breaks its catalogue promise; about 0.5 hour, $10.
2. **Which damage names a page.** The rule: a page is named only when a
   dropped or unread operator could have carried or decided text. Named:
   every filter fault (`flate_checksum`, `flate_corrupt`, `flate_truncated`,
   `flate_dictionary`, `lzw`, `ascii_hex`, `ascii85`, `run_length`,
   `predictor`); a stream left encoded with an image codec or an unknown
   filter (`unsupported_filter`), encrypted under a crypt filter whose key
   needs a user password (`encrypted_stream`; `pdfkit_check` lists it in
   `not_searched`) or held in an external file (`external_stream`); a `/Contents` entry that resolves to nothing
   (`missing_stream`); a `Do` of an XObject that resolves to nothing
   (`missing_object`, which may have been a form with text); an unknown
   operator outside `BX` and `EX` with a string, an array, a name or a
   dictionary among its operands (`unknown_operator`: `(x) zork`, a
   corrupted `Tj`, `Tf` or `Do`); an operator read into an operand or a
   string that runs to the end (`unlexable_content`); an inline image without
   a readable dictionary or an `EI` (`inline_image`). Not named: a wrong
   `/Length` or a missing `endstream` (a `repaired` reason), operands left at
   the end or before `BI`, a stray delimiter, an unknown operator with no
   operand or numbers alone (`qpdf-0060` page 14's mangled `endstreM-'m`
   after a stream recovery, page 5's `CT` written for `BT`, whose 8 glyphs
   read), an indirect reference written in content (`5 0 R` alone,
   `/OC 5 0 R BDC`, `<< /Foo [5 0 R] >> BDC`, `safedocs-0005` page 1's
   `7 0 R` in an inline image's dictionary); an `R` that takes another
   operator's operands (`(Secret) 5 0 R Tj`, `/Im1 5 0 R Do`) is named. One borderline case names the page: a checksum that
   disagrees though every byte decoded (`pdfminer-0048`; page 13 of
   `opf-0019` reads 700 glyphs whole but holds garbage after byte 38,206).
   **Recommended: keep**, because a false silence costs more than a false
   mention; the caller can ignore a class it trusts. Alternative: leave
   `flate_checksum` out when nothing else is wrong with the stream; about 1
   hour, $20, and a page whose garbage hides a word goes unnamed.
3. **What `text_redaction` means in the write report.** True on every damaged
   output page when the plan holds a search string (a value is searched on
   every page, so its verification over a damaged page is incomplete), and
   otherwise only on the pages of the plan's glyph-range, occurrence and
   string matches. Example: a plan removing "Smith" everywhere marks all 23
   damaged pages of `opf-0019`; a plan removing one located "EU" on page 4
   marks page 4 alone. **Recommended: keep.** Alternative: true only on a
   page where a match was found or edited; about 1 hour, $20, and a value
   that occurs only in the unread part of a page is then reported as no
   concern.
4. **What `copied_as_is` means.** True when the output page, read again by
   the same walk, still holds damage: a damaged stream the write did not
   touch (its bytes copied as they were, which a better decoder may read
   further), or bytes no operator draws that a rewrite kept (`4budget` in
   `mutate-0046` page 30, which a byte search finds and no reader shows).
   False where a rewrite wrote only what decoded (a truncated stream's lost
   tail is not in the output). `null` when the output could not be read
   again within the open limits, because unknown is neither copied nor not
   copied; that reading never fails a write. **Recommended: keep.**
   Alternative: a rewrite
   drops the bytes no operator draws, so the flag is false on every page a
   text redaction rewrote; about 3 to 4 hours, $60 to $80, a change to what
   the write copies.
5. **Inspect reads every page.** It reads each page for its damage alone and
   a damaged page in full, charged to the document's budget like any read: on
   2,000 pages of 60 lines 0.14 s, where it took 0.025 s before. A file near
   the 2 GiB document limit can meet it one call sooner. **Recommended:
   keep.** Alternative: inspect reads under a budget of its own; about 1
   hour, $20.
6. **The dataset expects no damaged page where none is recorded.** Every
   readable row but `opf-0019`, `mutate-0046` (23 pages each, measured) and
   the five `damaged_flate` mutations (`some`) expects `none`, so the first
   laptop run fails on each readable file with a damaged page the manifest
   does not hold, and `python tests/dataset/run.py --write-damage` records
   them. **Recommended: keep**, one laptop run and one commit, about 0.5 hour
   and $10. Alternative: start every unmeasured row at `n/a`, which checks
   nothing until filled.

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

## 3. Waiting on the owner: the reference panel's verdicts

Raised on 2026-11-08 by the session that built the panel's judge, on the
Linux run `panel-b-full` (`build-5.md` §9 item 21, `build-6.md` §9 item 18).

### 3.1 Poppler does not vote on the pixels of a stencil mask

**Decided by the session, to confirm.** A stencil mask is a one-bit image
that says where the page's fill colour is painted; its `/Decode [1 0]`
swaps which bit paints. Poppler's `pdfimages` applies `/Decode` to an
ordinary image but writes a stencil mask's bits as stored: synth-0263
draws the same bits three times, the third with `/Decode [1 0]`, and
Poppler writes all three alike while PDFium and MuPDF show the third
inverted. So the panel lets Poppler vote on every image but stencil masks,
where PDFium and MuPDF vote alone (`abstains` in `tests/panel/members.tsv`).
On the dataset this leaves 518 bitonal images, 489 decided, all 489
agreeing with pdfkit; 29 stencil-heavy images stay undecided because one of
the two remaining voters does not reach them.

**Recommended: confirm**, no cost. **Alternative: drop Poppler's pixel vote
everywhere**, about 0.5 hour and $5 to $10 (one line of `members.tsv` and a
gate run): the same stencils are judged as now and the other bitonal images
lose a voter. **Alternative: invert Poppler's stencil bits where the file
says `/Decode [1 0]`**, about 1 to 2 hours and $15 to $30: the 29 undecided
images gain a voter, but the panel would then repair a member's output,
which `docs/change-01.md` §8.3 rules out.

### 3.2 CMYK images convert through PDFium's Adobe table: the panel does not confirm it

The re-decision R2 measured pdfkit's RGB of the 151 CMYK-class images
(DeviceCMYK, four-component ICCBased, Separation, DeviceN) against each
pixel member's. The table holds by the measure when, on most images, most
voters lie within a mean of 2 per channel of pdfkit; that is so on 24 of
146 images. PDFium agrees with pdfkit on 102 of 144 (median distance 0),
Poppler on 25 of 146 (median 5.1), MuPDF on 14 of 146 (median 25.3; its
Linux package converts without ICC). The members do not agree with each
other either (PDFium and Poppler within 2 on 18 images, Poppler and MuPDF
on 11, PDFium and MuPDF on 9): three readers, three conversions, none held
by a majority. For example, on a 271 by 273 CMYK picture of dccc-0124
(object 50) pdfkit and PDFium are identical, Poppler is 5.9 apart and MuPDF
22.9. The table stays until you answer.

**Recommended: keep the table and re-measure on the laptop**, where MuPDF
1.28.5 converts with ICC: about 0.5 hour and $5 to $10 inside the laptop's
panel session; if MuPDF with ICC sits within 2 of pdfkit, the table is
confirmed by two of three. **Alternative: move to Poppler's conversion**,
about 0.5 to 2 hours and $10 to $40 (one function, its tests, the notice
row and a gate run); it moves pdfkit away from PDFium on about 100 images
and towards no majority. **Alternative: the naive complement**, the same
price; it failed the continuous-tone gate against PDFium before the table
was ported (`build-6.md` §9 item 5).

### 3.3 The panel compares pdfkit with each member on the same surface

**Decided by the session, to confirm.** Read literally (pdfkit's full text
against each member's own text, word for word), the reading gate counts 93
of 893 decided files against pdfkit (10.4 %), many of them where a member
reads something pdfkit leaves out by design or writes it in another order.
Three rules make the comparison like for like (`tests/panel/judge.py`):

- **A right-to-left word reads the same in either direction, for the
  members that write visual order.** MuPDF, pdf_oxide and pdf-extract
  write Hebrew in visual order ("םולש" for "שלום"); PDFium and Poppler
  write logical order, as pdfkit does, and are compared word for word.
- **pdfkit's visible text and its text with every duplicate copy count as
  other readings.** pdfkit reads off-page and hidden text on purpose;
  Poppler, MuPDF and pdf_oxide leave it out (mutate-0006: "Background" four
  times off the page, read by pdfkit and PDFium, by no other member).
- **Where annotations draw text, Poppler, MuPDF and pdf_oxide read a copy
  of the file without them.** They print form-field values and the text of
  stamps and notes (synth-0011's `MARKER-ACROFORM-01`), which pdfkit leaves
  out as a channel (`CLAUDE.md` §2). On the 91 files where QPDF finds an
  annotation that draws text (a widget, a FreeText, or another whose
  appearance shows a string; never a link alone), QPDF writes a copy with
  the annotations and the form left out, and those three members are
  judged on their text of the copy, word for word like the others. pdfkit
  is the reader under test, so it never makes the copy. A file QPDF cannot
  open (10, qpdf-0051 among them) keeps the members' own text.

With them 50 of 893 decided files count against pdfkit (5.60 %), median
1.0. An earlier draft scored those three members by the share of pdfkit's
words found in their text, which let pdfkit miss words on any annotated
file (three of five outvote the two others); the copy replaces it. The
Type 3 re-decision is measured on characters rather than words for the
same reason ("Property" read as "P ro p e rty" by three members), and the
causes table adds a `cjk` group for Chinese and Japanese word grouping, out
of scope by §1.5.

**Recommended: confirm**, no cost. **Alternative: the literal rule**, about
0.5 hour and $5 to $10 to switch the gate: 93 files against pdfkit, 43 more
than the rules count, most of them form fields, off-page text and
right-to-left order that pdfkit reads as designed.

### 3.4 The reading gate under the panel fails at 5.60 %, and 11 of the 50 are specimens pdfkit reads right

Of the 50 files the panel counts against pdfkit, 11 are synthesised
specimens (synth-0195 to synth-0204, synth-0209) whose manifest notes state
their text, for example synth-0196 "Contract number 4711 signed by Maria
Test": pdfkit reads exactly that (F1 1.0 against the notes) through the
font program, and all five members print the raw codes
(`"&%#$2*.-!+&&/1!`). The panel's majority is wrong there, and no majority
rule can see it. Six more are pdfkit's rules (Type 3 shape glyphs,
an unknown filter, the duplicate fold) or Czech that pdfkit and pdf_oxide
read and the others garble. These numbers assume §3.3's rules are
confirmed; under the literal rule the count is 93.

**Recommended: judge a synthesised specimen that states its text by that
text**: it agrees when pdfkit's F1 against the stated text is at least 0.90,
whatever the panel says, because the generator wrote the truth; 39 of 893
(4.37 %) then count against pdfkit and the gate passes before batch C's
fixes. About 0.5 to 1 hour and $10 to $20 (the rule, its test, a gate run).
**Alternative: keep the panel's count** at no cost: the gate fails until
batch C fixes at least 6 of its confirmed files (the six Type 3 files of
CR5 alone would bring it to 44 of 893, 4.93 %). Counting the `legitimate`
group neither way was refused on 2026-10-28 (§2.2) and is not offered.
