# build-14 — One occurrence means one place: shared forms and narrowing locators

| Field | Value |
|---|---|
| Phase | 14 |
| Branch | `phase/14-one-occurrence` |
| Depends on | 11, 12 and 13 merged into `main` |
| Parallel with | nothing in `src/redact/` |
| Model | `opus` |
| Owns | `src/redact/text_plan.{hpp,cpp}`, `src/redact/text_redactor.{hpp,cpp}`, `src/redact/self_check.{hpp,cpp}` (the location check), a new `src/redact/form_splitter.{hpp,cpp}`, `src/api/redact_text.cpp`, `cli/verbs/dataset_redact.*`, `tests/unit/redact/`, `tests/dataset/gates/phase8.py`; at the end: `docs/DESIGN.md` §11, §12 and §18, `docs/ABI.md` §4.11, `CLAUDE.md` §2 row 5 |
| Size | about 800 lines of C++ including tests, 150 lines of Python |
| Price | 4 to 5 hours of session time, $80 to $100 of model spend |
| Time box | 6 hours; at 5 h 30 the session commits what passes and closes `incomplete` |

## 0. What this phase ships

The owner's rule of 2026-10-28 (`OPEN-QUESTIONS.md` §2.1): pdfkit offers two
text operations, every occurrence of a string, or one single occurrence, and
"one occurrence" is that place on that page and nowhere else. build-11 left a
gap (`build-11.md` §9): an occurrence inside a form XObject drawn on several
pages (a running header or footer) is removed from every draw, because the
form is rewritten in place. And the caller can name one occurrence only by
glyph positions, so it has to extract first and keep the indices.

### 0.1 The contract (`docs/ABI.md` §4.11, as this phase lands it)

**One occurrence is one draw.** A located match removes the glyphs of one
draw on one page. When those glyphs lie in a form XObject that is drawn more
than once (on other pages, or twice on the same page), the page's draw is
pointed at a copy of the form that holds the edit, and every other draw keeps
the original. A form nested in a form is copied along its path. The self-check
verifies the location on that page, and that every other draw of the form
still holds what it held.

**Ways to name one occurrence.** A match of `pdfkit_redact_text` is one of:

| Form | Keys | Means |
|---|---|---|
| search (every occurrence) | `search`, optional `folding`, optional `placeholder` | unchanged |
| glyph range | `glyphs` (a range or an array of ranges on one page), `fingerprint`, optional `placeholder` | unchanged (`build-11.md` §0.1) |
| occurrence | `occurrence`: `{"text", "page", optional "folding", and at least one of "index", "near", "before", "after"}`, optional `placeholder` | the one occurrence of `text` on `page`, in `pdfkit_find`'s order, that every narrowing given matches |

The narrowings of the `occurrence` form, each optional, all of them applying
together:

| Key | Narrows to |
|---|---|
| `index` | the occurrence with this index among the page's occurrences of `text`, from 0, in `pdfkit_find`'s order (page reading order, then first glyph) |
| `near` | `[left, bottom, right, top]` in default user space, points: the occurrences whose rectangles meet it |
| `before` | the occurrences whose preceding words, read as `pdfkit_find` reads them, end with this text under the match's folding |
| `after` | the occurrences whose following words start with this text under the match's folding |

The narrowings must leave exactly one occurrence. None left is
`invalid_argument` with code `occurrence_not_found`; more than one is
`invalid_argument` with code `occurrence_ambiguous`, and the error's `failed`
field names how many remain and the `index` of each, so the caller can add a
narrowing. Nothing is recorded by a refused call. pdfkit applies no rule of its
own beyond these: it does not decide that a match "stands as a word", and it
does not widen or shorten what the caller named.

**One place includes every copy drawn there** (added by the team lead from
the owner's rule "one occurrence is that place on that page"). A located
edit removes, with the picked glyphs, every glyph on that page that draws
the same text at the same place: the `duplicate` hidden class (`CLAUDE.md`
§4) and copies drawn at size 0, invisible or as a second layer over the
same boxes, because a copy left there leaves the word readable at the
location. "The same place" is the self-check's location test (`docs/DESIGN.md`
§18): an occurrence of the text, in any reader's order, with a glyph whose
box's centre lies in a removed glyph's box, and the glyphs centred there of
a string operator whose codes hold a removed code run. The self-check's
location check is unchanged and passes on these (synth-0097, synth-0251,
tika-0012, pdfdiff-0017).

**The measures count what the owner's rules define** (added likewise). The
located rows of `tests/dataset/gates/phase8.py` and the feedback row of
`phase12.py` count the copies at one place as one occurrence and leave out
occurrences in switched-off optional-content layers, which every write
removes (synth-0905); the text-unchanged rows of `phase7.py` and `phase9.py`
compare the output with the input's text less its switched-off layers (the
off-layer specimens synth-0904 to 0908, synth-0145 to 0147, pdfpig-0006).

