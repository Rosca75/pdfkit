# CHANGE-01 — A panel of reference readers replaces PDFium as the only judge, the reading and bitonal gates are re-judged by its majority, and pdfkit's own text misses that the panel confirms are fixed

| Field | Value |
|---|---|
| Status | awaiting owner launch |
| Owner approval | decided by the owner on 2026-10-28 (`OPEN-QUESTIONS.md` §2.2 and §2.3 on branch chore/owner-answers, and the brief of that date); the author's choices flagged in §7 are for the owner to confirm or flip before launch |
| Depends on | nothing |
| Supersedes | nothing |
| Superseded by | nothing yet |
| Batches | `change-01a.md` (the panel's readers, adapters and cached answers), `change-01b.md` (majority judging, the re-expressed gates, the three re-decisions), `change-01c.md` (pdfkit's text misses the panel confirms, and the documentation sweep) |
| `SessionVersion` | not applicable: pdfkit persists no session file. The C ABI version stays 1 and no JSON shape of `docs/ABI.md` moves |
| Behaviour change | none in batches A and B (test tiers, gate scripts and documents only); yes in batch C: text extraction reads CJK fonts with a predefined CMap, Latin codes of a non-embedded CJK font, and the layout cases the panel confirms, checked against the 960-file dataset and the panel |
| New dependency | test-only, never linked, vendored or shipped: Poppler's `pdftotext` and `pdfimages`, MuPDF's `mutool`, QPDF, and three Rust crates (pdf_oxide, pdf-extract, lopdf) built by a pinned Rust toolchain, all fetched by hash into the main checkout's `build/cache/` (`CLAUDE.md` §6, "Reference readers judge as a panel, test-only"). Shipped: the Adobe CMap resources (BSD-3), batch C, a row in `third_party/inventory.tsv` and in `CLAUDE.md` §6's vendored table |
| Minimum model | `opus`, for every batch |

```json
{
  "launcher": 1,
  "order": "01",
  "status": "awaiting owner launch",
  "dependsOn": [],
  "minimumModel": "opus",
  "slug": "panel",
  "batches": [
    {
      "letter": "a",
      "dependsOn": [],
      "parallelWith": [],
      "minimumModel": "opus",
      "slug": "readers",
      "owns": ["tests/panel/", "build/check_network.py", "build/toolchain.md", "build/paths_pending.tsv", "docs/SOURCES.md", "CLAUDE.md", "dataset/README.md"]
    },
    {
      "letter": "b",
      "dependsOn": ["a"],
      "parallelWith": [],
      "minimumModel": "opus",
      "slug": "judge",
      "owns": ["tests/panel/judge.py", "tests/panel/test_judge.py", "tests/panel/specimens.py", "tests/panel/redecide.py", "tests/panel/text_causes.tsv", "tests/dataset/gates/phase5_panel.py", "tests/dataset/gates/phase6_panel.py", "tests/dataset/test_panel_gates.py", "build-5.md", "build-6.md", "OPEN-QUESTIONS.md", "docs/DESIGN.md", "docs/SOURCES.md", "build/paths_pending.tsv"]
    },
    {
      "letter": "c",
      "dependsOn": ["b"],
      "parallelWith": [],
      "minimumModel": "opus",
      "slug": "text-fixes",
      "owns": ["src/fonts/", "src/text/", "tests/unit/fonts/", "tests/unit/text/", "third_party/adobe-cmaps/", "third_party/inventory.tsv", "THIRD-PARTY-NOTICES.md", "build/fonts/", "build/check_generated.py", "build/budgets.tsv", "dataset/manifest.tsv", "tests/panel/text_causes.tsv", "tests/pdfium/text_causes.tsv", "docs/DESIGN.md", "docs/SOURCES.md", "CLAUDE.md", "dataset/README.md", "build-5.md", "build-6.md", "docs/archive/build/", "OPEN-QUESTIONS.md", "build/paths_pending.tsv"]
    }
  ],
  "shared": [
    { "path": "tests/panel/", "batches": ["a", "b", "c"] },
    { "path": "docs/SOURCES.md", "batches": ["a", "b", "c"] },
    { "path": "build/paths_pending.tsv", "batches": ["a", "b", "c"] },
    { "path": "CLAUDE.md", "batches": ["a", "c"] },
    { "path": "dataset/README.md", "batches": ["a", "c"] },
    { "path": "docs/DESIGN.md", "batches": ["b", "c"] },
    { "path": "build-5.md", "batches": ["b", "c"] },
    { "path": "build-6.md", "batches": ["b", "c"] },
    { "path": "OPEN-QUESTIONS.md", "batches": ["b", "c"] }
  ]
}
```

---

## 0. Context

You are executing a change order against the existing **pdfkit** repository: a
stand-alone C++20 library with a plain C ABI that reads PDF files into text,
images and metadata and redacts them into a new PDF, with a command-line tool
(`pdfkit`) that drives every dataset run. It builds with CMake and Ninja under
the pinned WinLibs GCC on Windows (`build/toolchain.md`). Its test tiers are
listed in `CLAUDE.md` §8.1.

### 0.1 What this order is

A test-tier capability, two gate re-expressions and a fonts and layout fix
batch. Today one reader, PDFium, is the truth every reading gate is measured
against, so a file PDFium misreads counts against pdfkit exactly like a file
pdfkit misreads: the phase 5 reading gate fails on 56 of 898 files of which
PDFium's own misreadings are 27, and the phase 6 bitonal gate fails on three
images where PDFium's bitmap call ignores what its own renderer applies. After
this order, five text readers and three pixel readers answer for every dataset
file from a cache, a file counts against pdfkit only when pdfkit disagrees with
the panel's majority, three reading choices taken on PDFium's word alone are
re-decided by that majority, and the text misses the panel confirms as
pdfkit's own are fixed.

### 0.2 Ground rules

- `CLAUDE.md` and `docs/DESIGN.md` remain authoritative. Nothing here weakens
  "the library takes bytes and returns bytes", the layer table of `CLAUDE.md`
  §3.1, or the leak measures, which stay at zero.
