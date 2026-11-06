# CHANGE-02: A replacement alphabet wide enough for masks and realistic text, and a black box in place of the removed glyphs

| Field | Value |
|---|---|
| Status | implemented on the branch claude/inspiring-ptolemy-6ahsex, awaiting the merge into `main` |
| Owner approval | 2026-11-05, through `doc-anonymiser-for-.NET:docs/change-18.md` (D2: build ABI 3 and merge it to `main`; the tag and the release stay the owner's) |
| Depends on | nothing |
| Supersedes | nothing |
| Superseded by | nothing yet |
| Batches | one, this file |
| `SessionVersion` | not applicable. The C ABI moves from 2 to 3 (`docs/ABI.md` §5): inputs and outputs only grow, and no meaning changes |
| Behaviour change | yes: `pdfkit_redact_text` accepts a placeholder over a wider alphabet and a per-match `style` of `box`; the self-check stops cutting placeholder text out of the surfaces it searches |
| New dependency | none; the placeholder subset of the vendored Liberation Sans grows |
| Minimum model | `opus` |

## 0. Context

pdfkit is the stand-alone C++20 library with a plain C ABI that doc-anonymiser
calls for every PDF. Read `CLAUDE.md` (it overrides everything), then
`docs/DESIGN.md` §12, §13, §14 and §18, `docs/ABI.md` §4 (`pdfkit_redact_text`)
and §5, and `build/fonts/subset.py`.

### 0.1 What this order is

A feature. doc-anonymiser gains four anonymisation modes, and its PDF copy
needs three things pdfkit cannot do today: draw a run of `*` (masking), draw
realistic text with lower case, spaces, punctuation and accented letters
(randomisation), and paint a black box where the glyphs were removed
(redaction). The glyph removal itself is unchanged, so the leak rule is
unchanged: no box ever covers live text.

### 0.2 Ground rules

- `CLAUDE.md` holds: one interpreter, the layers, no global mutable state,
  determinism, every external fact sourced, comments in the present tense
  naming no phase or change order, warnings are errors.
- The ABI only grows: new optional fields, a raised `pdfkit_abi_version`.
- A test written first fails first.

### 0.4 Where the work stands

| Fact | Value |
|---|---|
| Branch | claude/inspiring-ptolemy-6ahsex, merged locally into `main` once green (`CLAUDE.md` §11), with `Build-Phase` trailers not applicable (this is a change order, not a phase) |
| Build on Linux | `cmake -S . -B <out> -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DPDFKIT_COMPILER_CACHE=OFF -DCMAKE_C_FLAGS="-D_GNU_SOURCE -DOPJ_HAVE_POSIX_MEMALIGN" -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++`, then `cmake --build <out> --target pdfkit_tests` (about 5 minutes on 4 cores) and `<out>/pdfkit_tests --no-version` (685 cases, 0 failed at the base) |
| Checks that run on Linux | `python3 build/check_layout.py`, `check_headers.py`, `check_comments.py`, `check_catalogue.py`, `check_network.py`, `check_repo.py`, `check_paths.py`; `check_generated.py` with fontTools 4.67.0 on PATH (`pip install fonttools==4.67.0` in a venv); `check_format.py --no-tidy` with clang-format 23.1.3 from PyPI installed without hashes. `check.py --fast` does not run (CMake 3.28 cannot read the presets) and `check_third_party.py` reports five tree-hash mismatches that predate this order |

### 0.5 What the library does today

| Fact | Where |
|---|---|
| The alphabet `[]_0-9A-Z`, checked byte by byte; `codes()` throws `internal` on an unknown char | `src/writer/placeholder_font.hpp:41`, `placeholder_font.cpp:87-116` |
| `PlaceholderFitting::require_alphabet` raises `invalid_argument` naming the old alphabet, called before anything is recorded | `src/redact/placeholder_fitting.cpp:122-136`, `text_redactor.cpp:1080` |
| The font is cut by `build/fonts/subset.py` (`ALPHABET` :36, entries written as `'{ch}'` at :176) into the committed `src/writer/placeholder_font_data.inc`; `check_generated.py:24` regenerates and diffs it; `build/fonts/bitmap_font.py:133` reads `subset.ALPHABET` for the image caption font | as named |
| `to_unicode()` writes one `bfchar` block; `/W` is one run from the first gid and assumes contiguous gids | `placeholder_font.cpp:152-167`, `:216-218` |
| Fitting is width only (natural, scaled to a 0.6 floor, overflow), bold from the font | `placeholder_fitting.cpp:56-104` |
| The placeholder is drawn after the `ET` through `TextPlan::Insertion`, under the same CTM and clip, nothing for render mode 3 | `placeholder_drawing.cpp:34-48`, `text_redactor.cpp:668-686` |
| Matches merge by placeholder alone | `match_finder.cpp:791`, `property_strings.cpp:447` |
| The self-check replaces every placeholder substring with a space in every surface and in raw stream bytes before searching | `src/redact/self_check.cpp:57-67`, `:73`, `:232`, `:729-754` |
| Liberation Sans 2.1.5 Regular and Bold cover every code point of U+0020-007E, U+00A0-00FF and U+0100-017F; a subset of those 319 characters is 344 glyphs (composite components at gids 320-343), about 20 KB per face, the DLL about 32 KB larger, inside `build/budgets.tsv` | measured with fontTools 4.67.0 |

## 4. Change requests

| `CRx` | The request | Why it does not exist today | In this order? |
|---|---|---|---|
| CR1 | A placeholder may be any string over printable ASCII (U+0020-007E), the Latin-1 Supplement letters and symbols (U+00A0-00FF) and Latin Extended-A (U+0100-017F), sent as UTF-8 | the alphabet is the 40 characters a bracketed placeholder needs | yes |
| CR2 | A per-match `"style": "box"` (default `"text"`), with an optional `"colour": [r, g, b]` in 0 to 1 (default black), that removes the glyphs as today and paints a filled rectangle over the removed glyphs' area, one per reading line per stream, drawing no text | the only content insertion is the placeholder's text | yes |
| CR3 | The self-check stays a proof with arbitrary placeholder text | it cuts placeholder substrings out of what it searches, harmless only while a placeholder is bracketed capitals; a placeholder "de" would hide a surviving "Jean de la Fontaine", "e" would strip every e from the raw scan | yes |

## 5. Design rules

| `DRx` | The rule | Why | Enforced by |
|---|---|---|---|
| DR1 | A found occurrence of a redacted value is dismissed only where it lies wholly inside text drawn in the placeholder font (in the page text, by glyph; in the raw stream scan, by the string operands drawn under the embedded placeholder face's resource), never by cutting placeholder strings out of the surface | an arbitrary replacement must never hide a survivor | `self_check_test.cpp` survivor cases |
| DR2 | A box is drawn only over glyphs a viewer sees: a run whose every removed glyph is hidden (any class but `duplicate`) draws no box | a black bar over a hidden word reveals that a word was there | `redact_text_test.cpp` |
| DR3 | A ToUnicode `bfchar` block holds at most 100 entries | the CMap block limit (Adobe Technical Note 5099, recalled; add it to `docs/SOURCES.md` as recalled) | `placeholder_font_test.cpp` |
| DR4 | The image caption alphabet stays the narrow one | a caption is drawn by the bitmap font, whose alphabet is its own decision | `check_generated.py` |

## 9. The work

1. **Failing tests first** (`tests/unit/redact/` with `tests/unit/redact/support/redaction.hpp`'s `TextDocument`, `redact()`, `search()`, `page_text()`):
   - `placeholder_font_test.cpp`: every character of the new alphabet reads back through ToUnicode with its hmtx width; no block over 100 entries; `/W` agrees with hmtx for every gid; `"[REDACTED]"`, `"Jérôme Łukasz-O'Brien & Co."`, `"*****"` inside; U+0180 and U+2014 outside, the refusal naming `U+0180`.
   - `placeholder_fitting_test.cpp`: a lower-case accented placeholder with spaces fits, scales, or overflows by width.
   - `redact_text_test.cpp`: the two placeholders above round-trip in `page_text` and the self-check passes; a `box` match leaves no glyph of the value, draws `re` and `f` inside the reported `rectangles`, and two lines draw two boxes; a hidden run draws no box; the colour is honoured; an unknown style, a colour without `box`, a colour outside 0 to 1 and invalid UTF-8 are refused with `invalid_argument` and the three JSON fields.
   - `redact_text_test.cpp`, added on review: a `box` match under a non-opaque ExtGState (`/ca` below 1, a non-Normal `/BM`) paints its box with `/PdfkitBoxGS gs`, a state of `/ca 1 /CA 1 /BM /Normal /SMask /None` named in the page's resources, and the reader reads the fill as opaque; a partial `box` redaction inside a clipping text object (`4 Tr`) paints a box the text clip does not reduce to nothing, and the answer's `warnings` stay empty.
   - `self_check_test.cpp`: an `/Alt` survivor "Jean de la Fontaine" with placeholder "de", and a raw survivor with placeholder "e", are both reported; a placeholder that spells a value through its glyph codes is not a false survivor.
   - `abi_test.cpp`: ABI 3.
2. **The font**: `subset.py` gains `PLACEHOLDER_ALPHABET` (the three ranges), writes entries as `{0x00E9, gid, width}`, asserts the mapped gids are contiguous; `bitmap_font.py` reads a caption alphabet constant of its own; regenerate `placeholder_font_data.inc` with fontTools 4.67.0; `placeholder_font.*`: `char32_t` characters, the placeholder decoded from UTF-8 (invalid refused), glyph lookup by binary search, `outside()` answering a code point printed as `U+XXXX`, ToUnicode in blocks of at most 100.
3. **The fitting** reads code points; its refusal message names the new alphabet.
4. **The box**: `style`/`colour` on `MatchFinder::Match`, `Occurrence` and `TextPlan`; matches merge by `(placeholder, style, colour)`; `PlaceholderDrawing::box()` inserts `q <colour> rg <cm> x y w h re f Q` after the `ET` of each reading line's first removed glyph, the rectangle the union of that line's removed glyph boxes mapped into the anchor's text space (add the matrix inverse to `src/text/matrix.hpp`); under `box` the placeholder defaults to `""` and still replaces the text in `/Alt`, `/E` and the string matches; the answer gains `"style"` and `"boxes"` (device rectangles per occurrence), and `placeholder_drawn` is false under `box`. Respect the layers (§3.1): geometry in `text`, drawing in `redact`. Two requirements added on review: the box is opaque whatever graphics state it inherits (inside its `q` ... `Q` it sets `/PdfkitBoxGS gs`, an ExtGState of `/ca 1 /CA 1 /BM /Normal /SMask /None` named, like the placeholder face, in the resources the stream reads, before it fills), because text under `/ca` below 1, an `/SMask` or a non-Normal `/BM` would otherwise get a translucent box; and the text clip does not cut it: the box is written before the `BT` of the anchor's text object, under the same CTM, where the clip is the one the removed glyphs were painted with, because a clipping render mode (4 to 7) installs the kept glyphs' outlines as the clip at `ET`. A line for which no such place exists (no `BT` before the anchor in its walk, or a `cm`, `q` or `Q` between them, or a singular text matrix) paints no box and is reported in the answer's `warnings` as `box_not_drawn`.
5. **The self-check** per DR1.
6. **The version**: `pdfkit_abi_version` 3 and the version `0.3.0-preview` everywhere `0.2.1-preview` names the current one (`src/api/lifecycle.cpp:28`, `CMakeLists.txt:13`, `tests/unit/api/abi_test.cpp`, `tests/abi/run.py:35`, `README.md`, `docs/ABI.md:8,105,116`); the CLI's redact job reads `style` and `colour` too; `tests/abi/` .NET and Python callers send a box match if they cover redact.
7. **Documentation, last, from what landed**: `docs/ABI.md` §4 (`pdfkit_redact_text` input and output) and §5 (the ABI 3 row); `docs/DESIGN.md` §12, §13 (the alphabet, the glyph count, the box), §18 (DR1); `CLAUDE.md` §2 row 5 and §4 (placeholder, placeholder font); `docs/SOURCES.md` (the CMap block limit, recalled); `README.md`.

## 12. Tests
The unit tier on Linux (gcc Debug), the check scripts that run on Linux (§0.4). The dataset and PDFium tiers need the owner's laptop and are not run (`FEEDBACK` entry in `doc-anonymiser-for-.NET:FEEDBACK-18.md`).

## 13. Acceptance criteria

| `ACx` | Criterion | Gate |
|---|---|---|
| AC1 | The tests of §9.1 pass and the 685 base cases still pass | pre-merge |
| AC2 | `check_layout`, `check_headers`, `check_comments`, `check_catalogue`, `check_network`, `check_repo`, `check_paths`, `check_generated` and `check_format --no-tidy` pass on Linux | pre-merge |
| AC3 | `pdfkit_abi_version()` answers 3 and `pdfkit_version` names 0.3.0-preview | pre-merge |
| AC4 | The Linux shared library builds with the flags of `doc-anonymiser-for-.NET:build/pdfkit-linux.sh` | pre-merge |

## 14. Manual verification
On the laptop: `python build/check.py --full`, the sanitizer tiers, a dataset run, then the tag `v0.3.0-preview` and the release workflow (the owner's, `CLAUDE.md` §10).

## 16. Rollback
Revert the merge on `main`. Nothing is released by this order.

## 17. Agent brief
> Read `CLAUDE.md`, then `docs/change-02.md`, and implement it exactly as written in `/home/user/pdfkit` on the branch claude/inspiring-ptolemy-6ahsex. Build out of tree under `/tmp/claude-0/-home-user/db53a4a9-0cfd-54b4-b781-757c5fe9fec6/scratchpad/pdfkit/`. Begin by building and running the unit tier on the base and reading its count. Finish by committing (do not merge into `main`; the orchestrating session does), then report `done` with the counts and the checks that passed, or `incomplete` with the reason. Record any deviation in Appendix C.

## Appendix C. Deviations recorded by this order
1. The box is written before the `BT` that opens the anchor glyph's text object, not after its `ET` as §9 step 4 first said, because a clipping render mode installs the kept glyphs' outlines as the clip at `ET` (the review requirement recorded in step 4); a line with no such `BT` (or a `cm`, `q` or `Q` before it) paints no box and is answered as a `box_not_drawn` warning.
2. The box is painted under `/PdfkitBoxGS gs` (`/ca 1 /CA 1 /BM /Normal /SMask /None`), named in the resources the stream reads and, for a form without resources of its own, in every kept page drawing it, as the placeholder face is (the review requirement recorded in step 4).
3. The answer gains a top-level `warnings` array beside `style` and `boxes`; `boxes` is present (empty) under `text` too, `style` is written for every match, string matches included, and `scale` is 1 under `box`.
4. `style` and `colour` live on `MatchFinder::Match` and `MatchFinder::Occurrence`; `TextPlan` gains no style field but `Insertion::before` (with `after` renamed `at`) and `PageWork::box_states` and `form_box_states`, which is all the write needs.
5. `PropertyStrings` still merges the edits of one string by placeholder alone: a style changes nothing written into a string, and merging two edits of one placeholder keeps it written once.
6. `SelfCheck::run` loses its `placeholders` parameter, since nothing is cut any more; `pdfkit_check` and the tests calling it are updated. In the raw scan a content stream's font names are resolved through the resources it reads (its own, its page's, or every page's for a stream without resources of its own) and a name counts only when its font's `/BaseFont` is a placeholder face.
7. The CLI needed no change: its redact job hands the `text` section to `pdfkit_redact_text` byte for byte, so `style` and `colour` reach it as written.
8. The caption alphabet is named so in the image-treatment refusal, the caption drawer's header, `docs/ABI.md` and `docs/DESIGN.md` §14, and in the generated bitmap data's header comment; its rasterised bytes are unchanged (DR4).
9. The Bold subset holds 343 glyphs and the Regular 344 (one composite component fewer), not 344 each.
10. A box of turned text adds its device bound to the occurrence's `rectangles` when the line's rectangle does not hold it.
11. Existing tests moved with the rules: the self-check test "placeholders are cut out of every surface" becomes one asserting that nothing is cut; a string-match refusal used the placeholder `[x]`, which is now valid, and uses `[ƀ]`; the raw-survivor test keeps its stream reachable through an ExtGState, because the writer drops an unreferenced catalog entry.
12. On Linux the gcc 13 Release build of the shared library stops on a `-Wmaybe-uninitialized` in `src/syntax/object_streams.cpp` that the 0.2.1-preview build met too; the library was built with clang 18 as the sibling repository's script falls back to, and the gcc Debug unit tier is unaffected.
13. Five tree hashes of `third_party/inventory.tsv` (agl, doctest, liberation-fonts, unicode, zlib) failed on Linux because `build/check_third_party.py` sorted `Path` objects, which compare case-insensitively on Windows; every vendored byte was the recorded one. The script now sorts by the relative POSIX path's text and the five hashes are recorded in that order, in a commit of its own.
14. The order's own citations of the branch name and of the sibling repository's files are written so `build/check_paths.py` does not read them as pdfkit paths, and the `build/fonts` readme it named, which does not exist, is dropped.
15. The caller's end-to-end run read a randomised replacement glued to the next word ("Signed by Huismantoday."), and its other specimens read placeholders out of place ("23 sold Food to . [ENTITY_2]"). Both predate this order: `main` at 0.2.1-preview reads the same with bracketed placeholders ("Signed by [P_1]today."; "today. [PERSON_1]" then "Signed by" as a second block for a name in the middle of one string). The causes, fixed beyond the order's scope because the order's realistic text makes them common: the layout kept a later-drawn run whole after the run whose holes it filled, and split the line at the hole a narrower placeholder left (wider than 2.0 of the size); a placeholder narrowed to fill its hole ended on the next word's origin; an overflow by less than half the size was shuffled into the next word; and a realistic placeholder's own spaces raised the word gap of a whole line set without space glyphs. The layout now places runs in draw order, cut where a placed glyph stands in their gaps, a piece drawn over placed glyphs kept whole where it starts and every other piece by position; starts a run and a word at a glyph in another font drawn over the one ahead of it (an accent excepted); and takes the spaced word gap per font pair. The drawing follows a natural or scaled placeholder with one space of its face whose `Tc` makes its advance reach the next kept word (it paints nothing; the placeholder stops at least 0.02 pt short of that word so the space is read first), and draws an overflow that would end within 0.15 of the size past that word's origin just wide enough to reach that far (`docs/DESIGN.md` §7 and §13). Not changed: a box match draws no text, so its holes still split a line wider than 2.0 of the size; a placeholder narrower than a match glued to the next glyph (a mask over "Contoso23") reads the gap it leaves. The unit tier holds 711 cases; the dataset and PDFium tiers, which would measure the layout change against the reference readers, need the owner's laptop and were not run.
