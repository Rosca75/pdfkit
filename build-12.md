# build-12 — Find occurrences

| Field | Value |
|---|---|
| Phase | 12 |
| Branch | `phase/12-find` |
| Depends on | `main` with `chore/owner-answers` merged; `chore/hygiene` merged before the dataset step (it changes `tests/dataset/run.py`) |
| Parallel with | 11 and 13 (no code file shared; the shared documents are edited last, see §7) |
| Model | `opus` |
| Owns | `include/pdfkit/pdfkit.h` (one declaration), `src/api/find.cpp`, `src/api/catalogue.cpp` (one row), `src/redact/match_finder.{hpp,cpp}` (additive), `cli/verbs/find.cpp`, `cli/main.cpp` (one verb), `cli/verbs/dataset_find.{hpp,cpp}`, `cli/verbs/dataset.cpp` (one member), `tests/unit/redact/match_finder*`, `tests/unit/api/find*`, `tests/abi/` (the new symbol's cases), `dataset/manifest.tsv` (one column), `tests/dataset/run.py` (one column mapping), `tests/dataset/gates/phase12.py`; at the end, after rebasing on `main`: `CLAUDE.md` §2 (row 11 and the `find` verb), `docs/ABI.md` §4.2 and a new §4.16, `docs/DESIGN.md` §11, `tests/dataset/outcome.md`, `dataset/README.md` |
| Size | about 900 lines of C++ including tests, 250 lines of Python |
| Price | 4 to 5 hours of session time, $80 to $100 of model spend, calibrated from phase 8's match finder and phase 10's metadata function |
| Time box | 6 hours; at 5 h 30 the session commits what passes and closes `incomplete` with what is left |

## 0. What this phase ships

A catalogue row the caller uses before it edits anything: given values, every
place each occurs, so doc-anonymiser can show counts and let its user choose
one occurrence ("this EU, not the others") and send it back as a located match
(`build-11.md` §0.1). One rule finds both: the occurrences `pdfkit_find`
reports for a value are exactly those `pdfkit_redact_text` would remove for the
same search, because two searches that disagree make the count shown to the
user a lie.

Catalogue row of `CLAUDE.md` §2 added (§3.6):

| # | Function | C ABI symbol | Inputs | Outputs | Fails when | Build file |
|---|---|---|---|---|---|---|
| 11 | Find | `pdfkit_find` | document handle; JSON values, each with the caller's folding rules; options (pages, context words) | JSON per value: the occurrences in every order a reader reads, each with page, glyph ranges, rectangles, hidden classes and context; the document fingerprint | a value with an empty text; an unknown folding or key; a page out of range or listed twice; a limit met while reading a page (`limit_exceeded`) | `build-12.md` |

CLI verb `find`: calls 1.1 and 11, writes JSON on standard output, no file.

### 0.1 The contract (`docs/ABI.md` §4.16, as this phase lands it)

```c
PDFKIT_API pdfkit_status pdfkit_find(pdfkit_document* doc, const char* values_json,
                                     pdfkit_buffer* out);
```

`values_json`:

```json
{"values": [{"text": "EU", "folding": {"case": true, "diacritics": true,
                                        "whitespace": true, "hyphenation": true}}],
 "pages": [1, 2, 3],
 "context": 5}
```

| Key | Default | Meaning |
|---|---|---|
| `values` | required | one entry per value; `text` is UTF-8, non-empty; `folding` as in §4.11 (each flag `true` by default) |
| `pages` | every page | page numbers from 1, each at most once |
| `context` | 5 | words of context on each side, 0 to 50 |

Answer:

```json
{"fingerprint": "83ce7c...",
 "values": [{"value": 0, "text": "EU", "count": 2,
   "occurrences": [
     {"occurrence": 0, "page": 3,
      "glyphs": [{"page": 3, "first": 120, "last": 122}],
      "text": "EU", "order": "reading",
      "rectangles": [[70.17, 696.26, 84.8, 708.85]],
      "hidden": [],
      "before": "the member states of the", "after": "agreed on the following"}]}]}
```

| Key | Holds |
|---|---|
| `fingerprint` | the document fingerprint of `pdfkit_text` |
| `value`, `text`, `count` | the value's index in the call, its text as given, its occurrences (zero is an answer) |
| `occurrence` | the index within the value, in page order then first glyph |
| `glyphs` | the ranges in the page's glyph list of `pdfkit_text`; one range normally, several when the occurrence was found only in the drawing or a row order; passed back unchanged as a located match of `pdfkit_redact_text` with this `fingerprint` |
| `text` | the occurrence as the extraction reads it |
| `order` | `reading`, `drawing`, `rows_ltr` or `rows_rtl`: the first order that found it |
| `rectangles` | the device boxes of its glyphs, `[left, bottom, right, top]`, 0.01 pt |
| `hidden` | the hidden classes (`CLAUDE.md` §4) of any of its glyphs |
| `before`, `after` | up to `context` words of the page's reading-order text on each side |

Every key is written in the order shown; two calls give the same bytes. A
value found inside a longer word is an occurrence (`CLAUDE.md` §5): deciding
whether it stands as a word is the caller's.

**The integration sequence doc-anonymiser follows:** `pdfkit_open` →
`pdfkit_text` (to show the text) and `pdfkit_find` (values and their spelling
variants, counts and places) → `pdfkit_edit_new` → `pdfkit_redact_text` (each
value as a `search`; each single expression the user picked as `glyphs` with
the `fingerprint`) → `pdfkit_redact_images` and `pdfkit_set_metadata` as needed
→ `pdfkit_write` (refused output = status 3 with the surface named) →
optionally `pdfkit_check` on the bytes → `pdfkit_buffer_free`,
`pdfkit_edit_free`, `pdfkit_close`.

## 1. What it reads from earlier phases

| From | What | Why |
|---|---|---|
| phase 8 and `fix/phrase-search` | `redact::MatchFinder` and its four orders | the one rule both functions apply |
| phase 5 | the glyph list, the reading-order text, hidden classes | positions, context and classes |
| phase 10 | the ABI tier (`tests/abi/`, .NET and Python) | the new symbol is called from both |

## 2. Components

| File | What | Lines |
|---|---|---|
| `src/redact/match_finder.{hpp,cpp}` | an occurrence query that returns ranges, order and rectangles without touching an edit plan; `pdfkit_redact_text`'s search calls the same function | 150 |
| `src/api/find.cpp`, `src/api/catalogue.cpp`, `include/pdfkit/pdfkit.h` | the symbol, its JSON in and out, the registry row | 250 |
| `cli/verbs/find.cpp`, `cli/main.cpp` | the verb | 80 |
| `cli/verbs/dataset_find.*`, `cli/verbs/dataset.cpp` | the dataset member: the job's seeded words through `find`, then each occurrence fed back as a located match on a fresh edit | 150 |
| `tests/dataset/gates/phase12.py`, manifest column `expect_find` (`agree`, `known_gap:<reason>`, `n/a`) | the gate | 200 |

## 3. Tests

Unit (red first): a value inside a longer word is found; a value found only in
the drawing order reports two ranges and `order` `drawing`; a hidden
occurrence carries its classes; the context stops at the page's edges; find's
occurrences equal the redact search's for the same value on the builder's
fixtures; a range from find, sent back, removes exactly that occurrence
(needs build-11 merged; until then the case is skipped by name). ABI tier:
`pdfkit_find` from .NET and Python on the tier's files.

## 4. Gate

| Measure | Threshold |
|---|---|
| files where find's occurrences equal `pdfkit_redact_text`'s for the job's words (same ranges) | 100 % |
| occurrences fed back as located matches that remove exactly one occurrence | 100 % (with build-11 merged) |
| two runs identical | 100 % |
| crashes, hangs, anything but a status | 0 |
| time of the find member against the text member, whole dataset | at most 1.5 times |

## 5. Kill criterion

None of its own; a disagreement between find and the redaction search is a
defect fixed in the session.

## 6. Rollback

Revert the merge: the row, the symbol, the verb and the column leave together.
The ABI only grows, so after a release the symbol stays.

## 7. Execution order inside the phase

1. Unit tests, red. 2. The `MatchFinder` query. 3. The symbol, the registry
row, the verb, a provisional `CLAUDE.md` §2 row so `build/check_catalogue.py`
passes. 4. The dataset member and gate; one smoke run of about 60 files.
5. Rebase on `main` (build-11 and `chore/hygiene` merged), the feedback case
enabled; the full gate once. 6. The documentation sweep of the owned
sections. 7. The merge checks once. Between steps only the unit cases touched.

## 8. Opening prompt

> Read `CLAUDE.md` (§2, §3.6), `docs/ABI.md` §4.9 and §4.11, `docs/DESIGN.md`
> §11, then `build-12.md`, and implement it as written on `phase/12-find` in
> your own worktree. The contract of §0.1 is fixed: doc-anonymiser is coded
> against it; a change to it is a deviation the owner approves first. You own
> the files of the Owns row; build-11 owns the self-check and build-13 the
> writer. Follow `CLAUDE.md` §8.2 and stop at 5 h 30 with
> `Build-Phase: 12 incomplete` if the gate is not reached.

## 9. Deviations recorded by this phase

1. **Files beyond the Owns row**, each the smallest change the row's files
   need: `src/redact/text_redactor.cpp` (one hunk: the redaction's search
   calls `MatchFinder::occurrences`, the flattened `locate`, instead of
   assembling the three searches itself, so §0's one rule is one function);
   `src/api/CMakeLists.txt`, `cli/CMakeLists.txt`, `cli/verbs/verbs.hpp`,
   `tests/unit/CMakeLists.txt` (the new sources); `tests/unit/api/abi_test.cpp`
   (the symbol in the every-catalogue-symbol table); `dataset/tools/common.py`,
   `add.py`, `tests/test_common.py` and `tests/dataset/test_run.py` (the
   column's vocabulary, default and harness tests); `CLAUDE.md` §7.1 (the
   column list) and `docs/ABI.md` §6 (the verb's row).
2. **The contract's shape is unchanged; four of its rows are made precise**
   in `docs/ABI.md` §4.16: an occurrence found in the codes of one string
   operand reports `order` `drawing` (the order those codes are drawn in);
   the `text` of an occurrence of several ranges is its glyphs in the order
   that found them, a word gap read as one space; `rectangles` are the glyph
   boxes united per reading line, unpadded (the redaction's own rectangles
   stay padded); `before` and `after` are the words wholly before the first
   glyph and wholly after the last.
3. **The time measure** compares the `pdfkit_find` call with the text
   member's `pdfkit_text` call, both timed by the dataset verb and written to
   `<run>/outputs/<id>.find-time.json`, because an outcome holds no time.
4. **The feedback member** sends back at most two occurrences per file (the
   first of each of the first two values found), with an empty placeholder
   so nothing drawn spells a value again, and searches only the changed page
   of the output, so the member stays inside the verb's time limit.
5. **The feedback measure waits for build-11.** Until `pdfkit_redact_text`
   takes the array form of `glyphs`, the member reports `unsupported`, the
   gate reports the measure as not run, and the unit case is skipped by name.
   Checked on a throwaway local merge of `phase/11-located-check` (never
   committed): the unit case passes, and eight dataset files fed back 15 of
   15 occurrences exactly.
6. **The gate run `phase12-gate`** (2026-10-28, 963-row manifest before the
   rebase on the office rows, `--checks open,text,find --runs 2 --workers 2`,
   three other sessions building on the laptop): agreement 901 / 901, two
   runs identical 901 / 901, no crash and no sanitizer report; feedback not
   run (item 5); 14 hangs, all near the 30 s limit under load and the files
   that hang in the other phases' runs of the same day (`p11-gate` lists 7 of
   them, `hy-final` 4); time of the find call against the text member's 1.59.
   The 1.59 came from pages where the other orders were built for a value
   none of their glyphs can spell; the finder now rules those orders out
   from the characters the glyphs fold to before building them (exact, the
   unit cases unchanged). The gate was not run again inside the time box;
   the two calls timed one process each over the same 901 files read 0.97
   (896 s against 921 s, cold reads both, so an indication and not the
   gate's measure). Left for the next run, after build-11 merges: the full
   gate once, with the feedback case's skip removed.