- **No panel member is linked, vendored or shipped.** Each runs as a separate
  process started by `tests/`, its binary fetched or built by hash into the
  main checkout's `build/cache/`, as PDFium is (`CLAUDE.md` §6, "PDFium is
  test-only" and "Reference readers judge as a panel, test-only").
  `build/check_artefact.py` (the DLL's imports) and `build/check_network.py`
  stay green; the panel's fetch tool is a named exemption of the latter, with
  its reason, like `tests/pdfium/fetch.py`.
- **Batch C adds one shipped dependency, the Adobe CMap resources (BSD-3)**,
  and nothing else. It gets its row in `third_party/inventory.tsv`, its notice
  in `THIRD-PARTY-NOTICES.md` and its row in `CLAUDE.md` §6's vendored table.
- **A change is not finished until its tests move with it** (`CLAUDE.md` §8.2).
  The gating suites are `ctest --preset unit` and `python build/check.py` per
  batch of commits, the dataset tier (`python tests/dataset/run.py`) and the
  gate scripts once at the batch end, and `python build/check.py --full` with
  `ctest --preset sanitize` and `ctest --preset clang-sanitize` at the merge.
- **Tests run at planned points, not after every edit** (`CLAUDE.md` §8.2 on
  chore/owner-answers): while coding, the unit tests of the component touched
  (`pdfkit_tests -tc=...`, or `python -m unittest discover -s tests/panel -p
  "test_*.py"` for the panel's Python, which runs no member, as
  `tests/pdfium/test_results_cache.py` runs no PDFium); the smoke set of about 60 files (`tests/panel/smoke.tsv`,
  written by batch A) once mid-batch; the full gate once at the batch end and
  again only after a fix to one of its failures. A full tier is never re-run
  "to be sure": the full check and a gate run take 17 and 23 minutes.
- **Every session is at most 6 hours, with a stop rule at 5 h 30.** At 5 h 30 of
  wall-clock the session stops starting work, commits what passes its narrow
  tests, records in its own §C what is left and where it stopped, updates the
  downstream batch files and the index (§11.5), and pushes with the trailer
  `Change-Batch: 01<letter> incomplete`.
- **Comments state intent in the present tense** and cite no phase, build file
  or change order (`build/check_comments.py` fails on "phase 5", "build-5" and
  "change-01" in a comment). Name a gate by what it measures ("the reading
  gate", "the bitonal gate"). File names such as `phase5_panel.py` are not
  comments and follow `tests/dataset/gates/`'s existing names.
- **No PDF is committed** (`build/check_repo.py`). The specimens batch B needs
  are written at run time into the main checkout's `build/cache/`.
- **Every external fact carries its source and its status** (`CLAUDE.md` §9,
  `docs/SOURCES.md`). Every fact about a panel member in this order (a release
  asset's name, a command-line flag, a library call's behaviour) is recalled
  unless marked read; the batch that relies on it reads it at the source, or
  tests it on a specimen, and records the status in `docs/SOURCES.md`.
- **This order changes authoritative rules**: the reading and bitonal gates'
  counting rule (`build-5.md` §4, `build-6.md` §4, recorded as deviations in
  their §9), `CLAUDE.md` §4 (vocabulary), §6 (one vendored row), §7.5 and §8.1
  (the panel beside PDFium), and `docs/DESIGN.md` §5.2, §5.3 and §7 where batch
  C changes the reader. See §10.

### 0.3 The deviation rule

If a step below is wrong, contradicted by the code, or cannot be done as
written: **stop, say so, and propose the alternative before writing it.** Then
follow the deviation protocol: report it, ask if the answer is the owner's,
update the downstream batch files, and record it in §C. The measurements are
reproducible; if yours disagree with a number in §1.2, that disagreement is the
finding and it outranks the plan.

### 0.4 Where the work stands

| Fact | Value |
|---|---|
| Repository | Rosca75/pdfkit on GitHub (`origin`, over SSH) |
| Branch | a fresh branch per batch, `change/01-<letter>-<slug>` (change/01-a-readers, change/01-b-judge, change/01-c-text-fixes), branched from the tip of the batch it depends on (A from `main`), in a worktree of its own (`CLAUDE.md` §11) |
| Prerequisite on `main` | the branch chore/owner-answers merged: it carries the `CLAUDE.md` §5, §6 and §8.2 bullets and the `OPEN-QUESTIONS.md` this order obeys and cites. A batch started on a `main` without them stops and says so |
| Suites | `ctest --preset unit` and `python build/check.py`, both green on `main` at 7cd2968 |
| Merge gate | `python build/check.py --full`, `ctest --preset sanitize`, `ctest --preset clang-sanitize`, `python tests/abi/run.py` (`CLAUDE.md` §8.1, §10) |
| Dataset tier | `python tests/dataset/run.py --checks open,text --runs 2 --run-id <id>` (and `open,images` for the bitonal gate); the 960 files under the main checkout's `dataset/files/` |
| PDFium tier | `python tests/pdfium/text_agreement.py --run <id> --hidden`, `python tests/pdfium/image_agreement.py --out <run folder>` (census and `--listing`); answers cached in `build/cache/pdfium-results/` |
| Scoping rule | `CLAUDE.md` §8.2, "Tests run at planned points". Do not run a whole tier per edit |
| Completion trailer | `Change-Batch: 01a done` (`01b`, `01c`), or `Change-Batch: 01<letter> incomplete` with the reason |

### 0.5 What the repository does today, established by reading it

| Fact | Where |
|---|---|
| PDFium runs in worker processes behind a pool with a 30 s limit; nothing loads it in the caller's process | `tests/pdfium/pdfium.py` `Pool`, `Worker`, `serve` |
| PDFium's answers about an input file are cached per file SHA-256, PDFium release and archive digest, request and `SCHEMA`; transient failures (`timeout`, `crash`, `worker`) are never kept; records are written atomically | `tests/pdfium/results_cache.py` `record_path`, `keepable`, `write_bytes`, `cached_probe` |
| The shared caches are resolved in the main checkout through `common.pdfium_cache_dir()`; `build/cache/` is ignored by git | `dataset/tools/common.py`; `.gitignore:4` |
| The fetch tool pins a release tag, an asset URL and its SHA-256, refuses a mismatch, and is a named exemption of the network check | `tests/pdfium/fetch.py:30-33`; `build/check_network.py:21-25` |
| The reading gate's metric: NFKC, case-folded, words of two or more letters or digits, multiset F1 | `tests/pdfium/words.py` `words`, `f1` |
| The reading gate's thresholds are `AGREE = 0.90`, `MEDIAN = 0.97`, `SHARE_BELOW = 0.05`; a file below 0.90 is named with its cause from `tests/pdfium/text_causes.tsv` | `tests/pdfium/text_agreement.py:43-46`, `read_causes` |
| The 56 files below 0.90 and their causes: 21 `pdfkit`, 27 `pdfium`, 8 `legitimate`, one line of evidence each | `tests/pdfium/text_causes.tsv` |
| The bitonal comparison uses `FPDFImageObj_GetBitmap`, aligned with pdfkit's image by the SHA-256 of the encoded data | `tests/pdfium/image_agreement.py:10-11`, `:128-137`, `:318-330` |
| The release gate gathers phases 3 to 10 into one table and re-measures phases 3 to 6 itself | `tests/dataset/gates/phase10.py` header; `build-10.md` §9 item 22 |
| Gate scripts live beside each other as `phase<n>.py`; there is none for phases 5 and 6 (`text_agreement.py` and `image_agreement.py` are their gates) | `tests/dataset/gates/` |
| A glyph name `Cdd`, `Cxx`, `gNN` or `cidNN` is read only through the font program; without one (every Type 3 font) the code is unreadable | `docs/DESIGN.md` §5.2; `src/fonts/decoding_ladder.cpp:40` `numbered`, `:179`; `src/fonts/glyph_list.cpp:204` |
| A predefined CMap other than the identities and the Unicode CMaps ships no data; its codes are split by the implied codespace and counted unreadable | `docs/DESIGN.md` §5.3; `src/fonts/type0_font.cpp:46` `predefined_codespace` |
| A CID font's `/DW` is read when present, else 1000 | `src/fonts/type0_font.cpp:175`, `:292` |
| A missing `/MediaBox` is US Letter, "as viewers default it" | `src/syntax/page_tree.cpp:25`; `docs/DESIGN.md` §4 (line 247) |
| CMYK becomes RGB through PDFium's Adobe table, ported | `src/images/adobe_cmyk.cpp`; `build-6.md` §9 item 5 |
| Generated data follows one pattern: the upstream files under `third_party/<name>/`, a generator under `build/fonts/` with a lock file, the output under `src/fonts/generated/`, held equal by `build/check_generated.py` | `third_party/agl/`, `build/fonts/glyph_list.py`, `build/fonts/core14.lock`, `src/fonts/generated/` |
| The size budgets: DLL 7,242,240 bytes against 5,793,792 at the first whole-catalogue package (1,448,448 bytes of headroom) | `build/budgets.tsv` |
| A path an authoritative document cites in backticks must exist, be ignored by git, or be listed in `build/paths_pending.tsv` with the work that creates it; a pending row whose path exists fails | `build/check_paths.py`; `build/paths_pending.tsv` |
| The laptop has no Rust toolchain: no `cargo` or `rustc` on the path and no `.cargo` or `.rustup` folder in the user profile (checked 2026-10-28) | by hand |
| Python's `urllib` and `git` reach GitHub through the proxy; `curl` needs `--ssl-no-revoke` | `build/toolchain.md` §5 |

---

## 1. Baseline and evidences

### 1.1 What the owner provides

| `Ex` | Material | Where it lives | What it lets a session prove | How to use it |
|---|---|---|---|---|
| E1 | The phase 5 plan: the reading gate, its kill criterion and its deviations | `build-5.md` §4, §5, §9 items 15, 16, 18 | what the reading gate measures and how it failed | the gate's thresholds are kept; only the counting rule moves |
| E2 | The phase 6 plan: the bitonal gate and its deviations | `build-6.md` §4, §9 items 3, 4 | what the bitonal gate measures and how the census aligns images | the panel gate aligns images the same way |
| E3 | The release gate table on the final dataset | `build-10.md` §9 item 22 | the baseline numbers of §1.2 | re-run with the same commands for the before numbers |
| E4 | The 56 files below 0.90, each with its cause and evidence | `tests/pdfium/text_causes.tsv` | which files the panel must re-judge, and the 21 batch C may fix | batch B's verdict per file is compared row by row |
| E5 | The owner's answers of 2026-10-28 | `OPEN-QUESTIONS.md` §2.2, §2.3, §2.6 on branch chore/owner-answers; the old §2.5, §2.7, §2.10 and §2.15 in `git show 7cd2968:OPEN-QUESTIONS.md` | D1 to D5 | read before planning |
| E6 | The dataset: 960 files, their manifest and tags | `dataset/manifest.tsv`; the files in the main checkout's `dataset/files/` (outside git) | every gate number | `python dataset/tools/fetch.py --verify` before the first run |
| E7 | PDFium's cached answers about every input file | the main checkout's `build/cache/pdfium-results/` (about 210 MB) | PDFium's vote without re-asking it | read through `tests/pdfium/results_cache.py` |
| E8 | The pdf_oxide 0.3.78 evaluation: it does not compile as published unless `office_oxide` is pinned `=0.1.9`; a clean release build takes about 15 minutes on the laptop; positioned words are sometimes joined (`1Scope`) | `C:\Users\oliber001\Documents\Projects\pdf-to-markdown\docs\pdf_oxide-evaluation.md` and that project's `Cargo.toml` (read by the author on 2026-10-28) | that pdf_oxide builds and what its known quirk costs a vote | pin both crates as that project does |
| E9 | PDFium's header on the two bitmap calls: `FPDFImageObj_GetBitmap` "ignores" the image mask and matrix; `FPDFImageObj_GetRenderedBitmap` takes both into account | `fpdf_edit.h`, https://pdfium.googlesource.com/pdfium/+/refs/heads/main/public/fpdf_edit.h (the owner's reading of 2026-10-28; recalled by the author) | why PDFium's bitmap is not a pixel truth | batch A reads the header and records it in `docs/SOURCES.md` |
| E10 | The runs behind E3 | the main checkout's `dataset/reports/p10-final/` and p10-final-robust/ | the before numbers, re-checkable | compare batch B's re-run of the old scripts against them |

### 1.2 The baseline, as numbers

| Measure | Before | Target | Produced by |
|---|---|---|---|
| Reading gate: median word F1 with PDFium | 1.0 over 898 files | kept (at least 0.97, now against the panel) | `text_agreement.py`, run `p10-final` |
| Reading gate: files below 0.90 | 56 / 898 (6.24 %): 21 pdfkit, 27 PDFium, 8 legitimate | at most 5 % of the files counting against pdfkit under the panel rule (batch B), and fewer after batch C | same |
| Bitonal gate: images equal to PDFium's bitmap | 495 / 498 (99.40 %); the three are qpdf-0026, synth-0263, synth-0265 | 100 % of the images on which the pixel panel has a majority (batch B) | `image_agreement.py`, run `p10-final` |
| Text readers that answer for the dataset | 1 (PDFium) | 5 (batch A) | `tests/panel/collect.py` |
| pdfkit-caused files of the 56, by group | Type 3 `Cdd` names 6; CJK CMap data 6; CJK estimated widths 2; unreadable subset fonts 3; layout 3; a math font's unreadable codes 1 | each group closed or re-judged by the panel (batch C) | `tests/pdfium/text_causes.tsv` |
| DLL size | 5,793,792 bytes | within `build/budgets.tsv`, or the budget raised by the measured growth with its reason (D10) | `python build/package.py` |

---

## 2. Findings

### F1 (MEASUREMENT) Half of the reading gate's failures are PDFium's

27 of the 56 files below 0.90 are PDFium misreading the file, each shown by the
file's own text (E4), and 8 are legitimate differences. The gate counts every
file below 0.90 whatever the cause (`text_agreement.py`), so it passes only if
pdfkit fixes at least 13 of its 21. A judge's misreading counted as pdfkit's is
the reason a single reader cannot be the truth. Follows: D1, D2, CR1, CR2.

### F2 (LIMIT) The bitonal gate compares against a call that ignores what a viewer applies

The three disagreements are PDFium's: qpdf-0026's first Group 4 coding line,
which PDFium's bitmap and renderer show white where T.4's tables read black
(`build-6.md` §9 item 4), and synth-0263 and synth-0265, whose `/Decode [1 0]`
PDFium's renderer inverts as pdfkit does while `FPDFImageObj_GetBitmap` hands
back the samples uninverted (E5, old §2.15). E9 says the bitmap call ignores
mask and matrix by its own header. Follows: D2, D7, CR2.

### F3 (GAP) No second reader exists in the test tiers

`tests/` holds one reference reader. Every judgement of a reading choice
(F5) and every gate rests on it. Follows: CR1.

### F4 (GAP) The laptop has no Rust toolchain

Two text members and one structure member are Rust crates (D1). None can be
built without `cargo` and a linker. Follows: D9 and the pre-launch actions of
§16.

### F5 (INCONSISTENCY) Three reading choices rest on one reader or on lore

- Type 3 glyph names that restate their code (`/Differences [65 /C41 ...]`):
  unreadable by `docs/DESIGN.md` §5.2; PDFium reads the code, pdf.js reads the
  number (old `OPEN-QUESTIONS.md` §2.5); 6 of the 56 files.
- CMYK to RGB: PDFium's Adobe table, ported because the naive complement
  failed against PDFium (old §2.7); measured against PDFium alone.
- A missing `/MediaBox` is US Letter "as viewers default it"
  (`docs/DESIGN.md` §4): recalled, never measured.

Follows: D3, batch B's re-decisions.

### F6 (MEASUREMENT) pdfkit's own 21 files fall in six groups

From E4: Type 3 `Cdd` names (mutate-0008, -0011, -0018, -0023, -0028,
pdfjs-0047); CJK predefined CMaps or orderings with no data (pdfdiff-0036,
pdfjs-0053, pdfminer-0039, pdfminer-0044, pdfpig-0003, pdfplumber-0023); Latin
codes of a non-embedded CJK font at `/DW` 1000 (dccc-0012, synth-0151);
subset fonts no rung reads (pdfdiff-0025, pdfdiff-0026, pdfjs-0042); layout
(pdfdiff-0017 a duplicate fold in text at a negative size, pdfdiff-0018 two
lines of very large glyphs read as one, pdfium-0049 a vertical word split at a
0.2 em gap); and pdfpig-0002 (7,493 codes of its fonts unreadable). Follows:
CR3, CR4, CR5, DFT1 to DFT3.

### F7 (LIMIT) The Adobe CMap resources are larger than the DLL's headroom

The resources are 189 files and 6.79 MB (`docs/SOURCES.md`, read); the DLL
budget leaves 1,448,448 bytes. Shipping them as text cannot fit; packed range
tables may. Follows: D10, §16.

---

## 3. Defect log

Each is a deviation from `docs/DESIGN.md` §7 (a line is the glyphs of one
baseline; a word is not split by a gap inside it; a duplicate is a second draw
of the same glyph at the same place), reproduced on a dataset file and named
in E4. Batch C fixes each one the panel confirms as pdfkit's (batch B's
`tests/panel/text_causes.tsv`); one the panel does not confirm is closed there
with its evidence.

