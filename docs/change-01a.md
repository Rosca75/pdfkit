# CHANGE-01a — The reference panel's readers: fetched or built by hash, one adapter each, their answers cached for the 960 dataset files

| Field | Value |
|---|---|
| Status | awaiting owner launch |
| Owner approval | with the parent |
| Depends on | nothing |
| Supersedes | nothing |
| Superseded by | nothing yet |
| Batches | batch A of `docs/change-01.md` |
| `SessionVersion` | not applicable |
| Behaviour change | none: test tooling and documents only; nothing under `src/`, `include/` or `cli/` moves |
| New dependency | test-only, never linked or shipped: Poppler, MuPDF, QPDF, a Rust toolchain and the crates pdf_oxide, pdf-extract and lopdf, each pinned by hash in `build/cache/` |
| Minimum model | `opus` |
| Duration and price | 5 to 6 hours, $85 to $115 at list prices (parent §11.1), of which about 40 minutes are unattended compile and collection |

Read `docs/change-01.md` §0 to §8 first. This file is §9 onwards for batch A
alone.

---

## 0. Context

Inherited from the parent. Facts the batch needs beside it:

- **PDFium is already a member and is not re-fetched.** Its adapter answers
  through `tests/pdfium/pdfium.py`'s `Pool` and `results_cache.cached_probe`
  for text and page count, whose answers are cached already (E7). For what
  the existing worker does not answer (page sizes, `FPDFImageObj_GetRenderedBitmap`),
  the adapter defines its own worker by subclassing `pdfium.Worker` with its
  own `--serve` entry, as `tests/pdfium/image_agreement.py` `ImageWorker` and
  `serve` do (`:157-178`). Nothing under `tests/pdfium/` is edited.
- **The fetch pattern to copy** is `tests/pdfium/fetch.py`: a pinned tag, an
  asset URL, a SHA-256 recorded on the first fetch and then committed, a size
  ceiling, extraction into the main checkout's cache through
  `dataset/tools/common.py`, a refusal on a mismatch, downloads through
  Python's `urllib` (`build/toolchain.md` §5).
- **The cache pattern to copy** is `tests/pdfium/results_cache.py` (DR3 of
  the parent): key, atomic write, transient failures never kept.
- **Image alignment.** pdfkit's census aligns its images with PDFium's by the
  SHA-256 of the image's stored bytes (`build-6.md` §9 item 3), and
  `pdfkit images` names each XObject's object number (`docs/ABI.md`, the
  `source` and `object` fields). A member that names images by object number
  (Poppler's `pdfimages -list`, MuPDF's `mutool extract`, recalled) is aligned
  through that number; QPDF's raw stream data of that object
  (`qpdf --show-object=<n> --raw-stream-data`, recalled) gives the SHA-256
  the census uses. Inline images get no pixel vote.
- **No Rust toolchain exists on the laptop** (parent §0.5). The parent's §16
  says what the owner has allowed; if `cargo` or a download is refused, follow
  §9.6 step 3.

---

## 9. The work