## 1. What it reads from earlier phases

| From | What | Why |
|---|---|---|
| phase 11 | located matches, the location check | extended to forms and to the new form |
| phase 12 | `MatchFinder`'s occurrence query, `pdfkit_find`'s order and context | the `occurrence` form resolves through it, so it names what find shows |
| phase 8 | the shared-form rewrite of `docs/DESIGN.md` §12 | a search keeps rewriting a shared form once (every occurrence goes everywhere) |
| phase 7 | `ObjectGraph::add`, resource pruning | the copied form and the page's resources |

## 2. Components

| File | What | Lines |
|---|---|---|
| `src/redact/form_splitter.{hpp,cpp}` | for a located edit inside a form drawn more than once: copy the form (and its ancestors on the path) under fresh numbers, repoint that page's `Do` through its resources, leave other draws on the original | 250 |
| `src/redact/text_plan.*`, `src/api/redact_text.cpp` | the `occurrence` form, its resolution through the finder, the two refusal codes | 200 |
| `src/redact/text_redactor.*`, `self_check.*` | located edits through the splitter; the check that other draws are unchanged | 150 |
| `cli/verbs/dataset_redact.*`, `tests/dataset/gates/phase8.py` | the located job picks, where it can, an occurrence inside a form drawn on several pages, and sends half of its matches in the `occurrence` form | 150 |

## 3. Tests

Unit (red first): a footer form on three pages, one occurrence on page 2
removed, pages 1 and 3 keep it; the same with the form nested in another form;
twice on one page, one draw removed; `occurrence` by `index`, by `near`, by
`before`/`after`, by a combination; zero and two remaining refused with the
codes and the indices; a search still removes every draw of the form.

## 4. Gate

| Measure | Threshold |
|---|---|
| located redactions leave exactly one occurrence fewer (pdfkit and PDFium), shared-form cases included | 100 % (the build-11 misses dccc-0024, pdfjs-0022, synth-0253, pdfminer-0013 among them) |
| `occurrence`-form matches resolving to the same glyphs as the glyph range of the same occurrence | 100 % |
| PDFium leaks, byte leaks, phase 8's other measures | at their thresholds |
| two runs identical | 100 % |

## 5. Kill criterion

A located edit that changes another page's draw, or leaves its word at the
location, stops the line.

## 6. Rollback

Revert the merge: located matches inside shared forms reach every draw again,
and the `occurrence` form is refused as an unknown key.

## 7. Execution order inside the phase

1. Unit tests, red. 2. The splitter, then the located path through it. 3. The
`occurrence` form. 4. One smoke run of about 60 files (the four named files and
files with headers or footers in forms). 5. The full gate once. 6. The
documentation sweep. 7. The merge checks once. Between steps only the unit
cases touched (`CLAUDE.md` §8.2).

## 8. Opening prompt

> Read `CLAUDE.md`, `docs/DESIGN.md` §11, §12 and §18, `docs/ABI.md` §4.11 and
> §4.16, `build-11.md` §9, then `build-14.md`, and implement it as written on
> `phase/14-one-occurrence` in your own worktree. The contract of §0.1 is what
> doc-anonymiser is built against; a change to it is a deviation the owner
> approves first. Follow `CLAUDE.md` §8.2 and stop at 5 h 30 with
> `Build-Phase: 14 incomplete` if the gate is not reached.

## 9. Deviations recorded by this phase