### DFT1 (BUG) Two lines of very large glyphs read as one line

**Reproduce:** pdfdiff-0018: glyphs of 500 pt and 660 pt on baselines 80 pt
apart; `pdfkit text` reads "B8 B8" on one line.
**Harm:** the words of two lines interleave; a search for a phrase on either
line misses.
**Why wrong:** `docs/DESIGN.md` §7 groups a line by baseline; the tolerance of
a fifth of the size admits a second baseline at these sizes.
**Fix:** a baseline tolerance that cannot span the distance between two
baselines of the same run sizes; batch C §9.

### DFT2 (BUG) A vertical word split at a small adjustment

**Reproduce:** pdfium-0049: vertical text whose `TJ` adjustments leave a 0.2 em
gap inside "Hello"; pdfkit reads two words.
**Harm:** a search for the word misses it.
**Why wrong:** §7's word gap applies along the advance; the vertical case
measures the gap against the wrong threshold.
**Fix:** batch C §9.

### DFT3 (BUG) A glyph of mirrored text folded as a duplicate

**Reproduce:** pdfdiff-0017: text at a negative font size turns half a turn;
one "o" of "operator" lands within a tenth of the size of another and is
folded as a duplicate.
**Harm:** a letter is lost from the visible text.
**Why wrong:** §7's duplicate is a second draw of the same code; two different
positions of one word are not.
**Fix:** batch C §9.

