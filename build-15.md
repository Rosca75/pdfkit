# build-15 — ABI 2: what doc-anonymiser asked of the literal tool

| Field | Value |
|---|---|
| Phase | 15 |
| Branch | `claude/sleepy-meitner-62ahgh` (the session's designated branch; it merges into `main` through its pull request, see §8) |
| Depends on | 14 merged into `main`, `v0.1.0-preview` released |
| Parallel with | nothing in `src/redact/`, `src/api/` or `src/platform/unicode/` |
| Model | `opus` |
| Owns | `src/platform/unicode/folding.{hpp,cpp}`, `src/redact/match_finder.{hpp,cpp}`, `src/redact/text_plan.{hpp,cpp}`, `src/redact/text_redactor.cpp`, `src/redact/self_check.cpp`, a new `src/redact/property_strings.{hpp,cpp}`, a new `src/api/folding_argument.{hpp,cpp}`, `src/api/find.cpp`, `src/api/redact_text.cpp`, `src/api/check.cpp`, `src/api/lifecycle.cpp`, `cli/verbs/dataset_*.cpp`, `tests/unit/`, `tests/abi/`; at the end: `docs/ABI.md` §1, §4.1, §4.11, §4.12, §4.16 and §5, `docs/DESIGN.md` §11, §12 and §18, `CLAUDE.md` §1 and §3.3, `README.md`, `OPEN-QUESTIONS.md` |
| Size | about 1,200 lines of C++ including tests, 60 lines of Python and C# |
| Price | 6 to 8 hours of session time, $120 to $160 of model spend |
| Time box | 10 hours; at 9 h the session commits what passes and closes `incomplete` |

## 0. What this phase ships

doc-anonymiser (https://github.com/Rosca75/doc-anonymiser-for-.NET), built
against `v0.1.0-preview` (ABI 1), asked for seven things under one principle
its owner set: **doc-anonymiser owns every matching rule; pdfkit removes
literally what it is given, reports what it sees and applies no rule it was
not asked for.** Six change the library and land together as ABI 2
(`pdfkit_abi_version` returns 2), released as `v0.2.0-preview` with the
Windows x64 package attached; the seventh is a question answered in
`docs/DESIGN.md`.

### 0.1 The contract (`docs/ABI.md`, as this phase lands it)

**1. Glued letters.** Each occurrence of `pdfkit_find` gains two strings,
after `after`: `glued_before` and `glued_after`, the characters of the word
or words the occurrence lies inside that precede its first glyph and follow
its last, as the extraction reads them; empty when the occurrence starts or
ends a word. In the reading order a word is a word of `pdfkit_text` (a word
joined across a line by a hyphen is the two words the extraction writes); in
another order (`drawing`, `rows_ltr`, `rows_rtl`) they hold the glyphs that
order reads next to the occurrence up to its first word gap on each side.
Measured on the specimen of the request ("The EU and the neutral Europe
agreed."): the value `EU` (with case folding) finds the word with `""` and
`""` and the `eu` of "neutral" with `"n"` and `"tral"`. pdfkit decides
nothing with them.

**2. Folding is opt-in.** A folding object that is absent, or a flag left out
of one, folds nothing: `case`, `diacritics`, `whitespace` and `hyphenation`
default to `false` in `pdfkit_find`, in the search and occurrence forms of
`pdfkit_redact_text` and in `pdfkit_check`. A search with no folding matches
the page text code point for code point (the ligature decomposition of
`docs/DESIGN.md` §5.5 and the reading of a positional Arabic form as its
letters stay, because they are how the extraction reads the glyphs, not a
folding). The internal searches pdfkit runs for itself (the location test of
a located match, the self-check's metadata originals) keep every folding on,
because they verify rather than remove what a caller named.

*Decision recorded*: this changes what an existing call means, which
`docs/ABI.md` §5 forbade within an ABI version. The session judged the change
right and in scope: it is the owner's principle applied to the one place
pdfkit chose a rule for its caller, the only caller sends all four flags and
is unaffected, and nothing was released but a preview. It lands as ABI 2,
and §5 and `CLAUDE.md` §3.3 now say how a meaning may change: only on the
owner's order, in a raised ABI version, listed in §5 with that version.

**3. Character classes from the caller.** A `folding` object may hold
`equivalents`, an array of classes, each an array of single characters
(strings of one code point) that match one another, and `ignore`, an array
of single characters read as absent. A character of a class reads as the
class's first character before the other foldings apply; an ignored
character is dropped on both sides before anything else. They apply
wherever the folding applies: `pdfkit_find`, the search and occurrence forms
(and an occurrence's `before` and `after`), `pdfkit_check`, the self-check of
a search, and the marked-content strings a search rewrites. Refused as
`invalid_argument`: a member that is not one code point, a character in two
classes or in a class and in `ignore`, an empty class, and a class or
`ignore` that is not an array of strings. The byte scans of the codes (a
search's `find_codes`, the self-check's stream scan) try every spelling the
classes allow, up to 64 spellings per string, and do not skip ignored
characters, because they look for bytes a producer wrote in one piece.
With `{"equivalents": [["'", "’"]]}`, `O'Brien` finds `O’Brien`.

**4. The tie of the overlap rule.** Between overlapping occurrences with
different placeholders the earlier-starting one keeps the overlap; when two
start on the same glyph, the one that ends later keeps it (it covers the
other whole, so the shorter draws nothing); when they also end on the same
glyph, the match recorded first keeps it: a match of an earlier call before
one of a later call, and within a call the earlier entry of `matches`. So the
searches `Contoso` and `Contoso Group` on "Contoso Group" draw the
placeholder of `Contoso Group` whatever order they come in.

**5. The version text.** `docs/ABI.md` §1 and §4.1 name the version
`pdfkit_version` reports, label included: `0.2.0-preview` for this build
(`0.1.0-preview` was released as such).

**6. Occurrences in marked-content strings, and pointing at one.** Each value
of `pdfkit_find` gains `strings`, after `occurrences`: every occurrence of the
value, under its folding, in a string of a marked-content property list of
the pages asked (a `BDC` or `DP` operator, inline or named through
`/Properties`; the `/OC` lists, which every write removes, left out), the
strings a search rewrites and the write keeps. `/Alt` is surface `alt`, `/E`
`expansion`, `/ActualText` `actual_text` (only where no glyph reads it as its
text, because a glyph that does is in `occurrences` already) and any other
string at any depth `property`. Each entry:

```json
{"occurrence": 0, "page": 1, "list": 0, "surface": "alt", "path": ["Alt"],
 "stream": 0, "operator": 3, "properties": null, "object": null,
 "string": "Photo of Smith", "first": 9, "last": 14, "text": "Smith",
 "glued_before": "", "glued_after": ""}
```

`list` is the property list's index among the page's lists in the order the
content streams are read (forms where they are drawn); `path` the keys and
array indices from the list to the string; `stream` 0 for the page's content
or the form's object number; `operator` the index of the `BDC` or `DP` in
that stream; `properties` the `/Properties` name or `null` when inline;
`object` the list's object number when it is an indirect object; `first` and
`last` code-point offsets in `string` (half-open); `glued_*` the characters
of `string` glued to the occurrence up to white space on each side.

`pdfkit_redact_text` takes one back as a fourth form: `{"string": {"page",
"list", "path", "first", "last"} (the other keys of the find entry accepted
and checked against the document), "fingerprint", optional "placeholder"}`.
It replaces exactly that range of that string by the placeholder, at that
place alone: an inline list is rewritten in its operator; a named list that
no other operator of the written pages names is rewritten where it lives (its
object, or its entry in the `/Properties` that names it), and one that other
operators name too is written inline in this operator with the edit, the
named original kept for the others; a list inside a form drawn more than
once is changed in a copy of the form for this draw (`FormSplitter`), as a
located glyph edit is. The
self-check verifies it on its page: the output page holds the text in its
marked-content strings fewer times than the input page by at least the
number of such edits. Refusals (`invalid_argument`): a fingerprint of
another document, a page outside the document, a `list` the page does not
have, a `path` that names no string, a range that is empty or past the
string, a `text` or `string` that does not match the document.

**7. The question: removing text drawn in render mode 7.** Answered in
`docs/DESIGN.md` §12 from the code and two readers' source, without a change
(§0.2).

### 0.2 What removing a mode-7 glyph does (the answer of item 7)

A glyph shown in mode 7 paints nothing and adds its outline to the clipping
path the text object sets at `ET`. Text surgery removes the glyph's code
like any other and keeps the advance; the placeholder over removed glyphs
that are all invisible is drawn in mode 3, so it adds nothing to the clip.
So:

- **Some of the text object's clip glyphs removed**: the clip at `ET` is the
  union of the kept glyphs' outlines. What is painted afterwards (until the
  `Q` that restores the clip) is no longer painted inside the removed
  glyph's outline: the letter's shape disappears from the image or shading
  seen through the letters, and the page's background shows there.
- **Every clip glyph of the text object removed**: the operators that showed
  them become `TJ` of one number, so the text object shows no glyph in a
  clip mode. PDFium (`CPDF_StreamContentParser::Handle_EndText` and
  `AddTextObject`) and pdf.js (`CanvasGraphics.endText`), read in source,
  then leave the clip as it was before `BT`: what is painted afterwards is
  painted in full over the area the letters restricted it to (an image seen
  only through the word now shows whole). ISO 32000's wording for a text
  object that accumulates no glyph was not read.

The second case can show more of the page than the original did. The session
does not change it, because the request asked for an answer; the fix (close
such a text object with an empty clip, `0 0 m h W n` after its `ET`, so
nothing painted in the clip shows, as the union of no outlines) is put to the
owner in `OPEN-QUESTIONS.md` with its price.

## 1. What it reads from earlier phases

| From | What | Why |
|---|---|---|
| phase 12 | `MatchFinder::locate`, `pdfkit_find`'s order and context | the glued letters are computed beside the context |
| phase 14 | `FormSplitter`, located edits through `PageWork::splits` | a located string edit inside a shared form takes the same path |
| phase 8 | `MarkedContentRewriter`, the self-check's surfaces | the string occurrences are the strings it rewrites |

## 2. Components

| File | What | Lines |
|---|---|---|
| `src/platform/unicode/folding.*` | `Options` gains the classes and the ignored characters; `fold` and `fold_code_point` apply them first | 80 |
| `src/api/folding_argument.*` | one reader of a `folding` object for find, redact and check: the flags (default false), `equivalents`, `ignore`, the refusals | 150 |
| `src/redact/match_finder.*` | haystack caches keyed by the whole folding; byte-form spellings; `glued()` for any order | 200 |
| `src/redact/property_strings.*` | the page's marked-content property lists and their strings (find, the located string edit, the self-check count) | 220 |
| `src/redact/text_plan.*`, `text_redactor.cpp`, `self_check.cpp` | the `string` form: resolution, the inline or fresh-name rewrite, splits, the located count check | 250 |
| `src/api/find.cpp`, `redact_text.cpp`, `check.cpp`, `lifecycle.cpp` | the new fields and form, ABI 2 | 150 |
| `cli/verbs/dataset_*.cpp` | the dataset jobs send their folding explicitly (all four flags), so every gate measures what it measured | 20 |
| `tests/unit/`, `tests/abi/` | below | 500 |

## 3. Tests

Unit (red first): the glued letters on the request's specimen, for a phrase,
at the start and end of a word and in the drawing order; every folding off
when absent, each flag alone; the classes (`O'Brien` finds `O’Brien`, a
space class, an ignored soft hyphen in the page and in the value, a class
member in the codes), and each refusal; the tie of the overlap rule in both
orders and with equal ends; `pdfkit_version` and `pdfkit_abi_version`; a
`/Alt` in an inline list, a named list and a form drawn twice, found with its
offsets and glued letters, removed by a `string` match at that place alone
while a second list keeps its copy, and each refusal; a mode-7 glyph
removed, alone and with all its text object's clip glyphs, written as §0.2
says. ABI tier: the folding default and the glued fields from Python and
.NET, `pdfkit_abi_version() == 2`.

## 4. Gate

| Measure | Threshold |
|---|---|
| the dataset tier over every manifest file the session's machine holds, each expectation as on `v0.1.0-preview`, the dataset jobs sending all four flags | no file regresses |
| two runs identical | 100 % |
| find's glued letters: every reading-order occurrence of the find job whose `glued_before + text + glued_after` holds no space is a word of the page's `pdfkit_text` | 100 % |
| find's string occurrences sent back as `string` matches: written, self-check passed, one occurrence fewer on that page and no other page changed | 100 % of the files whose pages hold a marked-content string |

The PDFium and panel measures do not move in this phase (no reading, layout
or writing rule changes for a caller that sends its flags, which every
dataset job does); they are not re-run.

## 5. Kill criterion

A dataset expectation that regresses with every flag sent explicitly stops
the line: it means the folding rewrite changed the folding itself.

## 6. Rollback

Revert the merge: ABI 1 comes back with its folding defaults on, and the new
keys are refused as unknown.

## 7. Execution order inside the phase

1. This plan. 2. Unit tests, red. 3. Folding (items 2 and 3), the overlap
tie, the glued letters, then the string occurrences. 4. The CLI's dataset
jobs. 5. The full unit tier, the ABI tier, the dataset gate once. 6. The
documentation sweep. 7. The merge checks once.

## 8. Deviations

- The session runs in a Linux cloud container, not on the owner's laptop:
  the unit tier and the Python ABI tier run there with the system GCC 13 and
  CMake 4.3.2 from PyPI (the preset `mingw-debug` configured with
  `-DPDFKIT_LINKER=default -DCMAKE_POSITION_INDEPENDENT_CODE=ON`), and the
  checks tier, the unit tier and the ABI tier from .NET and Python run with
  the pinned toolchain on the hosted Windows runner of
  `.github/workflows/check.yml` on every push. The sanitizer tiers need the
  laptop's llvm-mingw cache and are not run.
- The dataset files are fetched into the container by
  `dataset/tools/fetch.py`, `synth/run.py` and `mutate.py`; the seven
  `office` rows, made on the laptop, are absent, and the Liberation archive
  came from Fedora's lookaside cache, whose copy has the pinned SHA-256,
  because the proxy refuses GitHub's attachment URL.
- Item 6 as built: a `string` match accepts the find entry's `occurrence`
  key without checking it (it indexes one value's answer, which the document
  does not hold); references inside a property list are followed when its
  strings are read (32 deep, 65,536 items per list); an `/ActualText` is left
  out only when a glyph names its span, so an inner `/ActualText` under an
  outer one is reported.
- The branch merges through a pull request rather than a local merge
  (`CLAUDE.md` §11), because the session may push only its designated
  branch; the tag `v0.2.0-preview` is pushed on the merged tip, which
  `.github/workflows/release.yml` packages.
