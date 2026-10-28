# CHANGE-01b — Majority judging: the reading and bitonal gates re-expressed by the panel, and three reading choices re-decided by it

| Field | Value |
|---|---|
| Status | awaiting owner launch |
| Owner approval | with the parent |
| Depends on | `change-01a.md` |
| Supersedes | nothing |
| Superseded by | nothing yet |
| Batches | batch B of `docs/change-01.md` |
| `SessionVersion` | not applicable |
| Behaviour change | none: gate scripts, the judge and documents; the library is not rebuilt with any change |
| New dependency | none beyond batch A's members |
| Minimum model | `opus` |
| Duration and price | 4.5 to 5.5 hours, $75 to $100 at list prices (parent §11.1) |

Read `docs/change-01.md` §0 to §8 first. This file is §9 onwards for batch B
alone.

---

## 0. Context

Inherited from the parent. Facts the batch needs beside it:

- **What batch A left.** Every present member's answers are cached for the
  960 files (`tests/panel/answers.py` `cached_answer`), `tests/panel/members.tsv`
  says which member has which capability, including the pixel facts of the
  parent's D7, and `tests/panel/smoke.tsv` is the smoke set. Read batch A's
  §C first: a member it could not fetch is absent, and every rule below counts
  the voters that are present.
- **pdfkit's side of each comparison** comes from a dataset run, exactly as
  the old gates read it: the text is `text.text` of each outcome
  (`tests/pdfium/text_agreement.py` `score_run`, `:64-97`, says which blocks
  are in it and why), and the bitonal pixels are the census's dumps
  (`tests/pdfium/image_agreement.py`, `--keep`, and `compare_with_pdfium`,
  `:232-283`). Import those helpers rather than copying them; edit nothing
  under `tests/pdfium/`.
- **The old gates stay as they are** (parent D8): `text_agreement.py` and
  `image_agreement.py` keep their PDFium meaning, because the release gate
  re-runs them. The panel gates are new scripts beside
  `tests/dataset/gates/phase7.py` to `phase10.py`, named after the gate they
  re-express.

---

## 9. The work

### 9.1 The judge (`tests/panel/judge.py`)

Pure functions over answers already in hand; it starts no process.

| Function | Rule |
|---|---|
| `text_verdict(pdfkit_text, answers)` | the voters are the members whose `text` answer is `ok`; pdfkit's F1 against each (`tests/pdfium/words.py` `f1`); `counts_against` when F1 is below 0.90 against a strict majority of the voters; `undecided` with fewer than 3 voters; else `agrees`. Returns the voters, each F1, the median F1 and the verdict (parent D6, DR2) |
| `pixel_verdict(pdfkit_pixels, answers)` | the voters are the members whose `image_pixels` answer has the image's width and height with `/Decode` applied (`tests/panel/members.tsv`); the majority answer is the sample array held identically by more than half of them; `counts_against` when pdfkit's samples differ from it; `undecided` with fewer than 2 voters or no majority (parent D7, DR2) |
| `colour_distance(pdfkit_rgb, answers)` | the mean absolute difference per channel of pdfkit's RGB against each voting member's, the measure of the CMYK re-decision |
| `member_agreement(answers_by_file)` | for every pair of text members, the median F1 between them over the files both read: reported, never used to drop a member (D6) |

`tests/panel/test_judge.py`, written first and seen red: a 3-of-5 majority, a
2-2 tie with one abstention (`undecided`), a member that times out abstaining,
pixels where two of three members agree, a member at the wrong size
abstaining, F1 thresholds at exactly 0.90.

### 9.2 The two gates

