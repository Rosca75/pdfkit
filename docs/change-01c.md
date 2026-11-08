# CHANGE-01c — pdfkit's own text misses that the panel confirms: CJK CMap data, half-width Latin in non-embedded CJK fonts, Type 3 code names when ordered, the layout faults; and the order-wide documentation sweep

| Field | Value |
|---|---|
| Status | done on change/01-c-text-fixes (2026-11-08, on Linux), awaiting the owner's landing merge |
| Owner approval | with the parent |
| Depends on | `change-01b.md` |
| Supersedes | nothing |
| Superseded by | nothing yet |
| Batches | batch C of `docs/change-01.md` |
| `SessionVersion` | not applicable; the C ABI version stays 1 and no JSON shape moves |
| Behaviour change | yes: `pdfkit_text` (and every function that reads text: redaction's search, the self-check) reads Type 3 code names and lays out the confirmed cases differently (the CJK items were dropped, §9.1 and §9.2); checked against the 960-file dataset, the panel and every release gate row |
| New dependency | none (the Adobe CMap resources were dropped with §9.1) |
| Minimum model | `opus` |
| Duration and price | 5.5 to 6 hours, $95 to $120 at list prices (parent §11.1); the owner lifted the time limits and split point for the run (Appendix C entry 1) |

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
- **What batch B left** (`docs/change-01b.md` Appendix C; run `panel-b-full`
  on Linux): `tests/panel/text_causes.tsv` holds 50 files in eight groups:
  `type3_code_names` 6 (CR5 ordered, the reading the code itself),
  `layout` 9 (pdfdiff-0017, pdfdiff-0018, pdfium-0049, pdfjs-0018,
  pdfminer-0042, pdfplumber-0014, pypdf-0019 to -0021), `other` 2
  (pikepdf-0022, qpdf-0051), `cjk_cmap` 4, `cjk_width` 2 and `cjk` 5
  (dccc-0073 among them), which `OPEN-QUESTIONS.md` §1.5 makes known gaps,
  not work, `subset_unreadable` 5 (pdfjs-0002 and pdfpig-0002 among them),
  which no member reads either (§8.2: not work), and `legitimate` 17, where
  pdfkit is right or follows its own rule. The
  owner's word cuts (§1.4) are `tests/panel/word_cuts.tsv`: 12 places the
  panel calls pdfkit's miss, in dccc-0124, dccc-0139, dccc-0097, pdfpig-0005
  and dccc-0073, and one it calls right. pdfminer-0044 is undecided (two of
  four voters below), so §9.1 leaves it. Re-run
  the gate with `python tests/dataset/gates/phase5_panel.py --run <run-id>`
  after a dataset run; it runs `pdfkit text` for the files with hidden or
  duplicate text and refuses when that binary reads them otherwise than the
  run did, so build the run's commit first; `--collect` asks QPDF's survey
  and the members' text of a new copy without annotations.
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

### 9.1 CJK CMap data (CR3; group `cjk_cmap`) — dropped

Withdrawn by the owner on 2026-11-07 (`OPEN-QUESTIONS.md` §1.5): no Adobe
CMap resource is vendored, no generator is written and the DLL's budget does
not move. The files of the group stay known gaps with their rows in
`tests/panel/text_causes.tsv`, and the panel still judges them.

### 9.2 Half-width Latin in a non-embedded CJK font (CR4; group `cjk_width`) — dropped

Withdrawn with §9.1 for the same reason; `/DW` stays as the specification
writes it, and the group's files (dccc-0012, synth-0151, and the "INS TALL"
cut of dccc-0139 that the same default causes) stay known gaps.

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
src/fonts/decoding_ladder.cpp, glyph_list.cpp   (§9.3, only if ordered)
src/fonts/CMakeLists.txt                        (the new sources)
src/text/                                       (§9.4, the layout passes the faults name)
tests/unit/fonts/, tests/unit/text/             (new cases)
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
| `Cdd` names (only if ordered) | unit | a Type 3 font with `/Differences [65 /C65]` reads as batch B's reading names; `/C66` on code 65 stays unreadable |
| DFT1, DFT2, DFT3 | unit (`tests/unit/text/`) | the geometry of each file, read as its words |

**Updated, deleted:** none; the CMap and half-width cases were dropped with
§9.1 and §9.2.

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
| CR3, CR4, F6, F7 | `OPEN-QUESTIONS.md` §1.5 | dropped | the rows of `tests/panel/text_causes.tsv` |
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