---

## 4. Change requests

| `CRx` | The request | Why it does not exist today | What it touches | In this order? |
|---|---|---|---|---|
| CR1 | A panel of reference readers answering for every dataset file, cached per file and member version | `tests/` holds one reader (F3) | `tests/panel/`, `build/cache/` | yes, batch A |
| CR2 | Gates and reading choices judged by the panel's majority | no judge exists beside PDFium | `tests/panel/judge.py`, `tests/dataset/gates/` | yes, batch B |
| CR3 | Read CJK fonts with a predefined CMap or an Adobe ordering and no `/ToUnicode` | no CMap data ships (`docs/DESIGN.md` §5.3) | `third_party/adobe-cmaps/`, `src/fonts/` | yes, batch C |
| CR4 | Latin codes of a non-embedded CJK font with no `/W` advance at half width | the specification's `/DW` default of 1000 is applied as written | `src/fonts/type0_font.cpp` | yes, batch C |
| CR5 | Type 3 glyph names that restate their code, read as the panel majority reads them | `docs/DESIGN.md` §5.2 reads them only through a program | `src/fonts/decoding_ladder.cpp`, `src/fonts/glyph_list.cpp` | yes, batch C, only if batch B's measure says so (D3) |

---

## 5. Design rules

| `DRx` | The rule | Why, and the failure it prevents | Enforced by |
|---|---|---|---|
| DR1 | A gate or a reading choice is judged by the panel's majority; a file counts against pdfkit only when pdfkit disagrees with that majority | one reader's quirks counted as truth make pdfkit's misses and the reference's indistinguishable (F1, F2) | `tests/panel/test_judge.py`; the two panel gate scripts |
| DR2 | A member that does not answer for a file (refuses, times out, crashes) abstains; a text verdict needs at least 3 voting members and a pixel verdict at least 2, else the file or image is `undecided`, listed and counted neither way | a verdict of one or two voters is the single-judge problem again | `tests/panel/test_judge.py` |
| DR3 | A member's answer is cached under the file's SHA-256, the member, its version and its binary's digest, the request and a schema number; a transient failure is never cached | the answers do not change between runs, and a cache keyed on less serves a stale answer after an upgrade | `tests/panel/test_answers.py` |
| DR4 | No panel member is linked, vendored or shipped; each is a separate process started by `tests/` | `CLAUDE.md` §6: an independent reader that the library depends on is not independent, and GPL and AGPL readers are allowed only there | `build/check_artefact.py`, `build/check_network.py`, `build/check_third_party.py` |

