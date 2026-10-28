# build-11 — A located match is verified at its location; a metadata field in the metadata

| Field | Value |
|---|---|
| Phase | 11 |
| Branch | `phase/11-located-check` |
| Depends on | nothing beyond `main` with `chore/owner-answers` merged |
| Parallel with | 12 and 13 (no code file shared; the shared documents are edited last, see §7) |
| Model | `opus` |
| Owns | `src/redact/self_check.{hpp,cpp}`, `src/redact/text_plan.{hpp,cpp}`, `src/api/redact_text.cpp`, `src/api/check.cpp` (reads only), `cli/verbs/dataset_redact.{hpp,cpp}`, `cli/verbs/dataset_metadata.{hpp,cpp}`, `tests/unit/redact/self_check*`, `tests/unit/redact/text_plan*`, `tests/dataset/gates/phase8.py`, `tests/dataset/gates/phase10.py`, `tests/pdfium/leak.py`; at the end, after rebasing on `main`: `docs/DESIGN.md` §11 and §18, `docs/ABI.md` §4.7, §4.11, §4.12, `CLAUDE.md` §2 rows 5 and 10.1, `tests/dataset/outcome.md` |
| Size | about 700 lines of C++ including tests, 200 lines of Python |
| Price | 4 to 5 hours of session time, $80 to $100 of model spend, calibrated from phase 8's self-check (`docs/archive/build/build-8.md` header) scaled to the part that changes |
| Time box | 6 hours; at 5 h 30 the session commits what passes and closes `incomplete` with what is left |

## 0. What this phase ships

`CLAUDE.md` §5, "pdfkit is a literal tool": a value is removed everywhere and
verified everywhere; an expression at a location is removed there and verified
there; a rewritten metadata field is verified in the metadata. Today the
self-check (`docs/DESIGN.md` §18) treats a glyph range's text as a string to
hunt in the whole output and searches every rewritten field's original in the
page text, so a caller redacting one "EU" is refused because "eu" stands
elsewhere, and a caller rewriting the author "PC" is refused because "PC" is in
the text. Short originals were exempted (three letters or digits) to keep that
working; the exemption goes, because the caller now chooses what is checked.

### 0.1 The contract (`docs/ABI.md` text this phase lands)

**`pdfkit_redact_text`, the located form.** A glyph-range match is one
expression at one place. `glyphs` is either one range
`{"page": n, "first": i, "last": j}` (as today) or an array of such ranges on
one page, read as one expression (the form `pdfkit_find` reports for an
occurrence found in a drawing or row order, which covers several stretches of
the glyph list). The ranges must not overlap; an empty array, ranges on two
pages, or overlapping ranges are `invalid_argument`. The placeholder is drawn
once, at the first range. The answer's occurrence keeps its keys and its
`glyphs` holds the ranges as given.

**What the self-check verifies, per kind of request** (`docs/DESIGN.md` §18):

| Request | Removed from | Verified on |
|---|---|---|
| a search string (a value) | every occurrence in every order of every page | every surface of §18, every page, as today |
| a glyph range (an expression at a location) | the glyphs of the ranges | that location: the output page's glyphs whose boxes meet the occurrence's rectangles hold no part of the expression's text in any order; the codes the answer reports under `codes` occur in no string operand drawn there; no `/ActualText`, `/Alt` or `/E` over that place holds the text. Nothing elsewhere is searched for it |
| a rewritten metadata field | the Info entry and its paired XMP properties | the Info dictionary, the XMP packet and every string object outside content streams, for each original value of any length; the page text is not searched for it |

A value whose search the caller also adds is verified everywhere as a value,
whatever else the plan holds. A refusal keeps its shape (`docs/ABI.md` §3); a
survivor of a located match names `surface` `location` with the page. The
write report's `self_check` gains `located` (the located matches verified) and
`metadata_values` (the originals searched), after its current keys.

**`pdfkit_check`** is unchanged: it searches strings everywhere, as a value.

## 1. What it reads from earlier phases

| From | What | Why |
|---|---|---|
| phase 8 | `src/redact/self_check.*`, `text_plan.*`, `match_finder.*` (read, not changed: build-12 owns it), the occurrence's `rectangles` and `codes` | the location check reads what the redaction recorded |
| phase 10 | the metadata needles of the self-check, `cli/verbs/dataset_metadata.*` (the job searching every original in the text) | the job keeps searching the originals as values, as a caller does, so the leak measures keep their meaning |
| `fix/phrase-search` | the four orders of `docs/DESIGN.md` §11 | the location check reads every order |

## 2. Components

| File | Change | Lines |
|---|---|---|
| `src/redact/text_plan.{hpp,cpp}` | a located match holds its ranges (one page, sorted, disjoint); validation of the array form | 120 |
| `src/api/redact_text.cpp` | parses `glyphs` as an object or an array | 60 |
| `src/redact/self_check.{hpp,cpp}` | needles split by kind: values (every surface), located matches (the location check of §0.1), metadata originals (metadata surfaces, every length); the changed-pages-only reading of §18 kept for values | 300 |
| `cli/verbs/dataset_redact.*` | the dataset job gains a located member: on each file, a word of three or more letters that occurs at least twice is redacted once by its glyph range; the outcome records the occurrence count before and after | 120 |
| `cli/verbs/dataset_metadata.*` | unchanged behaviour: the job still searches every original it rewrites as a value; the short-original filter goes | 20 |
| `tests/dataset/gates/phase8.py`, `phase10.py`, `tests/pdfium/leak.py` | the located measures of §4; originals of every length | 150 |

## 3. Tests