1. **The batch ran on Linux**, as `docs/change-01.md` Appendix C says:
   `linux-debug` and `linux-release` for `mingw-release`,
   `ctest --preset unit-linux` and `unit-linux-release` for
   `ctest --preset unit`, `python build/check.py --fast` for the routine
   and full checks; the sanitizer presets, `build/check.py --full`,
   `build/package.py --verify` and `build/check_artefact.py` are the
   owner's laptop steps. The comparison base is batch B's run
   `panel-b-full`, and AC6 compares with the Linux baseline
   `linux-base-09b8d78` (parent Appendix C entry 12). The owner lifted the
   session's time limits, stop rule and split point for this batch.
2. **§9.1 and §9.2 were dropped** by the owner's answer of
   `OPEN-QUESTIONS.md` §1.5: no CMap data, no generator, no budget raise;
   their rows of `build/paths_pending.tsv` went with the text that cited
   them, and the 12 CJK files the panel counts against pdfkit keep their
   rows as known gaps. pdfpig-0002 (§9.4) is Chinese and was not
   investigated further.
3. **CR5 needed a layout rule to read words.** pdfjs-0047's Type 3 font
   (`/FontMatrix [1 0 0 1 0 0]` at 0.24) draws glyphs 72 times its size, so
   with its codes read every gap ended a line ("P", "ro p e", "rty"). A
   Type 3 glyph whose box spans more than four times its size across the
   line is sized by its box. pdfkit then reads "Property Insurance" as
   PDFium and Poppler do, and four of the six files still count against
   pdfkit because MuPDF, pdf_oxide and pdf-extract cut the words, each in
   its own places (`OPEN-QUESTIONS.md` §3.6). The rule also joins
   pdfjs-0004's "Questions about your bill?" and moves nothing else.
   opf-0014 (`/C10`, decimal) and mutate-0036 read their codes too
   (`expect_fonts` of the manifest). A first form also took dvips's
   lower-case `cNN` names and read pdfpig-0009's bitmap fonts as letters
   shifted by three ("QXOOIlOOWHLQQHJDWLYHU"), text no member reads; the
   rule takes an upper-case `C` alone, the shape the panel confirmed, and
   pdfpig-0009 keeps its 76 unreadable codes and their warning.
4. **The layout fixes go beyond DFT1 to DFT3**: the brief added W2, the
   owner's word cuts of `OPEN-QUESTIONS.md` §1.4, and the layout group's
   other files. Added rules: a space that opens a run and that a `TJ`
   number moves the next glyph back over stands where that glyph starts
   (pdfjs-0018); a glyph whose text holds a space counts by what follows
   it for the direction pass (pypdf-0019 to -0021); on a line whose fonts
   draw spaces, a word set as an operator of its own after the one before
   ends at 0.15 of the size (dccc-0124's three cuts, dccc-0073's
   "新たに POS", and the other files of run `panel-c-full2` whose glued
   words, leaders and punctuation it separates). Each was measured on the
   whole dataset's text before it was kept. A first form at 0.1 of the size
   cut "Montelius" and "Whitlock" (dccc-0005, rotated text with gaps of
   0.096) and "file" (a zero-width "i", pdfplumber-0005). A second form also
   took two strings of one `TJ` array, which separated pdfjs-0016's
   "Ideas:" "The" and 16 other files' glued words but cut synthetic words a
   `TJ` letter-spaces ("Sp" "aced", "TRAV" "EL", "3." "14"); after review the
   rule takes operators alone, those 17 files read as before it, and unit
   cases pin the shapes. Two operators 0.16 of the size apart still read as
   two words ("Bo" "ld"), the price of dccc-0124's "in" "Hermeskeil" at
   0.158 (`OPEN-QUESTIONS.md` §3.7).
5. **DFT1's direction was not taken as written.** No baseline tolerance
   can separate pdfdiff-0018's baselines (80 pt apart under glyphs of 500
   and 660 pt, a fifth of either size being 100 pt or more). The fix
   separates two baselines whose runs of two glyphs or more cross each
   other, at least half of each run's glyphs over the other's, the smaller
   run at least 0.75 of the larger's size (pdfdiff-0018's are 0.76), within
   a budget of a million visits of baseline and run pairs and glyph
   comparisons per cluster, past which the lines already cut stay cut and
   later baselines join the line under way (a unit case reaches it). A first form that
   compared every glyph of a baseline split subscripts and LaTeX's raised
   "A" and was narrowed to runs; a second split dccc-0128's subscripts of
   "m1 x m2" and was narrowed by the size share after review. dccc-0128
   still reads a display formula's "∏" and its limits "k=1" on two lines,
   as it read them on one in another order before the batch.