---

## 6. Vocabulary added or retired

Rows batch C adds to `CLAUDE.md` §4 in its documentation sweep:

| Term | Definition |
|---|---|
| reference panel | the test-only readers that judge a reading choice or a gate: PDFium, Poppler's `pdftotext`, MuPDF's `mutool`, pdf_oxide and pdf-extract for text; PDFium, Poppler's `pdfimages` and MuPDF's `mutool` for image pixels; QPDF and lopdf for structure |
| panel majority | the answer held by more than half of the members that answered for a file or an image |
| undecided | a file or image with too few voters (DR2) or no majority; listed in every gate report and counted neither way |

**Retired:** "independent reader" as a name for PDFium alone; it becomes "a
member of the reference panel". The identifier side does not move:
`tests/pdfium/` keeps its name and its scripts.

---

## 7. Decisions confirmed

| `Dx` | Decision | Source | What it buys, and what it costs |
|---|---|---|---|
| D1 | The panel: for text PDFium, Poppler `pdftotext` (GPL, C++), MuPDF `mutool` (AGPL, C++), pdf_oxide (Rust, MIT OR Apache-2.0, crate 0.3.78 with `office_oxide` pinned `=0.1.9`) and pdf-extract (Rust, MIT); for structure QPDF and lopdf. All test-only, separate processes, fetched or built by hash into the main checkout's `build/cache/`, answers cached per file SHA-256 and reader version as `build/cache/pdfium-results/` does; never linked or shipped | owner, 2026-10-28 | judges that disagree with each other expose a judge's quirk; costs batch A and about 15 minutes of pdf_oxide compile per toolchain change |
| D2 | A file counts against pdfkit only when pdfkit disagrees with the panel majority. Applies to the reading gate (`build-5.md` §4 item 1) and the bitonal gate (`build-6.md` §4 item 2) | owner, 2026-10-28 | the gates measure pdfkit; costs batch B |
| D3 | Re-decided by the panel majority: Type 3 glyph names restating their code (old §2.5), the CMYK conversion (old §2.7), the US Letter default for a missing `/MediaBox`. Each outcome's action is written into batch B §9.3 so no session waits: a majority that confirms the current rule settles it; a majority for `Cdd` read as the code's character orders CR5 in batch C; a majority against the CMYK table or the Letter default is written as an owner question with its price, the rule unchanged until answered | owner, 2026-10-28; the per-outcome actions mine, flagged for review | the three choices rest on evidence; a flip of CMYK or `/MediaBox` costs 0.5 to 1 hour and $5 to $25 when the owner orders it |
| D4 | Sessions are at most 6 hours, with a stop rule at 5 h 30 (commit what passes, record what is left); tests at planned points (`CLAUDE.md` §8.2) | owner, 2026-10-28 | predictable sessions; a batch that runs out of room ends `incomplete` with its split point named |
| D5 | The owner launches this order himself; its status is "awaiting owner launch" | owner, 2026-10-28 | the author's flagged choices can be flipped before any spend |
| D6 | The text verdict: pdfkit's word F1 (`tests/pdfium/words.py`) is computed against each voting member; the file counts against pdfkit when its F1 is below 0.90 against a strict majority of the voters; the gate's median is the median over files of pdfkit's median F1 across voters; thresholds unchanged (median at least 0.97, at most 5 % of decided files counting against pdfkit). Each member's agreement with the others is reported, never used to drop a member | mine, flagged for review; local to `tests/panel/judge.py` | keeps the metric and the thresholds the owner already approved; only the counting moves |
| D7 | The pixel panel: PDFium through `FPDFImageObj_GetRenderedBitmap`, Poppler `pdfimages`, MuPDF `mutool extract`. A member votes on an image only when its answer has the image's own width and height with `/Decode` applied; otherwise it abstains for that image. The page renderers (PDFium, `pdftoppm`, `mutool draw`) are recorded as capabilities in `tests/panel/members.tsv` and not used by this order, whose gates judge decoded images, not pages. pdf_oxide and pdf-extract vote on text only; hayro is not taken (Appendix B) | mine, flagged for review; local to `tests/panel/readers/` and `judge.py` | a pixel vote is cast only where the comparison is exact; costs one capability measurement in batch A |
| D8 | The old gate scripts (`text_agreement.py`, `image_agreement.py`, `phase10.py`) stay and keep their meaning, because the release gate re-runs them; the panel gates are new scripts beside them, `tests/dataset/gates/phase5_panel.py` and `phase6_panel.py`. Batch B records the panel numbers in `build-5.md` §9 and `build-6.md` §9; when a panel gate passes on the landed order, the owner's landing merge carries `Build-Phase: 5 done` or `Build-Phase: 6 done` and batch C has prepared the plan's move to `docs/archive/build/` and the `CLAUDE.md` §1 row | mine, flagged for review | a phase can close on the owner's rule without rewriting the release gate; `phase10.py` gathering the panel tables is left to the release session (§8.2) |
| D9 | Rust: the `x86_64-pc-windows-gnu` toolchain at a pinned stable release, linking with the pinned WinLibs GCC (no MSVC), unpacked by hash into the main checkout's `build/cache/panel/rust/`; `cargo` with `http.check-revoke = false` for the proxy (recalled: a Windows-only cargo setting); `CARGO_TARGET_DIR` in `build/cache/panel/cargo-target/`; `Cargo.lock` committed | mine, flagged for review; the owner may instead install rustup by hand (§16) | one compile shared by every worktree; costs about 15 minutes of compile the first time and 2 to 3 GB of disk |
| D10 | The Adobe CMap resources ship as packed range tables cut by a generator under `build/fonts/` into `src/fonts/generated/`, the upstream text files kept under `third_party/adobe-cmaps/`, only the CMaps and orderings the predefined names need. If the DLL then passes its budget, batch C raises the budget by the measured growth with its reason in `build/budgets.tsv` | the data, owner (old §2.10 (a)); the packing and the budget rule mine, flagged | CJK files read; costs DLL bytes (§16) |
| D11 | The smoke set is `tests/panel/smoke.tsv`, about 60 files: the 21 pdfkit-caused files, 10 PDFium-caused, 4 legitimate, the 3 disputed bitonal images' files, 10 files with DeviceCMYK images and 12 clean files across the text-layout tags | mine, flagged | one mid-batch run of a few minutes instead of a full gate |
| D12 | The `/MediaBox` specimens are generated at run time by `tests/panel/specimens.py` into `build/cache/panel/specimens/`, never committed | mine (required by `build/check_repo.py`) | no dataset row moves; the dataset's 960 files and its denominators stay |
| D13 | The continuous-tone gate (`build-6.md` §4 item 3) stays judged by PDFium: it passes (99.03 %) | mine, flagged | no work where nothing fails |