Unit (written first and seen red): a page with "EU" twice, one redacted by
range, writes and keeps the other; the same with the other order's two-stretch
range; a located match whose glyphs survive (a fault injected into the surgery)
is refused with surface `location`; an Author "PC" rewritten while "PC" is in
the text writes; the same "PC" left in a custom Info key is refused; an array of
ranges on two pages, overlapping or empty, is refused before anything is
recorded. Dataset: the located member over every file with such a word.

## 4. Gate

| Measure | Threshold |
|---|---|
| located redactions: the output holds exactly one occurrence fewer of the word (pdfkit and PDFium text) | 100 % of the files with a candidate |
| located redactions: the location's rectangles hold none of the word in PDFium's text of the output page | 100 % |
| self-checks passing (known gaps apart), phase 8's job and the located job | 100 % |
| original metadata values of any length in an output's Info, XMP or string objects | 0 |
| phase 8's measures (PDFium leaks 0, byte leaks 0, pixel stability, placeholders) | at their phase 8 thresholds |
| two runs identical | 100 % |

## 5. Kill criterion

A located redaction that leaves its word at the location in PDFium's reading
stops the line (a leak); anything else is a fix in the session.

## 6. Rollback

Revert the merge; the glyph-range form returns to a document-wide needle.

## 7. Execution order inside the phase

1. Unit tests of §3, red. 2. `text_plan` and the API's array form. 3. The
self-check split. 4. The dataset job's located member; a smoke run of about 60
files (`tests/dataset/run.py --id ...` over files with repeated words), once.
5. The full gate once (`run.py --runs 2`, `compare.py --check redact`,
`leak.py --metadata`, `phase8.py`, `phase10.py`). 6. Rebase on `main`, then the
documentation sweep of the owned sections, written from what landed.
7. `python build/check.py --full`, the unit, sanitizer and ABI tiers, once, at
the merge. Between steps run only the unit cases touched (`CLAUDE.md` §8.2).

## 8. Opening prompt

> Read `CLAUDE.md`, `docs/DESIGN.md` §11 and §18, `docs/ABI.md` §4.11 and
> §4.12, then `build-11.md`, and implement it as written on
> `phase/11-located-check` in your own worktree. You own the files of the
> header's Owns row and nothing else; build-12 owns `src/redact/match_finder.*`
> and build-13 the writer. Follow the test rhythm of `CLAUDE.md` §8.2 and stop
> at 5 h 30 with `Build-Phase: 11 incomplete` if the gate is not reached.
> Record every deviation in §9 and in every downstream plan it affects.

## 9. Deviations recorded by this phase

1. **Files outside the Owns row.** The split could not be made inside it:
   `src/redact/text_redactor.{hpp,cpp}` (owned by no session) stop turning a
   glyph range's text into a value, take the array form and record each
   located match (ranges, text, removed glyphs' boxes, codes);
   `src/redact/write_pipeline.cpp` (build-13's) keeps its self-check stage
   but its body is one call to `SelfCheck::verify`, which holds what the
   stage computed, so build-13's added stage call does not meet this change;
   `src/writer/write_report.{hpp,cpp}` write `located` and
   `metadata_values`; `src/api/set_metadata.cpp` drops the three-letter
   floor on originals (the exemption of §0); the two cases of
   `tests/unit/metadata/self_check_metadata_test.cpp` that encoded the old
   contract (an original in the page text refused, a short original not
   searched) now test the new one; `tests/pdfium/pdfium.py` gains the
   `bounded` request (`FPDFText_GetBoundedText`) the location measure reads.
2. **The location is the removed glyphs' boxes**, unpadded, and a glyph is
   there when its box's centre lies in one, not "the occurrence's
   rectangles" of §0.1: those run over the rest of the line after the hole
   and over the placeholder, so a second occurrence on the same line would
   be refused, which is the refusal this phase removes. A kept neighbour's
   box touches a removed one but its centre lies outside; a second text
   layer drawn over the word lies inside. A marked-content span is over the
   place when its `BDC` ... `EMC` encloses a glyph there (the placeholder
   included) or the glyph's `/ActualText` names it.
3. **The located member is nested** in the outcome's `redact_text` as
   `located`, not a key of its own, because `cli/verbs/dataset.cpp` is
   build-12's.
4. **The PDFium count** of §4's first measure is judged over the files where
   PDFium reads the word in the input at all (a word PDFium reads otherwise,
   joined to a neighbour or not decoded, cannot lose an occurrence it never
   had); those files are listed by `phase8.py` as `not_read_by_pdfium`.
   PDFium's text inside the box is read with the box narrowed by a tenth of
   its width at each side, so a touching neighbour is not read.
5. **The unit test "PC left in a custom Info key"** leaves it in a field the
   caller did not rewrite (`/Subject`), because the metadata writer drops a
   key outside the field list that repeats an original, so a custom key
   never survives to be refused.
6. **The caller's written values are cut** out of the metadata surfaces
   before the originals are searched, as the placeholders are: the gate run
   refused pdfbox-0006, whose original Keywords "5" stood in the job's own
   written value "[_5_]". A written value holding an original is the
   caller's own (`docs/DESIGN.md` §18).
7. **The dataset gate ran under load**: three other sessions shared the
   laptop, and `open,text` alone took 54 s on opf-0005, against 8 s for every
   check of it in the run `bs-final`; the 19 files that met the timeout in
   `p11-gate` were run again apart (see the merge commit).
8. **Tests first**: the code of steps 2 and 3 was written before the unit
   tests of §3, which passed on their first run; they were not seen red.