6. **DFT2's threshold**: pdfkit measured the vertical gap along the
   advance already; the 0.2 em gap inside pdfium-0049's "Hello" passes the
   narrow word gap, so glyphs of vertical writing take the spaced one
   (0.3). The redaction's placeholder fitting
   (`src/redact/placeholder_fitting.cpp`) kept its own copy of the word
   gaps and glued a narrowed placeholder to a word the layout sets apart
   ("viele Huismannanfuhr und"); after review it reads the word break from
   the layout's words of the line instead (`src/redact/text_redactor.cpp`
   passes them), so the two cannot disagree. This edits `src/redact/`,
   which §0 assigned to other branches; no other branch held those files.
7. **Files left in the layout group**, each with its row and reason in
   `tests/panel/text_causes.tsv`: pdfdiff-0017 (DFT3 fixed; zero-size text
   read three ways by the members), pypdf-0019 to -0021 ("habibi" whole;
   the cluster's empty `/ToUnicode` entries read through the program by
   `docs/DESIGN.md` §5.4), pdfminer-0042 (letter spacing by `Tc` read as
   words, as PDFium does) and pdfplumber-0014 (music noteheads). They move
   to the `legitimate` group, with the four Type 3 files.
8. **dccc-0124's digit pairs stay apart** (`OPEN-QUESTIONS.md` §3.5,
   `docs/change-01.md` Appendix C entry 27): the rule that joined them
   also joined pdfplumber-0015's minus sign to its number and was
   withdrawn with its test.
9. **`tests/pdfium/text_causes.tsv` keeps pdfpig-0003**, at 0.9008 with
   PDFium on Linux as at batch B (parent Appendix C entry 23), and the
   manifest keeps its `expect_text`, `known_gap:pdfkit`, which
   `text_agreement.py --write-manifest` turned to `agree` on Linux alone;
   every other moved row of the manifest is a file this batch fixed.
10. **`build-5.md` and `build-6.md` stay at the root** although both panel
    gates pass on this tip: a phase closes only through the owner's
    landing merge (D8), which then carries `Build-Phase: 5 done` and
    `Build-Phase: 6 done`; their §9 holds the numbers.
11. **No ABI or JSON shape moves** (`docs/ABI.md` unchanged): the rung of a
    restated code is `glyph_name`, a value the JSON already carries.
12. **AC6 on Linux**, run `panel-c-full2` (two runs, `main` at 2305b1b
    merged) against the Linux
    baseline `linux-base-09b8d78` (parent Appendix C entry 12) and, for the
    rows that baseline did not measure, against `main` at c7601ae run once
    with the same scripts (`c-main-base`, deleted after the comparison).
    Unchanged: 961 files, both runs identical, no crash, no hang under
    `--timeout-scale 3`; the one real expectation miss is `mutate-0048`'s
    self-check refusal, as in the baseline; re-serialisation 12,121 of
    12,121 streams (the baseline's 11,722 and dccc-0129's 399); images
    6,133 of 6,139 occurrences decoded, continuous-tone 3,688 of 3,724, the
    census equal row for row; Info fields 3,052 of 3,052 (3,048 in the
    baseline; `main` at c7601ae reads the same 3,052); identity writes,
    page selections, the self-check (909 of 917 with the 7 laptop-only
    `office` rows and `mutate-0048`), image treatments and metadata rewrites
    equal `main`'s row for row; leaks: images 0, metadata 0, and in text
    the two words of `OPEN-QUESTIONS.md` §1.3 that PDFium reads on
    damaged pages of opf-0019 and mutate-0046, on `main` too. Pages
    stable outside the rectangles 8,010 of 8,051 (99.49 %), as on `main`.
    Moved, from the text the redaction jobs pick their words from and the
    placeholder fitting's word breaks: codes no rung reads 87,734 of
    16,562,502 (0.53 %) from 88,787 (0.54 %); placeholders found in
    PDFium's text 10,293 of 10,340 (99.55 %) from 9,385 of 9,434
    (99.48 %); located outputs one occurrence fewer in PDFium's text 413 of
    413 from 411 of 411; occurrence-form matches 213 of 213 from 214 of
    214. After review the gate ran again as `panel-c-full2` (the first,
    `panel-c-full`, deleted): the reading and bitonal numbers equal the
    first run's. The ABI tier passes from .NET and Python
    (31 files, 717 calls each).