---

## 8. Scope

### 8.1 In scope

- Batch A: fetch or build, pin by hash and cache the seven members; one adapter
  each; cached answers for the 960 files; the capability table; the smoke set.
- Batch B: the majority judge; the reading and bitonal gates re-expressed as
  new scripts; the panel numbers in `build-5.md` and `build-6.md`; the three
  re-decisions measured and either settled or written as owner questions; the
  list of files the panel confirms as pdfkit's.
- Batch C: CR3, CR4, CR5 when ordered, DFT1 to DFT3 and pdfpig-0002 where the
  panel confirms them; both gates re-run; the order-wide documentation sweep.

### 8.2 Out of scope

- **`phase10.py` reading the panel tables, and the release question.** The
  release (`OPEN-QUESTIONS.md` §2.4, a `0.1.0-preview` tag) is its own
  session; the release gate keeps its PDFium rows (D8).
- **The continuous-tone gate** (D13), **colour management** (old §2.8, kept
  as answered), **the structure gates of phase 3** (they pass; QPDF and lopdf
  answer and are reported, not judged).
- **Page rendering judged by the panel** (phases 7 to 9's pixel stability):
  they pass on PDFium's rendering; D7 records the renderers for a later order.
- **The subset fonts no reader reads** (pdfdiff-0025, -0026, pdfjs-0042) when
  the panel agrees no reader reads them: they become `legitimate`, not work.
- **The other actions of 2026-10-28** (the self-check scope, optional content
  removal, the release notes): their own branches.

### 8.3 Not to be "fixed"

- **The 27 PDFium-caused and 8 legitimate files** keep their causes in
  `tests/pdfium/text_causes.tsv`; pdfkit does not imitate a judge's
  misreading.
- **Shape glyphs of Type 3 fonts** (`/square`, `/rect`, `/triangle` in
  safedocs-0007 and others) stay unread: they draw shapes, not letters.
- **pdf_oxide's joined positioned words** (E8) are that member's quirk, which
  the majority absorbs; no adapter repairs a member's text.
- **The duplicate fold and the self-drawing forms** of `legitimate` rows are
  pdfkit's rules (`docs/DESIGN.md` §6.1, §7).

---

## 10. Code and documentation references

### 10.1 Files this order touches

```
tests/panel/                       (new: batch A; judge, specimens, re-decisions, causes: batch B; causes: batch C)
tests/dataset/gates/phase5_panel.py, phase6_panel.py, tests/dataset/test_panel_gates.py   (new, batch B)
build/check_network.py             (one exemption, batch A)
build/toolchain.md                 (a section on the panel's pins, batch A)
build/paths_pending.tsv            (rows added and removed, every batch)
src/fonts/, src/text/              (batch C)
third_party/adobe-cmaps/, third_party/inventory.tsv, THIRD-PARTY-NOTICES.md   (batch C)
build/fonts/ (a CMap generator), build/check_generated.py (its registration), build/budgets.tsv (batch C)
tests/unit/fonts/, tests/unit/text/ (batch C)
dataset/manifest.tsv               (expect_text and expect_fonts of the files batch C fixes)
tests/pdfium/text_causes.tsv       (rows of files batch C fixes, removed)
```

### 10.2 Authoritative documentation to update

| File | Edit |
|---|---|
| `CLAUDE.md` §1 | the order's row (made with the order); batch C, when a panel gate passed: the build plans' row loses `build-5.md` or `build-6.md` |
| `CLAUDE.md` §3.5 | `tests/panel/` in the layout block (batch A) |
| `CLAUDE.md` §4 | the vocabulary of §6 (batch C) |
| `CLAUDE.md` §6 | the Adobe CMap resources' vendored row (batch C) |
| `CLAUDE.md` §7.5, §8.1, §12 | the panel beside PDFium: its commands and cache (batch C) |
| `docs/DESIGN.md` §4, §9.4 | the `/MediaBox` and CMYK lines, as batch B's measure settles them |
| `docs/DESIGN.md` §5.2, §5.3, §7 | the reader as batch C leaves it |
| `docs/SOURCES.md` | one row per panel member and per fact read about it (batch A); the judge's rules (batch B); the CMap resources' status (batch C) |
| `docs/ABI.md` | unchanged: no JSON shape moves, and batch C says so in its §C |
| `dataset/README.md` §2 | the panel's cache beside PDFium's (batch A) |
| `build/toolchain.md` | the panel's pinned versions and digests, the Rust toolchain, the proxy setting, the collection timings (batch A) |
| `build-5.md`, `build-6.md` §9 | the panel gates' numbers as deviations (batch B; batch C adds its re-run) |
| `OPEN-QUESTIONS.md` | the re-decisions' questions where the panel flips a rule (batch B); §2.2's action marked done (batch C) |

---

## 11. Batches

### 11.1 The batch index

| Batch | File | Owns | Depends on | Parallel with | Minimum model | Ends with |
|---|---|---|---|---|---|---|
| A | `change-01a.md` | `tests/panel/` (created), `build/check_network.py`, `build/toolchain.md`, the `CLAUDE.md` §3.5 line, `dataset/README.md` §2 | nothing | nothing | `opus` | every member fetched or built by hash, its answers cached for the 960 files, `tests/panel/members.tsv` and `tests/panel/smoke.tsv` committed |
| B | `change-01b.md` | the judge, the two panel gate scripts, `tests/panel/text_causes.tsv`, the re-decisions, `build-5.md` and `build-6.md` §9, `OPEN-QUESTIONS.md` | A | nothing | `opus` | both panel gates run on the full dataset with their numbers in the build files; each re-decision settled or asked |
| C | `change-01c.md` | `src/fonts/`, `src/text/`, `third_party/adobe-cmaps/`, the generator, the order-wide documentation sweep | B | nothing | `opus` | the files the panel confirms as pdfkit's fixed or named, both gates re-run, the documents describing what landed |

| Batch | Hours | Model spend at list prices | Calibrated from |
|---|---|---|---|
| A | 5 to 6 (plus about 40 minutes of unattended compile and collection inside it) | $85 to $115 | `docs/archive/build/build-2.md` (the PDFium tier and dataset tools: about 3,500 lines of Python, 9 to 13 h, $130 to $220): about 1,400 lines of Python and 150 of Rust here |
| B | 4.5 to 5.5 | $75 to $100 | the same rate; about 900 lines of Python, two gate runs, three write-ups |
| C | 5.5 to 6 | $95 to $120 | old `OPEN-QUESTIONS.md` §2.10 (a): 8 to 12 h and $150 to $250 for the CMaps, the half-width default, §2.5 (b) and four layout faults; trimmed to the files the panel confirms, with a named split point |
| Order | 15 to 17.5 | $255 to $335 | the owner's price for the panel (§2.2: 10 to 14 h, $180 to $280) covers A and B (9.5 to 11.5 h, $160 to $215) |

Progress is reported at the end of each batch, in its final message and its
last commit: elapsed time, model spend, the gate numbers, and what remains.

### 11.2 The dependency, and why it is real

- **B needs A**: the judge reads the members' cached answers and
  `tests/panel/members.tsv`'s capabilities; without them there is nothing to
  vote.
- **C needs B**: C fixes only the files B's `tests/panel/text_causes.tsv`
  confirms as pdfkit's, and implements CR5 only if B's measure orders it.

### 11.3 Execution recommendation

A strict chain: A, then B from A's finished tip, then C from B's, one session
each, a branch and a worktree per batch, finished and pushed before the next
starts; the order lands on `main` once, after C. Nothing runs in parallel: B
and C share `tests/panel/`, `docs/DESIGN.md`, `build-5.md` and
`OPEN-QUESTIONS.md` with their predecessor, and each needs the predecessor's
result. Split points if a batch runs out of room: A after the text members
(pixels and structure next); B after the reading gate (bitonal gate and
re-decisions next); C after the CMaps (half width, CR5, layout next). Never
split inside the CMap generator and its data, which land together or not at
all.

**Other branches at launch time.** The owner's other actions of 2026-10-28 may
be in flight: the self-check scope (`src/redact/`, `docs/DESIGN.md` §18,
`docs/ABI.md` §4.11, `tests/dataset/gates/phase8.py`, `phase10.py`), optional
content removal (`src/writer/`, `docs/DESIGN.md` §15), and the release notes.
A and B touch none of their files. C touches `src/fonts/` and `src/text/`,
which none of them owns, but shares `docs/DESIGN.md`, `CLAUDE.md`,
`dataset/manifest.tsv` and `OPEN-QUESTIONS.md` textually; C rebases onto
whatever has merged before its documentation sweep, and never runs while
another branch owns `src/fonts/` or `src/text/`.

### 11.4 Conflict analysis

**Files touched by more than one batch**

| File | A | B | C | Note |
|---|---|---|---|---|
| `tests/panel/` | creates | adds the judge and causes | edits `tests/panel/text_causes.tsv` | additive files; C edits rows only |
| `docs/SOURCES.md` | adds member rows | adds judge rows | moves the CMap row's status | one table section each |
| `build/paths_pending.tsv` | removes its rows | removes its rows | removes its rows | each batch removes the rows of paths it created |
| `CLAUDE.md` | §3.5 one line | | §1, §4, §6, §7.5, §8.1, §12 | A's line is one entry of the layout block |
| `dataset/README.md` | §2 paragraph | | sweep | |
| `docs/DESIGN.md` | | §4, §9.4 lines | §5.2, §5.3, §7 | disjoint sections |
| `build-5.md`, `build-6.md` | | §9 item | §9 item; moved to `docs/archive/build/` when its panel gate passes | append-only, then one move |
| `OPEN-QUESTIONS.md` | | adds questions | marks §2.2 done | append-only |

**Shared files, as the launcher reads them.** Every row above is under
`shared` in the block; with the chain of §11.3 no two batches run together.

**Hotspots**

- **`build/check_network.py`**: the exemption names `tests/panel/fetch.py` and
  its reason; an adapter that imports `urllib` fails the check. Only the fetch
  tool downloads.
- **`build/check_headers.py` and `build/check_comments.py` over `tests/panel/`**:
  every `.py` file carries the licence and purpose header and is checked;
  `.rs` and `Cargo.toml` files carry them by hand, because the checks read
  only C++, C# and hash-commented suffixes (`build/checklib.py` `C_SUFFIXES`,
  `HASH_SUFFIXES`), as `src/images/dct_guard.c` does. A comment saying
  "phase 5" fails.
