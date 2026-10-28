# build-13 — Hidden layers are removed

| Field | Value |
|---|---|
| Phase | 13 |
| Branch | `phase/13-hidden-layers` |
| Depends on | `main` with `chore/owner-answers` merged |
| Parallel with | 11 and 12 (no code file shared; the shared documents are edited last, see §7) |
| Model | `opus` |
| Owns | `src/writer/channel_table.{hpp,cpp}`, a new `src/redact/optional_content_remover.{hpp,cpp}`, `src/redact/write_pipeline.cpp` (one stage call), `tests/unit/writer/channel_table_test.cpp`, `tests/unit/redact/optional_content*`, `tests/unit/support/pdf_builder_channels.hpp` (the off-layer fixture), `dataset/tools/synth/` (one specimen family), `tests/dataset/gates/phase7.py` (one measure); at the end, after rebasing on `main`: `docs/DESIGN.md` §15, `docs/ABI.md` §4.5, `CLAUDE.md` §2 row 8 |
| Size | about 500 lines of C++ including tests, 100 lines of Python |
| Price | 3 hours of session time, about $50 of model spend |
| Time box | 6 hours; at 5 h 30 the session commits what passes and closes `incomplete` |

## 0. What this phase ships

The owner's answer of 2026-10-28 (`OPEN-QUESTIONS.md` §2.5): content in an
optional-content group that the document's default configuration turns off is
removed from every output, and `/OCProperties` and every `/OC` key go with it.
Until now the writer kept the groups renamed `OC1`..`OCn` with their states, so
switched-off content stayed in the file, hidden by viewers and read by
extractors.

### 0.1 The contract

- Every `pdfkit_write` output holds no content drawn under an optional-content
  group or membership dictionary that the default configuration (`/D`, its
  `/BaseState`, `/ON`, `/OFF`) turns off: the marked-content sequence
  `/OC /name BDC ... EMC` is removed with what it draws, and a form or image
  XObject whose `/OC` is off is not drawn (its `Do` removed; the reachable
  sweep drops it). Content in a group that is on is kept, drawn as before, its
  `/OC` marks removed.
- The catalog's `/OCProperties` is removed and no `/OC` key remains.
- `pdfkit_channels` keeps the row `ch_optional_content` (renamed
  "optional content: groups, and content in groups turned off"); its count is
  the number of groups, and the answer gains `off_groups` after `count`.
- The write report's `channels` entry for the row counts the groups removed.
- Text extraction (`pdfkit_text`, `pdfkit_find`) of an input still reads
  off-layer text with its `optional_content_off` class, so a caller sees what
  the write will remove.

## 1. What it reads from earlier phases

| From | What | Why |
|---|---|---|
| phase 7 | `writer::ChannelTable`, `minimise_optional_content`, `ObjectGraph`, `ResourceUsage` | the strip becomes a removal |
| phase 5 | the content interpreter's optional-content state per operator | which operators an off group covers |
| phase 9 | `redact::ImageRemover` (removing a `Do` while keeping `q`/`Q` balanced) | the same surgery for an off XObject |

## 2. Components

| File | What | Lines |
|---|---|---|
| `src/redact/optional_content_remover.{hpp,cpp}` | a pipeline stage before the channel strip: walks every content stream and form, removes off sequences and off `Do`s, keeps the graphics state balanced | 250 |
| `src/writer/channel_table.cpp` | `ch_optional_content` strips `/OCProperties` and `/OC` wholly; `minimise_optional_content` goes | 60 |
| `src/redact/write_pipeline.cpp` | the stage call | 10 |
| specimen family in `dataset/tools/synth/` | off layers holding planted markers: a BDC sequence, an off form, an off image, nested groups, a `/BaseState /OFF` | 100 |

## 3. Tests

Unit (red first): each specimen's marker is absent from the output's raw bytes
and decoded streams; on-layer content is kept and draws the same pixels; a
nested on-inside-off sequence is removed; the graphics state stays balanced.

## 4. Gate

| Measure | Threshold |
|---|---|
| planted markers in off layers found in an output | 0 |
| outputs holding `/OCProperties` or an `/OC` key | 0 |
| identity-write pages pixel-identical to the input rendered with the default configuration | at phase 7's 99 % |
| phase 7's other measures | at their thresholds |

## 5. Kill criterion

A planted off-layer marker in an output stops the line.

## 6. Rollback

Revert the merge; the groups are kept hidden as before.

## 7. Execution order inside the phase

1. Unit tests, red. 2. The remover stage, then the channel row. 3. One smoke
run of about 60 files with optional content (`tags` holding
`ch_optional_content`). 4. The full phase 7 gate once. 5. Rebase on `main`,
the documentation sweep of the owned sections. 6. The merge checks once.

## 8. Opening prompt

> Read `CLAUDE.md`, `docs/DESIGN.md` §15, `docs/ABI.md` §4.5, then
> `build-13.md`, and implement it as written on `phase/13-hidden-layers` in
> your own worktree. You own the files of the Owns row; build-11 owns the
> self-check, build-12 the match finder and the API registry. Follow
> `CLAUDE.md` §8.2 and stop at 5 h 30 with `Build-Phase: 13 incomplete` if the
> gate is not reached.

## 9. Deviations recorded by this phase

1. **Files outside the Owns row**, each because the contract needs it and no
   other session owns it: `src/redact/CMakeLists.txt` and
   `tests/unit/CMakeLists.txt` (one line each for the new source and test);
   `src/api/channels.cpp` (writes `off_groups`); `tests/unit/support/
   pdf_builder_channels.cpp` (the fixture the owned header declares);
   `tests/unit/writer/optional_content_test.cpp` deleted (it tested
   `minimise_optional_content`, which goes; its cases live on in
   `tests/unit/redact/optional_content_remover_test.cpp`);
   `tests/dataset/test_phase7.py` (the tests of the two new gate measures);
   `docs/SOURCES.md` (one row for the recalled facts of §8.11);
   `docs/ABI.md` §4.7 (the write report's `channels` cell said the row was kept
   minimal); `dataset/tools/synth/channels/kit.py` (a comment naming marker
   row 25).
2. **The specimen family is not registered in the manifest**, because
   `dataset/manifest.tsv` belongs to build-12 and the manifest's row count is
   a charter number. The gate writes the five specimens itself through the
   command line (`off_layer_family` in `tests/dataset/gates/phase7.py`, into
   `<run>/off-layers/`), and reads them from the manifest too once registered.
   At merge, after build-12: `python dataset/tools/synth/run.py --pattern
   mark_off_layers`, then CLAUDE.md §7.1's row count.
3. **The remover walks every document**, not only one whose catalog has
   `/OCProperties`: a catalog without it shows every group, but its `/OC`
   marks and the `/Properties` entries naming groups (whose `/Name` can name
   things) would stay; `pdfpig-0015` (a second catalog holds the groups) is
   the dataset's case. The extra lex costs 3.7 s on the 910-page file.
4. **The leak fixture of §16 puts its optional-content group on the form only
   page n draws** instead of a BDC in the page's content, so the page's
   content stream stays byte-identical as the leak test asserts (an `/OC`
   mark is now removed from every output).
5. **`/PageMode /UseOC` reads as `/UseNone`**: the layers panel goes with
   `/OCProperties`.