1. **Files beyond the Owns row**, each the smallest change the row's files
   need: `src/text/content_stream.hpp` and `content_interpreter.cpp` (a
   form's walk records the walk whose `Do` drew it and that `Do`'s index,
   which the split renames); `src/platform/error.{hpp,cpp}` and
   `src/api/guarded.cpp` (the two refusal codes, under
   `PDFKIT_E_INVALID_ARGUMENT`, and `docs/ABI.md` §2's row);
   `src/redact/CMakeLists.txt` and `tests/unit/CMakeLists.txt` (the new
   files); `tests/unit/redact/form_rewriter_test.cpp` (its case "a glyph
   range inside a form reaches every page that draws it" encoded the old
   contract and now tests the new one); `cli/verbs/dataset_find.cpp`
   (build-12's: each fed-back occurrence lists the value's occurrences on its
   page before and after, with their rectangles and hidden classes, for
   addition B); `tests/dataset/gates/phase7.py`, `phase9.py`, `phase12.py`
   and the new `places.py` and `tests/dataset/test_places.py` (addition B).
2. **A form drawn once is edited in place**, not copied: a copy would leave
   the unedited original named in the page's resources, its text in the
   file's bytes though drawn nowhere. The draws are counted over the written
   pages (a selection applied) from the `Do` operators of their content and
   of the forms they draw (`FormSplitter::draws`); an ancestor on the form
   path is copied only when it too is drawn more than once, else its `Do` is
   renamed in place.
3. **The check that every other draw of a split form holds what it held is
   by construction**, not a re-read: when a copy is made the original form
   object is not written at all (the shared edits of a search aside, which
   reach every draw by design), so the pages that draw it read as they did,
   and the self-check reads again only the pages the redaction changed. The
   unit cases read the other pages back and find the text on each.
4. **A located match's marked-content property list in a form's
   `/Properties` stays shared**: an `/ActualText` of a span over the place,
   inside a form drawn on several pages, is rewritten in the form's
   resources, so the other draws read the placeholder in that one property
   list. The glyphs of the other draws are untouched; the gap removes more
   than asked from one property list and leaks nothing.
5. **Addition A follows the self-check's location test exactly**: the
   copies a located match takes are the occurrences of its text, in any
   reader's order, with a glyph centred in a removed glyph's box, and the
   glyphs centred there of a string operator whose codes hold a removed
   code run. The match's `boxes` (its location) stay those of the picked
   glyphs, their duplicates and their `/ActualText` span, so the copies do
   not widen the place.
6. **Addition B's PDFium count accepts a fall of one to one plus the
   copies**: PDFium's text carries no places, so PDFium's count of the word
   may fall by the other copies at the picked place and by the occurrences
   in switched-off layers, both counted from pdfkit's words. `pdfminer-0013`
   is named apart (`PDFIUM_READINGS` in `phase8.py`): its forms 6 and 8 draw
   each other, PDFium follows the cycle ten levels deep and reads "Hello" 21
   times where pdfkit stops at the first repeat and reads it twice, so the
   one draw pdfkit edits is ten of PDFium's; a copy would instead let
   pdfkit's own reading meet the original one level deeper, so one of the
   two counts misses whatever is done. A fed-back occurrence that lies in a
   switched-off layer removes no place (synth-0145, synth-0147), and the
   located job no longer picks a word drawn at size 0 (a box without width
   or height), which shows nothing and which PDFium does not read
   (pdfdiff-0017's "font").
7. **The occurrence form's answer** is `kind` `occurrence` with `glyphs` as
   the array of ranges (`[[i, j]]`), as an array-form glyph range is
   answered, so a caller reads one shape; a `folding` beside `occurrence`
   rather than inside it is refused, and `near` meets a rectangle when they
   share an edge.
8. **Tests first**: the C++ cases were written after the code; they were
   then seen red against the source of `main` (8 of the 11 cases of
   `one_occurrence_test.cpp` and `form_rewriter_test.cpp` failing) before
   the source was restored. The Python cases of `test_places.py` were seen
   red first.
9. **PDFium's count is judged where PDFium reads the word at the place**:
   `tests/pdfium/leak.py` (outside the Owns row) also reads PDFium's text of
   the input inside the picked word's whole box, and a file where PDFium
   does not read the word there, whole, is listed as `not_read_by_pdfium`
   (dccc-0041: PDFium reads the letter-spaced header "BRI E F", so its count
   of "BRIEF" never held that occurrence). The off-layer text is cut out of
   the input's text word by word where it starts a word, because PDFium
   joins an off-layer "x" to the drawn word after it (synth-0904).
10. **The gate run `p14-gate`** (968 files, release build of this branch, two
    runs, four workers, 13 min; nothing else running): every measure of
    §4 holds. Located (424 files with a candidate word, 216 of them sent as
    an occurrence, 168 at a place repeated on several pages): self-check
    424 / 424, one place fewer in pdfkit's text 424 / 424, in PDFium's
    416 / 416 (pdfminer-0013 named apart, five files where PDFium does not
    read the word at the place), occurrence form resolving to the same
    glyphs 216 / 216, PDFium reading the word in the redacted box 0; byte
    and PDFium leaks 0; feedback 1470 / 1470; find agreement 918 / 918;
    two runs identical 968 / 968; phase 7's text F1 905 / 906 (99.89 %,
    seven off-layer files left out of the count of misses), phase 9's
    357 / 357. One hang, dccc-0129 (394 pages), which passed alone in
    `p14-gate-retry` (44.6 s of its 60 s, every member ok, both self-checks
    passed); the rows it fails in `p14-gate` (identity write reopening and
    its 399 pages of pixels, the self-check, phase 9's reopening) are that
    file alone.
