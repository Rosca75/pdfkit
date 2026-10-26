# The pdfkit dataset

This file owns the dataset's manifest format, its taxonomy, its tools and its
licence rule. `CLAUDE.md` §7 says why the dataset exists; this file says how it
is built and read.

## 1. What the dataset is

Between 500 and 1,000 PDF files that are the regression suite, the acceptance
gate of every build phase and the catalogue of what breaks extraction and
redaction. The files live outside git; the committed tables describe them:

| File | What it holds |
|---|---|
| `dataset/manifest.tsv` | one row per file: where it came from, its licence, its hash, its tags and what each catalogue function is expected to do with it |
| `dataset/taxonomy.tsv` | every issue tag, its group, the minimum number of files that must carry it, and what detects it |
| `dataset/sources.tsv` | every public set considered, admitted or not, with its pin, licence and selection rule |
| `dataset/tools/` | the tools that fetch, select, tag, register, mutate, synthesise, generate and report |

## 2. Where the files live

Every worktree reads one copy of the files. The tools resolve the shared root as
`$PDFKIT_DATASET_ROOT` when it is set, else as the `dataset/` folder of the main
checkout, which is the parent of `git rev-parse --git-common-dir`. Under it:

| Folder | Holds |
|---|---|
| `files/<set>/<id>.pdf` | the files the manifest names, one per row |
| `cache/<set>/` | the fetched sets (a sparse git checkout in a repo folder, or the archive), with the cached scan of every candidate |
| `cache/<generator>-staging/` (synth, mutate, office) | the generators' outputs before registration |
| `reports/<run-id>/` | the reports of each run; a run id is `YYYYMMDD-HHMMSS-<git short hash>` |

PDFium, the independent reader, lands in `build/cache/pdfium/<tag>/` of the same
main checkout. All of these folders are ignored by git.

## 3. The manifest

`dataset/manifest.tsv` is tab-separated, UTF-8, LF line ends, one header line,
one row per file, sorted by `id`. No cell holds a tab or a line end.