| File | What it does |
|---|---|
| `tests/dataset/gates/phase5_panel.py` | `--run <run-id>`: reads the run's outcomes and the panel's cached answers (`--collect` asks a member for what is missing, otherwise a missing answer is an abstention, reported). Per file `text_verdict`. The gate: the median over decided files of pdfkit's median F1 is at least 0.97, and at most 5 % of the decided files count against pdfkit (`build-5.md` §4 item 1, thresholds unchanged). Writes `phase5_panel.json` and phase5_panel.md into the run folder: the two numbers, every file counting against pdfkit with its cause (from `tests/pdfium/text_causes.tsv` when it has the file, else `unclassified`), every `undecided` file with why, the member-pair agreement table, per-tag medians, and how each of the 56 files of `tests/pdfium/text_causes.tsv` moved |
| `tests/dataset/gates/phase6_panel.py` | `--run <run-id>`: over the bitonal images of the census (JBIG2, CCITT and other 1-bit images, as `image_agreement.py` counts them), `pixel_verdict` per image. The gate: 100 % of the images with a majority agree with it (`build-6.md` §4 item 2's threshold, unchanged). Writes `phase6_panel.json` and phase6_panel.md: agreed over decided, every disagreement and every `undecided` image with why, and the verdicts on qpdf-0026, synth-0263 and synth-0265 named |
| `tests/dataset/test_panel_gates.py` | both scripts over a run folder and cached answers written in code |
| `tests/panel/text_causes.tsv` | `id`, `group`, `evidence`: one row per file the reading panel gate counts against pdfkit, `group` one of `cjk_cmap`, `cjk_width`, `type3_code_names`, `subset_unreadable`, `layout`, `other`; the list batch C works from |

Neither script edits the manifest. Both name the gate by what it measures in
their comments (parent §0.2).

### 9.3 The three re-decisions (`tests/panel/redecide.py`)

Each outcome's action is fixed here, so the session never waits (parent D3).

| Choice | Measure | Majority confirms the current rule | Majority against it |
|---|---|---|---|
| **R1** Type 3 glyph names that restate their code (`docs/DESIGN.md` §5.2; the files tagged `differences_cdd`) | per file, the text members' words on the pages of those fonts: a member "reads the names" when its text there holds at least 10 words and agrees with another member's at F1 0.90 or more while pdfkit reads those codes unreadable | settled, kept: one sentence in `docs/DESIGN.md` §5.2 with the measured numbers; the 6 files become `legitimate` in `tests/panel/text_causes.tsv` | **settled for reading**: the files stay in `tests/panel/text_causes.tsv` under `type3_code_names`, and the reading gate's item in `build-5.md` §9 says "CR5 ordered" with the reading the majority uses (the number as decimal for `Cdd`, as hexadecimal for `Cxx`, or the code itself), measured on the files; batch C reads both |
| **R2** CMYK through PDFium's Adobe table (`build-6.md` §9 item 5) | over the DeviceCMYK, Separation and DeviceN images with a CMYK alternate, `colour_distance` per image; the table holds when, on a majority of images, a majority of the voters lie within a mean of 2 per channel | settled, kept: one sentence in `docs/DESIGN.md` §9.4 and the old §2.7 closed | an owner question in `OPEN-QUESTIONS.md`: the measured numbers, the conversion the majority sits closest to, and its price (about 0.5 to 2 hours, $10 to $40: one function, its tests, the notice row and a gate run); the table stays until answered |
| **R3** a missing `/MediaBox` read as US Letter (`docs/DESIGN.md` §4; `src/syntax/page_tree.cpp:25`) | `tests/panel/specimens.py` writes, at run time into `build/cache/panel/specimens/`, three one-page files: no `/MediaBox` anywhere; `/MediaBox` only on the `/Pages` root (the control); no `/MediaBox` but a `/CropBox`. The `pages` members report each page's size. Also counted: dataset files where `panel-lopdf` finds a page with no reachable `/MediaBox` | settled, kept: `docs/DESIGN.md` §4's "as viewers default it" becomes the measured fact with its numbers, and `docs/SOURCES.md`'s row turns from recalled to measured | an owner question: what the majority does (another size, or a refusal), and its price (about 1 hour, $15 to $25: the constant, its unit test, the §4 line and a gate run); Letter stays until answered |

A choice where no majority forms (too few voters, or a split) is settled as
kept, with the split recorded as its evidence.

### 9.4 Execution order inside this batch

The test rhythm of `CLAUDE.md` §8.2 and the stop rule of the parent §0.2 hold
throughout: at 5 h 30 of wall-clock stop, commit what passes and record what
is left.

1. **Base check.** On batch A's tip, `ctest --preset unit` and
   `python build/check.py` green; `python tests/panel/fetch.py --verify` green.
2. **Tests first**: `tests/panel/test_judge.py` and
   `tests/dataset/test_panel_gates.py`, seen red.
3. **The judge** (§9.1), then its tests green.
4. **One dataset run**, which both gates read:
   `cmake --preset mingw-release && cmake --build --preset mingw-release`,
   `python tests/dataset/run.py --checks open,text,images --runs 1 --run-id <id>`,
   `python tests/pdfium/text_agreement.py --run <id>`,
   `python tests/pdfium/image_agreement.py --out dataset/reports/<id>/ --keep`.
   Read each script's `--help` first; the flags above follow `build-5.md` §9
   item 15 and `build-6.md` §9 item 3. The old scripts' numbers must equal
   `build-10.md` §9 item 22's (E3): a difference is a finding recorded in §C
   before going on.
5. **Mid-batch smoke**: both gate scripts restricted to `tests/panel/smoke.tsv`
   (`--smoke`), read against the expectations of the parent's §1.2.
6. **The gates over the full run**, once (§9.2), and `tests/panel/text_causes.tsv`
   written from the reading gate's report.
7. **The re-decisions** (§9.3), with their actions.
8. **Kill criterion.** Quantity: the share of files `undecided` by the reading
   gate. Threshold: more than 10 % of the files the gate measures. Then the
   panel is too thin to judge: finish the batch, record the numbers, and write
   an owner question (which member to add or why members abstain, with the
   price of each answer); batch C proceeds on the decided files.
9. **Test sweep**: the panel's tests, `ctest --preset unit`,
   `python build/check.py`.
10. **Documentation sweep**, from what landed: `build-5.md` §9 and `build-6.md`
    §9, one item each: the rule (parent D2), the commands, the numbers, the
    files or images that count against pdfkit and the `undecided` ones, and
    whether the gate passes under the panel; `docs/DESIGN.md` §4, §5.2 or
    §9.4 lines as §9.3 settled them; `OPEN-QUESTIONS.md` the questions §9.3
    and step 8 raised, each with the recommended answer first and its price;
    `docs/SOURCES.md` the judge's rules and the re-decisions' evidence;
    `build/paths_pending.tsv` without this batch's rows.
11. **Merge gate** on the tip: `python build/check.py --full`; push with
    `Change-Batch: 01b done`.

---

## 10. Code and documentation references

### 10.1 Files this batch touches

```
tests/panel/judge.py, redecide.py, specimens.py, text_causes.tsv, test_judge.py   (new)
tests/dataset/gates/phase5_panel.py, phase6_panel.py                             (new)
tests/dataset/test_panel_gates.py                                                (new)
build-5.md, build-6.md                                                           (§9, one item each)
docs/DESIGN.md                                                                   (§4, §5.2 or §9.4, one line each, as settled)
OPEN-QUESTIONS.md, docs/SOURCES.md, build/paths_pending.tsv                      (documentation sweep)
```

### 10.2 Authoritative documentation to update

| File | Edit |
|---|---|
| `build-5.md` §9 | the reading gate under the panel |
| `build-6.md` §9 | the bitonal gate under the panel |
| `docs/DESIGN.md` §4, §5.2, §9.4 | as §9.3 settles them |
| `OPEN-QUESTIONS.md` | the questions §9.3 and step 8 raise |
| `docs/SOURCES.md` | the judge's rules; the `/MediaBox` row's status |

---

## 12. Tests

**New**

| Test | Tier | Asserts |
|---|---|---|
| `tests/panel/test_judge.py` | panel | DR1, DR2, D6, D7 as §9.1 lists |
| `tests/dataset/test_panel_gates.py` | dataset tooling | both gate scripts' numbers and lists over a run folder written in code |

**Updated, deleted:** none.

---

## 13. Acceptance criteria

| `ACx` | Criterion | Measure | Gate |
|---|---|---|---|
| AC2 | The reading gate under the panel | `phase5_panel.py` over the full run gives the median and the share counting against pdfkit over the decided files, lists every `undecided` file, and the numbers are in `build-5.md` §9 | pre-merge |
| AC3 | The bitonal gate under the panel | `phase6_panel.py` gives agreed over decided images, names its verdict on qpdf-0026, synth-0263 and synth-0265, and the numbers are in `build-6.md` §9 | pre-merge |
| AC4 | The three re-decisions | each of R1 to R3 has its measured majority and the action §9.3 assigns, with its evidence in a build file's §9 or `docs/DESIGN.md`, and a question in `OPEN-QUESTIONS.md` where it flips | pre-merge |
| AC4a | The old gates unchanged | step 4's old-script numbers equal `build-10.md` §9 item 22's | pre-merge |
| AC7 | Suites and checks | the panel's tests, `ctest --preset unit` and `python build/check.py --full` green on the tip | pre-merge |

---

## 15. Traceability matrix

| Item | Decided by | Proven by | Held by |
|---|---|---|---|
| F1, CR2, DR1, DR2 | D2, D6 | AC2 | `phase5_panel.py`, `test_judge.py` |
| F2 | D2, D7 | AC3 | `phase6_panel.py`, `test_judge.py` |
| F5 | D3, D12 | AC4 | `redecide.py`, `specimens.py` |
| D8 | D8 | AC4a | the old scripts' run |

---

## 16. Rollback and irreversible cost

**How to undo the code.** Revert the batch. **What cannot be undone:**
nothing. **What the owner must be told:** the questions §9.3 and step 8 raise,
at the end of the batch.

---

## 17. Opening prompt

> Read `CLAUDE.md` (§7.5, §8 and §11 closely), `build-5.md` §4 and §9,
> `build-6.md` §4 and §9, and `build-10.md` §9 item 22, then read
> `docs/change-01.md` §0 to §8 and all of `docs/change-01b.md`, and implement
> it exactly as written. Batch A is finished: read its §C in
> `docs/change-01a.md`, `tests/panel/members.tsv` and `tests/panel/answers.py`
> first. Before planning, read `tests/pdfium/text_agreement.py`,
> `tests/pdfium/words.py`, `tests/pdfium/image_agreement.py` (the census, the
> comparison and `agrees`), `tests/pdfium/text_causes.tsv` and
> `tests/dataset/gates/phase7.py` for a gate script's shape. Develop and push
> on the branch and in the working tree the launcher's preamble names, or,
> when started by hand, on change/01-b-judge branched from the tip of
> change/01-a-readers, in a worktree of its own. Begin by running
> `ctest --preset unit` and `python build/check.py` on that base; if a test is
> still red run alone, repair the base in a commit of its own before you start
> and record the repair as a deviation. Follow §9.4 with its test rhythm: the
> narrow tests while coding, the smoke set once mid-batch, one dataset run and
> one full gate run at the end, `python build/check.py --full` at the end;
> never re-run a full tier to be sure. Every re-decision's outcome has its
> action in §9.3: take it, and do not wait for the owner. This session lasts
> at most 6 hours: at 5 h 30 stop starting work, commit what passes, record
> what is left in §C and in `docs/change-01c.md`, and push. Finish by pushing a
> last commit whose message carries the trailer `Change-Batch: 01b done`, or
> `Change-Batch: 01b incomplete` with the reason in your final message, and
> report the elapsed time, the model spend, both gates' numbers and each
> re-decision's outcome. Do not start batch C and edit nothing under `src/`,
> `tests/pdfium/` or `dataset/manifest.tsv`. If you must deviate from the
> brief, report each deviation, ask where the answer is the owner's, update
> `docs/change-01c.md` and the batch index in `docs/change-01.md` so they
> describe what you actually left behind, and record it in this file's §C and
> in the merge message.

---

## Appendix C — Deviations recorded by this batch

1. *(none yet)*
