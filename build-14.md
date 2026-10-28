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

1. *(none yet)*