Every command-line flag, release asset name and library call named below is
**recalled** unless marked read. Before relying on one, read it at its source
(the member's manual page or release page) or try it on one dataset file, and
record the status in `docs/SOURCES.md`.

### 9.1 Fetching and building (`tests/panel/fetch.py`)

One tool, one pinned entry per member, each into
`build/cache/panel/<member>/<version>/` of the main checkout. The pins are the
latest stable release on the day, read on the release page and recorded with
the archive's SHA-256 in the tool and in `build/toolchain.md`.

| Member | What is fetched (recalled) | Licence | Executables kept |
|---|---|---|---|
| Poppler | the Windows build of the oschwartz10612/poppler-windows GitHub releases (`Release-<version>.zip`) | GPL-2.0-or-later (the packaging MIT) | `pdftotext`, `pdfimages`, `pdftoppm`, `pdfinfo` and their DLLs |
| MuPDF | `mupdf-<version>-windows.zip` from https://mupdf.com/downloads/archive/ | AGPL-3.0 | `mutool` |
| QPDF | `qpdf-<version>-mingw64.zip` (or `-msvc64`) from the qpdf/qpdf GitHub releases | Apache-2.0 | `qpdf` and its DLLs |
| Rust toolchain | `rust-<version>-x86_64-pc-windows-gnu.tar.xz` from https://static.rust-lang.org/dist/, its components unpacked into one sysroot (Python's `tarfile` reads `.xz`) | MIT OR Apache-2.0 | `rustc`, `cargo` |
| Rust adapters | built, not fetched: `tests/panel/rust/` by `cargo build --release --locked` with `CARGO_HOME` and `CARGO_TARGET_DIR` under `build/cache/panel/`, and `CARGO_HTTP_CHECK_REVOKE=false` for the proxy (parent D9, recalled) | pdf_oxide MIT OR Apache-2.0; pdf-extract MIT; lopdf MIT | `panel-oxide`, `panel-extract`, `panel-lopdf` |

| File | Change |
|---|---|
| `tests/panel/fetch.py` | `--member <name>` or all; `--verify` re-hashes every archive and every built binary; prints where each executable is. Refuses a hash mismatch with the three fields of an actionable error. The one file of `tests/panel/` that downloads |
| `tests/panel/rust/Cargo.toml` | a workspace of three binaries; `pdf_oxide = "=0.3.78"`, `office_oxide = { version = "=0.1.9", default-features = false }` (E8; the pin that makes pdf_oxide compile), pdf-extract and lopdf at the versions resolved on the day, pinned with `=`; `[profile.release]` with `opt-level = 2` |
| `tests/panel/rust/Cargo.lock` | committed: it pins every crate by checksum, which is the "pinned by hash" of `CLAUDE.md` §6 for crates |
| `tests/panel/rust/src/bin/panel-oxide.rs` | reads one PDF path, prints JSON on standard output: `{"ok": true, "pages": [{"text": ..., "box": [x0, y0, x1, y1]}]}` through pdf_oxide's per-page text and visible box (E8 names `get_page_visible_box`); a panic or error prints `{"ok": false, "error": ...}` and exits 2 |
| `tests/panel/rust/src/bin/panel-extract.rs` | the same shape through pdf-extract's per-page text (recalled: `extract_text_by_pages`; otherwise its text split on form feeds) |
| `tests/panel/rust/src/bin/panel-lopdf.rs` | the structure answer: opens, page count, object count, encrypted, whether a `/MediaBox` is reachable for each page |
| `build/check_network.py` | one exemption, `tests/panel/fetch.py`, with its reason ("downloads the pinned reference readers into the shared cache; a developer tool that never ships"), beside `tests/pdfium/fetch.py`'s |

The `.rs` and `Cargo.toml` files carry the licence and purpose headers by
hand (parent §11.4).

### 9.2 One adapter per member (`tests/panel/readers/`)

One module per member, the same interface each, so the judge never knows a
member's command line:

```
NAME, VERSION, LICENCE
binary_digest() -> str             # SHA-256 of the executable(s) as fetched or built
capabilities -> set[str]           # subset of {"text", "pages", "structure", "image_pixels", "render"}
answer(path, kind, timeout) -> dict   # {"ok": true, ...} or {"ok": false, "error": <cause>}
```

| Module | Kinds and how (recalled) |
|---|---|
| `pdfium_member.py` | `text`, `pages` count from the cached probe; `pages` sizes and `image_pixels` (`FPDFImageObj_GetRenderedBitmap`, E9) from its own worker subclass |
| `poppler.py` | `text`: `pdftotext -enc UTF-8 <file> -`, pages split on form feeds; `pages`: `pdfinfo -box -f 1 -l <n>`; `image_pixels`: `pdfimages -list` for object numbers, then `pdfimages -png -f <p> -l <p>` |
| `mupdf.py` | `text`: `mutool draw -F txt -o <tmp>/%d.txt <file>`; `pages`: `mutool pages <file>`; `image_pixels`: `mutool extract <file> <object>` in a scratch folder |
| `oxide.py`, `extract.py` | `text` (and `pages` sizes for `oxide`) from the Rust binaries' JSON |
| `qpdf.py` | `structure`: `qpdf --check`, `qpdf --show-npages`, and the raw stream bytes used for image alignment |
| `lopdf.py` | `structure` from `panel-lopdf` |

Every member runs as a child process with a timeout of 30 s per started block
of 200 pages, as the dataset tier does (`CLAUDE.md` §8.1); a timeout or a
crash is a transient failure (`timeout`, `crash`), never cached. A member's
own refusal of a file (a non-zero exit with its message) is an answer:
`{"ok": false, "error": "refused", "detail": ...}`, cached. Scratch output goes
under `build/cache/panel/tmp/` and is deleted after each answer.

### 9.3 The answers cache (`tests/panel/answers.py`)

A copy of `results_cache.py`'s pattern with the member in the key:
`build/cache/panel-results/<member>-<version>-<binary digest, 12 hex>/<kind>/<sha[:2]>/<sha>-<request key>.json`,
image pixels as blobs named by their own SHA-256 beside them, `SCHEMA = 1`,
`PDFKIT_PANEL_RESULTS` as the override, `cached_answer(member, path, kind,
**request)` as the one entry point. PDFium's `text` and page count are read
from `build/cache/pdfium-results/` through `results_cache.cached_probe`, never
copied.

### 9.4 Collection (`tests/panel/collect.py`)

`python tests/panel/collect.py --kinds text,pages,structure,image_pixels
[--members ...] [--set ...] [--id ...] [--smoke] [--workers N] --run-id <id>`:
answers every requested kind of every manifest file from each member that has
the capability, through the cache. `--workers` defaults to
`pdfium.default_workers()`. It writes `dataset/reports/<run-id>/panel/collect.json`
and prints, per member and kind: files answered, refused (by cause),
transient, and seconds. `image_pixels` is asked only for the images the
judge will need: every 1-bit image of the census (the bitonal gate's 498) and
every DeviceCMYK, Separation and DeviceN image with a CMYK alternate (the
CMYK re-decision), found from a dataset run's outcomes or from
`pdfkit images` (read `image_agreement.py` `--listing` for how).

### 9.5 The two committed tables

| File | Content |
|---|---|
| `tests/panel/members.tsv` | one row per member: name, role (text, pixels, structure), licence, version, upstream URL, archive SHA-256, binary digest, capabilities, and the measured pixel facts of D7: native size or not, `/Decode` applied or not, colour output (gray, RGB, CMYK). The pixel facts are measured on qpdf-0026, synth-0263, synth-0265 and 20 bitonal and 20 CMYK images of the smoke set, and a member that fails a fact gets no `image_pixels` capability |
| `tests/panel/smoke.tsv` | about 60 ids with the reason each is in (parent D11): the 21 `pdfkit` rows of `tests/pdfium/text_causes.tsv`, 10 `pdfium` and 4 `legitimate` rows, qpdf-0026, synth-0263, synth-0265, 10 files with DeviceCMYK images, 12 clean files spread over the text-layout tags of `dataset/taxonomy.tsv` |

### 9.6 Execution order inside this batch

The test rhythm of `CLAUDE.md` §8.2 and the stop rule of the parent §0.2 hold
throughout: narrow tests while coding, the smoke set once mid-batch, the full
collection once at the end, and at 5 h 30 of wall-clock stop, commit what
passes and record what is left.

1. **Base check.** On the base, `ctest --preset unit` and
   `python build/check.py` green; `python dataset/tools/fetch.py --verify`
   finds the 960 files; `python tests/pdfium/fetch.py` finds PDFium. A red
   base is repaired in its own commit and recorded in §C.
2. **Tests first.** `tests/panel/test_answers.py` (DR3) and
   `tests/panel/test_readers.py` (each adapter's parsing, over member outputs
   written in code; a missing member skips with the command that supplies it),
   seen red.
3. **Fetch and build** (§9.1), Poppler, MuPDF and QPDF first, then Rust. If a
   download or `cargo` is refused, or the build fails after one fix attempt:
   keep what fetched, mark the missing members absent in `tests/panel/members.tsv` with
   the command that supplies them, continue with the others (a text verdict
   needs three voters, which PDFium, Poppler and MuPDF give), and record it
   in §C and in the parent's §11.1 note for batch B.
4. **Adapters and the cache** (§9.2, §9.3), each tried on one file by hand, then
   the narrow tests green.
5. **Mid-batch smoke**: `python tests/panel/collect.py --smoke --kinds
   text,pages,structure,image_pixels --run-id <id>-smoke`. Measure the pixel
   facts of §9.5 here and write `tests/panel/members.tsv`.
6. **Full collection, once**: `python tests/panel/collect.py --kinds
   text,pages,structure,image_pixels --run-id <id>` over the 960 files, then
   the same command again, which must ask no member anything (AC1). Record
   the cold and warm timings.
7. **Test sweep**: the panel's tests, `ctest --preset unit`,
   `python build/check.py`.
8. **Documentation sweep**, from what landed: `build/toolchain.md` (a section
   "The reference panel" after §6.5: each member's version, archive and
   binary digests, the Rust toolchain and the cargo proxy setting as
   measured, the cold and warm collection timings, the cache's size);
   `dataset/README.md` §2 (the panel's cache beside PDFium's, one paragraph);
   `CLAUDE.md` §3.5 (one line, `tests/panel/` with what it holds);
   `docs/SOURCES.md` (one row per member: licence and URL, and each recalled
   fact now read or tested, with its status; E9's header sentence);
   `build/paths_pending.tsv` (remove the rows of every path this batch
   created).
9. **Merge gate** on the tip: `python build/check.py --full`; push with
   `Change-Batch: 01a done`.

---

## 10. Code and documentation references

### 10.1 Files this batch touches

```
tests/panel/fetch.py, answers.py, collect.py, members.tsv, smoke.tsv   (new)
tests/panel/readers/                                                  (new, one module per member)
tests/panel/rust/                                                     (new: Cargo.toml, Cargo.lock, src/bin/)
tests/panel/test_answers.py, test_readers.py                          (new)
build/check_network.py                                                (one exemption)
build/toolchain.md, dataset/README.md, CLAUDE.md §3.5, docs/SOURCES.md, build/paths_pending.tsv   (documentation sweep)
```

### 10.2 Authoritative documentation to update

| File | Edit |
|---|---|
| `build/toolchain.md` | the panel's section (step 8) |
| `dataset/README.md` §2 | the panel's cache |
| `CLAUDE.md` §3.5 | the `tests/panel/` line |
| `docs/SOURCES.md` | the members and the facts read |

---

## 12. Tests

**New**

| Test | Tier | Asserts |
|---|---|---|
| `tests/panel/test_answers.py` | panel (no member needed) | the key's parts (DR3); a transient failure not kept; a refusal kept; atomic write; a changed binary digest is a new key |
| `tests/panel/test_readers.py` | panel | each adapter parses its member's output (recorded in code) into the common shape; pages split right; a refusal and a timeout are told apart; an absent member skips by name |

**Updated, deleted:** none.

---

## 13. Acceptance criteria

| `ACx` | Criterion | Measure | Gate |
|---|---|---|---|
| AC1 | Every present member answers for the whole dataset from the cache | per member and kind, answered plus refused equals 960 (or the files the kind applies to); a second collection asks nothing (0 child processes started) | pre-merge |
| AC1a | The pins hold | `python tests/panel/fetch.py --verify` re-hashes every archive and binary and agrees with `tests/panel/members.tsv` and `build/toolchain.md` | pre-merge |
| AC1b | The pixel facts are measured | `tests/panel/members.tsv` gives each pixel member its native-size, `/Decode` and colour facts from step 5 | pre-merge |
| AC7 | Suites and checks | the panel's tests, `ctest --preset unit` and `python build/check.py --full` green on the tip | pre-merge |

---

## 15. Traceability matrix

| Item | Decided by | Proven by | Held by |
|---|---|---|---|
| CR1, F3 | D1 | AC1, AC1a | `test_answers.py`, `test_readers.py` |
| F4 | D9 | AC1a | `fetch.py --verify` |
| DR3 | D1 | AC1 | `test_answers.py` |
| D7's pixel facts | D7 | AC1b | `tests/panel/members.tsv` |
| DR4 | D1 | AC7 | `check_network.py` |

---

## 16. Rollback and irreversible cost

**How to undo the code.** Revert the batch; delete `build/cache/panel/` and
`build/cache/panel-results/`. **What cannot be undone:** nothing. **What the
owner must be told:** the five pre-launch items of the parent's §16.

---

## 17. Opening prompt

> Read `CLAUDE.md` (§6, §8 and §11 closely), `build/toolchain.md` §5 and §6.2,
> and `dataset/README.md` §2, then read `docs/change-01.md` §0 to §8 and all
> of `docs/change-01a.md`, and implement it exactly as written. Before
> planning, read `tests/pdfium/fetch.py`, `tests/pdfium/results_cache.py`,
> `tests/pdfium/pdfium.py` (the `Worker` and `Pool` classes),
> `tests/pdfium/image_agreement.py` lines 150 to 180 and `build/check_network.py`.
> Develop and push on the branch and in the working tree the launcher's
> preamble names, or, when started by hand, on change/01-a-readers branched
> from `main` in a worktree of its own; first check that `main` carries the
> "Reference readers judge as a panel, test-only" bullet of `CLAUDE.md` §6,
> and stop and say so if it does not. Begin by running `ctest --preset unit`
> and `python build/check.py` on that base; if a test is still red run alone,
> repair the base in a commit of its own before you start and record the
> repair as a deviation. Follow the execution order of §9.6 with its test
> rhythm: the narrow tests while coding, the smoke set once mid-batch, the
> full collection once at the end, `python build/check.py --full` at the end;
> never re-run a full tier to be sure. This session lasts at most 6 hours: at
> 5 h 30 stop starting work, commit what passes, record what is left in §C and
> in `docs/change-01b.md`, and push. If a download, `cargo` or a member's
> executable is refused, do not wait: keep what works, record the refused
> command in §C, and continue as §9.6 step 3 says. Finish by pushing a last
> commit whose message carries the trailer `Change-Batch: 01a done`, or
> `Change-Batch: 01a incomplete` with the reason in your final message, and
> report the elapsed time, the model spend, per member the files answered and
> the collection's timings. Do not start batch B and edit nothing under `src/`
> or `tests/pdfium/`. If you must deviate from the brief, report each
> deviation, ask where the answer is the owner's, update `docs/change-01b.md`,
> `docs/change-01c.md` and the batch index in `docs/change-01.md` so they
> describe what you actually left behind, and record it in this file's §C and
> in the merge message.

---

## Appendix C — Deviations recorded by this batch

1. *(none yet)*
