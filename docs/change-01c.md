# CHANGE-01c — pdfkit's own text misses that the panel confirms: CJK CMap data, half-width Latin in non-embedded CJK fonts, Type 3 code names when ordered, the layout faults; and the order-wide documentation sweep

| Field | Value |
|---|---|
| Status | awaiting owner launch |
| Owner approval | with the parent |
| Depends on | `change-01b.md` |
| Supersedes | nothing |
| Superseded by | nothing yet |
| Batches | batch C of `docs/change-01.md` |
| `SessionVersion` | not applicable; the C ABI version stays 1 and no JSON shape moves |
| Behaviour change | yes: `pdfkit_text` (and every function that reads text: redaction's search, the self-check) reads CJK fonts with a predefined CMap or an Adobe ordering and no `/ToUnicode`, advances Latin codes of a non-embedded CJK font at half width, and lays out the confirmed cases differently; checked against the 960-file dataset, the panel and every release gate row |
| New dependency | shipped: the Adobe CMap resources (BSD-3), as packed tables (parent D10) |
| Minimum model | `opus` |
| Duration and price | 5.5 to 6 hours, $95 to $120 at list prices (parent §11.1); the split point is after the CMaps (§9.6) |

Read `docs/change-01.md` §0 to §8 first. This file is §9 onwards for batch C
alone.

---

## 0. Context

Inherited from the parent. Facts the batch needs beside it:

- **Work only from the panel's list.** Batch B's `tests/panel/text_causes.tsv`
  names the files the reading panel gate counts against pdfkit, each with its
  group. A group below with no file in that list is not done; a file in it
  whose group is not below is left with its row, and named in the final
  message.
- **CR5 is ordered or not by batch B.** It is ordered when `build-5.md` §9's
  panel item says "CR5 ordered" and the `type3_code_names` rows are in
  `tests/panel/text_causes.tsv`; that item also names the reading the panel's
  majority uses. Otherwise skip §9.3.
- **The reader's code as it stands**: the decoding ladder's numbered names
  (`src/fonts/decoding_ladder.cpp:40` `numbered`, `:179`;
  `src/fonts/glyph_list.cpp:204`); the predefined CMap codespaces
  (`src/fonts/type0_font.cpp:46` `predefined_codespace`); `/DW`
  (`src/fonts/type0_font.cpp:175`, `:292`); the CMap model
  (`src/fonts/cmap.cpp` `add_cid_range`, `cid`). The layout passes live in
  `src/text/` (`build-5.md` §9 items 6 and 7 say what each does and why).
- **Generated data has one pattern** (parent §0.5): upstream files in
  `third_party/<name>/`, a generator and a lock file under `build/fonts/`, the
  output in `src/fonts/generated/`, held equal by `build/check_generated.py`.
  Read `build/fonts/glyph_list.py`, `build/fonts/cpp_table.py` and
  `third_party/agl/` before writing the CMap generator.
- **The fonts layer may include syntax and platform only** (`CLAUDE.md` §3.1);
  no new include crosses that line (`build/check_layout.py`).
- **Other branches may own `src/redact/`, `src/writer/` or `src/api/`** at
  launch (parent §11.3). This batch edits none of them; it rebases onto what
  has merged before its documentation sweep.

---

## 9. The work

Each fix starts with a unit case in `tests/unit/fonts/` or `tests/unit/text/`
that builds the file's pattern with `tests/unit/support/`'s PDF builder and is
seen red (`CLAUDE.md` §8.2). Every external fact below is **recalled** unless
marked read; read it at the source or test it on the named dataset files, and
record its status in `docs/SOURCES.md`.

### 9.1 CJK CMap data (CR3; group `cjk_cmap`: pdfdiff-0036, pdfjs-0053, pdfminer-0039, pdfminer-0044, pdfpig-0003, pdfplumber-0023 at batch A's base)

| File | Change |
|---|---|
| `third_party/adobe-cmaps/` | the upstream files the predefined names need, from https://github.com/adobe-type-tools/cmap-resources at a pinned commit (`docs/SOURCES.md` row 87, read): the predefined CMaps for the Adobe-Japan1, -GB1, -CNS1 and -Korea1 orderings that ISO 32000-1 lists for CID fonts (recalled: its table of predefined CMaps), and each ordering's `Adobe-<ordering>-UCS2` CID-to-Unicode CMap; the upstream licence file |
| `third_party/inventory.tsv`, `THIRD-PARTY-NOTICES.md` | the component's row (commit, upstream URL, BSD-3, SHA-256) and its notice; `python build/check_third_party.py` green |
| `build/fonts/cmaps.py`, `build/fonts/cmaps.lock` | the generator: parses the CMap files (`begincodespacerange`, `begincidrange`, `begincidchar`, `usecmap` among the predefined files) and writes packed range tables (code range to first CID, CID range to first code point) as `constexpr` arrays through `cpp_table.py`; registered in `build/check_generated.py` |
| `src/fonts/generated/` | the generated tables, never edited by hand |
| `src/fonts/predefined_cmaps.hpp`, `.cpp` | `pdfkit::fonts::PredefinedCmaps`: `lookup(name)` returns the `Cmap` of a predefined name (its codespace from the data, which replaces `predefined_codespace`'s implied one for the names the data has), `to_unicode(ordering, cid)` the character of a CID; both bounded table lookups with no allocation from file input |
| `src/fonts/type0_font.cpp` | a predefined name with data decodes through it; a CID font with no `/ToUnicode` whose `/CIDSystemInfo` names one of the four Adobe orderings maps CID to Unicode through that ordering (pdfdiff-0036 and pdfminer-0039 are `Identity-V` with Adobe-Japan1) |

**The rung** of a code answered through an ordering's CID-to-Unicode table is
`to_unicode`, because the table is the ToUnicode map the ordering defines and
a new rung would add a value to the JSON callers read (`docs/ABI.md`, the
`rungs` of `pdfkit_text`). Mine, flagged: a separate rung is a later order's
choice.

**Size**: after the build, `python build/package.py`. If the DLL passes its
budget, raise `build/budgets.tsv`'s DLL, and the zip's if it passes too, by the
measured growth plus 25 % of it, with the reason in the row's note (parent
D10), and say the new numbers in the final message.

### 9.2 Half-width Latin in a non-embedded CJK font (CR4; group `cjk_width`: dccc-0012, synth-0151)

In `src/fonts/type0_font.cpp`, for a CID font with no embedded program and no
`/W`, a code whose character is printable ASCII (U+0020 to U+007E) advances 500
thousandths instead of `/DW`. Recalled: viewers advance such glyphs at half
width because the substituted CJK font's Latin glyphs are half width; test it
on the two files (their words must no longer split at the media box) and keep
the narrowest rule that fixes them. `docs/DESIGN.md` §5.3 says the rule and
its status.

### 9.3 Type 3 names restating their code (CR5; only if ordered, §0)

In `src/fonts/decoding_ladder.cpp` `numbered`, for a font with no program, a
`Cdd` or `Cxx` name whose number equals its own code reads as batch B's
measured reading names it, rung `glyph_name`; any other number stays
unreadable and counted. `docs/DESIGN.md` §5.2 says so with the panel's
numbers. Files: mutate-0008, -0011, -0018, -0023, -0028, pdfjs-0047.

### 9.4 The layout faults (DFT1 to DFT3, and pdfpig-0002; group `layout` or `other`)

| Fault | File | Direction |
|---|---|---|
| DFT1 | pdfdiff-0018 | the baseline tolerance cannot join two baselines further apart than the smaller run's size |
| DFT2 | pdfium-0049 | the word gap of a vertical run is measured along its advance with the thresholds of `docs/DESIGN.md` §7 |
| DFT3 | pdfdiff-0017 | a duplicate needs the same code at the same place; two positions of one word under a half-turn are not one |
| investigate | pdfpig-0002 | 7,493 codes unreadable: find which rung should answer; fix if it is a reader fault inside §9's scope, else write its cause row |

Each rule change is narrow: a layout rule moves many files, and §9.6's full
gate is what proves no other file moved.

### 9.5 Records of what moved

| File | Change |
|---|---|
| `tests/panel/text_causes.tsv` | rows of fixed files removed; a file not fixed keeps its row with the reason |
| `tests/pdfium/text_causes.tsv` | rows of files now at or above 0.90 against PDFium removed (the old gate names only files below it) |
| `dataset/manifest.tsv` | `expect_text` and `expect_fonts` of the fixed files, through `python tests/pdfium/text_agreement.py --run <id> --write-manifest`; a row of any other file that moves is a deviation recorded in §C, never committed silently |

### 9.6 Execution order inside this batch

The test rhythm of `CLAUDE.md` §8.2 and the stop rule of the parent §0.2 hold
throughout: the unit tests of the component touched while coding
(`pdfkit_tests -tc="*cmap*"` and the like), the smoke set once after §9.1, the
full gate once at the end and again only after a fix to one of its failures,
and at 5 h 30 of wall-clock stop, commit what passes and record what is left.

1. **Base check.** On batch B's tip, `ctest --preset unit` and
   `python build/check.py` green; read `tests/panel/text_causes.tsv` and
   `build-5.md` §9's panel item.
2. **§9.1, the CMaps**: tests red, data and generator, `PredefinedCmaps`,
   `type0_font.cpp`, tests green, `python build/check.py`, the size measured.
   **Split point**: if 3 hours have passed here, finish §9.1, run step 3, skip
   to step 6, and end `incomplete` with §9.2 to §9.4 recorded for a follow-on
   batch D in the parent's index.
3. **Mid-batch smoke**: `cmake --build --preset mingw-release`,
   `python tests/dataset/run.py --checks open,text --runs 1 --ids-from tests/panel/smoke.tsv --run-id <id>-smoke`
   (read `run.py --help`; if it has no id filter, pass the ids as its help
   says), then `python tests/dataset/gates/phase5_panel.py --run <id>-smoke --smoke`.
4. **§9.2, §9.3 if ordered, §9.4**, each test red then green.
5. **Kill criterion.** Quantity: files that the reading panel gate counted as
   agreeing at batch B's tip and counts against pdfkit now, from the full run
   of step 6. Threshold: one. Then the rule that moved it is narrowed or
   reverted and step 6's gate re-run once (a re-run after a fix to its
   failure); if the session cannot narrow it, revert that rule, keep its
   test as an expected failure named in §C, and write the file's cause row.