- **`tests/pdfium/`** is read, never edited, by A and B: the PDFium member
  goes through `pdfium.Pool` and `results_cache.cached_probe`, so the PDFium
  cache stays valid. Only C edits `tests/pdfium/text_causes.tsv`, to remove fixed rows.
- **`src/fonts/generated/`**: a generated file edited by hand fails
  `build/check_generated.py`; the generator is the only writer.
- **`build/budgets.tsv`**: raised only by the measured growth with its reason,
  never loosened to pass (D10).

### 11.5 The deviation protocol for this order

A batch that deviates: reports each deviation to the owner in one sentence,
asks where the answer is the owner's, edits §11.1 and every downstream batch
file so they describe what was actually left behind, and records the
deviations in its own §C, its commit message and its merge message.

---

## 12. Tests

Per `CLAUDE.md` §8, which owns the tiers. Each batch lists its own; the order
adds:

**New**

| Test | Tier | Asserts |
|---|---|---|
| `tests/panel/test_answers.py` | panel (Python, no member needed) | DR3: the key holds the file digest, member, version, binary digest, request and schema; a transient failure is not kept; a record is written atomically |
| `tests/panel/test_readers.py` | panel | each adapter's parsing of a member's output, over recorded outputs written in code; a missing member skips by name with the command that supplies it |
| `tests/panel/test_judge.py` | panel | DR1, DR2, D6, D7: majority, abstention, `undecided`, ties |
| `tests/dataset/test_panel_gates.py` | dataset tooling | the two gate scripts over a run folder written in code |
| new cases in `tests/unit/fonts/`, `tests/unit/text/` | unit | CR3, CR4, CR5 if ordered, DFT1 to DFT3, each red before its fix |

**Updated:** none of the PDFium tier's tests. **Deleted:** none.

**Fixtures:** the panel's tests use members' outputs written in code; batch
C's use `tests/unit/support/`'s PDF builder; no PDF is committed.

---

## 13. Acceptance criteria