| Column | Meaning |
|---|---|
| `id` | `<set>-<nnnn>`, numbered in order of registration within the set; an id is never reused |
| `set` | the set of `dataset/sources.tsv` the file belongs to |
| `source` | a public file: its URL at the pinned commit (`.../blob/<commit>/<path>` or `.../+/<commit>/<path>`), or the archive URL and the member after `#`; a generated file: `synth:<pattern>`, `mutate:<source id>:<mutation>:<seed>` or `office:<generator>` |
| `licence` | an SPDX identifier, or `terms:<short name>` whose terms URL is the set's `licence_url`; a mutated file carries its source's licence |
| `sha256` | the SHA-256 of the file's bytes |
| `bytes` | the file's size |
| `pages` | PDFium's page count; empty when PDFium does not open the file |
| `producer` | the Info `/Producer` as PDFium reads it, else the XMP `pdf:Producer`; empty when absent |
| `tags` | comma-separated tags of `dataset/taxonomy.tsv`, sorted |
| `expect_open` | `opens` (no repair), `repaired` (opens through reconstruction), or `refused:<code>` with a refusal code of `docs/DESIGN.md` chapter 19 |
| `expect_text` | `agree` (pdfkit's text must agree with PDFium's), `known_gap:<reason>` (a disagreement known in advance, with its reason), or `n/a` (no text, or no reader can open it) |
| `expect_images` | `decoded` (every image decodes), `partial:<reason>`, or `n/a` (no image) |
| `expect_redact` | `stable` (a redaction leaves the page pixel-stable outside the changed rectangles) or `n/a` |
| `expect_metadata` | `read` (pdfkit_metadata reads the Info dictionary and the XMP packet), `known_gap:<reason>` (`known_gap:xmp_unreadable` for a packet that does not scan, which the reader reports and never guesses at) or `n/a` (neither is present, or the file is refused); whether the two places agree is the file's `meta_*` tag, not an expectation |
| `expect_fonts` | the decoding ladder over every shown code, read from the `text` outcome's per-page `unreadable` counts and filled by `tests/pdfium/text_agreement.py --write-manifest`: `loads` (every shown code decodes), `unreadable:<count>` (codes no rung of the decoding ladder reads), or `n/a` (no text, or the file is refused) |
| `expect_write` | `identical` (the identity write re-opens in pdfkit without repair with the same page count, and PDFium renders every page as the input renders), `known_gap:<reason>` (a difference known in advance, with its reason) or `n/a` (a file pdfkit refuses) |
| `expect_select` | `ok` (a seeded random proper subset of the pages, written in a shuffled order, re-opens without repair with that many pages, and each output page renders as its source page), `known_gap:<reason>` or `n/a` (one page, or a file pdfkit refuses) |
| `expect_redact_text` | `clean` (the seeded text redaction of `tests/dataset/outcome.md` passes the self-check and its output re-opens without repair with the input's page count; PDFium's text and the output's bytes hold no redacted word and the pages are pixel-stable outside the reported rectangles, which `tests/pdfium/` measures), `known_gap:<reason>` (the self-check refuses for a reason the job does not redact, such as a word that is also in the metadata) or `n/a` (a file pdfkit refuses) |
| `expect_set_metadata` | `clean` (the metadata rewrite of `tests/dataset/outcome.md` passes the self-check, re-opens without repair with the input's page count, and its Info dictionary and XMP packet carry the written values; PDFium's text and Info fields and the output's bytes hold no original value, which `tests/pdfium/leak.py --metadata` measures), `known_gap:<reason>` or `n/a` (no field present, or a file pdfkit refuses) |
| `notes` | free text: what a generated file plants, why PDFium refused it (`pdfium:<error>`) |

A new catalogue function adds its expectation column here and in
`dataset/tools/common.py` (`MANIFEST_COLUMNS`, `EXPECTATIONS`) in the same
change, and `tests/dataset/outcome.md` gains the fields the column reads.

### 3.1 How the expectations are set

`dataset/tools/add.py` sets them from what the scan and PDFium found, and a generator
overrides them from provenance:

- `expect_open`: `refused:password_required` when PDFium asks for a password or
  provenance planted one; `refused:unsupported_security_handler` for a handler
  other than `/Standard`; `repaired` when the scan finds damage that forces
  reconstruction (`xref_missing`, `xref_wrong_offsets`, `startxref_wrong`,
  `truncated`, `length_wrong`, `endstream_missing`, `endobj_missing`), or when
  PDFium refuses a file that still holds a page; `refused:not_pdf` or
  `refused:no_page` when the scan finds no header and no page; `opens` otherwise.
- `expect_text`: `n/a` for a refused file, a file PDFium cannot open or a file
  without text; `agree` otherwise. A synthesised file measures it: PDFium's
  text is scored against the text pdfkit's decoding ladder will report, and a
  score under 0.90 gives `known_gap:pdfium_f1_<score>`.
- `expect_images`: `decoded` when the file holds an image XObject or an inline
  image, `n/a` otherwise. `python tests/pdfium/image_agreement.py
  --write-manifest` rewrites it from a run of the images layer's decoders over
  the images the pages' resources reach: `decoded` when every one decodes,
  `partial:<encodings that failed>` (joined by `+`) otherwise, and `n/a` for
  a file not opened; a file whose resources reach no image keeps its value (an
  inline image is not reached that way); `--check` compares a run with it.
- `expect_redact`: `n/a` until the phase that delivers image redaction sets it.
- `expect_metadata`, `expect_set_metadata` and the tags of the `metadata`
  group: `n/a` and no tag for a new row, until `python
  tests/dataset/gates/phase10.py --run <id> --write-manifest` fills them from a
  dataset run's `metadata` and `set_metadata` outcomes; the same command turns
  an `expect_redact_text` known gap `clean` when the run shows it clean, and
  names the surface of one that stays (`known_gap:word_in_<surfaces>`).
- `expect_write`: `identical` when `expect_open` is `opens` or `repaired`, `n/a`
  otherwise (`common.default_expect_write`); a difference found by a run is
  recorded as `known_gap:<reason>` by hand.
- `expect_select`: `ok` when `expect_open` is `opens` or `repaired` and the
  `pages` column holds two or more, `n/a` otherwise
  (`common.default_expect_select`); `add.py`, the synthesiser and the mutator
  fill both columns for every new row the same way.
- `expect_redact_text`: `clean` when `expect_open` is `opens` or `repaired`,
  `n/a` otherwise (`common.default_expect_redact_text`, applied by `add.py`); a
  refusal a run explains is recorded as `known_gap:<reason>` by hand. The
  column sits before `notes`. The synthesised redaction traps
  (`dataset/tools/synth/redaction/`, tags `redact_<case>`) draw the marker the
  seeded job always redacts.

## 4. The licence rule

- **No client document is ever in the dataset**, whatever its origin, because
  the dataset must be shareable and reproducible.
- **A public set is admitted only when its licence or terms allow local
  testing.** A set whose licence cannot be established is recorded
  `licence_unclear` in `dataset/sources.tsv` and not fetched.
- **The licence is recorded per row.** Within a set, a file carries the set's
  licence unless the set states otherwise for it.
- **No PDF is committed to git**, whatever its licence; every PDF file is ignored by git and
  the repository check refuses any committed file starting with `%PDF`.
- **Downloads come only from the named sources or official project sites**,
  each pinned: a git set by its commit, an archive by its SHA-256.

## 5. The sets

`dataset/sources.tsv` holds one row per set considered. Its columns: `set`,
`url`, `fetch` (`git:<folder>;<folder>` with `*` for the whole repository and a
leading `!` excluding a sub-folder, `archive`, or `generated`), `pin`,
`licence`, `licence_url`, `optional` (`yes` for sets run only in a sandbox),
`select`, `status` (`admitted`, `refused_by_proxy`, `licence_unclear`,
`deferred`, `no_test_files`) and `notes`.

The admitted sets and the files taken:

| Set | Source | Licence | Files |
|---|---|---|---|
| `dccc` | Digital Corpora CC-MAIN-2021-31-PDF-UNTRUNCATED, zip 0000.zip, a seeded random sample | `terms:common-crawl` | 150 |
| `qpdf` | qpdf's test files | Apache-2.0 | 60 |
| `pdfjs` | pdf.js test/pdfs (committed files only) | Apache-2.0 | 60 |
| `pdfium` | PDFium testing/resources | BSD-3-Clause | 50 |
| `pdfminer` | pdfminer.six samples (without samples/nonfree) | MIT | 48 |
| `pikepdf` | pikepdf tests/resources | MPL-2.0 | 40 |
| `pdfdiff` | PDF Association pdf-differences | Apache-2.0 | 36 |
| `pypdf` | py-pdf sample-files | CC-BY-SA-4.0 | 35 |
| `opf` | Open Preservation Foundation format corpus (cabinet of horrors, hand-built corpus, GovDocs1 error files) | CC0-1.0 | 30 |
| `pdfplumber` | pdfplumber tests/pdfs | MIT | 25 |
| `safedocs` | PDF Association safedocs | Apache-2.0 | 20 |
| `verapdf` | veraPDF corpus (PDF/UA-1, PDF/A-1a and -2a, ISO 32000, TWG, Isartor) | CC-BY-4.0 | 20 |
| `tika` | Apache Tika's PDF parser test documents | Apache-2.0 | 20 |
| `pdfpig` | PdfPig integration and layout test documents | Apache-2.0 | 15 |
| `pdfbox` | PDFBox test resources | Apache-2.0 | 15 |
| `pdf20ex` | PDF Association PDF 2.0 examples | CC-BY-SA-4.0 | 7 |
| `nicar25` | public-records PDFs collected as hard extraction cases | MIT | 7 |
| `synth` | `dataset/tools/synth/` | Apache-2.0 | 220 |
| `mutate` | `dataset/tools/mutate.py` over admitted files | the source's | 75 |
| `office` | `dataset/tools/generate_office.ps1` | Apache-2.0 | 4 |

`liberation` is admitted as the font source of the synthesiser (SIL OFL 1.1),
not as a PDF set. The sets not admitted, and why, are rows of `dataset/sources.tsv`
with their status and a note.

## 6. The taxonomy

`dataset/taxonomy.tsv` has the columns `tag`, `group`, `minimum`,
`description` and `detector` (`autotag`, `synth`, `mutate`, `office`,
`pdfkit` or `manual`: what finds or makes the tag first; `pdfkit` is a dataset
run's outcome, read by `tests/dataset/gates/phase10.py --write-manifest`). The
groups are those of `CLAUDE.md` §7.3: `damage`, `encryption`, `fonts`,
`layout`, `hidden`, `images`, `channels` (one tag per row of the channel table
of `docs/DESIGN.md` chapter 15), `generators`, `traps`, and `metadata` (where
the metadata lives, how its two places compare and how the packet is written:
`meta_info_only`, `meta_xmp_only`, `meta_both_agree`, `meta_both_disagree`,
`meta_utf16`, `meta_xmp_custom_namespace`, `meta_xmp_history`,
`meta_xmp_unreadable`, whose three files are synthesised by
`dataset/tools/synth/metadata/`, as public files rarely hold a broken packet).
The minimum is 5 for the structural
damages and the font decoding paths, 1 for the rare cases (`vertical`,
`right_to_left`, `aesv3_r5`, `img_jbig2`, `img_jbig2_globals`, `img_jpx`,
`type3`, `deniable_removal`, and the generators only the owner's laptop can
produce), and 3 otherwise. A tag is added when a gap appears and never removed.

The tag `pages_multi` (two or more pages, minimum 50) is never written into a
row: `dataset/tools/report.py` counts it from the `pages` column. The tag
`ch_page_owned` marks the page-selection leak fixture of `docs/DESIGN.md`
chapter 16, whose rows also carry the tags of the channels they plant.

A tag means what the scan or the provenance can show: `tounicode_correct` means
a `/ToUnicode` map is present (only a synthesised file can say it lies,
`tounicode_wrong`), and `ch_unreferenced` means an object no reference reaches.

## 7. The tools

Every tool is Python 3.12 with the standard library, except the synthesiser's
use of fontTools, pinned in `dataset/tools/requirements.txt`
(`python -m pip install --user --require-hashes -r dataset/tools/requirements.txt`).
Each prints JSON or a table, and fails with what failed, what was expected and
how to fix it.

| Tool | What it does |
|---|---|
| `dataset/tools/common.py` | the shared root, the table readers and writers, hashes, run ids |
| `dataset/tools/fetch.py` | fetches the admitted sets at their pins into the shared cache, copies the manifest's public files into the shared files folder; `--verify` re-hashes every manifest file; `--offline` registers a set placed by hand |
| `dataset/tools/choose.py` | chooses a set's files by its `select` rule (greedy coverage of the tags under their minimum, or a seeded random sample) and registers them |
| `dataset/tools/autotag.py` | infers tags from the bytes |
| `dataset/tools/add.py` | registers files: copies, hashes, asks PDFium, tags, writes the row |
| `dataset/tools/report.py` | counts per tag, set and licence; fails on a tag under its minimum, an unknown tag, a bad expectation, too few rows, or with `--licences` a row outside an admitted set |
| `dataset/tools/mutate.py`, `dataset/tools/pdfcrypt.py` | the seeded mutator and its encryption primitives |
| `dataset/tools/synth/` | the deterministic writer, the font programs and the patterns; `dataset/tools/synth/run.py` with `--check` proves every synthesised row is reproduced |
| `dataset/tools/generate_office.ps1` | the reference document through Word, Excel, PowerPoint and Edge |

## 8. How to add

- **A file**: `python dataset/tools/add.py <file> --set <set> --licence <id> --source <url>`
  (`--tags`, `--drop-tags` and `--expect column=value` record provenance).
- **A public set**: a row in `dataset/sources.tsv` with its licence read on the source
  and `status` `admitted`, then `python dataset/tools/fetch.py --set <set>` and
  `python dataset/tools/choose.py --set <set>`.
- **A synthesised pattern**: a function in a module of `dataset/tools/synth/patterns/`, listed
  in its `PATTERNS`, then `python dataset/tools/synth/run.py --pattern <name>`. The
  planted-marker patterns of `dataset/tools/synth/channels/` (one per row of the
  channel table, plus the leak fixture) are registered the same way; each plants
  `PDFKITMARK<RR>X<NN>` markers in its channel only (RR the channel row, 90 to 93
  drawn content of the leak fixture, NN a counter or the page number).
- **A mutation**: an entry of `MUTATIONS` in `dataset/tools/mutate.py` with its tags and its
  `expect_open`, then `python dataset/tools/mutate.py --mutation <name>`.

A generator change that alters the bytes of a registered file is followed by
`dataset/tools/synth/run.py` with `--prune` (or the mutation re-run), so no row names bytes the tools
no longer write.

## 9. The commands

```
python dataset/tools/fetch.py                     # fetch the sets and restore files/
python dataset/tools/fetch.py --verify            # every row's file present, hash verified
python dataset/tools/report.py                    # coverage; fails on a tag under minimum
python dataset/tools/report.py --allow-under --licences
python dataset/tools/synth/run.py --check         # the synthesised rows are reproduced (absent ones rewritten)
python dataset/tools/mutate.py --restore          # absent mutated files rewritten from source and seed
python tests/pdfium/fetch.py                      # PDFium into build/cache/pdfium/
python tests/pdfium/compare.py --self-test        # the PDFium tier proves itself
python tests/dataset/run.py                       # the dataset tier over the manifest (--checks, --runs 1|2)
python tests/pdfium/compare.py --run <run-id> --check write,select,text   # the writer against PDFium
python tests/dataset/gates/phase7.py --run <run-id>   # the writer gate: markers, pixels, selection
python tests/pdfium/compare.py --run <run-id>     # text agreement of a run with PDFium
python -m unittest discover dataset/tools/tests
python -m unittest discover tests/pdfium
```

Generated files are not fetched: a lost synthesised or mutated file is written
again by its tool from the same seed; the Office outputs are not reproducible
byte for byte (each save stamps its time), so a lost one is generated again and
registered under a new id.