6. **The full gate, once**: `cmake --preset mingw-release && cmake --build --preset mingw-release`;
   `python tests/dataset/run.py --runs 2 --run-id <id>` (every check, both runs
   identical); `python tests/pdfium/text_agreement.py --run <id> --hidden`;
   `python tests/dataset/gates/phase5_panel.py --run <id>`; and the release
   gate's other rows over the same run, as `build-10.md` §9 item 22 lists
   them (`compare.py --check write,select,text,redact`, `leak.py --metadata`,
   `leak.py --images`, `metadata.py`, `tests/dataset/gates/phase7.py` to
   `phase10.py`), because the text model feeds redaction's search and the
   self-check.
7. **Records** (§9.5).
8. **Test sweep**: `ctest --preset unit`, `python build/check.py`.
9. **Documentation sweep, order-wide, from what landed**:
   `docs/DESIGN.md` §5.2 (if §9.3 ran), §5.3, §5.4 (the ordering's table as
   the `to_unicode` rung), §7 (the layout rules as changed);
   `docs/SOURCES.md` (the CMap resources shipped, the half-width rule's
   status, each layout rule's source); `CLAUDE.md` §4 (the vocabulary of the
   parent's §6), §6 (the Adobe CMap resources' row in the vendored table),
   §7.5 (the panel beside PDFium, and "independent reader" retired as PDFium's
   name), §8.1 (a panel tier row: `python tests/panel/fetch.py`,
   `python tests/panel/collect.py`, the two panel gates, the cache), §12;
   `dataset/README.md` (what `expect_text` is judged against, if it moved);
   `OPEN-QUESTIONS.md` (§2.2's action done, with the numbers; the old §2.10
   items closed or left with their files); `build-5.md` §9 (this batch's
   numbers); the `CLAUDE.md` §1 row of this order ("the reference panel
   order, finished; its records until it is archived").
   **If the reading panel gate passes on this tip**, also: move `build-5.md`
   to `docs/archive/build/` with `git mv`, update `CLAUDE.md` §1's build-plan
   row and §2's Build file cell of row 2, and say in the final message that
   the landing merge must carry `Build-Phase: 5 done`. The same for
   `build-6.md` and `Build-Phase: 6 done` if batch B's bitonal panel gate
   passed (`build-6.md` §9) and nothing here moved it.
   `build/paths_pending.tsv` without this batch's rows.
10. **Merge gate** on the tip: `python build/check.py --full`,
    `ctest --preset sanitize`, `ctest --preset clang-sanitize`,
    `python tests/abi/run.py`, `python build/package.py --verify`; push with
    `Change-Batch: 01c done`.

---

## 10. Code and documentation references

### 10.1 Files this batch touches

```
src/fonts/predefined_cmaps.hpp, .cpp            (new)
src/fonts/generated/                            (new generated tables)
src/fonts/type0_font.cpp                        (§9.1, §9.2)
src/fonts/decoding_ladder.cpp, glyph_list.cpp   (§9.3, only if ordered)
src/fonts/CMakeLists.txt                        (the new sources)
src/text/                                       (§9.4, the layout passes the faults name)
tests/unit/fonts/, tests/unit/text/             (new cases)
third_party/adobe-cmaps/, third_party/inventory.tsv, THIRD-PARTY-NOTICES.md
build/fonts/cmaps.py, cmaps.lock, build/check_generated.py, build/budgets.tsv
tests/panel/text_causes.tsv, tests/pdfium/text_causes.tsv, dataset/manifest.tsv   (rows of fixed files)
CLAUDE.md, docs/DESIGN.md, docs/SOURCES.md, dataset/README.md, OPEN-QUESTIONS.md, build-5.md, build/paths_pending.tsv   (sweep)
```

### 10.2 Authoritative documentation to update

| File | Edit |
|---|---|
| `CLAUDE.md` §1, §2, §4, §6, §7.5, §8.1, §12 | step 9 |
| `docs/DESIGN.md` §5.2, §5.3, §5.4, §7 | step 9 |
| `docs/SOURCES.md` | step 9 |
| `docs/ABI.md` | unchanged: no JSON shape and no rung value moves; §C says so |
| `dataset/README.md` | step 9, only if what `expect_text` means moved |
| `OPEN-QUESTIONS.md`, `build-5.md` | step 9 |

---

## 12. Tests

**New**

| Test | Tier | Asserts |
|---|---|---|
| a predefined CMap case per ordering | unit (`tests/unit/fonts/`) | a code of each of the four orderings' named CMaps decodes to its CID and character; a code outside the codespace is unreadable and counted |
| an `Identity-V` Adobe-Japan1 font with no `/ToUnicode` | unit | its CIDs read through the ordering, rung `to_unicode` |
| half-width Latin | unit | a non-embedded CID font with no `/W`: ASCII codes advance 500, an ideograph `/DW` |
| `Cdd` names (only if ordered) | unit | a Type 3 font with `/Differences [65 /C65]` reads as batch B's reading names; `/C66` on code 65 stays unreadable |
| DFT1, DFT2, DFT3 | unit (`tests/unit/text/`) | the geometry of each file, read as its words |
| the generator | checks | `python build/check_generated.py` re-cuts the tables byte for byte |

**Updated, deleted:** a unit case asserting "predefined CMap codes are
unreadable" for a name the data now covers changes its expectation; say so in
the commit, because that behaviour moved by design.

---

## 13. Acceptance criteria

| `ACx` | Criterion | Measure | Gate |
|---|---|---|---|
| AC5 | Confirmed misses fixed | of the files in `tests/panel/text_causes.tsv` at batch B's tip in the groups of §9, each is fixed (no longer counted against pdfkit by `phase5_panel.py`) or keeps a row saying why; the share counting against pdfkit is lower than at B's tip | order-end |
| AC5a | Nothing else moved in the reading gate | 0 files counted as agreeing at B's tip count against pdfkit at C's tip (step 5) | order-end |
| AC6 | No other release number moves | every row of `build-10.md` §9 item 22 other than the reading and bitonal rows equals its value there; the leak measures 0; both runs identical | order-end |
| AC7 | Suites, checks, size | `ctest --preset unit`, `python build/check.py --full`, both sanitizer tiers, `python tests/abi/run.py` and `python build/package.py --verify` green; the DLL within its budget as `build/budgets.tsv` then stands, with the reason of any raise in its note | order-end |
| AC8 | No member shipped | `build/check_artefact.py` green | order-end |

---

## 14. Manual verification

As the parent's §14: pdfjs-0053 and pdfplumber-0023 in a viewer beside
`pdfkit text` on this tip.

---

## 15. Traceability matrix

| Item | Decided by | Proven by | Held by |
|---|---|---|---|
| CR3, F6, F7 | D10 | AC5, AC7 | the CMap unit cases; `check_generated.py`; `package.py` |
| CR4 | D2 | AC5 | the half-width unit case |
| CR5 | D3 | AC5 | the `Cdd` unit case, only if ordered |
| DFT1, DFT2, DFT3 | D2 | AC5, AC5a | the layout unit cases |
| D8 | D8 | AC6 | the release gate's rows |
| DR4 | D1 | AC8 | `check_artefact.py` |

---

## 16. Rollback and irreversible cost

**How to undo the code.** Revert the batch; the manifest rows and cause rows
return with it. **What cannot be undone:** a release made from this tip ships
the CMap tables, and a raised budget is the floor of later packages (parent
§16). **What the owner must be told:** the DLL's new size and budget, and
whether the landing merge closes phase 5 or 6.

---

## 17. Opening prompt

> Read `CLAUDE.md` (§3, §6, §8, §9 and §11 closely), `docs/DESIGN.md` §5 and
> §7, and `build-5.md` §9, then read `docs/change-01.md` §0 to §8 and all of
> `docs/change-01c.md`, and implement it exactly as written. Batches A and B
> are finished: read their §C in `docs/change-01a.md` and `docs/change-01b.md`,
> then `tests/panel/text_causes.tsv` and the panel item of `build-5.md` §9,
> which say which files and groups are yours and whether CR5 is ordered.
> Before planning, read `src/fonts/type0_font.cpp`, `src/fonts/cmap.cpp`,
> `src/fonts/decoding_ladder.cpp`, `build/fonts/glyph_list.py`,
> `build/fonts/cpp_table.py` and `build/check_generated.py`. Develop and push
> on the branch and in the working tree the launcher's preamble names, or,
> when started by hand, on change/01-c-text-fixes branched from the tip of
> change/01-b-judge, in a worktree of its own; check that no other open
> branch owns `src/fonts/` or `src/text/`, and stop and say so if one does.
> Begin by running `ctest --preset unit` and `python build/check.py` on that
> base; if a test is still red run alone, repair the base in a commit of its
> own before you start and record the repair as a deviation. Follow §9.6 with
> its test rhythm: the unit tests of the component touched while coding, the
> smoke set once after the CMaps, the full gate once at the end and again only
> after a fix to one of its failures, the merge gate at the end; never re-run
> a full tier to be sure. This session lasts at most 6 hours: at 5 h 30 stop
> starting work, commit what passes, record what is left in §C and in the
> parent's batch index, and push; §9.6 step 2 names the split point. Finish by
> pushing a last commit whose message carries the trailer
> `Change-Batch: 01c done`, or `Change-Batch: 01c incomplete` with the reason
> in your final message, and report the elapsed time, the model spend, the
> reading panel gate before and after, the files fixed and left, the DLL's
> size, and whether the landing merge must carry `Build-Phase: 5 done` or
> `Build-Phase: 6 done`. Edit nothing under `src/redact/`, `src/writer/`,
> `src/api/` or `tests/pdfium/` beyond `tests/pdfium/text_causes.tsv`. If you must deviate
> from the brief, report each deviation, ask where the answer is the owner's,
> update the batch index in `docs/change-01.md` so it describes what you
> actually left behind, and record it in this file's §C and in the merge
> message.

---

## Appendix C — Deviations recorded by this batch

1. *(none yet)*