| `ACx` | Criterion | Measure | Gate |
|---|---|---|---|
| AC1 | Every member of D1 answers from the cache for the dataset | for each member, files answered plus files refused with a cause equals 960, and a second `collect.py` run asks no member anything | pre-merge (A) |
| AC2 | The reading gate re-expressed by the panel | `phase5_panel.py` over a full run reports the median and the share counting against pdfkit, every `undecided` file listed; numbers recorded in `build-5.md` §9 | pre-merge (B) |
| AC3 | The bitonal gate re-expressed by the panel | `phase6_panel.py` reports images agreeing with the pixel majority over images with a majority, the `undecided` ones listed, and the verdict on qpdf-0026, synth-0263 and synth-0265 named | pre-merge (B) |
| AC4 | The three re-decisions | each has its measured majority, its action of D3 taken, and its evidence in `build-5.md` or `build-6.md` §9 and, where it flips, a question in `OPEN-QUESTIONS.md` with its price | pre-merge (B) |
| AC5 | pdfkit's confirmed misses fixed | of the files `tests/panel/text_causes.tsv` confirms as pdfkit's at B's tip, each is fixed (no longer counting against pdfkit) or carries a cause line saying why not; the share counting against pdfkit does not rise | order-end (C) |
| AC6 | No other number moves | every row of `build-10.md` §9 item 22 except the reading and bitonal rows is unchanged on C's tip; the leak measures stay 0 | order-end (C) |
| AC7 | Suites and checks | `ctest --preset unit` and `python build/check.py` green on every batch tip; `python build/check.py --full`, both sanitizer tiers and `python tests/abi/run.py` green on C's tip; the DLL's imports clean | pre-merge, order-end |
| AC8 | No member shipped | `build/check_artefact.py` green; no member's file under `dist/` or `third_party/` | order-end |

---

## 14. Manual verification

| Check | Do this | Expect | Why no test can |
|---|---|---|---|
| Two CJK pages | open pdfjs-0053 and pdfplumber-0023 in a viewer beside `pdfkit text` on C's tip | the same characters | a reader's agreement with the panel is measured; that the panel and pdfkit are right needs eyes once |

---

## 15. Traceability matrix

| Item | Decided by | Proven by | Held by |
|---|---|---|---|
| F1 | D1, D2, D6 | AC2 | `phase5_panel.py`, `test_judge.py` |
| F2 | D2, D7 | AC3 | `phase6_panel.py`, `test_judge.py` |
| F3, CR1 | D1, D9 | AC1 | `test_answers.py`, `test_readers.py` |
| F4 | D9 | AC1 | the pre-launch actions of §16 |
| F5 | D3, D12 | AC4 | `tests/panel/redecide.py` |
| F6, CR3, CR4 | D10 | AC5 | unit cases of batch C; `phase5_panel.py` |
| CR5 | D3 | AC4, AC5 | unit cases of batch C when ordered |
| F7 | D10 | AC7 | `build/package.py` budgets |
| DFT1, DFT2, DFT3 | D2 | AC5 | unit cases of batch C |
| CR2, DR1, DR2 | D2, D6, D7 | AC2, AC3 | `test_judge.py` |
| DR3 | D1 | AC1 | `test_answers.py` |
| DR4 | D1 | AC8 | `check_artefact.py`, `check_network.py` |
| D13 | D13 | no criterion: no action, see §8.2 | none |

---

## 16. Rollback and irreversible cost

**How to undo the code.** Revert the order's landing merge. A and B add test
tooling and documents only; C's reader changes revert with it, and the
manifest's expectations return with the revert. The caches under
`build/cache/panel*/` can be deleted at any time and cost one slower run.

**What cannot be undone, and what it costs.**

- **The DLL grows** by the packed CMap data (D10); a release made after C
  ships it, and a budget raised in `build/budgets.tsv` is the new floor of
  every later package.
- **GPL and AGPL binaries sit in the main checkout's `build/cache/`.** They are
  never shipped (DR4), but the laptop holds them; deleting the folder removes
  them.

**What the owner must do or be told before batch A starts.**

1. **Merge chore/owner-answers into `main`**: the order cites its charter
   bullets and its `OPEN-QUESTIONS.md`.
2. **Allow the tools the batches run**, in the dispatcher's local configuration
   or by approving once: `python` (already used), `git`, `cargo` and `rustc`
   from `build/cache/panel/rust/`, and the executables under
   `build/cache/panel/` (`pdftotext`, `pdfimages`, `pdftoppm`, `pdfinfo`,
   `mutool`, `qpdf`, and the Rust adapter binaries).
3. **Allow the downloads**: GitHub release assets (Poppler's Windows build,
   QPDF), mupdf.com's download archive, static.rust-lang.org (the toolchain)
   and crates.io (the crates and their dependencies). All go through Python's
   `urllib` or `cargo`, which the proxy passes (`build/toolchain.md` §5;
   cargo's revocation check recalled, D9). If the owner prefers, he installs
   the Rust toolchain by hand once instead (about 10 minutes:
   `winget install Rustlang.Rustup`, then
   `rustup toolchain install stable-x86_64-pc-windows-gnu`), and batch A uses
   it and records its version.
4. **Disk**: about 3 GB in `build/cache/panel/` (the Rust target folder most of
   it) and a few hundred MB of answers in `build/cache/panel-results/`.
5. **The DLL size** (D10, F7): the budget may rise by the packed CMaps' size,
   expected under 2 MB (an estimate; batch C measures it).

---

## 17. Opening prompt

This parent is not launched by itself. Each batch file carries its own prompt:
`docs/change-01a.md` §17, `docs/change-01b.md` §17, `docs/change-01c.md` §17.

> Read `CLAUDE.md` and `docs/change-01.md` §0 to §8, then open the batch file
> the launcher names and follow its own opening prompt in its §17.

Before batch A starts, the owner has done the five items of §16.

---

## Appendix B — Considered and rejected

- **Counting only `pdfkit`-caused files under the PDFium gate** (old §2.10's
  suggestion): rejected by the owner (§2.2), because a cause line is the
  author's own judgement and the panel is evidence.
- **hayro (Rust renderer) as a pixel member**: no gate of this order judges
  rendered pages (D7), and `FPDFImageObj_GetRenderedBitmap`, `pdfimages` and
  `mutool extract` answer at the image's own size; a later order judging page
  pixels may add it.
- **pdf.js as a text member**: not in the owner's panel; a Node run per file
  would add a fourth runtime to the laptop.
- **Linking Poppler or MuPDF in-process through `ctypes`**: a separate process
  per call is what keeps a hostile file's crash out of the gate, as PDFium's
  pool does, and needs no binding to maintain.
- **One merged batch A and B**: about 10 to 11.5 hours, past the 6-hour session
  limit (D4).
